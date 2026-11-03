/*
 * isa.c
 *
 * Ver isa.h. Trama de resultado da IVT-S (datasheet IVT-S 1.03, igual ao
 * DBC powertrain_t26): byte 0 = id do resultado, byte 1 = erros + contador,
 * bytes 2..5 = valor int32 big-endian.
 */

#include "isa.h"
#include "powertrain_t26.h"
#include <stdlib.h>
#include <string.h>

void Isa_Init(void) {

	memset(&bms.isa_pack, 0, sizeof(bms.isa_pack));
	memset(&bms.isa_handcart, 0, sizeof(bms.isa_handcart));
	memset(&bms.accu, 0, sizeof(bms.accu));
}

bool Isa_OnFrame(bms_isa_t *isa, bool invert, uint32_t id, const uint8_t *data, uint8_t dlc, uint32_t now_ms) {

	if ((id < POWERTRAIN_T26_IVT_MSG_RESULT_I_FRAME_ID) || (id > POWERTRAIN_T26_IVT_MSG_RESULT_WH_FRAME_ID)) {
		return false;
	}
	if (dlc < 6u) {
		return true;   // da ISA mas curta: ignorada, nao conta como viva
	}

	int32_t value = (int32_t) (((uint32_t) data[2] << 24) | ((uint32_t) data[3] << 16) | ((uint32_t) data[4] << 8) | (uint32_t) data[5]);
	int32_t sign = invert ? -1 : 1;

	switch (id) {
	case POWERTRAIN_T26_IVT_MSG_RESULT_I_FRAME_ID:
		isa->current_mA = sign * value;
		isa->error_flags = (uint8_t) (data[1] >> 4);
		isa->current_rx_ms = now_ms;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_U1_FRAME_ID:
		isa->voltage_mV[0] = value;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_U2_FRAME_ID:
		isa->voltage_mV[1] = value;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_U3_FRAME_ID:
		isa->voltage_mV[2] = value;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_T_FRAME_ID:
		isa->temp_dC = (int16_t) value;   // 0,1 C
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_W_FRAME_ID:
		isa->power_W = sign * value;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_AS_FRAME_ID:
		isa->charge_As = sign * value;
		break;
	case POWERTRAIN_T26_IVT_MSG_RESULT_WH_FRAME_ID:
	default:
		isa->energy_Wh = sign * value;
		break;
	}

	isa->any_rx_ms = now_ms;
	isa->frames++;
	return true;
}

static void update_alive(bms_isa_t *isa, uint16_t timeout_ms, uint32_t now_ms) {

	isa->age_ms = (isa->current_rx_ms != 0u) ? (now_ms - isa->current_rx_ms) : UINT32_MAX;
	isa->alive = (isa->current_rx_ms != 0u) && (isa->age_ms <= timeout_ms);
}

/* condicao que tem de durar duration_ms; since_ms = 0 quando nao esta ativa */
static bool persists(bool condition, uint32_t *since_ms, uint16_t duration_ms, uint32_t now_ms) {

	if (!condition) {
		*since_ms = 0;
		return false;
	}
	if (*since_ms == 0u) {
		*since_ms = (now_ms != 0u) ? now_ms : 1u;
	}
	return (now_ms - *since_ms) >= duration_ms;
}

void Isa_Step(uint32_t now_ms) {

	const bms_config_t *config = &bms.config;
	bms_isa_t *pack = &bms.isa_pack;
	bms_accu_t *accu = &bms.accu;

	update_alive(pack, config->isa_pack_timeout_ms, now_ms);
	update_alive(&bms.isa_handcart, config->isa_handcart_timeout_ms, now_ms);

	uint8_t channel = ((config->isa_bus_channel >= 1u) && (config->isa_bus_channel <= 3u)) ? (uint8_t) (config->isa_bus_channel - 1u) : 0u;

	accu->current_mA = pack->current_mA;
	accu->voltage_mV = pack->voltage_mV[channel];
	accu->power_W = pack->power_W;
	accu->charge_As = pack->charge_As;
	accu->energy_Wh = pack->energy_Wh;
	accu->isa_temp_dC = pack->temp_dC;
	accu->isa_alive = pack->alive;
	accu->isa_age_ms = pack->age_ms;

	// FS 2027 EV 5.8.3: a corrente do TS tem de ser medida sempre
	bms.faults.isa_pack_timeout = !pack->alive;
	bms.faults.isa_pack_error = pack->alive && ((pack->error_flags & (ISA_ERROR_CHANNEL | ISA_ERROR_MEASUREMENT | ISA_ERROR_SYSTEM)) != 0u);

	// FS 2027 EV 5.8.10: corrente critica mais de 500 ms abre o SDC
	int64_t limit_mA = (pack->current_mA >= 0) ? ((int64_t) config->overcurrent_charge_A * 1000) : ((int64_t) config->overcurrent_discharge_A * 1000);
	bool over_limit = pack->alive && (llabs((long long) pack->current_mA) > limit_mA);

	bms.faults.overcurrent = persists(over_limit, &accu->overcurrent_since_ms, config->overcurrent_ms, now_ms);

	// com HV ligada o bus e o pack: a ISA tem de bater com a soma das celulas
	bool comparable = pack->alive && bms.cells.cells_valid && (bms.precharge.state == HV_ON) && (accu->voltage_sum_mV > 0u);
	int64_t difference_mV = llabs((long long) accu->voltage_mV - (long long) accu->voltage_sum_mV);
	bool mismatch = comparable && ((difference_mV * 100) > ((int64_t) accu->voltage_sum_mV * config->pack_voltage_tolerance_pct));

	bms.faults.pack_voltage_mismatch = persists(mismatch, &accu->mismatch_since_ms, config->pack_voltage_mismatch_ms, now_ms);
}
