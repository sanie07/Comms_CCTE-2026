/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "platform.h"
#include "sys_app.h"
#include "subghz_phy_app.h"
#include "radio.h"

/* USER CODE BEGIN Includes */
#include "main.h"  /* RF_CRL_TO_STM32_Pin, RF_CRL_TO_STM32_GPIO_Port */
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "utilities_def.h"
#include "AX25_SubGHz.h"
#include <string.h>
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
static UTIL_TIMER_Object_t timerTransmit;
static AX25SG_Client_t ax25Client;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* NOTE: All RF parameters are defined in subghz_phy_app.h */
#define TRANSMIT_PERIOD_MS                          5000

/* AX.25 Settings for 1200 bps FSK (FIUNA1 / CUBE1)
 * Path A: SX126x/SX1278 packet radio carrying AX.25 bytes as payload.
 * PHY already sends 8-byte preamble + 3-byte sync, so extra 0x7E flags
 * would push the frame over the SX1278 63-byte FSK FIFO limit.
 * Path B (direwolf/soundmodem) needs NRZI + bit-stuffing and no packet
 * header; see AX25_SubGHz.c. */
#define AX25_SRC_CALLSIGN                           "CCTE"
#define AX25_SRC_SSID                               0
#define AX25_DEST_CALLSIGN                          "FIUNA1"
#define AX25_DEST_SSID                              1
#define AX25_PREAMBLE_FLAGS                         0     /* PHY supplies preamble + syncword */
#define AX25_USE_G3RUH_SCRAMBLER                    0
#define AX25_INVERT_POLARITY                        0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */
uint8_t TxBuffer[PAYLOAD_LEN] = "PING";
volatile uint8_t TxComplete = 0;   /* 0=pending, 1=done, 2=timeout */
static uint8_t FskSyncWord[] = { 0xC1, 0x94, 0xC1 };
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/*!
 * @brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void);

/**
  * @brief Function to be executed on Radio Rx Done event
  * @param  payload ptr of buffer received
  * @param  size buffer size
  * @param  rssi
  * @param  LoraSnr_FskCfo
  */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);

/**
  * @brief Function executed on Radio Tx Timeout event
  */
static void OnTxTimeout(void);

/**
  * @brief Function executed on Radio Rx Timeout event
  */
static void OnRxTimeout(void);

/**
  * @brief Function executed on Radio Rx Error event
  */
static void OnRxError(void);

/* USER CODE BEGIN PFP */
static void TransmitPacket(void);
static void OnTxTimerEvent(void *context);
static void SendAx25Buffer(uint8_t *buf, uint16_t tamano);
/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */
  /* USER CODE END SubghzApp_Init_1 */

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);

  /* USER CODE BEGIN SubghzApp_Init_2 */
  /* Pin PHY to match ESP32/RadioLib SX1278:
   * BT=0.5, whitening off, CRC off, variable length, sync C1 94 C1. */
  TxConfigGeneric_t TxConfig = {0};

  Radio.SetChannel(RF_FREQUENCY);

  TxConfig.fsk.ModulationShaping = RADIO_FSK_MOD_SHAPING_G_BT_05;
  TxConfig.fsk.FrequencyDeviation = FSK_FDEV;
  TxConfig.fsk.BitRate = FSK_DATARATE;
  TxConfig.fsk.PreambleLen = FSK_PREAMBLE_LENGTH;
  TxConfig.fsk.SyncWordLength = sizeof(FskSyncWord);
  TxConfig.fsk.SyncWord = FskSyncWord;
  TxConfig.fsk.whiteSeed = 0x0000U;
  TxConfig.fsk.HeaderType = RADIO_FSK_PACKET_VARIABLE_LENGTH;
  TxConfig.fsk.CrcLength = RADIO_FSK_CRC_OFF;
  TxConfig.fsk.Whitening = RADIO_FSK_DC_FREE_OFF;
  if (0UL != Radio.RadioSetTxGenericConfig(GENERIC_FSK, &TxConfig,
                                           TX_OUTPUT_POWER, TX_TIMEOUT_VALUE))
  {
    APP_PRINTF("RadioSetTxGenericConfig failed\r\n");
    Error_Handler();
  }

  /* Initialize AX.25 Client (Callsign, SSID, Preamble flags) */
  AX25SG_Init(&ax25Client, AX25_SRC_CALLSIGN, AX25_SRC_SSID, AX25_PREAMBLE_FLAGS);

#if (AX25_INVERT_POLARITY == 1)
  /* Invert output bit polarity */
  AX25SG_SetInvert(&ax25Client, true);
#endif

#if (AX25_USE_G3RUH_SCRAMBLER == 1)
  /* Enable G3RUH scrambler required by UZ7HO High-Speed SoundModem (hs_soundmodem) */
  AX25SG_SetScrambler(&ax25Client, AX25SG_SCRAMBLER_G3RUH_POLY, AX25SG_SCRAMBLER_G3RUH_INIT);
#endif

  /* Register transmit task with the sequencer */
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, TransmitPacket);

  /* Create and start periodic transmission timer */
  UTIL_TIMER_Create(&timerTransmit, 0xFFFFFFFFU, UTIL_TIMER_PERIODIC, OnTxTimerEvent, NULL);
  UTIL_TIMER_SetPeriod(&timerTransmit, TRANSMIT_PERIOD_MS);
  UTIL_TIMER_Start(&timerTransmit);

  /* Trigger first transmission immediately */
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */
static void OnTxTimerEvent(void *context)
{
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
}

static void SendAx25Buffer(uint8_t *buf, uint16_t tamano)
{
  if ((buf == NULL) || (tamano == 0U))
  {
    APP_PRINTF("AX.25 build failed\r\n");
    return;
  }
  if (tamano > SX1278_FSK_MAX_PAYLOAD)
  {
    APP_PRINTF("AX.25 frame %u bytes exceeds SX1278 FSK limit %u\r\n",
               (unsigned)tamano, (unsigned)SX1278_FSK_MAX_PAYLOAD);
    return;
  }

  APP_PRINTF("\r\n--- Transmitting AX.25 UI Frame (%u bytes) ---\r\n",
             (unsigned)tamano);
  HAL_GPIO_WritePin(RF_CRL_TO_STM32_GPIO_Port, RF_CRL_TO_STM32_Pin, GPIO_PIN_SET);
  Radio.Send(buf, (uint8_t)tamano);
}

static void TransmitPacket(void)
{
  static uint8_t ax25TxBuf[AX25SG_MAX_FRAME_BUF];
  uint16_t tamano = AX25SG_BuildUIFrame(&ax25Client,
                                        "GPS:-25.330243,-57.517492,100.0,SAT:4",
                                        AX25_DEST_CALLSIGN,
                                        AX25_DEST_SSID,
                                        ax25TxBuf,
                                        sizeof(ax25TxBuf));
  SendAx25Buffer(ax25TxBuf, tamano);
}

void Transmitir_Mensaje_AX25(const char *mensaje)
{
  if (mensaje == NULL) return;
  static uint8_t customBuf[AX25SG_MAX_FRAME_BUF];
  uint16_t tamano = AX25SG_BuildUIFrame(&ax25Client,
                                        mensaje,
                                        AX25_DEST_CALLSIGN,
                                        AX25_DEST_SSID,
                                        customBuf,
                                        sizeof(customBuf));
  SendAx25Buffer(customBuf, tamano);
}

void Transmitir_HolaMundo_AX25(void)
{
  Transmitir_Mensaje_AX25("H");
}
/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  Radio.Sleep();
  HAL_GPIO_WritePin(RF_CRL_TO_STM32_GPIO_Port, RF_CRL_TO_STM32_Pin, GPIO_PIN_RESET);
  TxComplete = 1;
  APP_PRINTF("TX Complete: OK (AX.25 Frame sent)\r\n");
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  /* USER CODE END OnRxDone */
}

static void OnTxTimeout(void)
{
  /* USER CODE BEGIN OnTxTimeout */
  Radio.Sleep();
  HAL_GPIO_WritePin(RF_CRL_TO_STM32_GPIO_Port, RF_CRL_TO_STM32_Pin, GPIO_PIN_RESET);
  TxComplete = 2;
  APP_PRINTF("TX Complete: Timeout\r\n");
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  /* USER CODE END OnRxError */
}

/* USER CODE BEGIN PrFD */

/* USER CODE END PrFD */
