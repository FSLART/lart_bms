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

typedef enum {
    ADBMS_CELL,
    ADBMS_GPIO
} adbms_data_type_t;

HAL_StatusTypeDef ADBMS_CAN_Send_Master_MSC_3(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef ADBMS_CAN_SendMSC_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendVoltages_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendTemperatures_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendAll(CAN_HandleTypeDef *hcan);

uint16_t data_to_volts(int16_t, adbms_data_type_t);
float getTemperatureCAN(int16_t);

#endif /* INC_ADBMS_TO_CAN_H_ */
