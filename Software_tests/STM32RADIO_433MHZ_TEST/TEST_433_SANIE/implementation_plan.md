# Analysis: test1_433 (TX) ↔ test1_rx (RX) Configuration & UART2 Removal

## Project Overview

| | **test1_433 (TX)** | **test1_rx (RX)** |
|---|---|---|
| **MCU** | STM32WLE5 (internal SX1262 Sub-GHz) | ESP32 + RA-02 (SX1278) |
| **Protocol** | FSK with AX.25 framing (NRZI + bit-stuffing) | FSK raw reception via RadioLib |
| **Middleware** | STM32CubeWL SubGHz_Phy | RadioLib (SX1278 driver) |

---

## RF Parameter Comparison

| Parameter | TX (STM32WL) | RX (SX1278/RA-02) | Match? |
|---|---|---|---|
| **Frequency** | 433,018,893 Hz (433.019 MHz) | 433.0189 MHz | ✅ Match |
| **Modulation** | FSK (`MODEM_FSK`) | FSK (`beginFSK`) | ✅ Match |
| **Bitrate** | 1,200 bps | 1.2 kbps | ✅ Match |
| **Freq. Deviation** | 5,000 Hz | 5.0 kHz | ✅ Match |
| **TX Power** | 16 dBm (code says 22 dBm comment but `TX_OUTPUT_POWER=16`) | N/A (RX only) | ➡️ N/A |
| **RX Bandwidth** | 50,000 Hz (TX-side setting, unused) | 50.0 kHz | ✅ Match |
| **Preamble Length** | 8 bytes (radio HW) + 32 AX.25 flags | 8 bytes | ⚠️ See below |
| **Packet Length** | Variable (`fixLen=false`) | Variable (`variablePacketLengthMode`) | ✅ Match |
| **HW CRC** | OFF (`crcOn=false`) | OFF (`setCRC(false)`) | ✅ Match |

---

## Critical Issues Found

### 🔴 Issue 1: Data Whitening Incompatibility (WILL BREAK COMMS)

> [!CAUTION]
> **The STM32WL (SX1262) and SX1278 use INCOMPATIBLE hardware whitening algorithms.** This is the single most likely reason communication would fail.

- **TX (STM32WL/SX1262)**: The SubGHz_Phy middleware enables whitening by default for FSK with a polynomial `x^9 + x^5 + 1` and seed `0x01FF`. However, the **byte-level application of whitening** differs between SX1262 and SX1278 silicon.
- **RX (SX1278)**: `radio.setEncoding(RADIOLIB_ENCODING_WHITENING)` enables the SX1278's hardware whitening with its own internal implementation.
- **The Problem**: Despite both claiming "IBM whitening" with the same polynomial, **multiple community reports and Semtech documentation confirm these two chip generations produce incompatible whitening sequences**. The LFSR apply-order, bit-endianness, and/or initial state behavior differ at the silicon level.

**Fix**: Disable hardware whitening on **both** sides:
- **TX**: Already handled — since the TX data is AX.25 NRZI-encoded (which inherently provides DC balance and transition density), whitening is not needed. The STM32WL middleware `RadioSetTxConfig` with `DcFree = RADIO_DC_FREE_OFF` should be verified in the radio driver layer.
- **RX**: Change `radio.setEncoding(RADIOLIB_ENCODING_WHITENING)` → `radio.setEncoding(RADIOLIB_ENCODING_NRZ)` (plain NRZ, no whitening).

### 🔴 Issue 2: Sync Word Mismatch (LIKELY TO BREAK COMMS)

> [!CAUTION]
> **The sync word on the RX side may not match what the STM32WL actually sends.**

- **RX (SX1278)**: Hardcoded `{0xC1, 0x94, 0xC1}`, 3 bytes — based on the assumption from comments about the STM32WL driver.
- **TX (STM32WL)**: The `Radio.SetTxConfig()` call in [`subghz_phy_app.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/App/subghz_phy_app.c) does NOT explicitly set a sync word. The middleware's `RadioSetTxConfig()` internally calls `SUBGRF_SetSyncWord()` with a default value. This default depends on the middleware version and may **not** be `{0xC1, 0x94, 0xC1}`.

**Fix**: Either:
1. Read back the actual sync word registers (`0x06C0–0x06C7`) from the STM32WL after `SetTxConfig()` to confirm what's actually being used, OR
2. Explicitly set the sync word on the TX side after `SetTxConfig()` using direct register writes, OR
3. On the RX side, try disabling sync word detection entirely for debug (receive all packets and inspect raw bytes).

### 🟡 Issue 3: AX.25 Framing vs. Raw Packet RX (PROTOCOL MISMATCH)

> [!WARNING]
> **The TX sends AX.25-framed data, but the RX treats it as raw FSK packets.**

- **TX**: Sends a full AX.25 frame with: 32× `0x7E` preamble flags → address fields → control → PID → info payload → CRC-CCITT → NRZI encoding → bit-stuffing. The result is a byte stream that looks nothing like the original ASCII text.
- **RX**: Expects to receive raw bytes and prints them as hex/ASCII. It will **NOT** see readable text like `"GPS:-25.263..."`. Instead it will see NRZI-encoded, bit-stuffed AX.25 frame bytes.

**This is not necessarily a bug** — if the goal is simply to verify RF link integrity by seeing non-zero received bytes. But if you expect readable text on the RX, you need either:
1. An AX.25 decoder on the RX side, or
2. Remove AX.25 framing from the TX and send raw ASCII

### 🟡 Issue 4: TX Power Comment vs. Actual Value

- [`subghz_phy_app.h`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/App/subghz_phy_app.h#L47): `TX_OUTPUT_POWER = 16` dBm
- [`subghz_phy_app.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/App/subghz_phy_app.c#L136) comment says "22 dBm, HP PA" but uses `TX_OUTPUT_POWER` which is **16 dBm**.
- The board is configured for HP PA only (`RBI_CONF_RFO = RBI_CONF_RFO_HP`, max 22 dBm per [`radio_board_if.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/Target/radio_board_if.c#L258)).

**Minor**: Just a misleading comment. 16 dBm is valid for HP PA. Update comment or increase to 22 dBm if more range is desired.

### 🟢 Issue 5: DCDC Configuration Inconsistency

- [`radio_board_if.h`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/Target/radio_board_if.h#L78): `IS_DCDC_SUPPORTED = 0U` (DCDC disabled)
- [`radio_conf.h`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/Target/radio_conf.h#L92): `DCDC_ENABLE = 1UL` (DCDC enabled)

These could conflict. The middleware typically checks `RBI_IsDCDC()` which returns `IS_DCDC_SUPPORTED` (0). This means DCDC is effectively **disabled** regardless of `DCDC_ENABLE`. Not a showstopper, but wastes power if your board has a DCDC. Verify your board hardware.

---

## UART2 Removal Plan

> [!IMPORTANT]
> The UART2 is deeply integrated into the STM32CubeWL trace system. The cleanest approach is to **disable tracing at the config level** rather than gutting files, since those files are CubeMX-generated and will be overwritten on regeneration.

### Approach: Disable APP_LOG and make APP_PRINTF a no-op

This keeps all files structurally intact (CubeMX-safe) but prevents any UART2 traffic:

#### [MODIFY] [`sys_conf.h`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/Core/Inc/sys_conf.h)
- Change `APP_LOG_ENABLED` from `1` to `0`

#### [MODIFY] [`subghz_phy_app.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/App/subghz_phy_app.c)
- Remove/comment out all 3 `APP_PRINTF(...)` calls (lines 192, 228, 244)

#### [MODIFY] [`main.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/Core/Src/main.c)
- Remove `#include "usart.h"` (line 24)
- Remove `MX_USART2_UART_Init()` call (line 97)

---

## Open Questions

> [!IMPORTANT]
> 1. **Is the RX supposed to decode AX.25 frames?** If yes, you'll need an AX.25 decoder on the ESP32 side. If the goal is just RF link validation, the raw hex dump is fine.
> 2. **Do you want to keep the UART2 hardware init files** (`usart.c`, `usart_if.c`, `usart.h`, `usart_if.h`) in the project for potential future use, or completely remove them?
> 3. **Does your board have an external DCDC converter?** If yes, `IS_DCDC_SUPPORTED` should be `1U` to match `DCDC_ENABLE`.
> 4. **Should the TX power be increased to 22 dBm** for longer range, or is 16 dBm intentional?

---

## Proposed Changes Summary

| File | Change |
|---|---|
| [`sys_conf.h`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/Core/Inc/sys_conf.h) | `APP_LOG_ENABLED` → `0` |
| [`subghz_phy_app.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/SubGHz_Phy/App/subghz_phy_app.c) | Remove 3× `APP_PRINTF()` calls |
| [`main.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_433/Core/Src/main.c) | Remove `#include "usart.h"` and `MX_USART2_UART_Init()` |
| [`test1_rx.ino`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/test1_rx/test1_rx.ino) | Change whitening from `RADIOLIB_ENCODING_WHITENING` → `RADIOLIB_ENCODING_NRZ` |

## Verification Plan

### Manual Verification
- After UART2 removal: rebuild in STM32CubeIDE — no linker errors, no USART2 clock/GPIO usage
- After whitening fix: flash both devices and verify packets are received (hex dump shows non-zero data)
