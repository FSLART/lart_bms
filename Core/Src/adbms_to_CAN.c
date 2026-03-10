/*
 * adbms_to_CAN.c
 *
 *  Created on: Mar 9, 2026
 *      Author: jpser
 */

#include "adbms_to_CAN.h"
#include "adBms_Application.h"
#include <string.h>
#include "can.h"
#include "dbc/ams.h"


HAL_StatusTypeDef Slaves_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData) {
	if (dataLength > 8U) {
		//TODO: Add error handling i guesss
		//return HAL_ERROR;
		return HAL_OK;
	}

	// Just enqueue, do NOT send directly
	return CAN_TX_Add_To_Queue(hcan, canID, (uint8_t) dataLength, TxData);
}

uint16_t adbms_cellCode_to_mV_u16(int16_t code) {

	float mv = 1500.0f + ((float)code * 0.15f);

    if (mv < 0.0f)
        mv = 0.0f;
    if (mv > 65535.0f)
        mv = 65535.0f;

    return (uint16_t)(mv + 0.5f);   // 100 uV/LSB -> 10 LSB = 1 mV
}


HAL_StatusTypeDef ADBMS_CAN_SendAll(CAN_HandleTypeDef *hcan)
{
    for (uint8_t m = 0; m < TOTAL_IC && m < 12; m++)
    {
        if (ADBMS_CAN_SendVoltages_Module(hcan, m) != HAL_OK)
            return HAL_ERROR;

        /*if (ADBMS_CAN_SendTemperatures_Module(hcan, m) != HAL_OK)
            return HAL_ERROR;*/
    }

    return HAL_OK;
}

HAL_StatusTypeDef ADBMS_CAN_SendVoltages_Module(CAN_HandleTypeDef *hcan, uint8_t module) {

	if (module >= TOTAL_IC || module >= 12)
		return HAL_ERROR;

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;

	const cell_asic *ic = &IC[module];

	switch (slave) {
	case 1: {
		struct ams_slave_01_voltage_id_1_t v1;
		v1.cell_voltage_1 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[0]);
		v1.cell_voltage_2 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[1]);
		v1.cell_voltage_3 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[2]);
		v1.cell_voltage_4 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[3]);

		len = ams_slave_01_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_voltage_id_2_t v2;
		v2.cell_voltage_5 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[4]);
		v2.cell_voltage_6 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[5]);
		v2.cell_voltage_7 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[6]);
		v2.cell_voltage_8 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[7]);

		len = ams_slave_01_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_voltage_id_3_t v3;
		v3.cell_voltage_9 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[8]);
		v3.cell_voltage_10 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[9]);
		v3.cell_voltage_11 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[10]);
		v3.cell_voltage_12 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[11]);

		len = ams_slave_01_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 2: {
		struct ams_slave_02_voltage_id_1_t v1;
		v1.cell_voltage_13 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[0]);
		v1.cell_voltage_14 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[1]);
		v1.cell_voltage_15 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[2]);
		v1.cell_voltage_16 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[3]);

		len = ams_slave_02_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_voltage_id_2_t v2;
		v2.cell_voltage_17 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[4]);
		v2.cell_voltage_18 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[5]);
		v2.cell_voltage_19 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[6]);
		v2.cell_voltage_20 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[7]);

		len = ams_slave_02_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_voltage_id_3_t v3;
		v3.cell_voltage_21 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[8]);
		v3.cell_voltage_22 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[9]);
		v3.cell_voltage_23 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[10]);
		v3.cell_voltage_24 = adbms_cellCode_to_mV_u16(ic->cell.c_codes[11]);

		len = ams_slave_02_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	default:
		return HAL_OK;
	}

	return HAL_OK;
}

