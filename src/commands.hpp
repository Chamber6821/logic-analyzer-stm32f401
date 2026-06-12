#pragma once

#include <cstdint>

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

inline bool isShortCommand(uint8_t commandCode) {
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

inline bool isLongCommand(uint8_t commandCode) {
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
