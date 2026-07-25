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
#include "ax25.h"
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
#define TX_OUTPUT_POWER							20
#define GFSK_FDEV                               3000     /* Hz */
#define GFSK_DATARATE                           9600     /* bps */
#define GFSK_PREAMBLE_LENGTH                    5         /* Same for Tx and Rx */
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
	    /* Set Channel Frequency */
	Radio.SetChannel(RF_FREQUENCY);

	    /* Configure TX parameters for FSK */
	Radio.SetTxConfig(MODEM_FSK,
						TX_OUTPUT_POWER,
						GFSK_FDEV,
						0,
	                    GFSK_DATARATE,
						0,
	                    GFSK_PREAMBLE_LENGTH,
						GFSK_FIX_LENGTH_PAYLOAD_ON,
	                    true,
						0, 0,
						0,
						3000);
	uint8_t payload[] = "Hola mundo";
	uint8_t datos[256];
	/* AX25_BuildStuffedFrame genera el paquete con flags y bit-stuffing. 
	   Si tu radio hace el framming automáticamente, usa AX25_BuildRawFrame en su lugar. */
	uint16_t tamano = AX25_Build9600Frame_G3RUH_NRZ(payload, strlen((char*)payload), datos, sizeof(datos));
	
	TxComplete = 0;
	
	// 1. ENCENDER EL SWITCH DE ANTENA PARA TX
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	//Manda el paquete codificado en AX25
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	    /* Ensure radio is asleep */
	Radio.Sleep();
	
	// 4. APAGAR EL SWITCH DE ANTENA 
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
	                    true,
						0, 0,
						0,
						3000);
	uint8_t payload[] = "Hola mundo 1200 NRZI";
	uint8_t datos[256];
	
	/* Trama AX.25 1200 baudios: Aplica NRZI y SIN G3RUH */
	uint16_t tamano = AX25_Build1200Frame_NRZI(payload, strlen((char*)payload), datos, sizeof(datos));
	
	TxComplete = 0;
	
	// 1. ENCENDER EL SWITCH DE ANTENA PARA TX
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	// Manda el paquete codificado en AX25
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	/* Ensure radio is asleep */
	Radio.Sleep();
	
	// APAGAR EL SWITCH DE ANTENA 
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
	                    true,
						0, 0,
						0,
						3000);
	uint8_t payload[] = "Hola mundo 9600 G3RUH NRZ";
	uint8_t datos[256];
	
	/* Trama AX.25 9600 baudios: Aplica G3RUH y NRZ (sin NRZI) */
	uint16_t tamano = AX25_Build9600Frame_G3RUH_NRZ(payload, strlen((char*)payload), datos, sizeof(datos));
	
	TxComplete = 0;
	
	// 1. ENCENDER EL SWITCH DE ANTENA PARA TX
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_SET);
	
	// Manda el paquete codificado en AX25
	Radio.Send(datos, tamano);
	while(TxComplete == 0){
		Radio.IrqProcess();
	}

	/* Ensure radio is asleep */
	Radio.Sleep();
	
	// APAGAR EL SWITCH DE ANTENA 
	HAL_GPIO_WritePin(RF_CTRL_GPIO_Port, RF_CTRL_Pin, GPIO_PIN_RESET);
}
/* USER CODE END PrFD */
