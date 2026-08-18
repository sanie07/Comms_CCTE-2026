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
#define TRANSMIT_PERIOD_MS                          5000  /* 1000 ms between transmissions */

/* AX.25 Settings for 1200 bps FSK (FIUNA1 / CUBE1) */
#define AX25_SRC_CALLSIGN                           "CUBE1 "
#define AX25_SRC_SSID                               0
#define AX25_DEST_CALLSIGN                          "FIUNA1"
#define AX25_DEST_SSID                              1
#define AX25_PREAMBLE_FLAGS                         32    /* 32 preamble 0x7E flags for robust PLL lock */
#define AX25_USE_G3RUH_SCRAMBLER                    0    /* 0 = Standard NRZI (for standard SoundModem / DireWolf), 1 = G3RUH */
#define AX25_INVERT_POLARITY                        0    /* 1 = Invert FSK bit polarity (0 <-> 1), 0 = Normal */
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
  /* Configure FSK TX using SetTxConfig */
  Radio.SetModem(MODEM_FSK);
  Radio.SetChannel(RF_FREQUENCY);  /* 433.018 MHz */

  /* Set TX config for AX.25:
   * preambleLen = 0 (flags are encoded directly in the AX.25 bitstream)
   * crcOn = false (AX.25 FCS CRC-CCITT is computed and included in software)
   * fixLen = false (variable length packet) */
  Radio.SetTxConfig(MODEM_FSK,
                    TX_OUTPUT_POWER, /* 22 dBm, HP PA */
                    FSK_FDEV,        /* 5000 Hz deviation */
                    0,               /* bandwidth (0 = auto) */
                    FSK_DATARATE,    /* 1200 bps */
                    0,               /* coderate (FSK: unused) */
                    8,               /* preamble length (0 = software flags used) */
                    false,           /* fixed length off */
                    false,           /* HW CRC off (SW FCS used) */
                    0, 0,            /* freq hop: off */
                    0,               /* IQ invert: off */
                    3000);           /* TX timeout ms */

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

static void TransmitPacket(void)
{
  static uint8_t ax25TxBuf[AX25SG_MAX_FRAME_BUF];
  uint16_t tamano = AX25SG_BuildUIFrame(&ax25Client,
                                        "GPS:-25.263700,-57.575900,400.0,SAT:8",
                                        AX25_DEST_CALLSIGN,
                                        AX25_DEST_SSID,
                                        ax25TxBuf,
                                        sizeof(ax25TxBuf));

  if (tamano > 0)
  {
    APP_PRINTF("\r\n--- Transmitting AX.25 UI Frame (%d bytes) ---\r\n", tamano);
    HAL_GPIO_WritePin(RF_CRL_TO_STM32_GPIO_Port, RF_CRL_TO_STM32_Pin, GPIO_PIN_SET);
    Radio.Send(ax25TxBuf, tamano);
  }
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
  if (tamano > 0)
  {
    HAL_GPIO_WritePin(RF_CRL_TO_STM32_GPIO_Port, RF_CRL_TO_STM32_Pin, GPIO_PIN_SET);
    Radio.Send(customBuf, tamano);
  }
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
