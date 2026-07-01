/**
 * @file    app.c
 * @brief   GPS APRS tracker: TinyGPS + AX.25/AFSK TX via DRA818, SPI status to ESP32.
 */

#include "app.h"
#include "aprs_config.h"
#include "dra818.h"
#include "afsk.h"
#include "ax25.h"
#include "tinygps.h"
#include "spi.h"
#include "usart.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

typedef enum
{
    APP_IDLE = 0,
    APP_CHECK_CHANNEL,
    APP_PTT_ON,
    APP_TX,
    APP_PTT_OFF,
} AppState_t;

static AppState_t s_appState = APP_IDLE;
static uint32_t   s_beaconTimer = 0U;
static uint32_t   s_stateTimer = 0U;

static int s_handshakeResult = DRA818_ERR;
static uint8_t s_spiStatusMsg = SPI_STATUS_INIT;
static uint8_t s_dummyRx = 0;

static TinyGPSPlus s_gps;
static uint8_t s_uartRxByte = 0;

static void gpsStartRx(void)
{
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
    if (HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1) != HAL_OK)
    {
        s_gps.encodedCharCount = 999999U;
    }
}

static void onRxFrame(const uint8_t *info, uint16_t infoLen, const char *srcCall)
{
    (void)info;
    (void)infoLen;
    (void)srcCall;
}

static void formatAprsLat(double lat, char *out, uint8_t outLen)
{
    char hemi = (lat >= 0.0) ? 'N' : 'S';
    if (lat < 0.0)
    {
        lat = -lat;
    }

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
    {
        lng = -lng;
    }

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
        double lat = TinyGPSLocation_Lat(&s_gps.location);
        double lng = TinyGPSLocation_Lng(&s_gps.location);

        formatAprsLat(lat, aprsLat, sizeof(aprsLat));
        formatAprsLon(lng, aprsLon, sizeof(aprsLon));
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
    if (s_handshakeResult != DRA818_OK)
    {
        s_spiStatusMsg = SPI_STATUS_HANDSHAKE_ERR;
    }
    else if (s_gps.location.valid)
    {
        s_spiStatusMsg = SPI_STATUS_GPS_FIX;
    }
    else
    {
        s_spiStatusMsg = SPI_STATUS_GPS_WAIT;
    }
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
    TinyGPSPlus_Init(&s_gps);
    gpsStartRx();

    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    HAL_NVIC_SetPriority(SPI1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);

    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    AX25_Init(onRxFrame);
    AFSK_Init();

    s_handshakeResult = DRA818_Init();
    s_spiStatusMsg = (s_handshakeResult == DRA818_OK) ? SPI_STATUS_HANDSHAKE_OK : SPI_STATUS_HANDSHAKE_ERR;
    HAL_SPI_TransmitReceive_IT(&hspi1, &s_spiStatusMsg, &s_dummyRx, 1);

    HAL_TIM_Base_Start_IT(&htim2);

    s_beaconTimer = HAL_GetTick();
    s_stateTimer = HAL_GetTick();
    s_appState = APP_IDLE;
}

void App_Run(void)
{
    uint32_t now = HAL_GetTick();

    if (s_appState == APP_IDLE)
    {
        updateSpiStatusIdle();
    }

    switch (s_appState)
    {
    case APP_IDLE:
        if ((s_handshakeResult == DRA818_OK) &&
            ((now - s_beaconTimer) >= APRS_BEACON_INTERVAL_MS))
        {
            s_appState = APP_CHECK_CHANNEL;
            s_stateTimer = now;
        }
        break;

    case APP_CHECK_CHANNEL:
        if (!DRA818_IsChannelBusy())
        {
            uint8_t infoStr[AX25_MAX_INFO_LEN];
            uint16_t infoLen = 0U;

            if (buildGpsBeaconInfo(infoStr, &infoLen))
            {
                AX25_BuildTxFrame(infoStr, infoLen);
                DRA818_SetPTT(true);
                s_spiStatusMsg = SPI_STATUS_TX_ACTIVE;
                s_appState = APP_PTT_ON;
                s_stateTimer = now;
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
            s_appState = APP_TX;
        }
        break;

    case APP_TX:
        if (AFSK_TX_IsDone())
        {
            AFSK_TX_Stop();
            s_spiStatusMsg = SPI_STATUS_TX_DONE;
            s_appState = APP_PTT_OFF;
            s_stateTimer = now;
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
    *lat_frac = (int)((lat < 0 ? *lat_int - lat : lat - *lat_int) * 1000000);
    *lng_int = (int)lng;
    *lng_frac = (int)((lng < 0 ? *lng_int - lng : lng - *lng_int) * 1000000);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SPI1_CS_Pin)
    {
        if (HAL_GPIO_ReadPin(SPI1_CS_GPIO_Port, SPI1_CS_Pin) == GPIO_PIN_SET)
        {
            HAL_SPI_Abort(&hspi1);
            HAL_SPI_TransmitReceive_IT(&hspi1, &s_spiStatusMsg, &s_dummyRx, 1);
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        TinyGPSPlus_Encode(&s_gps, (char)s_uartRxByte);
        HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        s_gps.failedChecksumCount++;
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
        HAL_UART_Receive_IT(&huart2, &s_uartRxByte, 1);
    }
}
