/*
 * adbms_to_CAN.h
 *
 *  Created on: Mar 9, 2026
 *      Author: jpser
 */

#ifndef INC_ADBMS_TO_CAN_H_
#define INC_ADBMS_TO_CAN_H_

#include "main.h"
#include "can.h"
#include "adBms6830Data.h"

HAL_StatusTypeDef ADBMS_CAN_SendVoltages_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendTemperatures_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendAll(CAN_HandleTypeDef *hcan);


#endif /* INC_ADBMS_TO_CAN_H_ */
