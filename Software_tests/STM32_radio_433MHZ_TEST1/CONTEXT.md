# Sub-GHz 433 MHz Radio Test — Context & Reference

> **Module:** STM32RADIO_433MHZ_TEST / TEST_433_SANIE  
> **Parent Project:** [Comms_CCTE-2026 Root Context](../../../CONTEXT.md)  
> **Target Target Hardware:** STM32WLE5CCU6 (Payload 1) & ESP32-S3 RX/TX Test Node

---

## Overview

This directory contains the firmware test environment and validation suites for the **433 MHz Sub-GHz wireless communication subsystem** on the URUTAU-III suborbital rocket communication board.

### Key Functional Blocks:
1. **STM32WLE5 Internal Sub-GHz Radio:**
   - Integrated Semtech SX126x radio core.
   - Modulation: LoRa / (G)FSK / (G)MSK.
   - Frequency: 433.0 MHz (configurable across Sub-GHz ISM bands).
   - Middleware: STMicroelectronics `SubGHz_Phy` middleware (`Radio.Init()`, `Radio.Send()`, `Radio.Rx()`, `OnTxDone()`, `OnRxDone()`).

2. **RF Front-End & Antennas:**
   - Infineon `BGS12WN6` SPDT RF switch (`PA9` control line) for TX/RX path selection.
   - Discrete LC matching network and harmonic low-pass filter connected to SMA connector `J4`.
   - Optional impedance transformation via Mini-Circuits `ADT1.5-122+` / `NCS1.5-232+` balun on `pcb_antennas`.

3. **Inter-Module Directory Map:**
   - [`test1_433/`](./test1_433/): STM32WLE5 SubGHz PHY firmware configuration and driver framework.
   - [`test1_rx_idf/`](./test1_rx_idf/): ESP32 IDF receiver test station for validating over-the-air packet reception from the STM32.
   - [`../TEST_433_STM_RX/`](../TEST_433_STM_RX/): Complementary test fixture configuring STM32 as receiver and ESP32 as transmitter.

For complete hardware schematics, pin mappings, power trees, and APRS VHF modem documentation, refer to the main [CONTEXT.md](../../../CONTEXT.md).
