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
#include "stm32f7xx_hal.h"

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
#define SD_Detect_Pin GPIO_PIN_13
#define SD_Detect_GPIO_Port GPIOC
#define OCP_Enable_Pin GPIO_PIN_0
#define OCP_Enable_GPIO_Port GPIOC
#define BattSense_Pin GPIO_PIN_0
#define BattSense_GPIO_Port GPIOA
#define Accel_Int_1_Pin GPIO_PIN_5
#define Accel_Int_1_GPIO_Port GPIOA
#define Accel_Int_1_EXTI_IRQn EXTI9_5_IRQn
#define Magneto_Int_Pin GPIO_PIN_4
#define Magneto_Int_GPIO_Port GPIOC
#define Magneto_Int_EXTI_IRQn EXTI4_IRQn
#define Accel_Int_2_Pin GPIO_PIN_0
#define Accel_Int_2_GPIO_Port GPIOB
#define Accel_Int_2_EXTI_IRQn EXTI0_IRQn
#define Gyro_Int_1_Pin GPIO_PIN_1
#define Gyro_Int_1_GPIO_Port GPIOB
#define Gyro_Int_1_EXTI_IRQn EXTI1_IRQn
#define Gyro_Int_2_Pin GPIO_PIN_2
#define Gyro_Int_2_GPIO_Port GPIOB
#define Gyro_Int_2_EXTI_IRQn EXTI2_IRQn
#define IO_3_Pin GPIO_PIN_9
#define IO_3_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
