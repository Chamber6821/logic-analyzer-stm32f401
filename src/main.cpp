#include "main.h"
#include "RingQueue.hpp"
#include "StateMachine.hpp"
#include "commands.hpp"

#include <algorithm>
#include <blola/blola.hpp>
#include <blola/directWrite_SEGGER_RTT.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "stm32f401xc.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_tim.h"
#include "usbd_cdc_if.h"
#include "usbd_def.h"

#define METADATA_NUMBER(UINT32)                                                \
  (uint8_t)((UINT32) >> 24), (uint8_t)((UINT32) >> 16),                        \
      (uint8_t)((UINT32) >> 8), (uint8_t)((UINT32) >> 0)

extern USBD_HandleTypeDef hUsbDeviceFS;

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim1_up;

enum class State : std::uint8_t { IDLE, RUNNING, REPORTING };
enum class Input : std::uint8_t {
  RESET,
  RUN,
  ALL_MEASURED,
  REPORTED,
};

void onShortCommand(CommandShort command);
void onLongCommand(CommandLong command, uint32_t arg);
State stateTransition(State state, Input input);

constexpr uint32_t clockInMHz = 60;
constexpr uint32_t sampleClockInHz = clockInMHz * 1'000'000 / 6;

uint32_t readCount = 0;
uint8_t samples[55 * 1024] = {0x12, 0x34};
uint8_t triggerMask = 0;

uint8_t *volatile usbBuf = nullptr;
uint32_t volatile usbBufLen = 0;

auto idLine = std::to_array<std::uint8_t>({'1', 'A', 'L', 'S'});

auto metadata = std::to_array<std::uint8_t>(
    {// 1. Name
     0x01, 'S', 'T', 'M', '3', '2', ' ', 'L', 'A', ' ', 'C', 'a', 'm', 'b', 'e',
     'r', '6', '8', '2', '1', 0x00,

     // 2. Channels: 8 (0x00000008)
     0x20, METADATA_NUMBER(8),

     // 3. Sample memory (bytes)
     0x21, METADATA_NUMBER(sizeof(samples)),

     // 4. Max sample rate (Hz)
     0x23, METADATA_NUMBER(sampleClockInHz),

     // 5. SUMP protocol version
     0x24, METADATA_NUMBER(2),

     // Терминатор метаданных
     0x00});

StateMachine state{State::IDLE, RingQueue<Input, 16>{},
                   [](auto state, auto input) {
                     if (not input.has_value())
                       return state;
                     return stateTransition(state, input.value());
                   }};

class CommandReader {
  enum { COMMAND, BYTE1, BYTE2, BYTE3, BYTE4 } expectation = COMMAND;
  CommandLong command;
  uint32_t arg;

public:
  void putByte(uint8_t byte) {
    switch (expectation) {
    case COMMAND:
      if (isShortCommand(byte)) {
        onShortCommand(static_cast<CommandShort>(byte));
      } else if (isLongCommand(byte)) {
        command = static_cast<CommandLong>(byte);
        expectation = BYTE1;
      } else {
        blog("Unknown command: 0x%02hhX", byte);
      }
      break;
    case BYTE1:
      expectation = BYTE2;
      arg = byte;
      break;
    case BYTE2:
      expectation = BYTE3;
      arg |= byte << 8;
      break;
    case BYTE3:
      expectation = BYTE4;
      arg |= byte << 16;
      break;
    case BYTE4:
      expectation = COMMAND;
      arg |= byte << 24;
      onLongCommand(command, arg);
      break;
    }
  }
};

void blinkSignal(int times) {
  SET_BIT(LED_GPIO_Port->ODR, LED_Pin);
  for (int i = 0; i < times * 2; i++) {
    LED_GPIO_Port->ODR ^= LED_Pin;
    HAL_Delay(100);
  }
  SET_BIT(LED_GPIO_Port->ODR, LED_Pin);
  HAL_Delay(300);
}

void startMeasuring() {
  HAL_TIM_PWM_Start_IT(&htim3, TIM_CHANNEL_1);
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  HAL_DMA_Start(&hdma_tim1_up, (uint32_t)&(GPIOB->IDR), (uint32_t)samples,
                sizeof(samples));
  SET_BIT(TIM1->DIER, TIM_DIER_UDE);
  SET_BIT(TIM1->CR1, TIM_CR1_CEN);
}

void capture() {
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  TIM3->CNT = 0;
  SET_BIT(TIM1->CR1, TIM_CR1_CEN);
  EXTI->PR = 0;
  EXTI->IMR = triggerMask;
}

void continueMeasuring() {
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  TIM3->CNT = 0;
  EXTI->IMR = 0;
}

void trigger() { SET_BIT(TIM3->CR1, TIM_CR1_CEN); }
void setTriggerMask(uint8_t mask) { triggerMask = mask; }
uint8_t getTriggerMask() { return triggerMask; }
void setTriggerValue(uint8_t value) {
  EXTI->FTSR = ~value;
  EXTI->RTSR = value;
}

void setDelay(uint32_t samples) {
  blog("Set delay: %u samples", samples);
  samples /= DELAY_PRESCALER + 1;
  TIM3->ARR = samples;
  TIM3->CCR1 = samples;
}

void setRead(uint32_t samples) {
  blog("Set read: %u samples", samples);
  readCount = samples;
}

void setDivider(uint32_t divider) {
  auto sqrt = [](uint32_t x) {
    uint32_t root = 1;
    while (root * root < x)
      root++;
    return root;
  };

  if (divider <= 0x10000) {
    TIM1->PSC = 0;
    TIM1->ARR = divider - 1;
  } else {
    auto part = sqrt(divider) - 1;
    TIM1->PSC = part;
    TIM1->ARR = part;
  }

  blog("Set divider: %u (prescaller: %hu, arr: %hu)", divider,
       (uint16_t)TIM1->PSC, (uint16_t)TIM1->ARR);
}

uint32_t lastSampleIndex() {
  return sizeof(samples) - __HAL_DMA_GET_COUNTER(&hdma_tim1_up);
}

void reverse(uint8_t *begin, uint8_t *end) {
  end--;
  while (begin < end) {
    std::swap(*begin, *end);
    begin++;
    end--;
  }
}

void onShortCommand(CommandShort command) {
  blog("Short command: 0x%02hhX", static_cast<uint8_t>(command));
  switch (command) {
  case CommandShort::ID:
    blog("Send ID \"1ALS\"");
    CDC_Transmit_FS(idLine.data(), idLine.size());
    break;
  case CommandShort::GetMeatadata:
    blog("Send Metadata");
    CDC_Transmit_FS(metadata.data(), metadata.size());
    break;
  case CommandShort::Reset:
    state.input(Input::RESET);
    break;
  case CommandShort::Run:
    state.input(Input::RUN);
    break;
  case CommandShort::XON:
  case CommandShort::XOFF:
    blog("XON/XOFF not supported (ignored, I dont know what is it)");
    break;
  }
}

void onLongCommand(CommandLong command, uint32_t arg) {
  blog("Long command: 0x%02hhX 0x%08X", static_cast<uint8_t>(command), arg);
  switch (command) {
  case CommandLong::SetTriggerMaskStage0:
    setTriggerMask(arg);
    break;
  case CommandLong::SetTriggerValuesStage0:
    setTriggerValue(arg);
    break;
  case CommandLong::SetTriggerMaskStage1:
  case CommandLong::SetTriggerMaskStage2:
  case CommandLong::SetTriggerMaskStage3:
  case CommandLong::SetTriggerMaskStage4:
  case CommandLong::SetTriggerValuesStage1:
  case CommandLong::SetTriggerValuesStage2:
  case CommandLong::SetTriggerValuesStage3:
  case CommandLong::SetTriggerValuesStage4:
  case CommandLong::SetTriggerConfigurationStage0:
  case CommandLong::SetTriggerConfigurationStage1:
  case CommandLong::SetTriggerConfigurationStage2:
  case CommandLong::SetTriggerConfigurationStage3:
  case CommandLong::SetTriggerConfigurationStage4:
    blog("Triggers partial supported (ignored)");
    break;
  case CommandLong::SetDivider:
    // Sigrok игнорирует анонсированную частоту и всегда передает делитель для
    // 100МГц, поэтому делитель пересчитывается для основной частоты -
    // clockInMHz
    setDivider((arg + 1) * clockInMHz / 100);
    break;
  case CommandLong::SetReadAndDelayCount:
    setRead(((arg & 0xFFFF) + 1) << 2);
    setDelay(((arg >> 16 & 0xFFFF) + 1) << 2);
    break;
  case CommandLong::SetFlags:
    blog("Flags not supported (ignored)");
    break;
  }
}

struct NoEffect {
  void operator()() const {}
};

template <class Func = NoEffect> struct Transition {
  State state;
  Input input;
  State nextState;
  Func effect;
};

template <class... Funcs> class TransitionTable {
  std::tuple<Transition<Funcs>...> table;

public:
  TransitionTable(Transition<Funcs> &&...transactions)
      : table(std::move(transactions)...) {}

  constexpr auto dispatch(State state, Input input, State defaultState) const {
    auto result = defaultState;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      (([&]() {
         const auto &element = std::get<Is>(table);
         if (element.state == state and element.input == input) {
           result = element.nextState;
           element.effect();
           return true;
         }
         return false;
       }()) ||
       ...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(table)>>{});
    return result;
  }
};

State stateTransition(State state, Input input) {
  static const TransitionTable table{
      Transition{State::IDLE, Input::RUN, State::RUNNING,
                 []() {
                   blog("Capturing...");
                   capture();
                   if (getTriggerMask() == 0) {
                     trigger();
                   } else {
                     blog("Wait trigger");
                   }
                 }},
      Transition{State::RUNNING, Input::RESET, State::IDLE,
                 []() {
                   blog("Reset");
                   continueMeasuring();
                 }},
      Transition{State::RUNNING, Input::ALL_MEASURED, State::REPORTING,
                 []() {
                   blog("Reversing buffer...");
                   reverse(samples, samples + lastSampleIndex());
                   reverse(samples + lastSampleIndex(),
                           samples + sizeof(samples));
                   blog("Reporting...");
                   CDC_Transmit_FS(samples, readCount);
                 }},
      Transition{State::REPORTING, Input::REPORTED, State::IDLE,
                 []() {
                   blog("Reported!");
                   continueMeasuring();
                 }},
  };

  return table.dispatch(state, input, state);
}

extern "C" int cpp_main() {
  blog("Startup");

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  blinkSignal(2);
  setDivider(5);
  startMeasuring();

  CommandReader commandReader;
  while (true) {
    state.update();

    if (usbBuf) {
      auto buf = usbBuf;
      auto len = usbBufLen;
      for (uint32_t i = 0; i < len; i++) {
        commandReader.putByte(buf[i]);
      }
      usbBuf = nullptr;
      usbBufLen = 0;
      USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    }
  }
}

extern "C" void USB_CDC_RxHandler(uint8_t *Buf, uint32_t Len) {
  usbBuf = Buf;
  usbBufLen = Len;
}

extern "C" void USB_CDC_TxCompleteHandler(uint8_t *Buf, uint32_t Len,
                                          uint8_t epnum) {
  state.input(Input::REPORTED);
}

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  blog("Pulse finished");
  if (htim->Instance == TIM3) {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
      state.input(Input::ALL_MEASURED);
    }
  }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) { trigger(); }
