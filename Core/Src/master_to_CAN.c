/*
 * master_to_CAN.c
 *
 *  Created on: Mar 15, 2026
 *      Author: jpser
 */

#include "master_to_CAN.h"

#include <string.h>
#include <stdint.h>

#include "analog_readings.h"
#include "precharge.h"
#include "brain.h"
#include "can.h"
#include "dbc/powertrain_t26.h"
#include "main.h"
#include "brain.h"

/* Set version */
#define MASTER_FW_VERSION    7u

/* temporary */
#ifndef MASTER_FAN_PWM_DEFAULT
#define MASTER_FAN_PWM_DEFAULT    20u
#endif

extern uint8_t anyPecError;

static uint8_t Master_CAN_GetContactorCommandState(GPIO_TypeDef *port, uint16_t pin)
{

	//TODO: import state from precharge file
    GPIO_PinState s = HAL_GPIO_ReadPin(port, pin);
    return (s == GPIO_PIN_RESET) ? 1u : 0u;
}

HAL_StatusTypeDef Master_CAN_Send_MSC_1(CAN_HandleTypeDef *hcan)
{
    if (hcan == NULL) {
        return HAL_ERROR;
    }

    const AnalogReadings_t *adc = AnalogReadings_Get();
    if (adc == NULL || !adc->data_ready) {
        return HAL_ERROR;
    }

    struct powertrain_t26_master_msc_id_1_t msg = { 0 };
    uint8_t data[POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH];
    int len;

    uint8_t isPecDetected = 1;

    if(anyPecError > 0){
    	isPecDetected = 1;
    }else{
    	isPecDetected = 0;
    }

    msg.mcu_vref = powertrain_t26_master_msc_id_1_mcu_vref_encode(adc->vdda);
    msg.mcu_temperature = powertrain_t26_master_msc_id_1_mcu_temperature_encode(adc->mcu_temp_c);
    msg.ams_current_draw = powertrain_t26_master_msc_id_1_ams_current_draw_encode(adc->ams_master_current);

    msg.master_firmware_version = MASTER_FW_VERSION;

    msg.adbms_pec_error = isPecDetected;

    msg.master_fan_pwm = MASTER_FAN_PWM_DEFAULT;
    msg.master_state = (uint8_t)AMS_State;

    len = powertrain_t26_master_msc_id_1_pack(data, &msg, sizeof(data));
    if (len < 0) {
        return HAL_ERROR;
    }

    return CAN_TX_Add_To_Queue(
        hcan,
        POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID,
        POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH,
        data
    );
}

HAL_StatusTypeDef Master_CAN_Send_MSC_2(CAN_HandleTypeDef *hcan)
{
    if (hcan == NULL) {
        return HAL_ERROR;
    }

    struct powertrain_t26_master_msc_id_2_t msg = { 0 };
    uint8_t data[POWERTRAIN_T26_MASTER_MSC_ID_2_LENGTH];
    int len;

    uint32_t runtime = (uint16_t)getRuntimeSeconds();

    /* Fits in 16 bits only up to 65535 s */
    if (runtime > 65535u) {
    	runtime = 65535u;
    }

    msg.master_runtime = (uint16_t)runtime;

    /* TODO: replace these temporary values with real aggregated values */
    msg.overall_maximum_voltage = powertrain_t26_master_msc_id_2_overall_maximum_voltage_encode(0.0f);
    msg.overall_maximum_temperature = powertrain_t26_master_msc_id_2_overall_maximum_temperature_encode(0.0f);

    len = powertrain_t26_master_msc_id_2_pack(data, &msg, sizeof(data));
    if (len < 0) {
        return HAL_ERROR;
    }

    return CAN_TX_Add_To_Queue(
        hcan,
		POWERTRAIN_T26_MASTER_MSC_ID_2_FRAME_ID,
        POWERTRAIN_T26_MASTER_MSC_ID_2_LENGTH,
        data
    );
}

HAL_StatusTypeDef Master_CAN_SendPrecharge(CAN_HandleTypeDef *hcan)
{
    if (hcan == NULL) {
        return HAL_ERROR;
    }

    struct powertrain_t26_master_pre_charge_id_1_t msg = { 0 };
    uint8_t data[POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_LENGTH];
    int len;

    /* Using commanded GPIO outputs for now.
       Later, if you want, you can switch this to true feedback states.
    */
    msg.precharge_ctc_air_pos_state =
        Master_CAN_GetContactorCommandState(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin);

    msg.precharge_ctc_air_min_state =
        Master_CAN_GetContactorCommandState(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin);

    msg.precharge_ctc_charge_state =
        Master_CAN_GetContactorCommandState(CONTACT_PRE_GPIO_Port, CONTACT_PRE_Pin);

    msg.precharge_ctc_discharge_state =
        Master_CAN_GetContactorCommandState(CONTACT_DSCH_GPIO_Port, CONTACT_DSCH_Pin);

    msg.precharge_state = (uint8_t)Precharge_GetState();

    len = powertrain_t26_master_pre_charge_id_1_pack(data, &msg, sizeof(data));
    if (len < 0) {
        return HAL_ERROR;
    }

    return CAN_TX_Add_To_Queue(
        hcan,
        POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_FRAME_ID,
        POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_LENGTH,
        data
    );
}

HAL_StatusTypeDef Master_CAN_SendAll(CAN_HandleTypeDef *hcan)
{
    HAL_StatusTypeDef st1 = Master_CAN_Send_MSC_1(hcan);
    HAL_StatusTypeDef st3 = Master_CAN_Send_MSC_2(hcan);
    HAL_StatusTypeDef st2 = Master_CAN_SendPrecharge(hcan);

    if (st1 != HAL_OK || st2 != HAL_OK || st3 != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}
