#include "main.h"
#include "commands.hpp"

#include <blola/blola.hpp>
#include <blola/directWrite_SEGGER_RTT.hpp>

#include <array>
#include <cstdint>

#include "stm32f401xc.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal_dma.h"
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

void setDelay(uint32_t samples);
void setRead(uint32_t samples);
void setDivider(uint32_t divider);

volatile enum { IDLE, WAIT_TRIGGER, MEASURING, REPORTING } state = IDLE;
uint32_t readCount = 0;
uint8_t samples[1024] = {0x12, 0x34};
uint32_t sampleClock = 84'000'000 / 5;

uint8_t *volatile usbBuf = nullptr;
volatile uint32_t usbBufLen = 0;

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
     0x23, METADATA_NUMBER(sampleClock),

     // 5. SUMP protocol version
     0x24, METADATA_NUMBER(2),

     // Терминатор метаданных
     0x00});

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
    state = IDLE;
    break;
  case CommandShort::Run:
    state = WAIT_TRIGGER;
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
  case CommandLong::SetTriggerMaskStage1:
  case CommandLong::SetTriggerMaskStage2:
  case CommandLong::SetTriggerMaskStage3:
  case CommandLong::SetTriggerMaskStage4:
  case CommandLong::SetTriggerValuesStage0:
  case CommandLong::SetTriggerValuesStage1:
  case CommandLong::SetTriggerValuesStage2:
  case CommandLong::SetTriggerValuesStage3:
  case CommandLong::SetTriggerValuesStage4:
  case CommandLong::SetTriggerConfigurationStage0:
  case CommandLong::SetTriggerConfigurationStage1:
  case CommandLong::SetTriggerConfigurationStage2:
  case CommandLong::SetTriggerConfigurationStage3:
  case CommandLong::SetTriggerConfigurationStage4:
    blog("Triggers not supported (ignored)");
    break;
  case CommandLong::SetDivider:
    // Sigrok игнорирует анонсированную частоту и всегда передает делитель для
    // 100МГц, поэтому делитель пересчитывается для основной частоты - 84МГц
    setDivider((arg + 1) * 84 / 100);
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
} commandReader;

void blinkSignal(int times) {
  SET_BIT(LED_GPIO_Port->ODR, LED_Pin);
  for (int i = 0; i < times * 2; i++) {
    LED_GPIO_Port->ODR ^= LED_Pin;
    HAL_Delay(100);
  }
  SET_BIT(LED_GPIO_Port->ODR, LED_Pin);
  HAL_Delay(300);
}

void initMeasuring() {
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
  HAL_DMA_Start(&hdma_tim1_up, (uint32_t)&(GPIOB->IDR), (uint32_t)samples,
                sizeof(samples));
  SET_BIT(TIM1->DIER, TIM_DIER_UDE);
}

void startMeasuring() {
  TIM1->CNT = 0;
  TIM3->CNT = 0;
  SET_BIT(TIM1->CR1, TIM_CR1_CEN);
}

void stopMeasuring() { CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN); }

void trigger() { SET_BIT(TIM3->CR1, TIM_CR1_CEN); }

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

extern "C" int cpp_main() {
  blog("Startup");

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

  initMeasuring();
  setDelay(16);
  setDivider(499);
  trigger();
  startMeasuring();

  blinkSignal(2);

  while (true) {
    switch (state) {
    case IDLE:
      break;
    case WAIT_TRIGGER:
      break;
    case MEASURING:
      break;
    case REPORTING:
      break;
    }

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
  if (state == REPORTING)
    state = IDLE;
}
