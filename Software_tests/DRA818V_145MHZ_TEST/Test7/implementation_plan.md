# Implement DRA818V Periodic Data Transmission & SPI Confirmation

This plan adds the requested periodic transmission feature using the DRA818V module and the internal STM32 DAC, while maintaining and extending the SPI status confirmation.

## Open Questions

> [!IMPORTANT]
> - **Blocking vs Non-Blocking:** To generate the "simplest data" (a 500Hz audio tone), I plan to use a simple blocking loop with `HAL_Delay(1)` for 1 second inside `App_Run()`. Since this is a test, this should be fine, but let me know if you prefer a non-blocking timer-based approach.
> - **Audio Signal:** The simplest data will be a 500 Hz square wave (toggling the DAC value between 1900 and 2100 every 1ms). Is this acceptable for your test?
> - **EXTI for SPI CS:** I noticed `HAL_GPIO_EXTI_Callback` is defined in `app.c` for `SPI1_CS_Pin`, but CubeMX didn't seem to generate the EXTI interrupt handler in `stm32wlxx_it.c`. Make sure EXTI is enabled in your CubeMX `.ioc` file for `PA0` (SPI1 CS), otherwise the SPI status won't update when the ESP32 reads it!

## Proposed Changes

### Core/Inc/app.h
- Define clear states for the SPI confirmation messages to be sent to the master.

### Core/Src/app.c
- **Fix PTT Initialization:** Currently, CubeMX initializes the PTT pin LOW (which means ACTIVE for the DRA818V). I will explicitly set it HIGH (inactive) at the start of `App_Init()`.
- **SPI Status Tracking:** Map the handshake results and transmission states to the new macros (`SPI_STATUS_HANDSHAKE_OK`, `SPI_STATUS_TX_ACTIVE`, `SPI_STATUS_TX_DONE`).
- **Periodic Transmission:** Add logic in `App_Run()` using `HAL_GetTick()` to trigger every 10 seconds:
  1. Pull PTT LOW to start transmitting.
  2. Update SPI status to `SPI_STATUS_TX_ACTIVE`.
  3. Start the internal DAC (`hdac`).
  4. Generate a simple 500Hz square wave by alternating DAC values for 1 second.
  5. Stop the DAC.
  6. Release PTT to HIGH.
  7. Update SPI status to `SPI_STATUS_TX_DONE`.

## Verification Plan
- Flash the STM32 and monitor the PTT and DAC outputs using an oscilloscope or logic analyzer.
- Ensure the SPI master receives the sequence of states: Handshake OK -> TX Active -> TX Done -> TX Active (every 10s).
