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
#define RTC_N_PREDIV_S 10
#define RTC_PREDIV_S 5
#define RTC_PREDIV_A 4
#define RF_CTRL_TO_STM_Pin GPIO_PIN_4
#define RF_CTRL_TO_STM_GPIO_Port GPIOA
#define DRA_SQ_TO_STM_Pin GPIO_PIN_5
#define DRA_SQ_TO_STM_GPIO_Port GPIOA
#define STM_TO_DRA_PTT_Pin GPIO_PIN_6
#define STM_TO_DRA_PTT_GPIO_Port GPIOA
#define RF_CTRL_Pin GPIO_PIN_9
#define RF_CTRL_GPIO_Port GPIOA
#define STM_TO_MIC_Pin GPIO_PIN_10
#define STM_TO_MIC_GPIO_Port GPIOA
#define DRA_TO_STM_Pin GPIO_PIN_11
#define DRA_TO_STM_GPIO_Port GPIOA
#define OCP_NFAULT_Pin GPIO_PIN_14
#define OCP_NFAULT_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
