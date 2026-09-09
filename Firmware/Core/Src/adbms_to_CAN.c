/*
 * adbms_to_CAN.c
 *
 *  Created on: Mar 9, 2026
 *      Author: jpser
 */

#include "adbms_to_CAN.h"
#include "adBms_Application.h"
#include "fan_management.h"
#include "fault_manager.h"
#include "ams_error.h"

#include "can.h"
#include "dbc/powertrain_t26.h"

#include <math.h>
#include <string.h>

#define SAFETY_CELL_OV_V   4.20
#define SAFETY_CELL_UV_V   2.80
/* Abaixo disto nao e uma celula em subtensao, e um fio de sense partido: a
 * P45B nunca desce dos 2.5V em servico, e um tap aberto colapsa a leitura
 * para perto de 0. Deteta OW muito mais depressa que as fases dedicadas do
 * ADBMS (7 fases x 50ms = 350ms+), porque a tensao e lida em todos os ciclos */
#define SAFETY_CELL_OW_V   2.30
#define SAFETY_CELL_OT_C   60.0

//Cache for the delta, since it is in another message grouped with other adbms stuff
int s_module_voltage_delta[12];

/* Pack-level overalls, cached by ADBMS_CAN_Send_Master_MSC_3 for live debug */
uint16_t g_pack_vmax_mV = 0;
uint16_t g_pack_vmin_mV = 0;
int16_t  g_pack_tmax_cC = 0;
int16_t  g_pack_tmin_cC = 0;
uint32_t g_pack_voltage_sum_mV = 0;

void BMS_SafetyCheck(void) {

	/* Warmup de arranque: os registos dos ADBMS ainda estão no valor de
	 * reset (0x8000) até às primeiras leituras válidas de toda a chain.
	 * Não avaliar OV/UV/OT nos primeiros ciclos para não latchar faults
	 * fantasma no boot */
	static uint8_t boot_warmup_reads = 0;

	if (boot_warmup_reads < 10) {
		boot_warmup_reads++;
		return;
	}

	// quantos devices da chain estao com PEC error nesta leitura
	uint8_t pec_error_devices = 0;

	// OV/UV/OT nesta leitura (clearable: recupera quando a celula/NTC
	// volta a ficar dentro dos limites - o latch PCB externo e' que
	// mantem o veiculo em erro ate ao reset manual)
	uint8_t ovuvot_fault_now = 0;

	for (int module = 0; module < slaves_found && module < 12; module++) {

		const cell_asic *ic = &SLAVE[module];

		// device com PEC error (comms isoSPI ma) -> contar para a protecao
		if (ic->cccrc.cell_pec != 0) {
			pec_error_devices++;
		}

		// 12 células por módulo
		// PEC mau = codes sao lixo do parse do vendor -> nao julgar tensoes
		if (ic->cccrc.cell_pec == 0) {

			for (int cell = 0; cell < 12; cell++) {

				float cell_v = 1.5f + (ic->cell.c_codes[cell] * 0.00015f);

				/* modulo do valor: registo por escrever (0x8000) da -3.4V,
				 * que em valor absoluto vira 3.4V e nao dispara UV fantasma */
				cell_v = fabsf(cell_v);

				/* RAISE_ERROR continua a correr sempre: o fault fica visivel
				 * no fault manager, no CAN e no dashboard. So a ligacao a
				 * linha AMS_ERROR e' que segue o switch de ams_error.h */
				if (cell_v > SAFETY_CELL_OV_V) {
					RAISE_ERROR(FAULT_OVERVOLTAGE, .slave_idx = module + 1, .cell_idx = cell + 1, .measured_value = cell_v, .threshold_value = SAFETY_CELL_OV_V);
					ovuvot_fault_now |= AMS_ERR_SRC_OVERVOLTAGE;
				}

				// colapsada = fio de sense partido, nao subtensao real
				if (cell_v < SAFETY_CELL_OW_V) {
					RAISE_ERROR(FAULT_OW_DETECTED_CELL, .slave_idx = module + 1, .cell_idx = cell + 1, .measured_value = cell_v, .threshold_value = SAFETY_CELL_OW_V);
					ovuvot_fault_now |= AMS_ERR_SRC_OPENWIRE;

				} else if (cell_v < SAFETY_CELL_UV_V) {
					RAISE_ERROR(FAULT_UNDERVOLTAGE, .slave_idx = module + 1, .cell_idx = cell + 1, .measured_value = cell_v, .threshold_value = SAFETY_CELL_UV_V);
					ovuvot_fault_now |= AMS_ERR_SRC_UNDERVOLTAGE;
				}
			}
		}

		// 6 NTC por módulo
		// mesmo racional: raux com PEC mau nao serve para julgar OT
		if (ic->cccrc.raux_pec == 0) {

			for (int ntc = 0; ntc < 6; ntc++) {

				// NTC desativado por hardware: nao avaliar OT
				if (NTC_IsBypassed((uint8_t) (module + 1), (uint8_t) (ntc + 1))) {
					continue;
				}

				float cell_t = getTemperatureCAN(ic->raux.ra_codes[ntc]);

				// modulo do valor, mesmo racional das tensoes: leitura
				// negativa e lixo, nao temperatura real
				cell_t = fabsf(cell_t);

				// NTC desligado lê ~2 ou 150 -> não avaliar OT.
				// Open wire de NTC é reportado só como OW_DETECTED_RTH (check dedicado
				// no adBms_Application), para não duplicar faults do mesmo problema
				if (cell_t <= 2.0f || cell_t >= 149.0f) {
					continue;
				}

				if (cell_t >= SAFETY_CELL_OT_C) {
					RAISE_ERROR(FAULT_OVERTEMPERATURE, .slave_idx = module + 1, .channel_idx = ntc + 1, .measured_value = cell_t, .threshold_value = SAFETY_CELL_OT_C);
					ovuvot_fault_now |= AMS_ERR_SRC_OVERTEMPERATURE;
				}
			}
		}
	}

	/* OV/UV/OT (clearable): recupera sozinho quando a celula/NTC volta a
	 * ficar dentro dos limites. Clear so na transicao fault->ok (edge),
	 * para nao apagar a cada tick um erro clearable posto por outra
	 * fonte. O latch PCB externo e' quem mantem o veiculo em erro ate
	 * ao reset manual, o firmware so reporta a condicao em tempo real */
	static uint8_t ovuvot_active = 0;

	if (ovuvot_fault_now != 0) {
		AMS_Error_Trigger();
	} else if (ovuvot_active != 0) {
		AMS_Error_Clear();
	}

	ovuvot_active = ovuvot_fault_now;

	/* Mais de 1/4 dos devices em PEC error = comms da chain a degradar,
	 * medicoes nao confiaveis -> AMS_ERROR clearable (nao permanente:
	 * recupera sozinho quando a chain voltar). Clear so na transicao
	 * flood->ok (edge), para nao apagar a cada tick um erro clearable
	 * posto por outra fonte */
	static uint8_t pec_flood = 0;

	/* com AMS_ERR_SRC_PEC_FLOOD=0 fica sempre 0: nem o Trigger nem o Clear
	 * de borda chegam a correr, para nao apagarem um AMS_ERROR de outra fonte */
	uint8_t pec_flood_now = (AMS_ERR_SRC_PEC_FLOOD && (pec_error_devices > (slaves_found / 4))) ? 1 : 0;

	if (pec_flood_now != 0) {
		AMS_Error_Trigger();
	} else if (pec_flood != 0) {
		AMS_Error_Clear();
	}

	pec_flood = pec_flood_now;
}

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
	for (uint8_t slave = 0; slave < slaves_found && slave < 12; slave++) {

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
	uint32_t pack_voltage_sum = 0U;

	for (uint8_t module = 0; module < slaves_found && module < 12; module++) {
		const cell_asic *ic = &SLAVE[module];   // atualizar para a versão segura

		for (uint8_t i = 0; i < 12; i++) {
			int v = data_to_volts(ic->cell.c_codes[i], ADBMS_CELL);

			pack_voltage_sum += (uint32_t) v;

			if (v > overall_vmax)
				overall_vmax = v;
			if (v < overall_vmin)
				overall_vmin = v;
		}

		for (uint8_t i = 0; i < 6; i++) {

			// NTC desativado por hardware: fora do min/max do pack
			if (NTC_IsBypassed((uint8_t) (module + 1), (uint8_t) (i + 1)))
				continue;

			int t = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[i]));

			/* NTC aberto (1.99ºC) ou curto/erro (>=149ºC): não deixar
			 * inquinar o min/max do pack */
			if (t <= 200 || t >= 14900)
				continue;

			if (t > overall_tmax)
				overall_tmax = t;
			if (t < overall_tmin)
				overall_tmin = t;
		}
	}

	/* Sem slaves ou sem leituras válidas: min ficaria no valor de init
	 * (0xFFFF = 655.35 no CAN) -> enviar 0 em vez de lixo */
	if (overall_vmin == 0xFFFFU)
		overall_vmin = 0U;
	if (overall_tmin == 0xFFFFU)
		overall_tmin = 0U;

	/* Cache pack-level overalls for the live debug snapshot */
	g_pack_vmax_mV = overall_vmax;
	g_pack_vmin_mV = overall_vmin;
	g_pack_tmax_cC = (int16_t) overall_tmax;
	g_pack_tmin_cC = (int16_t) overall_tmin;
	g_pack_voltage_sum_mV = pack_voltage_sum;

	Update_Fan_Temperature(overall_tmax);

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
	if (module >= slaves_found || module >= 12) {
		return HAL_ERROR;
	}

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;
	const cell_asic *ic = &SLAVE[module];   // atualizar para a versão segura

	// IC voltage
	// do meu antigo código do bms
	int16_t vpv_raw = ic->aux.a_codes[11];
	float vpv_v = (vpv_raw * 0.00015f + 1.5f) * 25.0f;   // volts
	int ic_voltage = (int) (vpv_v * 1000.0f + 0.5f); // mV

	// IC internal temperature
	float itmp_voltage = ((ic->stata.itmp + 10000) * 0.000150f);
	float ic_temp_c = (itmp_voltage / 0.0075f) - 273.0f;
	if (ic_temp_c < 0.0f)
		ic_temp_c = 0.0f;
	int ic_temp = (int) (ic_temp_c + 0.5f - 100.0f);

	// Open wire identifier byte:
	uint8_t open_wire = 0;

	for (uint8_t cell = 0; cell < CELL; cell++) {
		if (ic->diag_result.cell_ow[cell]) {
			open_wire = (uint8_t)(cell + 1);
			break;
		}
	}

	if (open_wire == 0) {
		for (uint8_t gpio = 0; gpio < AUX; gpio++) {
			if (ic->diag_result.aux_ow[gpio]) {
				open_wire = (uint8_t)(gpio + 13);
				break;
			}
		}
	}

	int vdelta = s_module_voltage_delta[module];

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
	if (module >= slaves_found || module >= 12) {
		return HAL_ERROR;
	}

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;
	const cell_asic *ic = &SLAVE[module];   // atualizar para a versão segura

	/* RAUX has 6 channels: ic->raux.ra_codes[0..5]
	 * getTemperatureCAN() doesnt account for DBC factor
	 * NTC desativado herda o valor do anterior (NTC_ResolveSource), em vez
	 * de mandar 0 ou o lixo 2.0 do raux
	 */
	int t1 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 1) - 1]));
	int t2 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 2) - 1]));
	int t3 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 3) - 1]));
	int t4 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 4) - 1]));
	int t5 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 5) - 1]));
	int t6 = (int) (100 * getTemperatureCAN(ic->raux.ra_codes[NTC_ResolveSource(slave, 6) - 1]));

	int temps[6] = { t1, t2, t3, t4, t5, t6 };

	int temp_max = 0;
	int temp_min = 0;
	uint8_t temp_valid = 0;

	for (uint8_t i = 0; i < 6; i++) {
		/* NTC aberto lê 1.99ºC (199 cC) e em curto/erro >= 149ºC:
		 * descartar do max/min como no BMS_SafetyCheck */
		if (temps[i] <= 200 || temps[i] >= 14900)
			continue;

		if (!temp_valid) {
			temp_max = temps[i];
			temp_min = temps[i];
			temp_valid = 1;
			continue;
		}

		if (temps[i] > temp_max)
			temp_max = temps[i];
		if (temps[i] < temp_min)
			temp_min = temps[i];
	}

	int temp_delta = temp_max - temp_min;

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
	if (module >= slaves_found || module >= 12)
		return HAL_ERROR;

	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;

	const cell_asic *ic = &SLAVE[module];   // atualizar para a versão segura

	//shit for math and conversions
	int cell_voltages[12];
	uint32_t sum = 0;

	for (uint8_t i = 0; i < 12; i++) {
		if (AMS_Current_State == BALANCING) {
			cell_voltages[i] = data_to_volts(ic->cell.c_codes[i], ADBMS_CELL);
		} else {
			cell_voltages[i] = data_to_volts(ic->cell.c_codes[i], ADBMS_CELL);
		}

		sum += cell_voltages[i];
	}

	int vmin = cell_voltages[0];
	int vmax = cell_voltages[0];

	for (uint8_t i = 1; i < 12; i++) {
		if (cell_voltages[i] < vmin)
			vmin = cell_voltages[i];
		if (cell_voltages[i] > vmax)
			vmax = cell_voltages[i];
	}

	int vavg = (int) (sum / 12U);
	int vdelta = (int) (vmax - vmin);
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
		m1.module_voltage_sum = (int) sum;
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
float data_to_volts(int16_t code, adbms_data_type_t type) {
	float volts = 0;

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
		//volts = ((float)(unsigned_code + 10000)) * 0.00015;
		return ((code + 10000) * 0.000150);
		break;

	default:
		return 0;
	}

	if (volts < 0.0f)
		volts = 0.0f;
	if (volts > 65.535)
		volts = 65.535;

	/* return in mV */
	return (int) (volts * 1000.0f + 0.5f);
}

//Thermistor: Amphenol NKA502C1*1C
float getTemperatureCAN(int16_t code) {
	//Thermistor: Amphenol NKA502C1*1C
	float VREF2 = 3.0f;      // Reference voltage
	float R1 = 10000.0f;     // Fixed resistor (10k)
	float R0 = 5000.0f;     // Thermistor nominal resistance at 25°C
	float BETA = 3977.0f;    // Beta constant
	float T0_K = 298.15f;    // 25°C in Kelvin

	float gpio_voltage = ((code + 10000) * 0.000150);

	if (gpio_voltage <= 0.0f)
		return 1.99;
	if (gpio_voltage >= VREF2)
		return 150;

	float Rt = R1 * gpio_voltage / (VREF2 - gpio_voltage);
	float tempK = 1.0f / ((1.0f / T0_K) + (1.0f / BETA) * logf(Rt / R0));
	float tempC = tempK - 273.15f;

	if (tempC < 0.0f)
		tempC = 0.0f;
	if (tempC > 150.0f)
		tempC = 150.0f;

	return (tempC);
	//return (tempC);
}
