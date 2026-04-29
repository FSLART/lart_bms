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
#include "fault_manager.h"
#include "fan_management.h"

/* Set version */
#define MASTER_FW_VERSION    7

/* temporary */
#ifndef MASTER_FAN_PWM_DEFAULT
#define MASTER_FAN_PWM_DEFAULT    20
#endif

extern uint8_t anyPecError;

//255 means empty
#define FAULT_SLOT_EMPTY    0xFF

/* ─────────────────────────────────────────────────────────────────────────
 *  Fault slot index types
 *  These values must match the VAL_ table in powertrain_t26.dbc for
 *  fault1_index_type and fault2_index_type.
 * ─────────────────────────────────────────────────────────────────────────*/
typedef enum {
	FIDX_SLAVE = 0, /* index_value = slave_idx                        */
	FIDX_CELL = 1, /* index_value = cell_idx                         */
	FIDX_CHANNEL = 2, /* index_value = channel_idx (CAN bus, acq node…) */
	FIDX_CONTACTORS = 3, /* index_value = contactor bitmask                */
	FIDX_SLAVE_AND_CELL = 4, /* index_value = (slave<<4) | (cell & 0xF)        */
	FIDX_THERMISTOR = 5, /* index_value = NTC channel index                */
	FIDX_SLAVE_AND_NTC = 6, /* index_value = (slave<<4) | (NTC & 0xF)         */
	FIDX_NONE = 7, /* no meaningful index for this fault type         */
} FaultSlotIndexType_t;

uint8_t read_contactor_state(GPIO_TypeDef *gpio_port, uint16_t gpio_pin) {

	//TODO: import state from precharge file
	GPIO_PinState pin_state = HAL_GPIO_ReadPin(gpio_port, gpio_pin);

	if (pin_state == GPIO_PIN_RESET) {
		return 1;  //GPIO is LOW
	}

	return 0;  // GPIO is HIGH
}

//count how many faults are currently active
uint8_t count_active_faults(void) {

	uint8_t total_active = 0;

	for (uint8_t fault_index = 0; fault_index < (uint8_t) FAULT_COUNT; fault_index++) {
		FaultCode_t fault_code = (FaultCode_t) fault_index;

		if (FaultManager_IsActive(fault_code)) {
			total_active++;
		}
	}

	return total_active;

}

//Returns the index type for a given fault code
uint8_t get_fault_index_type(FaultCode_t fault_code) {
	switch (fault_code) {

	/* Voltage faults: we want to know exactly which slave and which cell */
	case FAULT_OVERVOLTAGE:
	case FAULT_UNDERVOLTAGE:
	case FAULT_OW_DETECTED_CELL:
		return FIDX_SLAVE_AND_CELL;

		/* Temperature / NTC faults: we want to know which slave and which sensor */
	case FAULT_OVERTEMPERATURE:
	case FAULT_UNDERTEMPERATURE:
	case FAULT_OVERTEMPERATURE_DISCHARGE:
	case FAULT_UNDERTEMPERATURE_CHARGE:
	case FAULT_TEMP_SENSOR_OPEN:
	case FAULT_BALANCING_OVERTEMP:
	case FAULT_OW_DETECTED_RTH:
		return FIDX_SLAVE_AND_NTC;

		/* Slave-layer faults: we just need to know which slave in the chain */
	case FAULT_SLAVE_NOT_DETECTED:
	case FAULT_PEC_ERROR:
	case FAULT_OPEN_WIRE:
	case FAULT_BALANCING_ERROR:
		return FIDX_SLAVE;

		/* CAN or acquisition faults: which bus or which node caused the issue */
	case FAULT_CAN_SEND_ERROR:
	case FAULT_CAN_INIT_ERROR:
	case FAULT_CAN_MAILBOX_FULL:
	case FAULT_CAN_BUS_OFF:
	case FAULT_CAN_RECEIVE_ERROR:
	case FAULT_ACQUISITION_TIMEOUT:
	case FAULT_ACQUISITION_NODE_TIMEOUT:
	case FAULT_ADC_ERROR:
	case FAULT_CURRENT_SENSOR_ERROR:
		return FIDX_CHANNEL;

		/* Contactor fault: a bitmask of which specific contactors are mismatched */
	case FAULT_CONTACTOR_MISMATCH:
		return FIDX_CONTACTORS;

		/* Everything else: no useful secondary index to send */
	default:
		return FIDX_NONE;
	}
}

//returns the index value for a given fault code and its context
uint8_t get_fault_index_value(FaultCode_t fault_code, const FaultContext_t *fault_ctx) {
	uint8_t index_type = get_fault_index_type(fault_code);

	if (index_type == FIDX_SLAVE) {
		return fault_ctx->slave_idx;
	}

	if (index_type == FIDX_SLAVE_AND_CELL) {
		/* Pack slave and cell into one byte: upper nibble = slave, lower nibble = cell */
		uint8_t slave_nibble = fault_ctx->slave_idx & 0x0F;
		uint8_t cell_nibble = fault_ctx->cell_idx & 0x0F;
		return (uint8_t) ((slave_nibble << 4) | cell_nibble);
	}

	if (index_type == FIDX_SLAVE_AND_NTC) {
		/* Pack slave and NTC channel into one byte: upper nibble = slave, lower nibble = NTC */
		uint8_t slave_nibble = fault_ctx->slave_idx & 0x0F;
		uint8_t ntc_nibble = fault_ctx->channel_idx & 0x0F;
		return (uint8_t) ((slave_nibble << 4) | ntc_nibble);
	}

	if (index_type == FIDX_CHANNEL) {
		return fault_ctx->channel_idx;
	}

	if (index_type == FIDX_CONTACTORS) {
		return fault_ctx->contactor_bits;
	}

	/* FIDX_NONE or anything unexpected: no index to send */
	return 0;
}

HAL_StatusTypeDef Master_CAN_Send_MSC_1(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return HAL_ERROR;
	}

	const AnalogReadings_t *analog_readings = AnalogReadings_Get();
	if (analog_readings == NULL || !analog_readings->data_ready) {
		return HAL_ERROR;
	}

	struct powertrain_t26_master_msc_id_1_t msg = { 0 };
	uint8_t data[POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH];
	int packed_length;

	/* Check if any PEC/CRC error has been flagged by the ADBMS driver */
	uint8_t pec_error_detected = 0;
	if (anyPecError > 0) {
		pec_error_detected = 1;
	}

	/* Fill in all the signal fields */
	msg.mcu_vref = powertrain_t26_master_msc_id_1_mcu_vref_encode(analog_readings->vdda);
	msg.mcu_temperature = powertrain_t26_master_msc_id_1_mcu_temperature_encode(analog_readings->mcu_temp_c);
	msg.ams_current_draw = powertrain_t26_master_msc_id_1_ams_current_draw_encode(analog_readings->ams_master_current);
	msg.master_firmware_version = MASTER_FW_VERSION;
	msg.adbms_pec_error = pec_error_detected;
	msg.master_fan_pwm = Get_Fan_PWM();
	msg.master_state = (uint8_t) AMS_State;

	packed_length = powertrain_t26_master_msc_id_1_pack(data, &msg, sizeof(data));
	if (packed_length < 0) {
		return HAL_ERROR;
	}

	/* Byte 7 is reserved for the total active fault count.
	 This is placed manually because the DBC signal does not cover it yet. */
	data[7] = count_active_faults();

	return CAN_TX_Add_To_Queue(hcan,
	POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID,
	POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH, data);
}

HAL_StatusTypeDef Master_CAN_Send_MSC_2(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return HAL_ERROR;
	}

	struct powertrain_t26_master_msc_id_2_t msg = { 0 };
	uint8_t data[POWERTRAIN_T26_MASTER_MSC_ID_2_LENGTH];
	int packed_length;

	/* Clamp the uptime to 16-bit range (max ~18 hours before it wraps) */
	uint32_t runtime_seconds = getRuntimeSeconds();
	if (runtime_seconds > 65535) {
		runtime_seconds = 65535;
	}
	msg.master_runtime = (uint16_t) runtime_seconds;

	packed_length = powertrain_t26_master_msc_id_2_pack(data, &msg, sizeof(data));
	if (packed_length < 0) {
		return HAL_ERROR;
	}

	/* Start both fault slots as empty in case there are fewer than 2 active faults */
	data[2] = FAULT_SLOT_EMPTY;
	data[3] = FIDX_NONE;
	data[4] = 0;
	data[5] = FAULT_SLOT_EMPTY;
	data[6] = FIDX_NONE;
	data[7] = 0;

	/* Walk through every fault code and fill the first two active ones */
	uint8_t faults_added = 0;

	for (uint8_t fault_index = 0; fault_index < (uint8_t) FAULT_COUNT; fault_index++) {
		FaultCode_t fault_code = (FaultCode_t) fault_index;

		if (!FaultManager_IsActive(fault_code)) {
			continue; /* this fault is not active, skip it */
		}

		const FaultContext_t *fault_ctx = FaultManager_GetContext(fault_code);
		uint8_t index_type = get_fault_index_type(fault_code);
		uint8_t index_value = get_fault_index_value(fault_code, fault_ctx);

		if (faults_added == 0) {
			/* Fill fault slot 1 */
			data[2] = (uint8_t) fault_code;
			data[3] = index_type;
			data[4] = index_value;
		} else {
			/* Fill fault slot 2 */
			data[5] = (uint8_t) fault_code;
			data[6] = index_type;
			data[7] = index_value;
		}

		faults_added++;

		if (faults_added >= 2) {
			break; /* both slots are full, no point continuing the loop */
		}
	}

	return CAN_TX_Add_To_Queue(hcan,
	POWERTRAIN_T26_MASTER_MSC_ID_2_FRAME_ID,
	POWERTRAIN_T26_MASTER_MSC_ID_2_LENGTH, data);
}

HAL_StatusTypeDef Master_CAN_SendPrecharge(CAN_HandleTypeDef *hcan)
{
    if (hcan == NULL) {
        return HAL_ERROR;
    }

    struct powertrain_t26_master_pre_charge_id_1_t msg = { 0 };
    uint8_t data[POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_LENGTH];
    int packed_length;

    msg.precharge_ctc_air_pos_state   = read_contactor_state(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin);
    msg.precharge_ctc_air_min_state   = read_contactor_state(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin);
    msg.precharge_ctc_charge_state    = read_contactor_state(CONTACT_PRE_GPIO_Port,          CONTACT_PRE_Pin);
    msg.precharge_ctc_discharge_state = read_contactor_state(CONTACT_DSCH_GPIO_Port,         CONTACT_DSCH_Pin);
    msg.precharge_state               = (uint8_t)Precharge_GetState();

    packed_length = powertrain_t26_master_pre_charge_id_1_pack(data, &msg, sizeof(data));
    if (packed_length < 0) {
        return HAL_ERROR;
    }

    return CAN_TX_Add_To_Queue(hcan,
        POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_FRAME_ID,
        POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_LENGTH,
        data);
}

HAL_StatusTypeDef Master_CAN_SendAll(CAN_HandleTypeDef *hcan)
{
    HAL_StatusTypeDef result_msc1      = Master_CAN_Send_MSC_1(hcan);
    HAL_StatusTypeDef result_msc2      = Master_CAN_Send_MSC_2(hcan);
    HAL_StatusTypeDef result_precharge = Master_CAN_SendPrecharge(hcan);

    if (result_msc1 != HAL_OK || result_msc2 != HAL_OK || result_precharge != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}
