# Periodic DRA818V Transmission with SPI Confirmation

This walkthrough covers the changes made to `STM32_test6_DRA_TX` to support periodic data transmission via the DRA818V and SPI-based status reporting.

## Changes Made

### 1. SPI Status Definitions (`app.h`)
We introduced new macros to track the transmission state cleanly. This extends the simple `1`/`2` handshake result with values for when a transmission is active or completed.
- `SPI_STATUS_INIT (0)`
- `SPI_STATUS_HANDSHAKE_OK (1)`
- `SPI_STATUS_HANDSHAKE_ERR (2)`
- `SPI_STATUS_TX_ACTIVE (3)`
- `SPI_STATUS_TX_DONE (4)`

### 2. State Initialization & PTT Setup (`app.c`)
- Included the `dac.h` header for using the DAC module.
- Added logic in `App_Init()` to ensure the `STM32_TO_DRA_PTT_Pin` (PA7) starts `HIGH`. Since the DRA818V PTT is active LOW, keeping it HIGH prevents the radio from randomly transmitting upon startup.
- The `s_spiStatusMsg` variable is now assigned using the macros defined in `app.h`.

### 3. Periodic Transmission Logic (`app.c`)
- Updated `App_Run()` to incorporate a non-blocking `10-second` timer using `HAL_GetTick()`.
- Every 10 seconds, the device will:
  1. Pull PTT `LOW` (start transmitting).
  2. Set `s_spiStatusMsg = SPI_STATUS_TX_ACTIVE`.
  3. Power on the DAC (`hdac`, `DAC_CHANNEL_1`).
  4. Run a 1-second blocking loop that toggles the DAC value between `1900` and `2100` every 1 ms. This produces a simple **500Hz square wave** audio tone that the DRA818V radio will transmit.
  5. Power off the DAC.
  6. Return PTT `HIGH` (stop transmitting).
  7. Set `s_spiStatusMsg = SPI_STATUS_TX_DONE`.

Because the SPI interface updates are triggered asynchronously by the external master driving the Chip Select (`CS`) line via `HAL_GPIO_EXTI_Callback`, the master will poll `s_spiStatusMsg` and read `3` during the 1-second transmission burst, and `4` when it finishes.

## Validation & Next Steps
- **Hardware Observation:** Connect an oscilloscope to the DAC output (`PA10`) and observe the 500Hz square wave every 10 seconds. You should also see PTT (`PA7`) drop LOW during the tone.
- **RF Validation:** Tune a secondary VHF receiver to the configured frequency and listen for the periodic 500Hz beeps.
- **SPI Polling:** On the ESP32 (or SPI Master) side, poll the STM32 via SPI. It should return `1` initially, `3` for 1 second out of every 10, and `4` for the remaining 9 seconds.

> [!TIP]
> Make sure `EXTI` mapping for `PA0` (SPI1_CS) is enabled in your STM32CubeIDE (`.ioc`) config. Without it, the master's CS signal won't trigger `HAL_GPIO_EXTI_Callback` in `app.c`.
