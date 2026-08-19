/**
 * @file    afsk.c
 * @brief   Bell 202 AFSK modem implementation for STM32WLE5CCU6.
 *
 * Timing (48 MHz system clock, TIM2 PSC=0 ARR=4999):
 *   TIM2 period = 48 000 000 / 5 000 = 9 600 Hz
 *   Samples per 1200-baud symbol = 9600 / 1200 = 8  (exact integer)
 */

#include "afsk.h"
#include "aprs_config.h"
#include "main.h"
#include "dac.h"
#include "adc.h"
#include <string.h>

/* ================================================================
 * Phase increment constants  (Q8 fixed-point)
 * ================================================================ */
#define PHASE_INC_MARK  \
    ((uint16_t)(((uint32_t)AFSK_SINE_LEN * AFSK_MARK_HZ  * 256UL) / AFSK_SAMPLE_RATE))

#define PHASE_INC_SPACE \
    ((uint16_t)(((uint32_t)AFSK_SINE_LEN * AFSK_SPACE_HZ * 256UL) / AFSK_SAMPLE_RATE))

/* ================================================================
 * TX state
 * ================================================================ */
extern DAC_HandleTypeDef hdac;

static uint16_t         sineTable[AFSK_SINE_LEN];

static volatile bool    txActive     = false;
static volatile bool    txDone       = false;
static volatile bool    toneActive   = false;
static volatile bool    calAltActive = false;

static const uint8_t   *txBits      = NULL;
static volatile uint16_t txBitTotal = 0;
static volatile uint16_t txBitIdx   = 0;

static uint16_t          txPhaseAcc  = 0;
static uint16_t          txPhaseInc  = PHASE_INC_MARK;
static uint8_t           txNRZI      = 0;
static uint8_t           txSampleCnt = 0;
static uint32_t          toneSampleCnt = 0;
static uint16_t          calAltHalfPeriods = 0;

/* ================================================================
 * RX state
 * ================================================================ */
static const int16_t c1200I[8] = { 128,  90,    0,  -90, -128,  -90,    0,   90 };
static const int16_t c1200Q[8] = {   0,  90,  128,   90,    0,  -90, -128,  -90 };
static const int16_t c2200I[8] = { 128,  17, -124,  -49,  111,   78,  -90, -102 };
static const int16_t c2200Q[8] = {   0, 127,   33, -118,  -64,  101,   90,  -78 };

static int16_t  rxSampleBuf[8];
static uint8_t  rxBufIdx    = 0;
static uint8_t  rxSampleCnt = 0;
static uint8_t  rxPrevSym   = 0;
static bool     rxFirstSym  = true;

/* ================================================================
 * RX output (read by app.c after each ISR tick)
 * ================================================================ */
volatile uint8_t afsk_rx_bit_ready = 0;
volatile uint8_t afsk_rx_bit       = 0;

/* ================================================================
 * Private helpers
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
 * RX correlator
 * ================================================================ */
static uint8_t rxGetSymbol(void)
{
    int32_t mI = 0, mQ = 0, sI = 0, sQ = 0;

    for (uint8_t k = 0; k < 8U; k++)
    {
        uint8_t  idx = (uint8_t)((rxBufIdx - k + 8U) & 7U);
        int16_t  s   = rxSampleBuf[idx];

        mI += (int32_t)s * c1200I[k];
        mQ += (int32_t)s * c1200Q[k];
        sI += (int32_t)s * c2200I[k];
        sQ += (int32_t)s * c2200Q[k];
    }

    uint32_t markMag  = (uint32_t)(i32abs(mI) + i32abs(mQ));
    uint32_t spaceMag = (uint32_t)(i32abs(sI) + i32abs(sQ));

    return (spaceMag > markMag) ? 1U : 0U;
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

    memset(rxSampleBuf, 0, sizeof(rxSampleBuf));

    /* ---- DAC ---- */
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, AFSK_DAC_MID);

    /* ---- ADC ---- */
    ADC_ChannelConfTypeDef sADCCh = {0};
    sADCCh.Channel      = ADC_CHANNEL_7;
    sADCCh.Rank         = ADC_REGULAR_RANK_1;
    sADCCh.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    HAL_ADC_ConfigChannel(&hadc, &sADCCh);

    HAL_ADCEx_Calibration_Start(&hadc);
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
    txNRZI      = 0;
    txPhaseAcc  = 0;
    txPhaseInc  = PHASE_INC_MARK;
    txSampleCnt = 0;
    txDone      = false;
    txActive    = true;
}

void AFSK_TX_Stop(void)
{
    txActive = false;
    toneActive = false;
    calAltActive = false;
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
        txPhaseAcc += txPhaseInc;
        DAC->DHR12R1 = sineTable[(txPhaseAcc >> 8U) & 0xFFU];

        txSampleCnt++;

        if (txSampleCnt >= AFSK_SAMPLES_PER_SYM)
        {
            txSampleCnt = 0;

            if (txBitIdx >= txBitTotal)
            {
                txActive     = false;
                txDone       = true;
                DAC->DHR12R1 = AFSK_DAC_MID;
            }
            else
            {
                uint8_t nrzBit = (txBits[txBitIdx >> 3U] >> (txBitIdx & 7U)) & 1U;
                txBitIdx++;

                if (nrzBit == 0U)
                    txNRZI ^= 1U;

                txPhaseInc = txNRZI ? PHASE_INC_SPACE : PHASE_INC_MARK;
            }
        }
    }

    /* ============================================================
     * RX path — read ADC, run correlator, output decoded bit
     * ============================================================ */
    if ((ADC->ISR & ADC_ISR_EOC) != 0U)
    {
        uint16_t raw    = (uint16_t)(ADC->DR & 0x0FFFU);

        rxSampleBuf[rxBufIdx] = (int16_t)raw - (int16_t)AFSK_DAC_MID;
        rxBufIdx = (rxBufIdx + 1U) & 7U;

        ADC->CR |= ADC_CR_ADSTART;

        rxSampleCnt++;

        if (rxSampleCnt >= AFSK_SAMPLES_PER_SYM)
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
                afsk_rx_bit       = (sym == rxPrevSym) ? 1U : 0U;
                afsk_rx_bit_ready = 1U;
                rxPrevSym         = sym;
            }
        }
    }
    else
    {
        if ((ADC->CR & ADC_CR_ADSTART) == 0U)
            ADC->CR |= ADC_CR_ADSTART;
    }
}
