/*
 * precharge.c
 *
 * Ver precharge.h. Todas as esperas sao estados medidos com o HAL_GetTick:
 * nunca se espera dentro de uma funcao.
 */

#include "precharge.h"
#include <stdlib.h>

#define VCU_REQUEST_FRESH_MS 1000u   // um 0x083 = 1 mais velho que isto ja nao arranca a precarga
#define MISMATCH_HOLD_MS     1000u   // o contactor_mismatch fica pelo menos isto (para o CAN e a UI o verem)

static uint32_t precharge_closed_ms;    // quando o rele de precarga foi mandado fechar

static const char *const state_names[] = {
	"START", "OPEN_ALL", "SWITCH_HVNEG", "WAIT_FOR_AIR_NEG_TO_CLOSE", "CHECKING_AIR_NEG_IS_CLOSED",
	"SWITCH_PRECHARGE", "WAIT_FOR_PRECHARGE_TO_CLOSE", "CHECKING_PRECHARGE_IS_CLOSED", "VERIFY_CURRENT",
	"VERIFY_BUS_VOLT", "SWITCH_HVPOS", "WAIT_FOR_AIR_POS_TO_CLOSE", "CHECKING_AIR_POS_IS_CLOSED",
	"TURN_OFF_PRECHARGE", "WAIT_FOR_PRECHARGE_TO_OPEN", "CHECKING_PRECHARGE_IS_OPEN", "HV_ON", "WRONG",
	"KILL", "RX_CAN",
};

_Static_assert((sizeof(state_names) / sizeof(state_names[0])) == (RX_CAN + 1), "um nome por estado da precarga");

const char* Precharge_StateName(precharge_state_t state) {

	return ((unsigned) state <= (unsigned) RX_CAN) ? state_names[state] : "?";
}

static void enter(precharge_state_t state, uint32_t now_ms) {

	bms.precharge.state = state;
	bms.precharge.entered_ms = now_ms;
}

static uint32_t waited_ms(uint32_t now_ms) {

	return now_ms - bms.precharge.entered_ms;
}

/* contactores que cada estado quer fechados: a unica fonte dos comandos */
static uint8_t commands_for(precharge_state_t state) {

	switch (state) {
	case SWITCH_HVNEG:
	case WAIT_FOR_AIR_NEG_TO_CLOSE:
	case CHECKING_AIR_NEG_IS_CLOSED:
		return CONTACTOR_BIT_AIR_NEG;

	case SWITCH_PRECHARGE:
	case WAIT_FOR_PRECHARGE_TO_CLOSE:
	case CHECKING_PRECHARGE_IS_CLOSED:
	case VERIFY_BUS_VOLT:
	case VERIFY_CURRENT:
		return CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_PRE;

	case SWITCH_HVPOS:
	case WAIT_FOR_AIR_POS_TO_CLOSE:
	case CHECKING_AIR_POS_IS_CLOSED:
		return CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_PRE | CONTACTOR_BIT_AIR_POS;

	case TURN_OFF_PRECHARGE:
	case WAIT_FOR_PRECHARGE_TO_OPEN:
	case CHECKING_PRECHARGE_IS_OPEN:
	case HV_ON:
		return CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_AIR_POS;

	default:
		return 0u;
	}
}

/* feedback esperado: o dos comandos; o da descarga (rele que segue o SDC)
 * esperado ligado com qualquer AIR fechado, como no firmware antigo. So conta
 * se o bit estiver no contactor_check_mask (por omissao nao esta) */
static uint8_t expected_for(uint8_t commands) {

	return (commands != 0u) ? (uint8_t) (commands | CONTACTOR_BIT_DISCH) : 0u;
}

static uint8_t feedback_mask(void) {

	const bms_precharge_t *precharge = &bms.precharge;
	uint8_t actual = 0;

	actual |= precharge->air_neg_fb ? CONTACTOR_BIT_AIR_NEG : 0u;
	actual |= precharge->precharge_fb ? CONTACTOR_BIT_PRE : 0u;
	actual |= precharge->air_pos_fb ? CONTACTOR_BIT_AIR_POS : 0u;
	actual |= precharge->discharge_fb ? CONTACTOR_BIT_DISCH : 0u;
	return actual;
}

static bool feedback_matches(uint8_t expected) {

	return ((feedback_mask() ^ expected) & bms.config.contactor_check_mask) == 0u;
}

// check da sequencia: fica registado (expected_mask/actual_mask) para o diagnostico
static bool feedback_is(uint8_t expected) {

	bms.precharge.expected_mask = expected;
	bms.precharge.actual_mask = feedback_mask();
	return feedback_matches(expected);
}

static bool in_sequence(precharge_state_t state) {

	return (state != KILL) && (state != HV_ON) && (state != WRONG);
}

static void mismatch(uint32_t now_ms) {

	bms.faults.contactor_mismatch = true;
	enter(WRONG, now_ms);
}

/* ================== PEDIDOS ================== */

void Precharge_OnVcuRequest(uint8_t request, uint32_t now_ms) {

	bms.precharge.vcu_request = (request > 0u) ? 1 : 0;
	bms.precharge.vcu_request_rx_ms = now_ms;
	bms.precharge.vcu_alive_rx_ms = now_ms;
}

void Precharge_OnVcuAlive(uint32_t now_ms) {

	bms.precharge.vcu_alive_rx_ms = now_ms;
}

void Precharge_Init(uint32_t now_ms) {

	bms_precharge_t *precharge = &bms.precharge;

	enter(KILL, now_ms);
	precharge->source = PRECHARGE_SOURCE_NONE;
	precharge->vcu_request = -1;
	precharge->lockout_until_ms = now_ms;
	precharge->handcart_start = false;
	precharge->kill_request = false;
	precharge->trigger_air_pos_cmd = false;
	precharge->trigger_air_neg_cmd = false;
	precharge->trigger_precharge_cmd = false;
	precharge->trigger_discharge_cmd = false;
}

/* KILL -> RX_CAN: so com AMS_ERROR inativo, SDC fechado, fora do lockout */
static void handle_requests(uint32_t now_ms, bool blocked) {

	bms_precharge_t *precharge = &bms.precharge;
	const bms_config_t *config = &bms.config;

	if (precharge->state == KILL) {

		bool lockout_over = (int32_t) (now_ms - precharge->lockout_until_ms) >= 0;
		bool vcu_fresh = (precharge->vcu_request > 0) && ((now_ms - precharge->vcu_request_rx_ms) <= VCU_REQUEST_FRESH_MS);
		uint8_t source = precharge->handcart_start ? PRECHARGE_SOURCE_HANDCART : (vcu_fresh ? PRECHARGE_SOURCE_VCU : PRECHARGE_SOURCE_NONE);

		if ((source != PRECHARGE_SOURCE_NONE) && !blocked && lockout_over) {
			precharge->source = source;
			precharge->lockout_until_ms = now_ms + config->precharge_lockout_ms;
			precharge->sequence_start_ms = now_ms;
			precharge->mismatch_count = 0;
			bms.faults.precharge_timeout = false;
			bms.faults.precharge_failure = false;
			enter(RX_CAN, now_ms);
		}
	} else if (precharge->state != WRONG) {

		bool vcu_off = (precharge->source == PRECHARGE_SOURCE_VCU) && (precharge->vcu_request <= 0);

		// paragem normal (pedido, SDC aberto por outro, AMS_ERROR): sem fault
		if (blocked || precharge->kill_request || vcu_off) {
			enter(KILL, now_ms);
		}
	}

	// os pedidos do charger sao flancos: consumidos em cada passo
	precharge->handcart_start = false;
	precharge->kill_request = false;
}

/* ================== MAQUINA ================== */

void Precharge_Step(uint32_t now_ms) {

	bms_precharge_t *precharge = &bms.precharge;
	const bms_config_t *config = &bms.config;
	bool blocked = bms.ams_error.active || !precharge->inputs_valid || !precharge->sdc_closed;

	precharge->vcu_request_age_ms = (precharge->vcu_request >= 0) ? (now_ms - precharge->vcu_request_rx_ms) : UINT32_MAX;

	handle_requests(now_ms, blocked);

	switch (precharge->state) {

	case RX_CAN:
		enter(START, now_ms);
		break;

	case START:
		enter(OPEN_ALL, now_ms);
		break;

	case OPEN_ALL:
		if (feedback_is(0u)) {
			enter(SWITCH_HVNEG, now_ms);
		} else if (waited_ms(now_ms) >= config->precharge_air_wait_ms) {
			mismatch(now_ms);   // algum contactor fechado com tudo mandado abrir (soldado?)
		}
		break;

	case SWITCH_HVNEG:
		enter(WAIT_FOR_AIR_NEG_TO_CLOSE, now_ms);
		break;

	case WAIT_FOR_AIR_NEG_TO_CLOSE:
		if (waited_ms(now_ms) >= config->precharge_air_wait_ms) {
			enter(CHECKING_AIR_NEG_IS_CLOSED, now_ms);
		}
		break;

	case CHECKING_AIR_NEG_IS_CLOSED:
		if (feedback_is(expected_for(commands_for(CHECKING_AIR_NEG_IS_CLOSED)))) {
			enter(SWITCH_PRECHARGE, now_ms);
		} else {
			mismatch(now_ms);
		}
		break;

	case SWITCH_PRECHARGE:
		precharge_closed_ms = now_ms;
		enter(WAIT_FOR_PRECHARGE_TO_CLOSE, now_ms);
		break;

	case WAIT_FOR_PRECHARGE_TO_CLOSE:
		if (waited_ms(now_ms) >= config->precharge_air_wait_ms) {
			enter(CHECKING_PRECHARGE_IS_CLOSED, now_ms);
		}
		break;

	case CHECKING_PRECHARGE_IS_CLOSED:
		if (feedback_is(expected_for(commands_for(CHECKING_PRECHARGE_IS_CLOSED)))) {
			enter(VERIFY_BUS_VOLT, now_ms);
		} else {
			mismatch(now_ms);
		}
		break;

	case VERIFY_BUS_VOLT: {

		if (!feedback_is(expected_for(commands_for(VERIFY_BUS_VOLT)))) {
			mismatch(now_ms);
			break;
		}

		uint8_t channel = (config->isa_bus_channel >= 1u) && (config->isa_bus_channel <= 3u) ? (uint8_t) (config->isa_bus_channel - 1u) : 0u;
		int32_t bus_mV = bms.isa_pack.alive ? bms.isa_pack.voltage_mV[channel] : 0;
		int64_t accu_mV = bms.cells.cells_valid ? (int64_t) bms.accu.voltage_sum_mV : 0;

		precharge->bus_voltage_mV = bus_mV;

		if ((now_ms - precharge_closed_ms) < config->precharge_min_ms) {
			break;
		}

		bool charged = (config->precharge_mode == 0u)
				|| ((accu_mV > 0) && (((int64_t) bus_mV * 100) >= (accu_mV * config->precharge_bus_percentage)));

		if (charged) {
			enter(VERIFY_CURRENT, now_ms);
		}
		break;
	}

	case VERIFY_CURRENT:
		if (!feedback_is(expected_for(commands_for(VERIFY_CURRENT)))) {
			mismatch(now_ms);
		} else if ((config->precharge_end_current_mA == 0u)
				|| (bms.isa_pack.alive && (labs((long) bms.isa_pack.current_mA) <= (long) config->precharge_end_current_mA))) {
			enter(SWITCH_HVPOS, now_ms);
		}
		break;

	case SWITCH_HVPOS:
		enter(WAIT_FOR_AIR_POS_TO_CLOSE, now_ms);
		break;

	case WAIT_FOR_AIR_POS_TO_CLOSE:
		if (waited_ms(now_ms) >= config->precharge_air_wait_ms) {
			enter(CHECKING_AIR_POS_IS_CLOSED, now_ms);
		}
		break;

	case CHECKING_AIR_POS_IS_CLOSED:
		if (feedback_is(expected_for(commands_for(CHECKING_AIR_POS_IS_CLOSED)))) {
			enter(TURN_OFF_PRECHARGE, now_ms);
		} else {
			mismatch(now_ms);
		}
		break;

	case TURN_OFF_PRECHARGE:
		enter(WAIT_FOR_PRECHARGE_TO_OPEN, now_ms);
		break;

	case WAIT_FOR_PRECHARGE_TO_OPEN:
		if (waited_ms(now_ms) >= config->precharge_air_wait_ms) {
			enter(CHECKING_PRECHARGE_IS_OPEN, now_ms);
		}
		break;

	case CHECKING_PRECHARGE_IS_OPEN:
		if (feedback_is(expected_for(commands_for(CHECKING_PRECHARGE_IS_OPEN)))) {
			precharge->mismatch_count = 0;
			precharge->mismatch_check_ms = now_ms;
			precharge->hv_on_count++;
			enter(HV_ON, now_ms);
		} else {
			mismatch(now_ms);
		}
		break;

	case HV_ON:
		/* EMI: um mismatch so e real se aparecer em contactor_mismatch_count
		 * checks seguidos, espacados do debounce (cada um ja e um feedback novo) */
		if (feedback_is(expected_for(commands_for(HV_ON)))) {
			precharge->mismatch_count = 0;
			precharge->mismatch_check_ms = now_ms;
		} else if ((now_ms - precharge->mismatch_check_ms) >= config->contactor_debounce_ms) {
			precharge->mismatch_count++;
			precharge->mismatch_check_ms = now_ms;
			if (precharge->mismatch_count >= config->contactor_mismatch_count) {
				mismatch(now_ms);
			}
		}
		break;

	case WRONG:
		bms.faults.precharge_failure = true;
		enter(KILL, now_ms);
		precharge->source = PRECHARGE_SOURCE_NONE;
		break;

	case KILL:
		precharge->source = PRECHARGE_SOURCE_NONE;
		/* o mismatch limpa quando os feedbacks voltam a dizer tudo aberto
		 * (se estiver no ams_error_enable, nao fica preso: a precarga nao
		 * podia voltar a arrancar com o AMS_ERROR ativo) */
		if (bms.faults.contactor_mismatch && precharge->inputs_valid && feedback_matches(0u) && (waited_ms(now_ms) >= MISMATCH_HOLD_MS)) {
			bms.faults.contactor_mismatch = false;
		}
		break;

	default:
		enter(WRONG, now_ms);
		break;
	}

	// tempo maximo da sequencia inteira (RX_CAN ate HV_ON)
	if (in_sequence(precharge->state) && ((now_ms - precharge->sequence_start_ms) >= config->precharge_timeout_ms)) {
		bms.faults.precharge_timeout = true;
		enter(WRONG, now_ms);
	}

	bms.faults.vcu_timeout = (config->vcu_timeout_ms > 0u) && (precharge->source == PRECHARGE_SOURCE_VCU) && (precharge->state != KILL)
			&& ((now_ms - precharge->vcu_alive_rx_ms) > config->vcu_timeout_ms);

	uint8_t commands = commands_for(precharge->state);

	precharge->trigger_air_neg_cmd = (commands & CONTACTOR_BIT_AIR_NEG) != 0u;
	precharge->trigger_precharge_cmd = (commands & CONTACTOR_BIT_PRE) != 0u;
	precharge->trigger_air_pos_cmd = (commands & CONTACTOR_BIT_AIR_POS) != 0u;
	precharge->trigger_discharge_cmd = (commands != 0u);
	precharge->state_ms = now_ms - precharge->entered_ms;
}
