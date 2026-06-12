#include "main.h"

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
extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim1_up;

bool run = false;

uint32_t readCount = 0;
uint32_t delayCount = 0;

uint8_t samples[1024] = {0x12, 0x34};

uint32_t sampleClock = 84'000'000;

enum class CommandShort {
  Reset = 0x00,
  Run = 0x01,
  ID = 0x02,
  GetMeatadata = 0x04,
  XON = 0x11,
  XOFF = 0x13,
};

enum class CommandLong {
  SetTriggerMaskStage0 = 0xC0,
  SetTriggerMaskStage1 = 0xC4,
  SetTriggerMaskStage2 = 0xC8,
  SetTriggerMaskStage3 = 0xCC,
  SetTriggerMaskStage4 = 0xD0,

  SetTriggerValuesStage0 = 0xC1,
  SetTriggerValuesStage1 = 0xC5,
  SetTriggerValuesStage2 = 0xC9,
  SetTriggerValuesStage3 = 0xCD,
  SetTriggerValuesStage4 = 0xD1,

  SetTriggerConfigurationStage0 = 0xC2,
  SetTriggerConfigurationStage1 = 0xC6,
  SetTriggerConfigurationStage2 = 0xCA,
  SetTriggerConfigurationStage3 = 0xCE,
  SetTriggerConfigurationStage4 = 0xD2,

  SetDivider = 0x80,
  SetReadAndDelayCount = 0x81,
  SetFlags = 0x82,
};

bool isShortCommand(uint8_t commandCode) {
  switch (static_cast<CommandShort>(commandCode)) {
  case CommandShort::Reset:
  case CommandShort::Run:
  case CommandShort::ID:
  case CommandShort::GetMeatadata:
  case CommandShort::XON:
  case CommandShort::XOFF:
    return true;
  }
  return false;
}

bool isLongCommand(uint8_t commandCode) {
  switch (static_cast<CommandLong>(commandCode)) {
  case CommandLong::SetDivider:
  case CommandLong::SetReadAndDelayCount:
  case CommandLong::SetFlags:
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
    return true;
  }
  return false;
}

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
    run = false;
    break;
  case CommandShort::Run:
    run = true;
    break;
  case CommandShort::XON:
  case CommandShort::XOFF:
    blog("Short command: 0x%02hhX", static_cast<uint8_t>(command));
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
    break;
  case CommandLong::SetReadAndDelayCount:
    readCount = ((arg & 0xFFFF) + 1) << 2;
    delayCount = ((arg >> 16 & 0xFFFF) + 1) << 2;
    blog("Set delay: %u read: %u", delayCount, readCount);
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

uint8_t *volatile usbBuf = nullptr;
volatile uint32_t usbBufLen = 0;

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
  samples /= DELAY_PRESCALER + 1;
  TIM3->ARR = samples;
  TIM3->CCR1 = samples;
}

void serDivider(uint32_t divider) {
  // Sigrok игнорирует анонсированную частоту и всегда передает делитель для
  // 100МГц
  auto effectiveDevider = (divider + 1) * 84 / 100;
  if (effectiveDevider < 0xFFFF) {
    TIM1->PSC = 0;
    TIM1->ARR = effectiveDevider - 1;
  } else {
  }
}

uint32_t lastSampleIndex() {
  return sizeof(samples) - __HAL_DMA_GET_COUNTER(&hdma_tim1_up);
}

extern "C" int cpp_main() {
  blog("Startup");

  initMeasuring();
  setDelay(4);
  trigger();
  startMeasuring();

  blinkSignal(2);

  blog("DMA Status: %hhX", (uint8_t)HAL_DMA_GetState(&hdma_tim1_up));

  blog("Initial GPIO state: 0x%08X", GPIOB->IDR);
  for (int i = 0; i < 6; i++) {
    blog("Sample[%hhu] = 0x%02hhX", (uint8_t)i, samples[i]);
  }

  while (true) {
    if (run) {
      blog("Reporting... Count: %u", readCount);
      for (uint32_t i = 0; i < readCount; i += sizeof(samples)) {
        while (CDC_Transmit_FS(samples, sizeof(samples)) == USBD_BUSY)
          ;
      }
      blog("Reported!");
      run = false;
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
