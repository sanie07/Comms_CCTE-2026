/**
 * @file    app.c
 * @brief   APRS tracker bring-up state machine for Test8.
 */

#include "app.h"
#include "app_test_modes.h"
#include "aprs_config.h"
#include "dra818.h"
#include "afsk.h"
#include "ax25.h"
#include "digipeater.h"
#include "tinygps.h"
#include "spi.h"
#include "usart.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#define TONE_BURST_MS        3000UL
#define TONE_INTERVAL_MS     15000UL
#define LOOPBACK_PERIOD_MS   5000UL
#define APP_PACKET_TX_MAX_MS 5000UL
#define APP_ENABLE_SPI_STATUS 1

/* ---- SPI frame dump (raw AX.25 frame bytes sent to master byte-by-byte) ---- */
#define SPI_FRAME_DUMP_MAX   295U   /* max bytes of a decoded frame to dump */

static uint8_t   s_spiFrameBuf[SPI_FRAME_DUMP_MAX + 1U]; /* +1 for 0x0A header */
static uint16_t  s_spiFrameTotal = 0U;
static uint16_t  s_spiFrameIdx   = 0U;
static volatile bool s_spiFramePending = false;

typedef enum
{
    APP_IDLE = 0,
    APP_CHECK_CHANNEL,
    APP_PTT_ON,
    APP_TX,
    APP_PTT_OFF,
} AppState_t;

static AppState_t s_appState = APP_IDLE;
static uint32_t s_beaconTimer = 0U;
static uint32_t s_stateTimer = 0U;
static int s_handshakeResult = DRA818_ERR;
static uint8_t s_spiStatusMsg = SPI_STATUS_INIT;
static uint8_t s_dummyRx = 0U;
static TinyGPSPlus s_gps;
static uint8_t s_uartRxByte = 0U;
static bool s_loopbackOk = false;

static bool modeUsesGps(void)
{
#if (APP_TEST_MODE == APP_TEST_GPS) || (APP_TEST_MODE == APP_TEST_APRS_TRACKER)
    return true;
#else
    return false;
#endif
}

static bool modeUsesAfsk(void)
{
#if (APP_TEST_MODE == APP_TEST_TONE_1200) || \
    (APP_TEST_MODE == APP_TEST_CAL_ALT) || \
    (APP_TEST_MODE == APP_TEST_AX25_LOOPBACK) || \
    (APP_TEST_MODE == APP_TEST_AX25_OTA) || \
    (APP_TEST_MODE == APP_TEST_APRS_TRACKER) || \
    (APP_TEST_MODE == COMPLETE_DIGIPEATER) || \
    (APP_TEST_MODE == APP_TEST_RX_MONITOR)
    return true;
#else
    return false;
#endif
}

static bool modeUsesDra(void)
{
#if (APP_TEST_MODE == APP_TEST_DRA818_ONLY) || \
    (APP_TEST_MODE == APP_TEST_TONE_1200) || \
    (APP_TEST_MODE == APP_TEST_CAL_ALT) || \
    (APP_TEST_MODE == APP_TEST_AX25_OTA) || \
    (APP_TEST_MODE == APP_TEST_APRS_TRACKER) || \
    (APP_TEST_MODE == COMPLETE_DIGIPEATER) || \
    (APP_TEST_MODE == APP_TEST_RX_MONITOR)
    return true;
#else
    return false;
#endif
}

static void gpsStartRx(void)
{
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
    (void)HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U);
}

static void onRxFrame(const uint8_t *info, uint16_t infoLen, const char *srcCall)
{
    (void)srcCall;

    if ((infoLen == (uint16_t)strlen(APRS_FIXED_INFO)) &&
        (memcmp(info, APRS_FIXED_INFO, infoLen) == 0))
    {
        s_loopbackOk = true;
        s_spiStatusMsg = SPI_STATUS_LOOPBACK_OK;
    }
}

/**
 * Raw-frame callback — registered when COMPLETE_DIGIPEATER mode is active.
 * Runs in TIM2 ISR context: only flag writes / memcpy allowed.
 */
static void onRxFrameRaw(const uint8_t *frame, uint16_t len)
{
    /* ---- SPI frame dump: send 0x0A header + raw bytes to master ---- */
    if (!s_spiFramePending && len > 0U)
    {
        uint16_t copyLen = (len > SPI_FRAME_DUMP_MAX) ? SPI_FRAME_DUMP_MAX : len;
        s_spiFrameBuf[0] = SPI_STATUS_RX_FRAME; /* header byte */
        for (uint16_t i = 0U; i < copyLen; i++)
            s_spiFrameBuf[i + 1U] = frame[i];
        s_spiFrameTotal   = copyLen + 1U;
        s_spiFrameIdx     = 0U;
        s_spiFramePending = true;
    }

    /* ---- Digipeater path processing ---- */
    Digi_ProcessFrame(frame, len);
}

/**
 * RX-monitor callback — registered when APP_TEST_RX_MONITOR mode is active.
 * Like onRxFrameRaw but with NO digipeater processing and NO TX.
 * Just dumps the raw frame over SPI so the ESP32 can display it.
 * Runs in TIM2 ISR context.
 */
static void onRxFrameMonitor(const uint8_t *frame, uint16_t len)
{
    /* Signal ESP32 that a frame arrived (brief RX_SEEN pulse) */
    s_spiStatusMsg = SPI_STATUS_RX_SEEN;

    /* Queue the full raw frame for SPI byte-by-byte dump */
    if (!s_spiFramePending && len > 0U)
    {
        uint16_t copyLen = (len > SPI_FRAME_DUMP_MAX) ? SPI_FRAME_DUMP_MAX : len;
        s_spiFrameBuf[0] = SPI_STATUS_RX_FRAME; /* header byte */
        for (uint16_t i = 0U; i < copyLen; i++)
            s_spiFrameBuf[i + 1U] = frame[i];
        s_spiFrameTotal   = copyLen + 1U;
        s_spiFrameIdx     = 0U;
        s_spiFramePending = true;
    }
    /* No Digi_ProcessFrame, no PTT, no TX */
}

static void formatAprsLat(double lat, char *out, uint8_t outLen)
{
    char hemi = (lat >= 0.0) ? 'N' : 'S';
    if (lat < 0.0)
        lat = -lat;

    int deg = (int)lat;
    int minTimes100 = (int)((lat - (double)deg) * 6000.0 + 0.5);
    int min = minTimes100 / 100;
    int minFrac = minTimes100 % 100;

    snprintf(out, outLen, "%02d%02d.%02d%c", deg, min, minFrac, hemi);
}

static void formatAprsLon(double lng, char *out, uint8_t outLen)
{
    char hemi = (lng >= 0.0) ? 'E' : 'W';
    if (lng < 0.0)
        lng = -lng;

    int deg = (int)lng;
    int minTimes100 = (int)((lng - (double)deg) * 6000.0 + 0.5);
    int min = minTimes100 / 100;
    int minFrac = minTimes100 % 100;

    snprintf(out, outLen, "%03d%02d.%02d%c", deg, min, minFrac, hemi);
}

static bool buildGpsBeaconInfo(uint8_t *buf, uint16_t *len)
{
    char aprsLat[9];
    char aprsLon[10];

    if (s_gps.location.valid)
    {
        formatAprsLat(TinyGPSLocation_Lat(&s_gps.location), aprsLat, sizeof(aprsLat));
        formatAprsLon(TinyGPSLocation_Lng(&s_gps.location), aprsLon, sizeof(aprsLon));
    }
    else
    {
        strncpy(aprsLat, APRS_LATITUDE, sizeof(aprsLat));
        aprsLat[sizeof(aprsLat) - 1U] = '\0';
        strncpy(aprsLon, APRS_LONGITUDE, sizeof(aprsLon));
        aprsLon[sizeof(aprsLon) - 1U] = '\0';
    }

    int n = snprintf((char *)buf, AX25_MAX_INFO_LEN,
                     "!%s%c%s%c%s",
                     aprsLat,
                     APRS_SYMBOL_TABLE,
                     aprsLon,
                     APRS_SYMBOL_CODE,
                     APRS_BEACON_MSG);

    *len = (n > 0) ? (uint16_t)n : 0U;
    return (*len > 0U);
}

static void updateSpiStatusIdle(void)
{
    if (modeUsesDra() && (s_handshakeResult != DRA818_OK))
    {
        s_spiStatusMsg = SPI_STATUS_HANDSHAKE_ERR;
    }
    else if (s_loopbackOk)
    {
        s_spiStatusMsg = SPI_STATUS_LOOPBACK_OK;
    }
    else if (modeUsesGps())
    {
        s_spiStatusMsg = s_gps.location.valid ? SPI_STATUS_GPS_FIX : SPI_STATUS_GPS_WAIT;
    }
    else
    {
        s_spiStatusMsg = SPI_STATUS_HANDSHAKE_OK;
    }
}

static bool loadFixedFrame(void)
{
    const uint8_t *info = (const uint8_t *)APRS_FIXED_INFO;
    return AX25_BuildTxFrame(info, (uint16_t)strlen(APRS_FIXED_INFO));
}

static bool loadTrackerFrame(void)
{
    uint8_t infoStr[AX25_MAX_INFO_LEN];
    uint16_t infoLen = 0U;

#if (APP_TEST_MODE == APP_TEST_AX25_OTA)
    return loadFixedFrame();
#else
    if (!buildGpsBeaconInfo(infoStr, &infoLen))
        return false;

    return AX25_BuildTxFrame(infoStr, infoLen);
#endif
}

static void spiStatusStartIfReady(void)
{
#if APP_ENABLE_SPI_STATUS
    if (HAL_SPI_GetState(&hspi1) == HAL_SPI_STATE_READY)
    {
        if (s_spiFramePending && (s_spiFrameIdx < s_spiFrameTotal))
        {
            /* Send next byte of the raw frame dump */
            s_dummyRx = 0U;
            (void)HAL_SPI_TransmitReceive_IT(&hspi1,
                                             &s_spiFrameBuf[s_spiFrameIdx],
                                             &s_dummyRx, 1U);
            s_spiFrameIdx++;
            if (s_spiFrameIdx >= s_spiFrameTotal)
                s_spiFramePending = false;
        }
        else
        {
            /* Normal 1-byte status heartbeat */
            (void)HAL_SPI_TransmitReceive_IT(&hspi1, &s_spiStatusMsg, &s_dummyRx, 1U);
        }
    }
#endif
}

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

void App_Init(void)
{
#if APP_ENABLE_SPI_STATUS
    HAL_NVIC_SetPriority(SPI1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
#else
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
#endif

    if (modeUsesGps())
    {
        TinyGPSPlus_Init(&s_gps);
        HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
        gpsStartRx();
    }

    if (modeUsesAfsk())
    {
#if (APP_TEST_MODE == COMPLETE_DIGIPEATER)
        Digi_Init();
        AX25_Init2(NULL, onRxFrameRaw);
#elif (APP_TEST_MODE == APP_TEST_RX_MONITOR)
        AX25_Init2(NULL, onRxFrameMonitor);
#else
        AX25_Init(onRxFrame);
#endif
        AFSK_Init();
        HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(TIM2_IRQn);
        HAL_TIM_Base_Start_IT(&htim2);
    }

    if (modeUsesDra())
    {
        s_handshakeResult = DRA818_Init();
        s_spiStatusMsg = (s_handshakeResult == DRA818_OK) ? SPI_STATUS_HANDSHAKE_OK : SPI_STATUS_HANDSHAKE_ERR;
    }
    else
    {
        s_handshakeResult = DRA818_OK;
        updateSpiStatusIdle();
    }

    spiStatusStartIfReady();

    s_beaconTimer = HAL_GetTick();
    s_stateTimer = HAL_GetTick();
    s_appState = APP_IDLE;
}

void App_Run(void)
{
    uint32_t now = HAL_GetTick();

    if (s_appState == APP_IDLE)
    {
        if (modeUsesDra())
            DRA818_SetPTT(false);
        updateSpiStatusIdle();
    }

#if (APP_TEST_MODE == APP_TEST_DRA818_ONLY) || (APP_TEST_MODE == APP_TEST_GPS)
    return;
#elif (APP_TEST_MODE == APP_TEST_TONE_1200) || (APP_TEST_MODE == APP_TEST_CAL_ALT)
    switch (s_appState)
    {
    case APP_IDLE:
        if ((s_handshakeResult == DRA818_OK) && ((now - s_beaconTimer) >= TONE_INTERVAL_MS))
        {
            DRA818_SetPTT(true);
            s_spiStatusMsg = SPI_STATUS_TX_ACTIVE;
            s_stateTimer = now;
            s_appState = APP_PTT_ON;
        }
        break;

    case APP_PTT_ON:
        if ((now - s_stateTimer) >= DRA818_PTT_ON_DELAY_MS)
        {
#if (APP_TEST_MODE == APP_TEST_TONE_1200)
            AFSK_ToneStart(AFSK_MARK_HZ);
#else
            AFSK_CalAltStart();
#endif
            s_stateTimer = now;
            s_appState = APP_TX;
        }
        break;

    case APP_TX:
#if (APP_TEST_MODE == APP_TEST_TONE_1200)
        if ((now - s_stateTimer) >= TONE_BURST_MS)
#else
        if (AFSK_TX_IsDone())
#endif
        {
            AFSK_ToneStop();
            s_spiStatusMsg = SPI_STATUS_TX_DONE;
            s_stateTimer = now;
            s_appState = APP_PTT_OFF;
        }
        break;

    case APP_PTT_OFF:
        if ((now - s_stateTimer) >= DRA818_PTT_OFF_DELAY_MS)
        {
            DRA818_SetPTT(false);
            s_beaconTimer = now;
            s_appState = APP_IDLE;
        }
        break;

    default:
        s_appState = APP_IDLE;
        break;
    }
#elif (APP_TEST_MODE == APP_TEST_AX25_LOOPBACK)
    switch (s_appState)
    {
    case APP_IDLE:
        if ((now - s_beaconTimer) >= LOOPBACK_PERIOD_MS)
        {
            s_loopbackOk = false;
            if (loadFixedFrame())
            {
                s_spiStatusMsg = SPI_STATUS_TX_ACTIVE;
                AFSK_TX_Start();
                s_appState = APP_TX;
            }
            else
            {
                s_spiStatusMsg = SPI_STATUS_LOOPBACK_ERR;
                s_beaconTimer = now;
            }
        }
        break;

    case APP_TX:
        if (AFSK_TX_IsDone())
        {
            AFSK_TX_Stop();
            if (!s_loopbackOk)
                s_spiStatusMsg = SPI_STATUS_LOOPBACK_ERR;
            s_beaconTimer = now;
            s_appState = APP_IDLE;
        }
        break;

    default:
        s_appState = APP_IDLE;
        break;
    }
#elif (APP_TEST_MODE == APP_TEST_RX_MONITOR)
    /* ---- APP_TEST_RX_MONITOR mode ----------------------------------------
     * Pure listen mode. No PTT, no TX ever.
     * The onRxFrameMonitor ISR callback handles all frame reporting via SPI.
     * App_Run just keeps the status byte at RX_IDLE between frames.
     * ----------------------------------------------------------------------- */
    s_spiStatusMsg = SPI_STATUS_RX_IDLE;
    (void)now; /* suppress unused-variable warning */

#elif (APP_TEST_MODE == COMPLETE_DIGIPEATER)
    /* ---- COMPLETE_DIGIPEATER mode ----------------------------------------
     * APP_IDLE:   Poll Digi_IsPending(). When set AND channel is clear, build
     *             the digipeated bit-stream, assert PTT, advance to APP_PTT_ON.
     *             If channel stays busy for DRA818_CHANNEL_WAIT_SEC seconds,
     *             drop the frame and return to APP_IDLE.
     * APP_PTT_ON: Wait DRA818_PTT_ON_DELAY_MS then start AFSK TX.
     * APP_TX:     Wait for AFSK_TX_IsDone() (or timeout), then deassert PTT.
     * APP_PTT_OFF:Wait DRA818_PTT_OFF_DELAY_MS, return to APP_IDLE.
     * ----------------------------------------------------------------------- */
    switch (s_appState)
    {
    case APP_IDLE:
    {
        uint8_t  *digiFrame = NULL;
        uint16_t  digiLen   = 0U;

        s_spiStatusMsg = SPI_STATUS_RX_IDLE;

        if (Digi_IsPending() && Digi_GetFrame(&digiFrame, &digiLen))
        {
            if (!DRA818_IsChannelBusy())
            {
                /* Channel clear: encode and key up */
                if (AX25_BuildDigiTxFrame(digiFrame, digiLen))
                {
                    Digi_ClearPending();
                    DRA818_SetPTT(true);
                    s_spiStatusMsg = SPI_STATUS_DIGI_TX;
                    s_stateTimer   = now;
                    s_appState     = APP_PTT_ON;
                }
                else
                {
                    Digi_ClearPending(); /* Overflow — drop */
                }
            }
            else if ((now - s_beaconTimer) >= ((uint32_t)DRA818_CHANNEL_WAIT_SEC * 1000UL))
            {
                Digi_ClearPending(); /* Timed out waiting for clear channel */
                s_beaconTimer = now;
            }
        }
        else
        {
            s_beaconTimer = now; /* Nothing pending — reset channel-wait timer */
        }
        break;
    }

    case APP_PTT_ON:
        if ((now - s_stateTimer) >= DRA818_PTT_ON_DELAY_MS)
        {
            AFSK_TX_Start();
            s_stateTimer = now;
            s_appState   = APP_TX;
        }
        break;

    case APP_TX:
        if (AFSK_TX_IsDone() || ((now - s_stateTimer) >= APP_PACKET_TX_MAX_MS))
        {
            AFSK_TX_Stop();
            s_spiStatusMsg = SPI_STATUS_TX_DONE;
            s_stateTimer   = now;
            s_appState     = APP_PTT_OFF;
        }
        break;

    case APP_PTT_OFF:
        if ((now - s_stateTimer) >= DRA818_PTT_OFF_DELAY_MS)
        {
            DRA818_SetPTT(false);
            s_beaconTimer = now;
            s_appState    = APP_IDLE;
        }
        break;

    default:
        s_appState = APP_IDLE;
        break;
    }
#else
    switch (s_appState)
    {
    case APP_IDLE:
        if ((s_handshakeResult == DRA818_OK) &&
            ((now - s_beaconTimer) >= APRS_BEACON_INTERVAL_MS))
        {
            s_stateTimer = now;
            s_appState = APP_CHECK_CHANNEL;
        }
        break;

    case APP_CHECK_CHANNEL:
        if (!DRA818_IsChannelBusy())
        {
            if (loadTrackerFrame())
            {
                DRA818_SetPTT(true);
                s_spiStatusMsg = SPI_STATUS_TX_ACTIVE;
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
            s_spiStatusMsg = SPI_STATUS_TX_DONE;
            s_stateTimer = now;
            s_appState = APP_PTT_OFF;
        }
        break;

    case APP_PTT_OFF:
        if ((now - s_stateTimer) >= DRA818_PTT_OFF_DELAY_MS)
        {
            DRA818_SetPTT(false);
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

bool App_GpsHasFix(void)
{
    return s_gps.location.valid;
}

void App_GetGpsCoords(int *lat_int, int *lat_frac, int *lng_int, int *lng_frac)
{
    double lat = TinyGPSLocation_Lat(&s_gps.location);
    double lng = TinyGPSLocation_Lng(&s_gps.location);

    *lat_int = (int)lat;
    *lat_frac = (int)((lat < 0 ? *lat_int - lat : lat - *lat_int) * 1000000.0);
    *lng_int = (int)lng;
    *lng_frac = (int)((lng < 0 ? *lng_int - lng : lng - *lng_int) * 1000000.0);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    (void)GPIO_Pin;
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spiStatusStartIfReady();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spiStatusStartIfReady();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        TinyGPSPlus_Encode(&s_gps, (char)s_uartRxByte);
        HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        s_gps.failedChecksumCount++;
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
        HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1U);
    }
}
