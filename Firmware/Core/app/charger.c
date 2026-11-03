/*
 * charger.c
 *
 * Ver charger.h. Logica do firmware antigo (charger.c, 2026-08-06) com os
 * limites passados para a config e o debounce nas flags do carregador (uma
 * trama com starting_state_fault ja nao corta a carga).
 */

#include "charger.h"
#include "precharge.h"
#include <stdlib.h>
#include <string.h>

static const char *const state_names[] = { "WAIT_HV", "PRESTART_STOP", "CHARGING", "STOPPING", "DONE" };

static const char *const end_names[] = { "NONE", "COMPLETE", "SWITCH_OFF", "SDC_OPEN", "HV_LOST", "ISA_TIMEOUT", "OVERTEMP", "OVERCURRENT",
		"CHARGER_TIMEOUT", "CHARGER_FAULT", "BUS_MISMATCH", "AMS_ERROR", "HANDCART_TIMEOUT" };

_Static_assert((sizeof(state_names) / sizeof(state_names[0])) == (CHARGER_DONE + 1), "um nome por estado do charger");
_Static_assert((sizeof(end_names) / sizeof(end_names[0])) == (CHARGE_END_HANDCART_TIMEOUT + 1), "um nome por fim de carga");

const char* Charger_StateName(charger_state_t state) {

	return ((unsigned) state <= (unsigned) CHARGER_DONE) ? state_names[state] : "?";
}

const char* Charger_EndName(charge_end_t reason) {

	return ((unsigned) reason <= (unsigned) CHARGE_END_HANDCART_TIMEOUT) ? end_names[reason] : "?";
}

static void enter(charger_state_t state, uint32_t now_ms) {

	bms.charger.state = state;
	bms.charger.state_ms = now_ms;
}

void Charger_Init(void) {

	memset(&bms.charger, 0, sizeof(bms.charger));
	bms.charger.state = CHARGER_WAIT_HV;
}

bool Charger_SessionActive(void) {

	const bms_charger_t *charger = &bms.charger;

	return (charger->state == CHARGER_PRESTART_STOP) || (charger->state == CHARGER_CHARGING) || (charger->state == CHARGER_STOPPING)
			|| ((bms.precharge.source == PRECHARGE_SOURCE_HANDCART) && (bms.precharge.state != KILL));
}

void Charger_OnSwitch(bool switch_on, uint32_t now_ms) {

	bms_charger_t *charger = &bms.charger;
	bool was_on = charger->switch_on && charger->switch_seen;

	charger->switch_on = switch_on;
	charger->switch_seen = true;
	charger->switch_rx_ms = now_ms;

	/* so o flanco OFF->ON arranca: o stream de 100 ms nao pode voltar a
	 * arrancar a precarga sozinho logo a seguir a um corte */
	if (switch_on && !was_on && (bms.precharge.state == KILL)) {
		bms.precharge.handcart_start = true;
		charger->end_reason = CHARGE_END_NONE;
		enter(CHARGER_WAIT_HV, now_ms);
	}
}

void Charger_OnStatus(const charger_status_t *status, uint32_t now_ms) {

	bms_charger_t *charger = &bms.charger;

	charger->out_voltage_dV = status->output_voltage_dV;
	charger->out_current_dA = status->output_current_dA;
	charger->hw_failure = status->hw_failure;
	charger->over_temp = status->temp_otp;
	charger->input_voltage_fault = status->input_voltage_fault;
	charger->starting_state_fault = status->starting_state_fault;
	charger->comm_timeout = status->comm_timeout;
	charger->temp_C = status->temperature_C;
	charger->status_seen = true;
	charger->updated_ms = now_ms;
}

/* corte: para o carregador e abre os contactores ja */
static void cut(charge_end_t reason, uint32_t now_ms) {

	bms.charger.req_enable = false;
	bms.charger.end_reason = reason;
	bms.precharge.kill_request = true;
	enter(CHARGER_DONE, now_ms);
}

/* paragem ordenada: para o carregador e espera que a corrente caia */
static void stop(charge_end_t reason, uint32_t now_ms) {

	bms.charger.req_enable = false;
	bms.charger.end_reason = reason;
	enter(CHARGER_STOPPING, now_ms);
}

/* antes de arrancar: a ISA do handcart (U1) tem de bater com a soma das
 * celulas; fora disso ha um contactor aberto, sensor avariado ou ligacao errada */
static bool bus_plausible(void) {

	int64_t pack_mV = bms.cells.cells_valid ? (int64_t) bms.accu.voltage_sum_mV : 0;
	int64_t isa_mV = llabs((long long) bms.isa_handcart.voltage_mV[0]);

	if (!bms.isa_handcart.alive || (pack_mV <= 0)) {
		return false;
	}

	return (llabs((long long) (isa_mV - pack_mV)) * 100) <= (pack_mV * bms.config.charge_bus_tolerance_pct);
}

void Charger_Step(uint32_t now_ms) {

	bms_charger_t *charger = &bms.charger;
	const bms_config_t *config = &bms.config;
	bool hv_on = (bms.precharge.state == HV_ON) && (bms.precharge.source == PRECHARGE_SOURCE_HANDCART);

	charger->handcart_connected = charger->switch_seen && ((now_ms - charger->switch_rx_ms) <= config->handcart_timeout_ms);
	charger->charger_alive = charger->status_seen && ((now_ms - charger->updated_ms) <= config->charge_status_timeout_ms);
	charger->charger_connected = charger->charger_alive;
	charger->req_voltage_dV = config->charge_voltage_dV;
	charger->req_current_dA = config->charge_current_dA;

	// flags do carregador com debounce
	bool flags_now = charger->charger_alive && (charger->hw_failure || charger->over_temp || charger->input_voltage_fault || charger->starting_state_fault);

	if (!flags_now) {
		charger->fault_since_ms = 0;
	} else if (charger->fault_since_ms == 0u) {
		charger->fault_since_ms = (now_ms != 0u) ? now_ms : 1u;
	}
	bms.faults.charger_fault = flags_now && ((now_ms - charger->fault_since_ms) >= config->charge_fault_debounce_ms);

	bool session = Charger_SessionActive();
	bool switch_off = !charger->switch_on || !charger->handcart_connected;

	bms.faults.handcart_timeout = session && charger->switch_seen && !charger->handcart_connected;
	bms.faults.charger_timeout = ((charger->state == CHARGER_PRESTART_STOP) || (charger->state == CHARGER_CHARGING)) && charger->status_seen
			&& !charger->charger_alive;

	switch (charger->state) {

	case CHARGER_WAIT_HV:
		charger->req_enable = false;

		if ((bms.precharge.source == PRECHARGE_SOURCE_HANDCART) && (bms.precharge.state != KILL) && switch_off) {
			cut(charger->handcart_connected ? CHARGE_END_SWITCH_OFF : CHARGE_END_HANDCART_TIMEOUT, now_ms);
		} else if (hv_on) {
			if (bus_plausible()) {
				enter(CHARGER_PRESTART_STOP, now_ms);
			} else {
				cut(bms.isa_handcart.alive ? CHARGE_END_BUS_MISMATCH : CHARGE_END_ISA_TIMEOUT, now_ms);
			}
		}
		break;

	case CHARGER_PRESTART_STOP:
		charger->req_enable = false;   // Control = 1 durante o pre-arranque

		if (switch_off) {
			cut(charger->handcart_connected ? CHARGE_END_SWITCH_OFF : CHARGE_END_HANDCART_TIMEOUT, now_ms);
		} else if (!hv_on) {
			cut(CHARGE_END_HV_LOST, now_ms);
		} else if ((now_ms - charger->state_ms) >= config->charge_prestart_ms) {
			charger->req_enable = true;
			charger->sessions++;
			enter(CHARGER_CHARGING, now_ms);
		}
		break;

	case CHARGER_CHARGING: {

		int32_t charge_mA = labs((long) bms.isa_handcart.current_mA);

		// por ordem: o que corta ja primeiro, o fim normal depois
		if (!bms.precharge.sdc_closed) {
			cut(CHARGE_END_SDC_OPEN, now_ms);
		} else if (bms.ams_error.active) {
			cut(CHARGE_END_AMS_ERROR, now_ms);
		} else if (!bms.isa_handcart.alive) {
			cut(CHARGE_END_ISA_TIMEOUT, now_ms);   // sem ISA o corte de corrente ficava cego
		} else if (!hv_on) {
			cut(CHARGE_END_HV_LOST, now_ms);
		} else if (bms.cells.temps_valid && (bms.cells.temp_max_dC >= config->charge_temp_max_dC)) {
			cut(CHARGE_END_OVERTEMP, now_ms);
		} else if (charge_mA > (int32_t) config->charge_cut_current_mA) {
			cut(CHARGE_END_OVERCURRENT, now_ms);
		} else if (bms.faults.charger_timeout) {
			cut(CHARGE_END_CHARGER_TIMEOUT, now_ms);
		} else if (bms.faults.charger_fault) {
			cut(CHARGE_END_CHARGER_FAULT, now_ms);
		} else if (switch_off) {
			stop(charger->handcart_connected ? CHARGE_END_SWITCH_OFF : CHARGE_END_HANDCART_TIMEOUT, now_ms);
		} else if (bms.cells.cells_valid && (bms.cells.cell_max_mV >= config->cell_high_warn_mV)) {
			stop(CHARGE_END_COMPLETE, now_ms);   // abaixo do OV: a carga completa nunca toca na protecao
		} else {
			charger->req_enable = true;
		}
		break;
	}

	case CHARGER_STOPPING: {

		int32_t charge_mA = labs((long) bms.isa_handcart.current_mA);
		bool current_gone = bms.isa_handcart.alive && (charge_mA < (int32_t) config->charge_open_mA);

		charger->req_enable = false;
		if (current_gone || ((now_ms - charger->state_ms) >= config->charge_settle_ms) || !bms.precharge.sdc_closed) {
			bms.precharge.kill_request = true;
			enter(CHARGER_DONE, now_ms);
		}
		break;
	}

	case CHARGER_DONE:
	default:
		// latch: mesmo que a tensao baixe nao recomeca, so o switch OFF->ON
		charger->req_enable = false;
		if (charger->state != CHARGER_DONE) {
			enter(CHARGER_DONE, now_ms);
		}
		break;
	}
}
