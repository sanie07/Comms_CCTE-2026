# Sub-GHz 433 MHz Radio Test — Context & Reference

> **Module:** STM32RADIO_433MHZ_TEST / TEST_433_STM_RX  
> **Parent Project:** [Comms_CCTE-2026 Root Context](../../../CONTEXT.md)  
> **Target Hardware:** STM32WLE5CCU6 (Payload 1 Receiver) & ESP32-S3 TX Test Node

---

## Overview

This directory contains the firmware test environment and validation suites for the **433 MHz Sub-GHz wireless communication subsystem** on the URUTAU-III suborbital rocket communication board, configured with the **STM32WLE5 as the receiver** and the **ESP32-S3 as the transmitter**.

### Key Functional Blocks:
1. **ESP32-S3 Sub-GHz Transmitter (`test1_tx_idf`):**
   - Transmits periodic AX.25 UI telemetry frames over 433.018 MHz using an external Semtech SX1278 / HopeRF RFM98 transceiver.
   - Modulation: GFSK 1200 bps, $F_{\text{dev}} = 5.0\text{ kHz}$, $BT = 0.5$, 8-byte preamble, 3-byte sync word (`0xC1, 0x94, 0xC1`), PA_BOOST @ +17 dBm.
   - Built on ESP-IDF v5.x with USB Serial/JTAG console diagnostics.

2. **STM32WLE5 Internal Sub-GHz Receiver (`test1_433`):**
   - Integrated Semtech SX126x radio core listening for over-the-air packets.
   - Modulation: (G)FSK matching ESP32-S3 PHY configuration.
   - Middleware: STMicroelectronics `SubGHz_Phy` middleware (`Radio.Init()`, `Radio.Rx()`, `OnRxDone()`).
   - RF Switch: Infineon `BGS12WN6` SPDT switch (`PA9` set for RX path).

3. **Inter-Module Directory Map:**
   - [`test1_433/`](./test1_433/): STM32WLE5 SubGHz PHY firmware configured for RX.
   - [`test1_tx_idf/`](./test1_tx_idf/): ESP32-S3 IDF transmitter test station.
   - [`../TEST_433_SANIE/`](../TEST_433_SANIE/): Complementary test fixture configuring STM32 as transmitter and ESP32 as receiver.

For complete hardware schematics, pin mappings, power trees, and APRS VHF modem documentation, refer to the main [CONTEXT.md](../../../CONTEXT.md).

