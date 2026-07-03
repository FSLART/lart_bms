/*
 * master_to_CAN.h
 *
 *  Created on: Mar 15, 2026
 *      Author: jpser
 */

#ifndef INC_MASTER_TO_CAN_H_
#define INC_MASTER_TO_CAN_H_

#include "main.h"
#include "brain.h"
#include "can.h"

HAL_StatusTypeDef Master_CAN_Send_MSC_1(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef Master_CAN_Send_MSC_2(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef Master_CAN_Send_MSC_4(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef Master_CAN_SendPrecharge(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef Master_CAN_SendAll(CAN_HandleTypeDef *hcan);

uint8_t count_active_faults(void);
uint8_t read_contactor_state(GPIO_TypeDef *gpio_port, uint16_t gpio_pin);


#endif /* INC_MASTER_TO_CAN_H_ */
