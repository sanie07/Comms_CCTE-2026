/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32wlxx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI1_CS_Pin GPIO_PIN_0
#define SPI1_CS_GPIO_Port GPIOA
#define STM32_TO_DRA_ENA_Pin GPIO_PIN_6
#define STM32_TO_DRA_ENA_GPIO_Port GPIOA
#define STM32_TO_DRA_PTT_Pin GPIO_PIN_7
#define STM32_TO_DRA_PTT_GPIO_Port GPIOA
#define DRA_SQ_TO_STM32_Pin GPIO_PIN_8
#define DRA_SQ_TO_STM32_GPIO_Port GPIOA
#define STM32_DAC_TO_DRA_Pin GPIO_PIN_10
#define STM32_DAC_TO_DRA_GPIO_Port GPIOA
#define DRA_TO_STM32_ADC_Pin GPIO_PIN_11
#define DRA_TO_STM32_ADC_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
