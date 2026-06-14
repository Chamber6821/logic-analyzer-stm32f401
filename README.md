# STM32 Logic Analyzer (OLS)

This firmware implement [Extended SUMP](https://docs.buspirate.com/docs/binmode-reference/protocol-sump/) protocol
to communicate with software like [PulseView](https://sigrok.org/wiki/PulseView)

The firmware adopt for Black Pill (STM32F401CCU6)

## Characteristics

- `PB0-PB7` - channels 0-7
- `PA1` - reference 100kHz PWM
- 56k - samples buffer
- 16.8MHz - max sample rate
- support simple trigger for any channels (on rise or fall)

## Requirements

- Make
- CMake
- GCC (supports C++23)
- OpenOCD

## Commands

- Build - `make build`
- Build and upload - `make upload`
- Collect logs - `make rtt`
- Debug execution - `make gdb`
