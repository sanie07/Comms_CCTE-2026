/**
 * @file    afsk.c
 * @brief   Bell 202 AFSK modem implementation for STM32WLE5CCU6.
 *
 * Timing (48 MHz system clock, TIM2 PSC=0 ARR=4999):
 *   TIM2 period = 48 000 000 / 5 000 = 9 600 Hz
 *   Samples per 1200-baud symbol = 9600 / 1200 = 8  (exact integer)
 *
 * TX DDS (Direct Digital Synthesis):
 *   16-bit Q8 phase accumulator: upper byte indexes 256-entry sine table.
 *   phaseInc_MARK  = 256 * 1200 / 9600 * 256 =  8192  → +32.0  table steps/tick
 *   phaseInc_SPACE = 256 * 2200 / 9600 * 256 = 15019  → +58.67 table steps/tick
 *   Continuous-phase FSK: accumulator is never reset on tone change.
 *
 * RX correlator:
 *   8-sample sliding window; integer I/Q correlation against precomputed
 *   reference vectors for 1200 Hz and 2200 Hz.  L1-norm magnitude
 *   comparison (avoids sqrt).  Symbol output every 8 samples.
 *   NRZI decode: same symbol as previous → bit=1, different → bit=0.
 */

#include "afsk.h"
#include "aprs_config.h"
#include "main.h"
#include "dac.h"
#include "adc.h"
#include <string.h>

/* ================================================================
 * Phase increment constants  (Q8 fixed-point)
 * phaseInc = round( AFSK_SINE_LEN * freq / AFSK_SAMPLE_RATE * 256 )
 * ================================================================ */
#define PHASE_INC_MARK  \
    ((uint16_t)(((uint32_t)AFSK_SINE_LEN * AFSK_MARK_HZ  * 256UL) / AFSK_SAMPLE_RATE))
/* = (256 * 1200 * 256) / 9600 = 8192  (exact) */

#define PHASE_INC_SPACE \
    ((uint16_t)(((uint32_t)AFSK_SINE_LEN * AFSK_SPACE_HZ * 256UL) / AFSK_SAMPLE_RATE))
/* = (256 * 2200 * 256) / 9600 = 15019 */

/* ================================================================
 * TX state
 * ================================================================ */
extern DAC_HandleTypeDef hdac;

static uint16_t         sineTable[AFSK_SINE_LEN];

static volatile bool    txActive     = false;
static volatile bool    txDone       = false;
static volatile bool    toneActive   = false;
static volatile bool    calAltActive = false;

static const uint8_t   *txBits      = NULL;  /* Pointer to loaded bit-stream */
static volatile uint16_t txBitTotal = 0;     /* Total bits in stream         */
static volatile uint16_t txBitIdx   = 0;     /* Next bit to transmit         */

static uint16_t          txPhaseAcc  = 0;    /* 16-bit Q8 DDS accumulator    */
static uint16_t          txPhaseInc  = PHASE_INC_MARK;
static uint8_t           txNRZI      = 0;    /* Current tone: 0=MARK 1=SPACE  */
static uint8_t           txSampleCnt = 0;    /* Samples elapsed in symbol (0-7)*/
static uint32_t          toneSampleCnt = 0;
static uint16_t          calAltHalfPeriods = 0;

/* ================================================================
 * RX state — Bell 202 AFSK demodulator with BPF + LPF + PLL + DCD
 *
 * Signal path (per 9600 Hz tick):
 *   ADC raw → centre (-2048) → 8-tap BPF → correlator I/Q →
 *   >>14 → L1-norm diff → 15-tap LPF → symbol decision →
 *   32-bit PLL (clock recovery) + DCD → 3-sample majority vote →
 *   NRZI decode → AX25_RxBit() (gated by DCD).
 *
 * Hardware notes (from schematic):
 *   DC bias at PA11 = 3.3 V × R3/(R3+R4) = 3.3 V × 10k/20k = 1.65 V
 *                   = 2048 ADC counts  →  AFSK_DAC_MID = 2048 is correct.
 *   Source impedance = R4‖R3 = 5 kΩ.
 *   AC coupling via C37 (220 nF) + C44 (1 µF), fc ≈ 177 Hz — fine for
 *   1200/2200 Hz AFSK.
 *   DRA818V AF_OUT provides de-emphasised FM audio.  The 8-tap BPF
 *   applies 6 dB pre-emphasis (boosts 2200 Hz / SPACE tone) to
 *   compensate, restoring equal mark/space amplitudes at the correlator.
 *
 * BPF (8-tap, VP-Digi bpf1200, Fs=9600):
 *   gainShift = 15  (accumulator >> 32768).
 *
 * Correlator coefficients (Q7, ×128):
 *   1200 Hz: 45°/sample;   2200 Hz: 82.5°/sample.
 *   Outputs right-shifted >>14 before L1-norm.
 *
 * LPF (15-tap, VP-Digi lpf1200, fc ≈ 600 Hz, Fs=9600):
 *   int64_t accumulator, gainShift = 15.
 *
 * PLL (32-bit overflow, step = 2^32/8 = 0x20000000):
 *   Symbol sampled at counter overflow (sign: + → −).
 *   On transition: counter × 0.74 (DCD locked) or × 0.50 (unlocked).
 *
 * DCD (pulse counter, VP-Digi algorithm):
 *   +2 when transition near PLL zero, −1 when far away.
 *   DCD asserted when counter > 20 (max 60).
 * ================================================================ */

/* --- 8-tap BPF: pre-emphasis (+6 dB @ 2200 Hz), Fs=9600 Hz ---
 *  (VP-Digi bpf1200, gainShift=15)                              */
static const int16_t c_bpf[8] = {
     728, -13418,  -554, 19493,  -554, -13418,   728,  2104
};

/* --- 15-tap LPF, fc ≈ 600 Hz, Fs=9600 Hz ---
 *  (VP-Digi lpf1200, gainShift=15, int64_t accumulator)         */
static const int16_t c_lpf[15] = {
    -6128, -5974, -2503,  4125, 12679, 21152, 27364,
    29643, 27364, 21152, 12679,  4125, -2503, -5974, -6128
};

/* --- I/Q correlator reference vectors (Q7, ×128) --- */
static const int16_t c1200I[8] = { 128,  90,    0,  -90, -128,  -90,    0,   90 };
static const int16_t c1200Q[8] = {   0,  90,  128,   90,    0,  -90, -128,  -90 };
static const int16_t c2200I[8] = { 128,  17, -124,  -49,  111,   78,  -90, -102 };
static const int16_t c2200Q[8] = {   0, 127,   33, -118,  -64,  101,   90,  -78 };

/* BPF state buffer (8 taps) */
static int32_t  bpfBuf[8];
/* LPF state buffer (15 taps) */
static int32_t  lpfBuf[15];

/* Correlator circular buffer — stores BPF-filtered samples */
static int16_t  rxSampleBuf[8];
static uint8_t  rxBufIdx = 0;

/* 32-bit overflow PLL for bit-clock recovery.
 * Step = 2^32 / 8 = 0x2000_0000 → overflows once per symbol period.
 * Symbol is sampled when PLL sign changes from positive to negative.
 * On symbol transition the PLL is nudged toward zero:
 *   locked   → × (189/256) ≈ 0.74
 *   unlocked → × (128/256) = 0.50                                    */
static int32_t rxPll = 0;
#define RX_PLL_STEP        ((int32_t)0x20000000)
#define RX_PLL_LOCKED_TUNE 189   /* 0.74 × 256 (integer, avoid float in ISR) */
#define RX_PLL_UNLKD_TUNE  128   /* 0.50 × 256 */
#define RX_PLL_TUNE_SHIFT  8U

/* DCD pulse counter (VP-Digi algorithm) */
static int32_t  dcdPll     = 0;   /* DCD PLL counter (same step as bit PLL) */
static uint8_t  dcdLastSym = 0;   /* Previous symbol for transition detect  */
static uint16_t dcdCounter = 0;   /* Pulse counter                          */
#define DCD_MAXPULSE  60U
#define DCD_THRESHOLD 20U
#define DCD_INC       2U
#define DCD_DEC       1U
#define DCD_TUNE      189   /* 0.74 × 256 */

/* Symbol shift registers */
static uint8_t rxRawSym  = 0U;  /* 3 LSBs = last 3 raw symbol decisions    */
static uint8_t rxSyncSym = 0U;  /* 2 LSBs = last 2 sync'd symbols (NRZI)  */

/* ================================================================
 * RX output (polled by app.c after each ISR tick)
 * ================================================================ */
volatile uint8_t afsk_rx_bit_ready = 0;
volatile uint8_t afsk_rx_bit       = 0;
volatile uint8_t afsk_rx_dcd       = 0;  /* 1 = carrier detected, 0 = noise */

/* ================================================================
 * Private helper: L1 absolute value
 * ================================================================ */
static inline int32_t i32abs(int32_t x) { return (x < 0) ? -x : x; }

static const int16_t sineQuarterQ15[65] = {
        0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
     6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767
};

static int16_t sineQ15(uint8_t idx)
{
    uint8_t quadrant = idx >> 6U;
    uint8_t offset = idx & 0x3FU;

    switch (quadrant)
    {
    case 0U:
        return sineQuarterQ15[offset];
    case 1U:
        return sineQuarterQ15[64U - offset];
    case 2U:
        return (int16_t)-sineQuarterQ15[offset];
    default:
        return (int16_t)-sineQuarterQ15[64U - offset];
    }
}

static uint16_t phaseIncForHz(uint16_t hz)
{
    return (uint16_t)(((uint32_t)AFSK_SINE_LEN * (uint32_t)hz * 256UL) /
                      (uint32_t)AFSK_SAMPLE_RATE);
}

/* ================================================================
 * rxApplyBPF — 8-tap band-pass pre-emphasis filter.
 *
 * Boosts SPACE tone (2200 Hz) by ~6 dB to compensate the DRA818V
 * FM discriminator's built-in de-emphasis (6 dB/octave roll-off).
 * Coefficients: VP-Digi bpf1200.  gainShift = 15.
 * ================================================================ */
static int16_t rxApplyBPF(int16_t sample)
{
    for (uint8_t i = 7U; i > 0U; i--)
        bpfBuf[i] = bpfBuf[i - 1U];
    bpfBuf[0] = (int32_t)sample;

    int32_t acc = 0;
    for (uint8_t i = 0U; i < 8U; i++)
        acc += bpfBuf[i] * (int32_t)c_bpf[i];

    return (int16_t)(acc >> 15);
}

/* ================================================================
 * rxApplyLPF — 15-tap low-pass filter on correlator output.
 *
 * Smooths the mark/space energy difference to suppress noise spikes
 * that would otherwise cause spurious symbol transitions.
 * Coefficients: VP-Digi lpf1200.  gainShift = 15, int64_t acc.
 * ================================================================ */
static int32_t rxApplyLPF(int32_t sample)
{
    for (uint8_t i = 14U; i > 0U; i--)
        lpfBuf[i] = lpfBuf[i - 1U];
    lpfBuf[0] = sample;

    int64_t acc = 0;
    for (uint8_t i = 0U; i < 15U; i++)
        acc += (int64_t)lpfBuf[i] * (int64_t)c_lpf[i];

    return (int32_t)(acc >> 15);
}

/* ================================================================
 * rxGetCorrelation — I/Q correlator on the BPF-filtered sample ring.
 *
 * Returns positive value when MARK (1200 Hz) dominates,
 *         negative value when SPACE (2200 Hz) dominates.
 * Accumulator right-shifted >>14 before L1-norm to prevent
 * overflow when the result is fed into the 15-tap LPF.
 * ================================================================ */
static int32_t rxGetCorrelation(void)
{
    int32_t mI = 0, mQ = 0, sI = 0, sQ = 0;

    for (uint8_t k = 0U; k < 8U; k++)
    {
        uint8_t  idx = (uint8_t)((rxBufIdx - k + 8U) & 7U);
        int16_t  s   = rxSampleBuf[idx];

        mI += (int32_t)s * (int32_t)c1200I[k];
        mQ += (int32_t)s * (int32_t)c1200Q[k];
        sI += (int32_t)s * (int32_t)c2200I[k];
        sQ += (int32_t)s * (int32_t)c2200Q[k];
    }

    /* Scale down before L1-norm to prevent LPF input overflow */
    mI >>= 14;  mQ >>= 14;
    sI >>= 14;  sQ >>= 14;

    int32_t markMag  = i32abs(mI) + i32abs(mQ);
    int32_t spaceMag = i32abs(sI) + i32abs(sQ);

    return markMag - spaceMag;  /* positive = MARK dominant, negative = SPACE dominant */
}

/* ================================================================
 * AFSK_Init
 * ================================================================ */
void AFSK_Init(void)
{
    for (uint16_t i = 0U; i < AFSK_SINE_LEN; i++)
    {
        int32_t sample = (int32_t)AFSK_DAC_MID +
                         (((int32_t)AFSK_DAC_AMP * (int32_t)sineQ15((uint8_t)i)) / 32767L);

        if (sample < 0)
            sample = 0;
        else if (sample > 4095)
            sample = 4095;

        sineTable[i] = (uint16_t)sample;
    }

    /* Clear all RX state */
    memset(rxSampleBuf, 0, sizeof(rxSampleBuf));
    memset(bpfBuf,      0, sizeof(bpfBuf));
    memset(lpfBuf,      0, sizeof(lpfBuf));
    rxBufIdx    = 0U;
    rxPll       = 0;
    dcdPll      = 0;
    dcdLastSym  = 0U;
    dcdCounter  = 0U;
    afsk_rx_dcd = 0U;
    rxRawSym    = 0U;
    rxSyncSym   = 0U;

    /* ---- DAC ---- */
    /* Start DAC channel 1 (PA10) and output silence mid-point */
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, AFSK_DAC_MID);

    /* ---- ADC ---- */
    /* Configure ADC channel 7 (PA11 = DRA_TO_STM32_ADC) */
    ADC_ChannelConfTypeDef sADCCh = {0};
    sADCCh.Channel      = ADC_CHANNEL_7;
    sADCCh.Rank         = ADC_REGULAR_RANK_1;
    sADCCh.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    HAL_ADC_ConfigChannel(&hadc, &sADCCh);

    /* Self-calibrate (improves linearity) */
    HAL_ADCEx_Calibration_Start(&hadc);

    /* Enable ADC and trigger first conversion.
     * Subsequent conversions are re-triggered in AFSK_TimerTick(). */
    HAL_ADC_Start(&hadc);
}

/* ================================================================
 * TX API
 * ================================================================ */

void AFSK_TX_Load(const uint8_t *bits, uint16_t bitCount)
{
    txBits     = bits;
    txBitTotal = bitCount;
    txBitIdx   = 0;
    txDone     = false;
}

void AFSK_TX_Start(void)
{
    toneActive   = false;
    calAltActive = false;
    txNRZI      = 0;               /* Start transmitting MARK (1200 Hz) */
    txPhaseAcc  = 0;
    txPhaseInc  = PHASE_INC_MARK;
    txSampleCnt = 0;
    txDone      = false;
    txActive    = true;            /* Enable ISR TX path */
}

void AFSK_TX_Stop(void)
{
    txActive = false;
    toneActive = false;
    calAltActive = false;
    /* Return DAC to mid-point (silence) via direct register write */
    DAC->DHR12R1 = AFSK_DAC_MID;
}

bool AFSK_TX_IsDone(void)
{
    return txDone;
}

void AFSK_ToneStart(uint16_t hz)
{
    txActive = false;
    toneActive = true;
    calAltActive = false;
    txDone = false;
    txPhaseAcc = 0;
    txPhaseInc = phaseIncForHz(hz);
    toneSampleCnt = 0;
}

void AFSK_CalAltStart(void)
{
    txActive = false;
    toneActive = true;
    calAltActive = true;
    txDone = false;
    txPhaseAcc = 0;
    txPhaseInc = PHASE_INC_MARK;
    toneSampleCnt = 0;
    calAltHalfPeriods = 0;
}

void AFSK_ToneStop(void)
{
    toneActive = false;
    calAltActive = false;
    txDone = true;
    DAC->DHR12R1 = AFSK_DAC_MID;
}

/* ================================================================
 * AFSK_TimerTick — called from TIM2 ISR at 9600 Hz
 *
 * During packet TX the ISR updates the DAC first, then still samples the
 * ADC so the PA10-to-PA11 loopback test can validate AX.25 decode.
 * ================================================================ */
void AFSK_TimerTick(void)
{
    if (toneActive)
    {
        txPhaseAcc += txPhaseInc;
        DAC->DHR12R1 = sineTable[(txPhaseAcc >> 8U) & 0xFFU];

        if (calAltActive)
        {
            toneSampleCnt++;
            if (toneSampleCnt >= ((uint32_t)AFSK_SAMPLE_RATE * AFSK_CAL_ALT_TONE_MS / 1000UL))
            {
                toneSampleCnt = 0;
                calAltHalfPeriods++;
                txPhaseInc = (txPhaseInc == PHASE_INC_MARK) ? PHASE_INC_SPACE : PHASE_INC_MARK;

                if (calAltHalfPeriods >= ((uint16_t)AFSK_CAL_ALT_CYCLES * 2U))
                {
                    AFSK_ToneStop();
                }
            }
        }

        return;
    }

    /* ============================================================
     * TX path
     * ============================================================ */
    if (txActive)
    {
        /* Update DDS phase and write to DAC (direct register = no HAL overhead) */
        txPhaseAcc += txPhaseInc;
        DAC->DHR12R1 = sineTable[(txPhaseAcc >> 8U) & 0xFFU];

        txSampleCnt++;

        if (txSampleCnt >= AFSK_SAMPLES_PER_SYM) /* Every 8 samples = 1 symbol */
        {
            txSampleCnt = 0;

            if (txBitIdx >= txBitTotal)
            {
                /* All bits transmitted */
                txActive     = false;
                txDone       = true;
                DAC->DHR12R1 = AFSK_DAC_MID;
            }
            else
            {
                /* Fetch next NRZ bit (packed LSB-first) */
                uint8_t nrzBit = (txBits[txBitIdx >> 3U] >> (txBitIdx & 7U)) & 1U;
                txBitIdx++;

                /* NRZI encode: bit 0 → toggle tone,  bit 1 → keep tone */
                if (nrzBit == 0U)
                    txNRZI ^= 1U;

                txPhaseInc = txNRZI ? PHASE_INC_SPACE : PHASE_INC_MARK;
            }
        }

        /* Continue into the RX path so PA10->PA11 loopback can decode TX. */
    }

    /* ============================================================
     * RX path — ADC → BPF → Correlator → LPF → PLL + DCD → NRZI
     * ============================================================ */

    if ((ADC->ISR & ADC_ISR_EOC) != 0U)
    {
        /* Read ADC result (reading DR clears EOC automatically) */
        uint16_t raw = (uint16_t)(ADC->DR & 0x0FFFU);

        /* Centre around zero.
         * Hardware bias: PA11 is held at 3.3V × R3/(R3+R4) = 1.65V
         * via the 10k/10k voltage divider, which equals 2048 counts. */
        int16_t centered = (int16_t)raw - (int16_t)AFSK_DAC_MID;

        /* Apply 8-tap pre-emphasis BPF.
         * Boosts 2200 Hz (SPACE) by ~6 dB to compensate the DRA818V
         * FM receiver de-emphasis that attenuates the SPACE tone. */
        int16_t filtered = rxApplyBPF(centered);

        /* Store BPF output in correlator circular buffer */
        rxSampleBuf[rxBufIdx] = filtered;
        rxBufIdx = (rxBufIdx + 1U) & 7U;

        /* Re-arm ADC for next tick */
        ADC->CR |= ADC_CR_ADSTART;

        /* ---- Correlator: mark/space energy difference ---- */
        int32_t corr = rxGetCorrelation();

        /* ---- Post-correlator 15-tap LPF ---- */
        int32_t lpfOut = rxApplyLPF(corr);

        /* ---- Symbol decision ----
         * lpfOut > 0: MARK energy dominates → symbol = 0 (MARK)
         * lpfOut < 0: SPACE energy dominates → symbol = 1 (SPACE) */
        uint8_t curSym = (lpfOut > 0) ? 0U : 1U;

        /* ==== DCD PLL — tick at 9600 Hz (8 ticks per symbol) ==== */
        dcdPll = (int32_t)((uint32_t)dcdPll + (uint32_t)RX_PLL_STEP);

        if (curSym != dcdLastSym)   /* symbol transition detected */
        {
            if ((uint32_t)i32abs(dcdPll) < (uint32_t)RX_PLL_STEP)
            {
                /* Transition near PLL zero-crossing → valid AFSK carrier */
                dcdCounter += DCD_INC;
                if (dcdCounter > DCD_MAXPULSE)
                    dcdCounter = DCD_MAXPULSE;
            }
            else
            {
                /* Transition far from zero → random noise */
                if (dcdCounter >= DCD_DEC)
                    dcdCounter -= DCD_DEC;
                else
                    dcdCounter = 0U;
            }
            /* Nudge DCD PLL phase toward zero on each transition */
            dcdPll = (int32_t)(((int64_t)dcdPll * (int64_t)DCD_TUNE) >> 8);
        }
        dcdLastSym  = curSym;
        afsk_rx_dcd = (dcdCounter > DCD_THRESHOLD) ? 1U : 0U;

        /* ==== Bit-clock PLL — tick at 9600 Hz ==== */
        int32_t pllPrev = rxPll;
        rxPll = (int32_t)((uint32_t)rxPll + (uint32_t)RX_PLL_STEP);

        /* Accumulate raw symbols for majority vote and PLL tuning */
        rxRawSym = (uint8_t)((rxRawSym << 1U) | (curSym & 1U));

        /* Tune PLL on symbol transitions (last 2 raw symbols differ) */
        if (((rxRawSym & 0x03U) == 0x01U) || ((rxRawSym & 0x03U) == 0x02U))
        {
            if (afsk_rx_dcd)    /* PLL locked: nudge gently (×0.74) */
                rxPll = (int32_t)(((int64_t)rxPll * (int64_t)RX_PLL_LOCKED_TUNE) >> RX_PLL_TUNE_SHIFT);
            else                /* PLL unlocked: acquire fast (×0.50) */
                rxPll = (int32_t)(((int64_t)rxPll * (int64_t)RX_PLL_UNLKD_TUNE) >> RX_PLL_TUNE_SHIFT);
        }

        /* ==== Sample symbol at PLL overflow (sign change + → −) ==== */
        if ((rxPll < 0) && (pllPrev > 0))
        {
            /* 3-sample majority vote on the last 3 raw symbols.
             * A majority of two or more 1s → symbol = 1 (SPACE),
             * otherwise symbol = 0 (MARK). */
            uint8_t sym3 = rxRawSym & 0x07U;
            uint8_t sym  = ((sym3 == 0x07U) || (sym3 == 0x06U) ||
                             (sym3 == 0x05U) || (sym3 == 0x03U)) ? 1U : 0U;

            /* NRZI decode: store sync'd symbol and compare with previous */
            rxSyncSym = (uint8_t)((rxSyncSym << 1U) | (sym & 1U));

            uint8_t nrziBit;
            if (((rxSyncSym & 0x03U) == 0x03U) || ((rxSyncSym & 0x03U) == 0x00U))
                nrziBit = 1U;  /* same consecutive symbols → bit 1 */
            else
                nrziBit = 0U;  /* symbol transition → bit 0 */

            /* Gate decoded bits through DCD — do not feed noise to AX.25 */
            if (afsk_rx_dcd)
            {
                afsk_rx_bit       = nrziBit;
                afsk_rx_bit_ready = 1U;
            }
        }
    }
    else
    {
        /* No conversion ready — start one (handles first-tick case) */
        if ((ADC->CR & ADC_CR_ADSTART) == 0U)
            ADC->CR |= ADC_CR_ADSTART;
    }
}
