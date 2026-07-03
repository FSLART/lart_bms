/*
 * adbms_to_CAN.h
 *
 *  Created on: Mar 9, 2026
 *      Author: jpser
 */

#ifndef INC_ADBMS_TO_CAN_H_
#define INC_ADBMS_TO_CAN_H_

#include "main.h"
#include "brain.h"
#include "can.h"
#include "adBms6830Data.h"

typedef enum {
    ADBMS_CELL,
    ADBMS_GPIO
} adbms_data_type_t;

HAL_StatusTypeDef ADBMS_CAN_Send_Master_MSC_3(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef ADBMS_CAN_SendMSC_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendVoltages_Module(CAN_HandleTypeDef *hcan, uint8_t module, AMSStates_t ams_current_state);
HAL_StatusTypeDef ADBMS_CAN_SendTemperatures_Module(CAN_HandleTypeDef *hcan, uint8_t module);
HAL_StatusTypeDef ADBMS_CAN_SendAll(CAN_HandleTypeDef *hcan, AMSStates_t ams_current_state);

/* Pack-level overalls cached by ADBMS_CAN_Send_Master_MSC_3, for live debug */
extern uint16_t g_pack_vmax_mV;
extern uint16_t g_pack_vmin_mV;
extern int16_t  g_pack_tmax_cC;
extern int16_t  g_pack_tmin_cC;
extern uint32_t g_pack_voltage_sum_mV;

float data_to_volts(int16_t, adbms_data_type_t);
float getTemperatureCAN(int16_t);

void BMS_SafetyCheck(void);

#endif /* INC_ADBMS_TO_CAN_H_ */
