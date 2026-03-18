/*
 * adbms_to_CAN.c
 *
 *  Created on: Mar 9, 2026
 *      Author: jpser
 */

#include "adbms_to_CAN.h"
#include "adBms_Application.h"

#include "can.h"
#include "dbc/powertrain_t26.h"

#include <math.h>
#include <string.h>

//Cache for the delta, since it is in another message grouped with other adbms stuff
uint16_t s_module_voltage_delta[12];

HAL_StatusTypeDef Slaves_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData) {
	if (dataLength > 8U) {
		//TODO: Add error handling i guesss
		//return HAL_ERROR;
		return HAL_OK;
	}

	// Just enqueue, do NOT send directly
	return CAN_TX_Add_To_Queue(hcan, canID, (uint8_t) dataLength, TxData);
}

HAL_StatusTypeDef ADBMS_CAN_SendAll(CAN_HandleTypeDef *hcan, AMSStates_t ams_current_state) {
	for (uint8_t slave = 0; slave < TOTAL_IC && slave < 12; slave++) {

		if (ADBMS_CAN_SendVoltages_Module(hcan, slave, ams_current_state) != HAL_OK)
			return HAL_ERROR;

		if (ADBMS_CAN_SendTemperatures_Module(hcan, slave) != HAL_OK)
			return HAL_ERROR;

		if (ADBMS_CAN_SendMSC_Module(hcan, slave) != HAL_OK)
			return HAL_ERROR;
	}

	// Está ca fora pq tem que ir pelos tdos os slaves para realemnte encontrar o maximo e o minimo antes de enviar a mensagem CAN
	if (ADBMS_CAN_Send_Master_MSC_3(hcan) != HAL_OK)
		return HAL_ERROR;

	return HAL_OK;
}

HAL_StatusTypeDef ADBMS_CAN_Send_Master_MSC_3(CAN_HandleTypeDef *hcan) {
	uint8_t data[8];
	int len;

	uint16_t overall_vmax = 0U;
	uint16_t overall_vmin = 0xFFFFU;
	uint16_t overall_tmax = 0U;
	uint16_t overall_tmin = 0xFFFFU;

	for (uint8_t module = 0; module < TOTAL_IC && module < 12; module++) {
		const cell_asic *ic = &IC[module];

		for (uint8_t i = 0; i < 12; i++) {
			uint16_t v = data_to_volts(ic->cell.c_codes[i], ADBMS_CELL);

			if (v > overall_vmax)
				overall_vmax = v;
			if (v < overall_vmin)
				overall_vmin = v;
		}

		for (uint8_t i = 0; i < 6; i++) {
			uint16_t t = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[i]);

			if (t > overall_tmax)
				overall_tmax = t;
			if (t < overall_tmin)
				overall_tmin = t;
		}
	}

	struct powertrain_t26_master_msc_id_3_t m3 = { 0 };
	m3.overall_maximum_voltage = overall_vmax;
	m3.overall_maximum_temperature = overall_tmax;
	m3.overall_minimum_voltage = overall_vmin;
	m3.overall_minimum_temperature = overall_tmin;

	len = powertrain_t26_master_msc_id_3_pack(data, &m3, sizeof(data));
	if (len < 0)
		return HAL_ERROR;

	if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_MASTER_MSC_ID_3_FRAME_ID, len, data) != HAL_OK)
		return HAL_ERROR;

	return HAL_OK;
}

HAL_StatusTypeDef ADBMS_CAN_SendMSC_Module(CAN_HandleTypeDef *hcan, uint8_t module) {
	if (module >= TOTAL_IC || module >= 12) {
		return HAL_ERROR;
	}

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;
	const cell_asic *ic = &IC[module];

	// IC voltage
	// do meu antigo código do bms
	int16_t vpv_raw = ic->aux.a_codes[11];
	float vpv_v = (vpv_raw * 0.00015f + 1.5f) * 25.0f;   // volts
	uint16_t ic_voltage = (uint16_t) (vpv_v * 1000.0f + 0.5f); // mV

	// IC internal temperature
	float itmp_voltage = ((ic->stata.itmp + 10000) * 0.000150f);
	float ic_temp_c = (itmp_voltage / 0.0075f) - 273.0f;
	if (ic_temp_c < 0.0f)
		ic_temp_c = 0.0f;
	uint16_t ic_temp = (uint16_t) (ic_temp_c + 0.5f);

	// Existing open-wire / diag fault flag
	uint8_t open_wire = (ic->statc.cs_flt != 0U || ic->statc.vde || ic->statc.vdel) ? 1U : 0U;

	uint16_t vdelta = s_module_voltage_delta[module];

	//check under and over voltage registers
	uint8_t module_overvoltage = 0U;
	uint8_t module_undervoltage = 0U;
	uint8_t module_under_over_identifier = 0U;

	// Check cells first: identifiers 1..12
	for (uint8_t i = 0; i < 12; i++) {
		if (ic->statd.c_ov[i]) {
			module_overvoltage = 1U;
			module_under_over_identifier = (uint8_t) (i + 1U);
			break;
		}
	}

	for (uint8_t i = 0; i < 12; i++) {
		if (ic->statd.c_uv[i]) {
			module_undervoltage = 1U;

			// only set identifier if not already set by OV
			if (module_under_over_identifier == 0U) {
				module_under_over_identifier = (uint8_t) (i + 1U);
			}
			break;
		}
	}

	switch (slave) {
	case 1: {
		struct powertrain_t26_slave_01_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;
		m2.module_overvoltage = module_overvoltage;
		m2.module_undervoltage = module_undervoltage;
		m2.module_under_over_identifier = module_under_over_identifier;

		len = powertrain_t26_slave_01_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 2: {
		struct powertrain_t26_slave_02_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;
		m2.module_overvoltage = module_overvoltage;
		m2.module_undervoltage = module_undervoltage;
		m2.module_under_over_identifier = module_under_over_identifier;

		len = powertrain_t26_slave_02_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 3: {
		struct powertrain_t26_slave_03_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_03_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 4: {
		struct powertrain_t26_slave_04_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_04_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 5: {
		struct powertrain_t26_slave_05_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_05_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 6: {
		struct powertrain_t26_slave_06_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_06_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 7: {
		struct powertrain_t26_slave_07_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_07_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 8: {
		struct powertrain_t26_slave_08_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_08_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 9: {
		struct powertrain_t26_slave_09_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_09_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 10: {
		struct powertrain_t26_slave_10_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_10_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 11: {
		struct powertrain_t26_slave_11_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_11_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 12: {
		struct powertrain_t26_slave_12_msc_id_2_t m2 = { 0 };
		m2.module_voltage_delta = vdelta;
		m2.module_ic_voltage = ic_voltage;
		m2.module_open_wire = open_wire;
		m2.module_ic_temperature = ic_temp;

		len = powertrain_t26_slave_12_msc_id_2_pack(data, &m2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_MSC_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	default:
		return HAL_ERROR;
	}

	return HAL_OK;
}

HAL_StatusTypeDef ADBMS_CAN_SendTemperatures_Module(CAN_HandleTypeDef *hcan, uint8_t module) {
	if (module >= TOTAL_IC || module >= 12) {
		return HAL_ERROR;
	}

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;
	const cell_asic *ic = &IC[module];

	/* RAUX has 6 channels: ic->raux.ra_codes[0..5]
	 * getTemperatureCAN() already returns scaled raw value for DBC factor
	 */
	uint16_t t1 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[0]);
	uint16_t t2 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[1]);
	uint16_t t3 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[2]);
	uint16_t t4 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[3]);
	uint16_t t5 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[4]);
	uint16_t t6 = (uint16_t) getTemperatureCAN(ic->raux.ra_codes[5]);

	uint16_t temp_max = t1;
	uint16_t temp_min = t1;

	if (t2 > temp_max)
		temp_max = t2;
	if (t3 > temp_max)
		temp_max = t3;
	if (t4 > temp_max)
		temp_max = t4;
	if (t5 > temp_max)
		temp_max = t5;
	if (t6 > temp_max)
		temp_max = t6;

	if (t2 < temp_min)
		temp_min = t2;
	if (t3 < temp_min)
		temp_min = t3;
	if (t4 < temp_min)
		temp_min = t4;
	if (t5 < temp_min)
		temp_min = t5;
	if (t6 < temp_min)
		temp_min = t6;

	uint16_t temp_delta = temp_max - temp_min;

	switch (slave) {
	case 1: {
		struct powertrain_t26_slave_01_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_01_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_01_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_01_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 2: {
		struct powertrain_t26_slave_02_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_02_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_02_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_02_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 3: {
		struct powertrain_t26_slave_03_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_03_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_03_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_03_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 4: {
		struct powertrain_t26_slave_04_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_04_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_04_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_04_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 5: {
		struct powertrain_t26_slave_05_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_05_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_05_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_05_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 6: {
		struct powertrain_t26_slave_06_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_06_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_06_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_06_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 7: {
		struct powertrain_t26_slave_07_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_07_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_07_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_07_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 8: {
		struct powertrain_t26_slave_08_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_08_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_08_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_08_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 9: {
		struct powertrain_t26_slave_09_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_09_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_09_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_09_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 10: {
		struct powertrain_t26_slave_10_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_10_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_10_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_10_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 11: {
		struct powertrain_t26_slave_11_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_11_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_11_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_11_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 12: {
		struct powertrain_t26_slave_12_temperature_id_1_t temp1 = { 0 };
		temp1.temperature_value_1 = t1;
		temp1.temperature_value_2 = t2;
		temp1.temperature_value_3 = t3;
		temp1.temperature_value_4 = t4;

		len = powertrain_t26_slave_12_temperature_id_1_pack(data, &temp1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_12_temperature_id_2_t temp2_msg = { 0 };
		temp2_msg.temperature_value_5 = t5;
		temp2_msg.temperature_value_6 = t6;
		temp2_msg.temperature_maximum = temp_max;
		temp2_msg.temperature_delta = temp_delta;

		len = powertrain_t26_slave_12_temperature_id_2_pack(data, &temp2_msg, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	default:
		return HAL_ERROR;
	}

	return HAL_OK;
}

HAL_StatusTypeDef ADBMS_CAN_SendVoltages_Module(CAN_HandleTypeDef *hcan, uint8_t module, AMSStates_t AMS_Current_State) {
	if (module >= TOTAL_IC || module >= 12)
		return HAL_ERROR;

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;
	const cell_asic *ic = &IC[module];

	//shit for math and conversions
	uint16_t cell_voltages[12];
	uint32_t sum = 0;

	for (uint8_t i = 0; i < 12; i++) {
		if (AMS_Current_State == BALANCING) {
			cell_voltages[i] = data_to_volts(ic->scell.sc_codes[i], ADBMS_CELL);
		} else {
			cell_voltages[i] = data_to_volts(ic->cell.c_codes[i], ADBMS_CELL);
		}

		sum += cell_voltages[i];
	}

	uint16_t vmin = cell_voltages[0];
	uint16_t vmax = cell_voltages[0];

	for (uint8_t i = 1; i < 12; i++) {
		if (cell_voltages[i] < vmin)
			vmin = cell_voltages[i];
		if (cell_voltages[i] > vmax)
			vmax = cell_voltages[i];
	}

	uint16_t vavg = (uint16_t) (sum / 12U);
	uint16_t vdelta = (uint16_t) (vmax - vmin);
	s_module_voltage_delta[module] = vdelta;

	switch (slave) {
	case 1: {
		struct powertrain_t26_slave_01_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_01_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_01_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_01_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_01_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_01_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_01_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_01_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_01_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 2: {
		struct powertrain_t26_slave_02_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_02_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_02_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_02_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_02_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_02_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_02_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_02_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_02_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 3: {
		struct powertrain_t26_slave_03_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_03_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_03_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_03_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_03_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_03_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_03_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_03_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_03_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 4: {
		struct powertrain_t26_slave_04_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_04_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_04_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_04_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_04_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_04_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_04_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_04_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_04_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 5: {
		struct powertrain_t26_slave_05_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_05_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_05_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_05_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_05_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_05_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_05_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_05_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_05_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 6: {
		struct powertrain_t26_slave_06_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_06_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_06_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_06_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_06_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_06_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_06_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_06_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_06_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 7: {
		struct powertrain_t26_slave_07_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_07_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_07_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_07_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_07_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_07_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_07_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_07_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_07_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 8: {
		struct powertrain_t26_slave_08_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_08_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_08_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_08_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_08_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_08_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_08_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_08_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_08_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 9: {
		struct powertrain_t26_slave_09_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_09_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_09_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_09_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_09_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_09_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_09_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_09_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_09_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 10: {
		struct powertrain_t26_slave_10_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_10_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_10_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_10_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_10_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_10_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_10_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_10_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_10_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 11: {
		struct powertrain_t26_slave_11_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_11_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_11_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_11_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_11_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_11_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_11_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_11_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_11_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	case 12: {
		struct powertrain_t26_slave_12_voltage_id_1_t v1 = { 0 };
		v1.cell_voltage_1 = cell_voltages[0];
		v1.cell_voltage_2 = cell_voltages[1];
		v1.cell_voltage_3 = cell_voltages[2];
		v1.cell_voltage_4 = cell_voltages[3];

		len = powertrain_t26_slave_12_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_12_voltage_id_2_t v2 = { 0 };
		v2.cell_voltage_5 = cell_voltages[4];
		v2.cell_voltage_6 = cell_voltages[5];
		v2.cell_voltage_7 = cell_voltages[6];
		v2.cell_voltage_8 = cell_voltages[7];

		len = powertrain_t26_slave_12_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_12_voltage_id_3_t v3 = { 0 };
		v3.cell_voltage_9 = cell_voltages[8];
		v3.cell_voltage_10 = cell_voltages[9];
		v3.cell_voltage_11 = cell_voltages[10];
		v3.cell_voltage_12 = cell_voltages[11];

		len = powertrain_t26_slave_12_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct powertrain_t26_slave_12_msc_id_1_t m1 = { 0 };
		m1.module_voltage_sum = (uint16_t) sum;
		m1.module_voltage_avg = vavg;
		m1.module_voltage_min = vmin;
		m1.module_voltage_max = vmax;

		len = powertrain_t26_slave_12_msc_id_1_pack(data, &m1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, POWERTRAIN_T26_SLAVE_12_MSC_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		break;
	}

	default:
		return HAL_ERROR;
	}

	return HAL_OK;
}

// as duas formas de conversoes de adc, dependendo do tipo
uint16_t data_to_volts(int16_t code, adbms_data_type_t type) {
	float volts = 0.0f;

	switch (type) {
	case ADBMS_CELL:
		/* Cell code -> volts
		 * 150 uV / LSB with 1.5 V offset
		 */
		volts = 1.5f + ((float) code * 0.00015f);
		break;

	case ADBMS_GPIO:
		/* GPIO / RAUX code -> volts
		 * (code + 10000) * 150 uV
		 */
		volts = ((float) code + 10000.0f) * 0.00015f;
		break;

	default:
		return 0;
	}

	if (volts < 0.0f)
		volts = 0.0f;
	if (volts > 65.535f)
		volts = 65.535f;

	/* return in mV */
	return (uint16_t) (volts * 1000.0f + 0.5f);
}

//Thermistor: Amphenol NKA502C1*1C
float getTemperatureCAN(int16_t code) {
	const float VREF2 = 3.0f;
	const float R1 = 5000.0f;
	const float R0 = 5000.0f;
	const float BETA = 3977.0f;
	const float T0_K = 298.15f;

	float voltage = (float) data_to_volts(code, ADBMS_GPIO) / 1000.0f;

	if (voltage <= 0.0f)
		return -40.0f;
	if (voltage >= VREF2 - 0.001f)
		return 150.0f;

	float Rt = R1 * voltage / (VREF2 - voltage);
	float tempK = 1.0f / ((1.0f / T0_K) + (1.0f / BETA) * logf(Rt / R0));
	float tempC = tempK - 273.15f;

	if (tempC < 0.0f)
		tempC = 0.0f;
	if (tempC > 150.0f)
		tempC = 150.0f;

	return (tempC * 100.0f + 0.5f);
}
