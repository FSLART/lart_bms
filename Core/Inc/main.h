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
/* BMS error codes */
typedef enum {
	ERROR_NONE              = 0x00,
	ERROR_SDC_TRIGGERED     = 0x01,  // Bit 0
	ERROR_IMD_TRIGGERED     = 0x02,  // Bit 1
	ERROR_CONTACT_MISMATCH  = 0x04,  // Bit 2
	ERROR_TIMER_FAILURE     = 0x08,  // Bit 3
	ERROR_FDCAN_FAILED      = 0x10,  // Bit 4
	ERROR_OVERVOLTAGE       = 0x20,   // Bit 5
	ERROR_BMS_FAIL			= 0x40   // Bit 6
} ErrorCode_t;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim5;
extern TIM_HandleTypeDef htim6;
extern RTC_HandleTypeDef hrtc;
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

#define TOTAL_IC        1
#define TOTAL_CELL      12
//#define TOTAL_AD68      (TOTAL_IC - 1)
#define TOTAL_AD68      1

#define RTH_PER_MODULE 5

/* USER CODE END EM */

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
#define BMS_CAN_RX_Pin GPIO_PIN_11
#define BMS_CAN_RX_GPIO_Port GPIOA
#define BMS_CAN_TX_Pin GPIO_PIN_12
#define BMS_CAN_TX_GPIO_Port GPIOA
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
