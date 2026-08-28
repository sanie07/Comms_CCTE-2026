# ESP32-S3 Sub-GHz 433 MHz Transmitter (`test1_tx_idf`)

> **Module:** `Software_tests/STM32RADIO_433MHZ_TEST/TEST_433_STM_RX/test1_tx_idf`  
> **Target Hardware:** ESP32-S3 (Xtensa Dual-Core @ 240 MHz) & Semtech SX1278 / HopeRF RFM98  
> **Companion Receiver Node:** STM32WLE5CCU6 SubGHz PHY Receiver (`../test1_433`)  
> **Framework:** ESP-IDF (v5.x)  

---

## 1. Overview

This firmware configures the **ESP32-S3** as a dedicated **433 MHz Sub-GHz FSK transmitter** broadcasting periodic **AX.25 UI telemetry frames** to test and validate the **STM32WLE5 receiver node** on the URUTAU-III suborbital rocket communication board.

### Features:
- **Transceiver Core:** SX1278 / RFM98 operated via ESP-IDF SPI Master driver.
- **Physical Modulation (FSK Path A):**
  - Carrier Frequency: `433.018893 MHz` (`433018893 Hz`, aligned with STM32 HSE crystal offset).
  - Bitrate: `1200 bps`.
  - Frequency Deviation: `5.0 kHz` ($F_{\text{dev}} = 5000\text{ Hz}$).
  - Pulse Shaping: Gaussian filter $BT = 0.5$.
  - Preamble: 8 bytes `0x55`.
  - Sync Word: 3 bytes (`0xC1, 0x94, 0xC1`).
  - Variable length packet format (max 63 bytes payload).
  - RF Output Power: `+17 dBm` using internal `PA_BOOST`.
- **Protocol:** AX.25 Unnumbered Information (UI) frame encoder with CRC-16 CCITT FCS.
- **Console / Telemetry Monitor:** Native USB Serial/JTAG console support.
- **Visual Feedback:** LED blink on packet transmission.

---

## 2. Hardware Pinout (ESP32-S3 $\leftrightarrow$ SX1278 / RFM98)

| SX1278 / RFM98 Pin | ESP32-S3 Pin | Description |
|---|---|---|
| **VCC (3.3V)** | `3V3` | Clean 3.3V Power Supply |
| **GND** | `GND` | Ground Reference |
| **SCK** | `GPIO 9` | SPI Clock |
| **MISO** | `GPIO 10` | SPI Master-In-Slave-Out |
| **MOSI** | `GPIO 11` | SPI Master-Out-Slave-In |
| **NSS / CS** | `GPIO 12` | SPI Chip Select (Active LOW) |
| **RST** | `GPIO 13` | Hardware Reset (Active LOW) |
| **DIO0** | `GPIO 14` | Interrupt (`PacketSent` / `PayloadReady`) |
| **STATUS LED** | `GPIO 21` | Visual TX indicator |

---

## 3. Build & Flash Instructions

Ensure ESP-IDF environment is active in your terminal (e.g. ESP-IDF v5.1+):

```bash
# 1. Navigate to project directory
cd Software_tests/STM32RADIO_433MHZ_TEST/TEST_433_STM_RX/test1_tx_idf

# 2. Set target to ESP32-S3 (only needed once)
idf.py set-target esp32s3

# 3. Build project
idf.py build

# 4. Flash and open USB Serial/JTAG monitor (replace COM_PORT with your device port)
idf.py -p COM_PORT flash monitor
```

---

## 4. Expected Console Output

Upon boot, the ESP32-S3 initializes the SX1278 module and begins transmitting:

```
==================================================
 ESP32-S3 Sub-GHz 433 MHz Transmitter (test1_tx_idf)
 SX1278 / RFM98 FSK AX.25 Telemetry Transmitter
==================================================
Target:       ESP32-S3 (Xtensa Dual-Core @ 240 MHz)
Frequency:    433018893 Hz (433.018 MHz)
Modulation:   FSK 1200 bps, FDEV=5.0 kHz, BT=0.5
Sync Word:    0xC1 0x94 0xC1 (3 bytes)
TX Power:     +17 dBm (PA_BOOST)
TX Interval:  5000 ms
Pinout:       SCK=9 MISO=10 MOSI=11 CS=12 RST=13 DIO0=14 LED=21
==================================================
[1] Initializing SX1278 radio...
[2] SX1278 Ready! Chip Version: 0x12
Starting periodic AX.25 telemetry transmission loop...

--------------------------------------------------
>>> TRANSMITTING PACKET #0 (56 bytes) >>>
  Dest: FIUNA1-1
  Src:  CCTE-0
  Info: GPS:-25.330243,-57.517492,100.0,SAT:4 #0
  Hex Payload:
  00: 8C 92 AA 9C 62 62 62 86 86 A8 8A 40 40 61 03 F0 
  10: 47 50 53 3A 2D 32 35 2E 33 33 30 32 34 33 2C 2D 
  20: 35 37 2E 35 31 37 34 39 32 2C 31 30 30 2E 30 2C 
  30: 53 41 54 3A 34 20 23 30 3A 9A 
>>> TX SUCCESS in 378 ms (Rate: 1200 bps) <<<
--------------------------------------------------
```

