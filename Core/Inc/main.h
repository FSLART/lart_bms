/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c3;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim5;
//extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim8;
extern TIM_HandleTypeDef htim10;
extern TIM_HandleTypeDef htim11;
extern RTC_HandleTypeDef hrtc;
extern ADC_HandleTypeDef hadc1;
extern CAN_HandleTypeDef hcan1;
extern CAN_HandleTypeDef hcan2;
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

#define TOTAL_IC        2
#define TOTAL_CELL      12
//#define TOTAL_AD68      (TOTAL_IC - 1)
//#define TOTAL_AD68      1

#define RTH_PER_MODULE 6

#define OW_RATIO_MIN      0.85f   // intact lower bound (S-ADC OW path)
#define OW_RATIO_MAX      0.95f   // intact upper bound (S-ADC OW path)
#define OW_UV_IGNORE_THRESH  0.10f   // Vref below this -> treat as invalid for OW ratio (V)
#define OW_OPEN_EDGE         0.80f   // clearly open if below this

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define B1_EXTI_IRQn EXTI15_10_IRQn
#define BT_USART2_TX_Pin GPIO_PIN_2
#define BT_USART2_TX_GPIO_Port GPIOA
#define BT_USART2_RX_Pin GPIO_PIN_3
#define BT_USART2_RX_GPIO_Port GPIOA
#define BMS_CS_Pin GPIO_PIN_4
#define BMS_CS_GPIO_Port GPIOA
#define CONTACT_DSCH_Pin GPIO_PIN_4
#define CONTACT_DSCH_GPIO_Port GPIOC
#define CONTACT_PRE_Pin GPIO_PIN_5
#define CONTACT_PRE_GPIO_Port GPIOC
#define CONTACT_AIR_positivo_Pin GPIO_PIN_0
#define CONTACT_AIR_positivo_GPIO_Port GPIOB
#define CONTACT_AIR_negativo_Pin GPIO_PIN_1
#define CONTACT_AIR_negativo_GPIO_Port GPIOB
#define LED_BLUE_Pin GPIO_PIN_2
#define LED_BLUE_GPIO_Port GPIOB
#define LED_RED_Pin GPIO_PIN_10
#define LED_RED_GPIO_Port GPIOB
#define CHARGER_CAN_RX_Pin GPIO_PIN_12
#define CHARGER_CAN_RX_GPIO_Port GPIOB
#define CHARGER_CAN_TX_Pin GPIO_PIN_13
#define CHARGER_CAN_TX_GPIO_Port GPIOB
#define FAN_CONTROL_Pin GPIO_PIN_14
#define FAN_CONTROL_GPIO_Port GPIOB
#define CURRENT_SENS_Pin GPIO_PIN_15
#define CURRENT_SENS_GPIO_Port GPIOB
#define CURRENT_SENS_EXTI_IRQn EXTI15_10_IRQn
#define MCU_SDC_FB_Pin GPIO_PIN_7
#define MCU_SDC_FB_GPIO_Port GPIOC
#define AMS_ERROR_Pin GPIO_PIN_8
#define AMS_ERROR_GPIO_Port GPIOC
#define AUX_SENS_SDA_Pin GPIO_PIN_9
#define AUX_SENS_SDA_GPIO_Port GPIOC
#define AUX_SENS_SCL_Pin GPIO_PIN_8
#define AUX_SENS_SCL_GPIO_Port GPIOA
#define BMS_CAN_RX_Pin GPIO_PIN_11
#define BMS_CAN_RX_GPIO_Port GPIOA
#define BMS_CAN_TX_Pin GPIO_PIN_12
#define BMS_CAN_TX_GPIO_Port GPIOA
#define MCU_PRE_FB_Pin GPIO_PIN_15
#define MCU_PRE_FB_GPIO_Port GPIOA
#define MCU_AIR__FB_Pin GPIO_PIN_10
#define MCU_AIR__FB_GPIO_Port GPIOC
#define MCU_AIR__FBC11_Pin GPIO_PIN_11
#define MCU_AIR__FBC11_GPIO_Port GPIOC
#define MCU_DISCH_FB_Pin GPIO_PIN_12
#define MCU_DISCH_FB_GPIO_Port GPIOC
#define LED_CAN_STATUS_Pin GPIO_PIN_2
#define LED_CAN_STATUS_GPIO_Port GPIOD
#define LED_isoSPI_STATUS_Pin GPIO_PIN_3
#define LED_isoSPI_STATUS_GPIO_Port GPIOB
#define BMS_INT_Pin GPIO_PIN_5
#define BMS_INT_GPIO_Port GPIOB
#define BMS_WAKE_Pin GPIO_PIN_6
#define BMS_WAKE_GPIO_Port GPIOB
#define BMS_MSTR_Pin GPIO_PIN_7
#define BMS_MSTR_GPIO_Port GPIOB
#define EEPROM_SCL_Pin GPIO_PIN_8
#define EEPROM_SCL_GPIO_Port GPIOB
#define EEPROM_SDA_Pin GPIO_PIN_9
#define EEPROM_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
