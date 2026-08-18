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
#include <math.h>
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

static const uint8_t   *txBits      = NULL;  /* Pointer to loaded bit-stream */
static volatile uint16_t txBitTotal = 0;     /* Total bits in stream         */
static volatile uint16_t txBitIdx   = 0;     /* Next bit to transmit         */

static uint16_t          txPhaseAcc  = 0;    /* 16-bit Q8 DDS accumulator    */
static uint16_t          txPhaseInc  = PHASE_INC_MARK;
static uint8_t           txNRZI      = 0;    /* Current tone: 0=MARK 1=SPACE  */
static uint8_t           txSampleCnt = 0;    /* Samples elapsed in symbol (0-7)*/

/* ================================================================
 * RX state
 * ================================================================
 *
 * Reference vectors (Q7, scaled ×128) for 8-sample correlator window.
 *
 * 1200 Hz @ 9600 Hz SR:  phase step = 360°/8 = 45° per sample
 *   cos: [128, 90,   0, -90,-128, -90,   0,  90]
 *   sin: [  0, 90, 128,  90,   0, -90,-128, -90]
 *
 * 2200 Hz @ 9600 Hz SR:  phase step = 360° × 2200/9600 = 82.5° per sample
 *   n=0:cos  0°=1.000→128  sin  0°=0.000→  0
 *   n=1:cos 82.5°=0.131→17  sin 82.5°=0.991→127
 *   n=2:cos165°=-0.966→-124 sin165°=0.259→ 33
 *   n=3:cos247.5°=-0.383→-49 sin247.5°=-0.924→-118
 *   n=4:cos330°=0.866→111  sin330°=-0.500→-64
 *   n=5:cos52.5°=0.609→ 78  sin 52.5°=0.793→101
 *   n=6:cos135°=-0.707→-90  sin135°=0.707→ 90
 *   n=7:cos217.5°=-0.793→-102 sin217.5°=-0.609→-78
 */
static const int16_t c1200I[8] = { 128,  90,    0,  -90, -128,  -90,    0,   90 };
static const int16_t c1200Q[8] = {   0,  90,  128,   90,    0,  -90, -128,  -90 };
static const int16_t c2200I[8] = { 128,  17, -124,  -49,  111,   78,  -90, -102 };
static const int16_t c2200Q[8] = {   0, 127,   33, -118,  -64,  101,   90,  -78 };

static int16_t  rxSampleBuf[8];  /* Circular sample buffer (DC-centered) */
static uint8_t  rxBufIdx    = 0; /* Write index into circular buffer      */
static uint8_t  rxSampleCnt = 0; /* Samples since last symbol decision    */
static uint8_t  rxPrevSym   = 0; /* Previous symbol for NRZI decode       */
static bool     rxFirstSym  = true;

/* ================================================================
 * RX output (read by app.c after each ISR tick)
 * ================================================================ */
volatile uint8_t afsk_rx_bit_ready = 0;
volatile uint8_t afsk_rx_bit       = 0;

/* ================================================================
 * Private helper: L1 absolute value
 * ================================================================ */
static inline int32_t i32abs(int32_t x) { return (x < 0) ? -x : x; }

/* ================================================================
 * Private: compute mark/space energy from 8 samples in circular buf
 * Returns 0 = MARK, 1 = SPACE
 * ================================================================ */
static uint8_t rxGetSymbol(void)
{
    int32_t mI = 0, mQ = 0, sI = 0, sQ = 0;

    for (uint8_t k = 0; k < 8U; k++)
    {
        /* Walk backwards through the circular buffer */
        uint8_t  idx = (uint8_t)((rxBufIdx - k + 8U) & 7U);
        int16_t  s   = rxSampleBuf[idx]; /* Already DC-centered at store */

        mI += (int32_t)s * c1200I[k];
        mQ += (int32_t)s * c1200Q[k];
        sI += (int32_t)s * c2200I[k];
        sQ += (int32_t)s * c2200Q[k];
    }

    uint32_t markMag  = (uint32_t)(i32abs(mI) + i32abs(mQ));
    uint32_t spaceMag = (uint32_t)(i32abs(sI) + i32abs(sQ));

    return (spaceMag > markMag) ? 1U : 0U;  /* 0 = MARK, 1 = SPACE */
}

/* ================================================================
 * AFSK_Init
 * ================================================================ */
void AFSK_Init(void)
{
    /* Build sine table (uses FPU — STM32WLE5 is Cortex-M4F).
     * Called once at startup; takes ~200 µs. */
    for (uint16_t i = 0; i < AFSK_SINE_LEN; i++)
    {
        float angle  = 2.0f * 3.14159265358979f * (float)i / (float)AFSK_SINE_LEN;
        sineTable[i] = (uint16_t)((float)AFSK_DAC_MID +
                                   (float)AFSK_DAC_AMP * sinf(angle));
    }

    /* Clear RX sample buffer */
    memset(rxSampleBuf, 0, sizeof(rxSampleBuf));

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
    /* Return DAC to mid-point (silence) via direct register write */
    DAC->DHR12R1 = AFSK_DAC_MID;
}

bool AFSK_TX_IsDone(void)
{
    return txDone;
}

/* ================================================================
 * AFSK_TimerTick — called from TIM2 ISR at 9600 Hz
 *
 * TX and RX are mutually exclusive.  When txActive is set the ISR
 * outputs the next DAC sample and returns immediately without sampling
 * the ADC.
 * ================================================================ */
void AFSK_TimerTick(void)
{
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

        return; /* Skip RX while transmitting */
    }

    /* ============================================================
     * RX path — read ADC, run correlator, output decoded bit
     * ============================================================ */

    /* Poll for conversion end (EOC flag in ISR register) */
    if ((ADC->ISR & ADC_ISR_EOC) != 0U)
    {
        /* Read result (reading DR automatically clears EOC) */
        uint16_t raw    = (uint16_t)(ADC->DR & 0x0FFFU);

        /* Center around zero (12-bit mid = 2048) */
        rxSampleBuf[rxBufIdx] = (int16_t)raw - (int16_t)AFSK_DAC_MID;
        rxBufIdx = (rxBufIdx + 1U) & 7U;

        /* Re-arm ADC for next tick */
        ADC->CR |= ADC_CR_ADSTART;

        rxSampleCnt++;

        if (rxSampleCnt >= AFSK_SAMPLES_PER_SYM) /* Every 8 samples = 1 symbol */
        {
            rxSampleCnt = 0;

            uint8_t sym = rxGetSymbol();

            if (rxFirstSym)
            {
                rxFirstSym = false;
                rxPrevSym  = sym;
            }
            else
            {
                /* NRZI decode: same symbol as before → bit 1, change → bit 0 */
                afsk_rx_bit       = (sym == rxPrevSym) ? 1U : 0U;
                afsk_rx_bit_ready = 1U;
                rxPrevSym         = sym;
            }
        }
    }
    else
    {
        /* No conversion ready yet — start one (handles first-tick case) */
        if ((ADC->CR & ADC_CR_ADSTART) == 0U)
            ADC->CR |= ADC_CR_ADSTART;
    }
}
