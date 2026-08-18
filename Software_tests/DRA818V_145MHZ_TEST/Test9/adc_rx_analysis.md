# APRS RX Decode Chain — Deep Analysis
## Project: STM32WLE5 Test9 — `APP_TEST_RX_MONITOR`

---

## 1. Hardware & Clock Summary

| Parameter | Value |
|-----------|-------|
| MCU | STM32WLE5CCU6 |
| System clock | 48 MHz (HSE=32 MHz → PLL ×6 ÷2) |
| ADC input pin | PA11 → `ADC_IN7` (`DRA_TO_STM32_ADC`) |
| DAC output pin | PA10 → `DAC_OUT1` (`STM32_DAC_TO_DRA`) |
| Sampling timer | TIM2, PSC=0, ARR=4999 |
| Timer frequency | 48 000 000 / 5 000 = **9 600 Hz** |
| Samples / symbol | 9 600 / 1 200 = **8** |

The clocking math is correct and matches `aprs_config.h`. TIM2 IRQ fires at exactly 9 600 Hz.

---

## 2. ADC Configuration — Identified Issues

### 2.1 ADC Clock — Potential Speed Problem

In [`adc.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/Test9/STM32_test9_DRA_TX_RX/Core/Src/adc.c#L44):
```c
hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
// → ADC clock = 48 MHz / 2 = 24 MHz
```

> [!CAUTION]
> The STM32WLE5 ADC maximum clock is **16 MHz** (from the reference manual, RM0461). Running it at 24 MHz is **out of spec** and can cause incorrect conversions, poor linearity, and unpredictable results.

**Fix:** Change to `ADC_CLOCK_SYNC_PCLK_DIV4` (12 MHz) or `ADC_CLOCK_SYNC_PCLK_DIV8` (6 MHz).

### 2.2 Sampling Time — Too Short for Audio Input

```c
hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;  // 1.5 ADC cycles
```

At 24 MHz (overclock) that is **~83 ns total acquisition time** — far too short for a source impedance coming from the DRA818V audio output, which is likely in the kΩ range. This causes the ADC to not fully charge the S&H capacitor, producing distorted samples.

**Fix:** Set at minimum `ADC_SAMPLETIME_39CYCLE_5` (or more). At 12 MHz ADC clock + 39.5-cycle sampling, total conversion time ≈ 4.5 µs, far below the 104 µs tick budget.

### 2.3 ADC Not Using DMA — Polling in ISR

The current design polls `ADC->ISR & ADC_ISR_EOC` inside the TIM2 ISR on every tick. If a conversion is not ready it restarts, which means:
- There is a race condition: first tick → start, second tick → might read stale data.
- No double-buffering.

The VP-Digi reference uses **continuous ADC with DMA** (`ADC_CR2_CONT | DMA`), with **4× oversampling** decimated in software. This is far more robust.

### 2.4 Continuous Mode Disabled

```c
hadc.Init.ContinuousConvMode = DISABLE;
```

The ADC must be re-triggered every tick with `ADC->CR |= ADC_CR_ADSTART`. If the TIM2 ISR takes too long, the next ADC sample arrives late → **jitter in sampling phase**, which destroys correlation accuracy.

---

## 3. AFSK RX Correlator — Issues Found

### 3.1 Sample Rate is Only 8× — Very Low Oversampling

Current implementation: **8 samples per symbol** at 9 600 Hz. The VP-Digi reference uses **4× oversampling of the 9 600 Hz ADC clock** (i.e., 38 400 Hz effective ADC rate, decimated to 9 600 Hz with averaging), giving **8 samples/symbol but with better SNR**. The micro-APRS and LibAPRS references also use higher oversampling.

With only 8 samples/symbol and no filtering, even a small clock offset or noise spike can corrupt a symbol decision.

### 3.2 No Pre-filter / Band-pass Filter Before Correlator

In [`afsk.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/Test9/STM32_test9_DRA_TX_RX/Core/Src/afsk.c#L146-L165), the raw ADC sample (DC-biased subtracted) goes directly into the correlator with no filtering:

```c
rxSampleBuf[rxBufIdx] = (int16_t)raw - (int16_t)AFSK_DAC_MID;
```

The VP-Digi reference applies a **bandpass filter (BPF)** before the correlator, centered between 1200 and 2200 Hz, with a 6 dB pre-emphasis on 2200 Hz. This is critical because:
- The DRA818V audio output has **de-emphasized audio** (FM receiver applies a de-emphasis filter that rolls off highs at 6 dB/octave). This attenuates the 2200 Hz space tone by ~6 dB relative to 1200 Hz.
- Without compensating pre-emphasis, the space tone's correlation energy is systematically weaker, causing MARK/SPACE decisions to be skewed.

### 3.3 DPLL Clock Recovery Is Fragile

The current DPLL in [`afsk.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/Test9/STM32_test9_DRA_TX_RX/Core/Src/afsk.c#L371-L403):
```c
if (rxSampleCnt >= 1U && rxSampleCnt <= 3U) rxSampleCnt--;
else if (rxSampleCnt >= 5U && rxSampleCnt <= 7U) rxSampleCnt++;
```

This is a simple ±1 counter nudge — effectively no fractional PLL. The VP-Digi reference uses a **proper 32-bit overflow PLL**:
```c
dem->pll = (int32_t)((uint32_t)(dem->pll) + (uint32_t)(dem->pllStep));
// Sample at overflow; tune by multiplying pll by 0.74 on transition
```
This approach:
- Allows sub-sample clock tracking.
- Distinguishes locked vs unlocked states (different tune factors).
- Has proven DCD (Data Carrier Detect) for gate control.

### 3.4 No DCD (Data Carrier Detect) — Always Decoding Noise

The current code feeds bits to `AX25_RxBit()` constantly, even when no signal is present. The AX.25 decoder gets flooded with random noise bits. The result is:
- Many spurious frame-start detections (`ax25_dbg_flagCount` incrementing from noise).
- CRC failures everywhere (`ax25_dbg_crcFail` incrementing).
- The rare valid frame gets buried in noise events.

**VP-Digi DCD mechanism:** A PLL-based pulse counter that increments when symbol transitions happen near the expected baud clock zero-crossing and decrements when they happen far from it. Only when `dcdCounter > dcdThres` is the signal considered valid.

### 3.5 Correlator Coefficient Precision

The 8-sample I/Q correlator uses `int16_t` scaled to ±128. The VP-Digi reference uses ±4095 coefficients. With only ±128, multiplying by a 12-bit ADC value (0–4095, centered → ±2048) gives products up to 128×2048 = 262 144, which fits in `int32_t` but with very coarse quantization in the coefficients themselves.

---

## 4. AX.25 Decoder — Minor Issues

The [`ax25.c`](file:///c:/Users/sanie/Documents/GitHub/Comms_CCTE-2026/Software_tests/Test9/STM32_test9_DRA_TX_RX/Core/Src/ax25.c) HDLC state machine looks largely correct. The debug counters are well placed. Key check:

- **Flag detection threshold:** `rxOnesCount > 6` aborts (7+ consecutive 1s). This is correct per HDLC.
- **CRC algorithm:** CRC-16 CCITT reflected (poly=0x8408), standard for AX.25. Correct.

> [!NOTE]
> If `ax25_dbg_flagCount > 0` but `ax25_dbg_crcPass == 0` and `ax25_dbg_crcFail > 0`, the bit errors are in the AFSK decoder (NRZI or clock recovery), not in the AX.25 logic itself.

---

## 5. Comparison with VP-Digi Reference Architecture

| Feature | Current Test9 | VP-Digi Reference |
|---------|--------------|-------------------|
| ADC clock | 24 MHz (**OOB!**) | ~12 MHz (legal) |
| ADC sampling time | 1.5 cycles (**too fast**) | 41.5 cycles |
| ADC mode | Software-triggered, polled | Continuous + DMA |
| Oversampling | 1× (direct 9600 Hz) | 4× (38400 Hz → 9600 Hz) |
| Pre-filter | **None** | BPF (with pre-emphasis or de-emphasis) |
| Post-filter | **None** | LPF (15-tap) after correlator |
| Clock recovery | Simple ±1 nudge | 32-bit overflow PLL |
| DCD | **None** | PLL-based pulse counter |
| Parallel demodulators | 1 | 2 (one flat, one pre-emphasized) |
| Symbol majority vote | **None** (1 sample) | 3-sample majority (rawSymbols & 0x07) |

---

## 6. Root Cause Summary

The RX decode chain fails primarily due to:

1. **🔴 ADC over-clocked (24 MHz > 16 MHz max)** → corrupt samples.
2. **🔴 ADC sampling time too short (1.5 cycles)** → incomplete S&H → distorted samples.
3. **🔴 No BPF / pre-emphasis** → space tone (2200 Hz) under-detected due to FM de-emphasis.
4. **🟡 No DCD gate** → AX.25 decoder flooded with random bits from noise floor.
5. **🟡 Simple DPLL** → poor clock tracking on off-frequency or noisy signals.
6. **🟡 No post-correlator LPF** → no noise smoothing after tone decision.

---

## 7. Recommended Fixes (Priority Order)

### Fix 1 — ADC Clock (Critical, in `adc.c`)
```c
// BEFORE:
hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;  // 24 MHz — ILLEGAL

// AFTER:
hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;  // 12 MHz — legal
```

### Fix 2 — ADC Sampling Time (Critical, in `adc.c` and `afsk.c`)
```c
// BEFORE:
hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;

// AFTER:
hadc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_39CYCLE_5;
// Conversion time @ 12 MHz ADC clock: (39.5 + 12.5) / 12e6 ≈ 4.3 µs (< 104 µs budget)
```

In `afsk.c`, also update the channel config:
```c
sADCCh.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;  // already correct, ensure it picks up new config
```

### Fix 3 — Add Pre-emphasis BPF (High Priority, in `afsk.c`)

Port the VP-Digi `bpf1200` filter (8-tap, 6 dB pre-emphasis on 2200 Hz) and apply it to each raw sample before storing in `rxSampleBuf`. This compensates FM de-emphasis.

```c
// From VP-Digi modem.c — copy these coefficients:
static const int16_t bpf1200_preemph[8] = {
     728, -13418,  -554, 19493,  -554, -13418,   728,  2104
};
// Apply with gainShift = 15 (divide by 32768)
```

### Fix 4 — Implement LPF After Correlator (High Priority, in `afsk.c`)

After the IQ correlation energy difference, apply a 15-tap LP filter before the symbol decision. Use VP-Digi's `lpf1200` coefficients:
```c
static const int16_t lpf1200[15] = {
    -6128, -5974, -2503, 4125, 12679, 21152, 27364,
     29643, 27364, 21152, 12679, 4125, -2503, -5974, -6128
};
```

### Fix 5 — Replace DPLL with 32-bit Overflow PLL (Medium Priority, in `afsk.c`)

Port the VP-Digi PLL:
- `pllStep = (1 << 32) / 8` (since N=8 samples/symbol at 9600 Hz)
- On symbol transition: `pll = pll * 0.74` (locked) or `pll = pll * 0.50` (unlocked)
- Sample at PLL counter overflow (`pll < 0 && previous > 0`)

### Fix 6 — Add DCD Gate (Medium Priority, in `afsk.c` or `app.c`)

Port the VP-Digi DCD pulse counter:
- `dcdMax=60`, `dcdThres=20`, `dcdInc=2`, `dcdDec=1`, `dcdTune=0.74`
- Only call `AX25_RxBit()` when `dcd == 1`

### Fix 7 — Symbol Majority Vote (Low Priority, in `afsk.c`)

Before NRZI decoding, use a 3-sample majority vote:
```c
uint8_t sym3 = rawSymbols & 0x07;
uint8_t sym = (sym3 == 0b111 || sym3 == 0b110 || sym3 == 0b101 || sym3 == 0b011) ? 1 : 0;
```

---

## 8. Debug Procedure (in STM32CubeIDE)

Add these to Live Expressions while running `APP_TEST_RX_MONITOR`:

| Variable | Expected when working |
|----------|-----------------------|
| `g_dbg_timerTicks` | Incrementing at ~9600/s |
| `g_dbg_rxBits` | Incrementing (any signal present) |
| `ax25_dbg_flagCount` | Incrementing when valid APRS is on air |
| `ax25_dbg_crcFail` | Should be 0 or very low |
| `ax25_dbg_crcPass` | Should increment with each valid frame |
| `g_dbg_rxFrames` | Should equal `ax25_dbg_crcPass` |

**Diagnosis tree:**
1. `g_dbg_timerTicks` not growing → TIM2 ISR not running.
2. `g_dbg_rxBits` = 0 → ADC not producing samples (check ADC clock/pin/power).
3. `ax25_dbg_flagCount` = 0 → No HDLC flags found → bit clock or NRZI wrong.
4. `ax25_dbg_flagCount` > 0 but `ax25_dbg_crcFail` >> 0 → Bits mostly correct but errors → pre-emphasis, DPLL, or sampling time issue.
5. `ax25_dbg_crcPass` > 0 but `g_dbg_rxFrames` = 0 → Callback registration bug (shouldn't happen with current code).
