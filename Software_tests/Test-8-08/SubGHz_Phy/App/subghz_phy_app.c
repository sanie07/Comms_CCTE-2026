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
#include "Protocol/AX25_SubGHz.h"
#include <string.h>
#include "main.h"
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RF_FRECUENCY 							433000000
#define TX_OUTPUT_POWER							16
#define GFSK_FDEV                               4800     /* Hz — h=1.0 @ 9600bps, compatible G3RUH SDR */
#define GFSK_DATARATE                           9600     /* bps */
#define GFSK_PREAMBLE_LENGTH                    64         /* Same for Tx and Rx */
#define GFSK_FIX_LENGTH_PAYLOAD_ON              false
#define TX_TIMEOUT_VALUE						3000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */
volatile uint8_t TxComplete = 0;

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

  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */

/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
	TxComplete = 1;
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
	TxComplete = 2;
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
void Transmitir_Prueba_GFSK(void){
	Radio.Init(&RadioEvents);
	Radio.SetModem(MODEM_FSK);
	Radio.SetChannel(RF_FREQUENCY);

	Radio.SetTxConfig(MODEM_FSK,
						TX_OUTPUT_POWER,
						GFSK_FDEV,
						0,
	                    GFSK_DATARATE,
						0,
	                    GFSK_PREAMBLE_LENGTH,
						GFSK_FIX_LENGTH_PAYLOAD_ON,
	                    false,
						0, 0,
						0,
						3000);
	
	static AX25SG_Client_t client;
	AX25SG_Init(&client, "NOCALL", 0, 16);
	AX25SG_SetScrambler(&client, AX25SG_SCRAMBLER_G3RUH_POLY, AX25SG_SCRAMBLER_G3RUH_INIT);

	uint8_t datos[AX25SG_MAX_FRAME_BUF];
	uint16_t tamano = AX25SG_BuildUIFrame(&client, "Hola mundo", "CQ", 0, datos, sizeof(datos));

	if (tamano == 0) return;
	
	TxComplete = 0;
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	Radio.Sleep();
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_RESET);
}

void Transmitir_1200_GFSK(void){
	Radio.Init(&RadioEvents);
	Radio.SetModem(MODEM_FSK);
	Radio.SetChannel(RF_FREQUENCY);

	/* Configurar TX a 1200 bps */
	Radio.SetTxConfig(MODEM_FSK,
						TX_OUTPUT_POWER,
						GFSK_FDEV,
						0,
	                    1200,
						0,
	                    GFSK_PREAMBLE_LENGTH,
						GFSK_FIX_LENGTH_PAYLOAD_ON,
	                    false,
						0, 0,
						0,
						3000);
	
	static AX25SG_Client_t client;
	AX25SG_Init(&client, "NOCALL", 0, 16);
	/* 1200 bps: NRZI sin G3RUH (scramblerPoly = 0 por defecto) */

	uint8_t datos[AX25SG_MAX_FRAME_BUF];
	uint16_t tamano = AX25SG_BuildUIFrame(&client, "Hola mundo 1200 NRZI", "APRS", 0, datos, sizeof(datos));

	if (tamano == 0) return;
	
	TxComplete = 0;
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	Radio.Sleep();
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_RESET);
}

void Transmitir_9600_GFSK(void){
	Radio.Init(&RadioEvents);
	Radio.SetModem(MODEM_FSK);
	Radio.SetChannel(RF_FREQUENCY);

	/* Configurar TX a 9600 bps */
	Radio.SetTxConfig(MODEM_FSK,
						TX_OUTPUT_POWER,
						GFSK_FDEV,
						0,
	                    9600,
						0,
	                    GFSK_PREAMBLE_LENGTH,
						GFSK_FIX_LENGTH_PAYLOAD_ON,
	                    false,
						0, 0,
						0,
						3000);
	
	static AX25SG_Client_t client;
	AX25SG_Init(&client, "NOCALL", 0, 16);
	AX25SG_SetScrambler(&client, AX25SG_SCRAMBLER_G3RUH_POLY, AX25SG_SCRAMBLER_G3RUH_INIT);

	uint8_t datos[AX25SG_MAX_FRAME_BUF];
	uint16_t tamano = AX25SG_BuildUIFrame(&client, "Hola mundo 9600 G3RUH NRZ", "APRS", 0, datos, sizeof(datos));

	if (tamano == 0) return;
	
	TxComplete = 0;
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	Radio.Sleep();
	
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_RESET);
}

/* -----------------------------------------------------------------------
 * Transmitir_HolaMundo_AX25
 * Envía "Hola Mundo" (o el mensaje indicado) como trama AX.25 UI a 9600 bps
 * usando el middleware SubGHz (Radio.Send).
 * ----------------------------------------------------------------------- */
void Transmitir_Mensaje_AX25(const char *mensaje)
{
    if (mensaje == NULL) {
        mensaje = "Hola Mundo";
    }

    static AX25SG_Client_t ax25Client;
    static uint8_t clientInitDone = 0U;

    if (!clientInitDone) {
        AX25SG_Init(&ax25Client,
                    "NOCALL",   /* Callsign de origen     */
                    11U,        /* SSID de origen          */
                    16U);       /* 16 bytes de preámbulo  */
        /* G3RUH para 9600 bps */
        AX25SG_SetScrambler(&ax25Client,
                            AX25SG_SCRAMBLER_G3RUH_POLY,
                            AX25SG_SCRAMBLER_G3RUH_INIT);
        clientInitDone = 1U;
    }

    static uint8_t txBuf[AX25SG_MAX_FRAME_BUF];
    uint16_t tamano = AX25SG_BuildUIFrame(
                          &ax25Client,
                          mensaje,        /* Mensaje Info          */
                          "NOCALL",           /* Callsign destino      */
                          0U,             /* SSID destino          */
                          txBuf,
                          sizeof(txBuf));

    if (tamano == 0U) {
        return;   /* Error al construir la trama */
    }

    Radio.Init(&RadioEvents);
    Radio.SetModem(MODEM_FSK);
    Radio.SetChannel(RF_FREQUENCY);

    Radio.SetTxConfig(MODEM_FSK,
                      TX_OUTPUT_POWER,
                      GFSK_FDEV,
                      0,
                      GFSK_DATARATE,
                      0,
                      GFSK_PREAMBLE_LENGTH,
                      GFSK_FIX_LENGTH_PAYLOAD_ON,
                      false,
                      0, 0,
                      0,
                      TX_TIMEOUT_VALUE);

    TxComplete = 0;

    HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);

    Radio.Send(txBuf, tamano);

    while (TxComplete == 0) {
        Radio.IrqProcess();
    }

    Radio.Sleep();

    HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_RESET);
}

void Transmitir_HolaMundo_AX25(void)
{
    Transmitir_Mensaje_AX25("Hola Mundo");
}

void Transmitir_HolaMundo_AX25RadioLib(void)
{
    Transmitir_HolaMundo_AX25();
}
/* USER CODE END PrFD */
