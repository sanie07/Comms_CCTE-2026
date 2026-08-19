/**
 * @file    app.c
 * @brief   Test10 application — SPI dual-test state machine (no GPS).
 *
 *  TEST_MODE 0: Receive coords from ESP32 via SPI, build APRS frame,
 *               transmit via DRA818V.
 *  TEST_MODE 1: Send hardcoded raw coords to ESP32 via SPI for display.
 */

#include "app.h"
#include "aprs_config.h"
#include "dra818.h"
#include "afsk.h"
#include "ax25.h"
#include "spi.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#define APP_PACKET_TX_MAX_MS  5000UL

/* ================================================================
 * Application state machine
 * ================================================================ */
typedef enum
{
    APP_IDLE = 0,
    APP_CHECK_CHANNEL,
    APP_PTT_ON,
    APP_TX,
    APP_PTT_OFF,
} AppState_t;

static AppState_t s_appState     = APP_IDLE;
static uint32_t   s_beaconTimer  = 0U;
static uint32_t   s_stateTimer   = 0U;
static int        s_handshakeResult = DRA818_ERR;
static uint8_t    s_spiStatus    = SPI_STATUS_INIT;

/* ================================================================
 * SPI packet buffers (32 bytes each)
 * ================================================================ */
static SpiPacket_t s_spiTxPkt;   /* What we send to the master              */
static SpiPacket_t s_spiRxPkt;   /* What we receive from the master         */
static uint16_t    s_seqNum = 0U;

/* Received coordinates from ESP32 (Test Mode 0) */
static volatile bool  s_coordsReceived = false;
static int32_t  s_rxLat_e6  = 0;
static int32_t  s_rxLon_e6  = 0;
static int32_t  s_rxAlt_cm  = 0;

/* ================================================================
 * Checksum computation
 * ================================================================ */
static uint8_t computeChecksum(const uint8_t *data, uint8_t len)
{
    uint8_t xor_sum = 0U;
    for (uint8_t i = 0U; i < len; i++)
        xor_sum ^= data[i];
    return xor_sum;
}

/* ================================================================
 * SPI slave exchange — prepare TX packet and arm the SPI peripheral
 * ================================================================ */
static void spiPrepareAndArm(void)
{
    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY)
        return;

#if (TEST_MODE == TEST_MODE_STM32_TO_ESP_RAW)
    /* Mode 1: Preload raw coordinate data for ESP32 to read */
    memset(&s_spiTxPkt, 0, sizeof(s_spiTxPkt));
    s_spiTxPkt.magic        = SPI_PACKET_MAGIC;
    s_spiTxPkt.cmd          = SPI_CMD_RAW_TELEMETRY;
    s_spiTxPkt.seq_num      = s_seqNum++;
    s_spiTxPkt.lat_e6       = STM32_HARDCODED_LAT_E6;
    s_spiTxPkt.lon_e6       = STM32_HARDCODED_LON_E6;
    s_spiTxPkt.alt_cm       = STM32_HARDCODED_ALT_CM;
    s_spiTxPkt.timestamp_ms = HAL_GetTick();
    strncpy(s_spiTxPkt.comment, "TEST10-RAW", sizeof(s_spiTxPkt.comment) - 1);
    s_spiTxPkt.comment[sizeof(s_spiTxPkt.comment) - 1] = '\0';
    s_spiTxPkt.status       = s_spiStatus;
    s_spiTxPkt.checksum     = computeChecksum((const uint8_t *)&s_spiTxPkt,
                                               SPI_PACKET_SIZE - 1U);
#else
    /* Mode 0: Reply with status only */
    memset(&s_spiTxPkt, 0, sizeof(s_spiTxPkt));
    s_spiTxPkt.magic        = SPI_PACKET_MAGIC;
    s_spiTxPkt.cmd          = SPI_CMD_POLL_STATUS;
    s_spiTxPkt.seq_num      = s_seqNum++;
    s_spiTxPkt.timestamp_ms = HAL_GetTick();
    strncpy(s_spiTxPkt.comment, "TEST10", sizeof(s_spiTxPkt.comment) - 1);
    s_spiTxPkt.comment[sizeof(s_spiTxPkt.comment) - 1] = '\0';
    s_spiTxPkt.status       = s_spiStatus;
    s_spiTxPkt.checksum     = computeChecksum((const uint8_t *)&s_spiTxPkt,
                                               SPI_PACKET_SIZE - 1U);
#endif

    (void)HAL_SPI_TransmitReceive_IT(&hspi1,
                                      (uint8_t *)&s_spiTxPkt,
                                      (uint8_t *)&s_spiRxPkt,
                                      SPI_PACKET_SIZE);
}

/* ================================================================
 * Process received SPI packet (called after SPI transfer completes)
 * ================================================================ */
static void spiProcessRxPacket(void)
{
    /* Validate magic byte */
    if (s_spiRxPkt.magic != SPI_PACKET_MAGIC)
        return;

    /* Validate checksum */
    uint8_t expected_cs = computeChecksum((const uint8_t *)&s_spiRxPkt,
                                          SPI_PACKET_SIZE - 1U);
    if (expected_cs != s_spiRxPkt.checksum)
        return;

#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
    /* Mode 0: ESP32 sends coordinates for DRA818 transmission */
    if (s_spiRxPkt.cmd == SPI_CMD_SEND_COORDS)
    {
        s_rxLat_e6  = s_spiRxPkt.lat_e6;
        s_rxLon_e6  = s_spiRxPkt.lon_e6;
        s_rxAlt_cm  = s_spiRxPkt.alt_cm;
        s_coordsReceived = true;
        s_spiStatus = SPI_STATUS_COORD_RCVD;
    }
#endif
}

/* ================================================================
 * APRS coordinate formatting — convert e6 integers to DDMM.MMx
 * ================================================================ */
static void formatAprsLat_e6(int32_t lat_e6, char *out, uint8_t outLen)
{
    char hemi = (lat_e6 >= 0) ? 'N' : 'S';
    if (lat_e6 < 0) lat_e6 = -lat_e6;

    int32_t deg = lat_e6 / 1000000L;
    int32_t frac = lat_e6 % 1000000L;
    /* Convert fractional degrees to minutes * 100 */
    int32_t minTimes100 = (frac * 60L) / 10000L;
    int32_t min = minTimes100 / 100L;
    int32_t minFrac = minTimes100 % 100L;

    snprintf(out, outLen, "%02d%02d.%02d%c",
             (int)deg, (int)min, (int)minFrac, hemi);
}

static void formatAprsLon_e6(int32_t lon_e6, char *out, uint8_t outLen)
{
    char hemi = (lon_e6 >= 0) ? 'E' : 'W';
    if (lon_e6 < 0) lon_e6 = -lon_e6;

    int32_t deg = lon_e6 / 1000000L;
    int32_t frac = lon_e6 % 1000000L;
    int32_t minTimes100 = (frac * 60L) / 10000L;
    int32_t min = minTimes100 / 100L;
    int32_t minFrac = minTimes100 % 100L;

    snprintf(out, outLen, "%03d%02d.%02d%c",
             (int)deg, (int)min, (int)minFrac, hemi);
}

/* ================================================================
 * Build APRS beacon frame from received coordinates
 * ================================================================ */
static bool buildBeaconFrame(void)
{
    char aprsLat[9];
    char aprsLon[10];
    uint8_t infoStr[AX25_MAX_INFO_LEN];
    uint16_t infoLen = 0U;

#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
    if (s_coordsReceived)
    {
        formatAprsLat_e6(s_rxLat_e6, aprsLat, sizeof(aprsLat));
        formatAprsLon_e6(s_rxLon_e6, aprsLon, sizeof(aprsLon));
    }
    else
    {
        strncpy(aprsLat, APRS_LATITUDE, sizeof(aprsLat));
        aprsLat[sizeof(aprsLat) - 1U] = '\0';
        strncpy(aprsLon, APRS_LONGITUDE, sizeof(aprsLon));
        aprsLon[sizeof(aprsLon) - 1U] = '\0';
    }
#else
    /* Mode 1 should not call this, but fallback just in case */
    strncpy(aprsLat, APRS_LATITUDE, sizeof(aprsLat));
    aprsLat[sizeof(aprsLat) - 1U] = '\0';
    strncpy(aprsLon, APRS_LONGITUDE, sizeof(aprsLon));
    aprsLon[sizeof(aprsLon) - 1U] = '\0';
#endif

    int n = snprintf((char *)infoStr, AX25_MAX_INFO_LEN,
                     "!%s%c%s%c%s",
                     aprsLat,
                     APRS_SYMBOL_TABLE,
                     aprsLon,
                     APRS_SYMBOL_CODE,
                     APRS_BEACON_MSG);

    infoLen = (n > 0) ? (uint16_t)n : 0U;
    if (infoLen == 0U)
        return false;

    return AX25_BuildTxFrame(infoStr, infoLen);
}

/* ================================================================
 * RX callback for loopback detection (not actively used in Test10
 * but kept for decoder completeness)
 * ================================================================ */
static void onRxFrame(const uint8_t *info, uint16_t infoLen, const char *srcCall)
{
    (void)info;
    (void)infoLen;
    (void)srcCall;
}

/* ================================================================
 * HAL Callbacks
 * ================================================================ */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2)
    {
        App_AFSK_TimerCallback();
    }
}

void App_AFSK_TimerCallback(void)
{
    AFSK_TimerTick();

    if (afsk_rx_bit_ready)
    {
        afsk_rx_bit_ready = 0U;
        AX25_RxBit(afsk_rx_bit);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    (void)GPIO_Pin;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spiProcessRxPacket();
        spiPrepareAndArm();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        /* Abort any in-progress transfer and reset the SPI state to READY */
        HAL_SPI_Abort_IT(&hspi1);
        spiPrepareAndArm();
    }
}

/* ================================================================
 * App_Init
 * ================================================================ */
void App_Init(void)
{
    /* SPI1 interrupt — highest priority so TIM2 (9600 Hz) cannot
     * preempt multi-byte slave transfers and cause overrun errors. */
    HAL_NVIC_SetPriority(SPI1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);

    /* Arm the SPI slave for the first exchange BEFORE blocking in DRA818_Init */
    spiPrepareAndArm();

#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
    /* Mode 0: Initialize AFSK modem, AX.25, and DRA818V radio */
    AX25_Init(onRxFrame);
    AFSK_Init();

    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2);

    s_handshakeResult = DRA818_Init();
    s_spiStatus = (s_handshakeResult == DRA818_OK)
                  ? SPI_STATUS_DRA_READY
                  : SPI_STATUS_DRA_ERR;
#else
    /* Mode 1: No DRA818 needed, just SPI raw telemetry */
    s_handshakeResult = DRA818_OK;
    s_spiStatus = SPI_STATUS_READY;
#endif

    s_beaconTimer = HAL_GetTick();
    s_stateTimer  = HAL_GetTick();
    s_appState    = APP_IDLE;
}

/* ================================================================
 * App_Run
 * ================================================================ */
void App_Run(void)
{
#if (TEST_MODE == TEST_MODE_STM32_TO_ESP_RAW)
    /* Mode 1: Nothing to do in the main loop.
     * SPI transfers are fully handled via interrupts.
     * The STM32 just re-arms the SPI with fresh data after each transfer.
     */
    return;
#else
    /* Mode 0: APRS beacon state machine — TX coords received from ESP32 */
    uint32_t now = HAL_GetTick();

    if (s_appState == APP_IDLE)
    {
        DRA818_SetPTT(false);
    }

    switch (s_appState)
    {
    case APP_IDLE:
        if ((s_handshakeResult == DRA818_OK) &&
            s_coordsReceived &&
            ((now - s_beaconTimer) >= APRS_BEACON_INTERVAL_MS))
        {
            s_stateTimer = now;
            s_appState = APP_CHECK_CHANNEL;
        }
        break;

    case APP_CHECK_CHANNEL:
        if (!DRA818_IsChannelBusy())
        {
            if (buildBeaconFrame())
            {
                DRA818_SetPTT(true);
                s_spiStatus = SPI_STATUS_TX_ACTIVE;
                s_stateTimer = now;
                s_appState = APP_PTT_ON;
            }
            else
            {
                s_beaconTimer = now;
                s_appState = APP_IDLE;
            }
        }
        else if ((now - s_stateTimer) >= ((uint32_t)DRA818_CHANNEL_WAIT_SEC * 1000UL))
        {
            s_beaconTimer = now;
            s_appState = APP_IDLE;
        }
        break;

    case APP_PTT_ON:
        if ((now - s_stateTimer) >= DRA818_PTT_ON_DELAY_MS)
        {
            AFSK_TX_Start();
            s_stateTimer = now;
            s_appState = APP_TX;
        }
        break;

    case APP_TX:
        if (AFSK_TX_IsDone() || ((now - s_stateTimer) >= APP_PACKET_TX_MAX_MS))
        {
            AFSK_TX_Stop();
            s_spiStatus = SPI_STATUS_TX_DONE;
            s_stateTimer = now;
            s_appState = APP_PTT_OFF;
        }
        break;

    case APP_PTT_OFF:
        if ((now - s_stateTimer) >= DRA818_PTT_OFF_DELAY_MS)
        {
            DRA818_SetPTT(false);
            s_spiStatus = SPI_STATUS_DRA_READY;
            s_coordsReceived = false;  /* Wait for next coordinate packet */
            s_beaconTimer = now;
            s_appState = APP_IDLE;
        }
        break;

    default:
        s_appState = APP_IDLE;
        break;
    }
#endif
}
