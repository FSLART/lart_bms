/*
 * live_debug.c
 *
 *  Fills the live_debug snapshot from existing firmware state and sends it
 *  as one JSON line on UART2 (BT). See live_debug.h.
 */

#include "live_debug.h"

#include <stdarg.h>
#include <stdio.h>

#include "main.h"
#include "master_to_CAN.h"   // count_active_faults(), read_contactor_state()
#include "adbms_to_CAN.h"    // g_pack_* overalls
#include "isa_ivt-s.h"       // IVT_Get*()
#include "fan_management.h"  // Get_Fan_PWM()
#include "analog_readings.h" // AnalogReadings_Get()
#include "can.h"             // CAN_IsStarted(), CanTx_GetQueueDepth()
#include "ams_error.h"       // AMS_Error_IsActive() / IsPermanent()
#include "fault_manager.h"   // FaultManager_GetActiveMask() / GetName()
#include "adBms_Application.h" // IC[], balanceStage, global_min_mV, Balancing_GetPhase()
#include "cell_balancing.h"    // cell_code_to_mV(), BALANCING_CELL_COUNT
#include "soc.h"               // SOC_GetPercent() ...
#include "charger.h"           // Charger_GetState() ...
#include "dbc/handcart_t26.h"  // struct do status do carregador
#include "uartDMA.h"           // uart2Write()

/* Externs owned by other translation units */
extern uint8_t slaves_found;     // adBms_Application.c
extern uint8_t anyPecError;      // adBms6830GenericType.c
extern volatile uint32_t uart2DmaErrors;    // uartDMA.c
extern volatile uint32_t uart2DmaRestarts;  // uartDMA.c
extern volatile uint32_t rn4871Reboots;     // uartDMA.c

live_debug_t live_debug = { 0 };

static void LiveDebug_SendJson(void);

void LiveDebug_Update(void) {

	/* brain / AMS state machine */
	live_debug.brain.ams_state          = AMS_State;
	live_debug.brain.ams_previous_state = AMS_Previous_State;
	live_debug.brain.runtime_s          = getRuntimeSeconds();
	live_debug.brain.runtime_ms         = getRuntimeMs();
	live_debug.brain.active_fault_count = count_active_faults();
	live_debug.brain.watchdog_flag      = watchdog_flag;
	live_debug.brain.wwdg_reset         = ((watchdog_flag & RCC_CSR_WWDGRSTF) != 0) ? 1U : 0U;

	/* linha AMS_ERROR + faults que a podem ter disparado */
	live_debug.ams_error.active    = AMS_Error_IsActive();
	live_debug.ams_error.permanent = AMS_Error_IsPermanent();

	if (live_debug.ams_error.permanent != 0) {
		live_debug.ams_error.state_name = "ERROR_PERMANENT";
	} else if (live_debug.ams_error.active != 0) {
		live_debug.ams_error.state_name = "ERROR";
	} else {
		live_debug.ams_error.state_name = "OK";
	}

	uint64_t fault_mask = FaultManager_GetActiveMask();

	live_debug.ams_error.fault_mask_lo = (uint32_t) (fault_mask & 0xFFFFFFFFu);
	live_debug.ams_error.fault_mask_hi = (uint32_t) (fault_mask >> 32);

	uint8_t fault_listed = 0;
	uint8_t fault_total = 0;

	for (uint8_t code = 0; code < FAULT_COUNT; code++) {

		if ((fault_mask & (1ULL << code)) == 0) {
			continue;
		}

		fault_total++;

		if (fault_listed < LIVE_DEBUG_FAULT_LIST) {
			live_debug.ams_error.faults[fault_listed] = FaultManager_GetName((FaultCode_t) code);
			fault_listed++;
		}
	}

	live_debug.ams_error.fault_count = fault_total;

	/* limpar o resto da lista para nao ficarem nomes velhos la */
	while (fault_listed < LIVE_DEBUG_FAULT_LIST) {
		live_debug.ams_error.faults[fault_listed] = 0;
		fault_listed++;
	}

	/* adbms driver + pack overalls */
	live_debug.adbms.slaves_found        = slaves_found;
	live_debug.adbms.adbms_state         = adbms_current_state;
	live_debug.adbms.any_pec_error       = anyPecError;
	live_debug.adbms.pack_vmax_mV        = g_pack_vmax_mV;
	live_debug.adbms.pack_vmin_mV        = g_pack_vmin_mV;
	live_debug.adbms.pack_tmax_cC        = g_pack_tmax_cC;
	live_debug.adbms.pack_tmin_cC        = g_pack_tmin_cC;
	live_debug.adbms.pack_voltage_sum_mV = g_pack_voltage_sum_mV;

	/* ISA IVT-S do pack (CAN1) - a que alimenta o SOC */
	live_debug.ivt_can1.current_mA    = IVT_GetCurrent_mA();
	live_debug.ivt_can1.u1_voltage_mV = IVT_GetPackVoltage_mV();
	live_debug.ivt_can1.power_W       = IVT_GetPower_W();
	live_debug.ivt_can1.coulombs_As   = IVT_GetCoulombs_As();
	live_debug.ivt_can1.temp_dC       = IVT_GetTemperature_dC();
	live_debug.ivt_can1.rx_age_ms     = IVT_GetLastRxAgeMs();

	/* ISA IVT-S do handcart (CAN2) - corrente de carga, separada do pack */
	live_debug.ivt_can2.current_mA    = IVT_GetCurrentCan2_mA();
	live_debug.ivt_can2.u1_voltage_mV = IVT_GetPackVoltageCan2_mV();
	live_debug.ivt_can2.power_W       = IVT_GetPowerCan2_W();
	live_debug.ivt_can2.coulombs_As   = IVT_GetCoulombsCan2_As();
	live_debug.ivt_can2.temp_dC       = IVT_GetTemperatureCan2_dC();
	live_debug.ivt_can2.rx_age_ms     = IVT_GetLastRxAgeMsCan2();

	/* SOC */
	live_debug.soc.percent        = SOC_GetPercent();
	live_debug.soc.used_charge_As = SOC_GetUsedCharge_As();
	live_debug.soc.ready          = SOC_IsReady() ? 1U : 0U;

	/* carregador + handcart */
	const struct handcart_t26_charger_status_p1000_t *chg = Charger_GetLastStatus();

	live_debug.charger.state                 = Charger_GetState();
	live_debug.charger.handcart_switch       = Charger_IsRequested();
	live_debug.charger.handcart_age_ms       = Charger_GetSwitchFeedbackAgeMs();
	live_debug.charger.status_ok             = Charger_HasStatus();
	live_debug.charger.output_voltage_dV     = chg->output_voltage;
	live_debug.charger.output_current_dA     = chg->output_current;
	live_debug.charger.hw_failure            = chg->hw_failure;
	live_debug.charger.temp_otp              = chg->temp_otp;
	live_debug.charger.input_voltage_fault   = chg->input_voltage_fault;
	live_debug.charger.starting_state_fault  = chg->starting_state_fault;
	live_debug.charger.comm_timeout          = chg->comm_timeout;
	live_debug.charger.temp_C                = (int16_t) chg->charger_temperature - 40;   // DBC offset -40
	live_debug.charger.requested_current_raw = Charger_GetRequestedCurrentRaw();

	/* todas as celulas (mV) e todos os NTC (C) por slave. Slaves acima de
	 * slaves_found ficam a 0. Usa o SLAVE[] (snapshot coerente) */
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {

		for (uint8_t c = 0; c < LIVE_DEBUG_MEAS_CELLS; c++) {
			if (m < slaves_found) {
				live_debug.meas.cell_mV[m][c] = (int16_t) (1500.0f + (float) SLAVE[m].cell.c_codes[c] * 0.15f);
			} else {
				live_debug.meas.cell_mV[m][c] = 0;
			}
		}

		for (uint8_t n = 0; n < LIVE_DEBUG_MEAS_NTC; n++) {
			if (m < slaves_found) {
				/* NTC desativado herda o valor do anterior, igual ao CAN */
				uint8_t src = NTC_ResolveSource((uint8_t) (m + 1), (uint8_t) (n + 1));
				live_debug.meas.ntc_c[m][n] = getTemperatureCAN(SLAVE[m].raux.ra_codes[src - 1]);
			} else {
				live_debug.meas.ntc_c[m][n] = 0.0f;
			}
		}

		/* diagnostico por slave: die, PEC e open-wire. IC[] e nao SLAVE[]:
		 * os flags de PEC e o diag_result sao escritos no IC[] depois do snapshot */
		if (m < slaves_found) {
			/* mesma conversao da guarda termica do balanceamento; lixo
			 * (registo 0x8000, PEC mau) fica preso a -999.9 / 999.9 C */
			float die_c = ((IC[m].stata.itmp + 10000) * 0.000150f) / 0.0075f - 273.0f;
			if (!(die_c > -999.9f)) {
				die_c = -999.9f;
			} else if (die_c > 999.9f) {
				die_c = 999.9f;
			}
			live_debug.meas.die_dC[m] = (int16_t) (die_c * 10.0f);

			live_debug.meas.pec_flags[m] = (uint8_t) (((IC[m].cccrc.cell_pec != 0) ? 0x01U : 0U)
					| ((IC[m].cccrc.acell_pec != 0) ? 0x02U : 0U)
					| ((IC[m].cccrc.aux_pec != 0) ? 0x04U : 0U)
					| ((IC[m].cccrc.raux_pec != 0) ? 0x08U : 0U)
					| ((IC[m].cccrc.stat_pec != 0) ? 0x10U : 0U));

			uint16_t ow_cell = 0U;
			for (uint8_t c = 0; c < LIVE_DEBUG_MEAS_CELLS; c++) {
				if (IC[m].diag_result.cell_ow[c] != 0) {
					ow_cell |= (uint16_t) (1U << c);
				}
			}
			live_debug.meas.ow_cell_mask[m] = ow_cell;

			uint8_t ow_ntc = 0U;
			for (uint8_t n = 0; n < LIVE_DEBUG_MEAS_NTC; n++) {
				if (IC[m].diag_result.aux_ow[n] != 0) {
					ow_ntc |= (uint8_t) (1U << n);
				}
			}
			live_debug.meas.ow_ntc_mask[m] = ow_ntc;
		} else {
			live_debug.meas.die_dC[m]       = 0;
			live_debug.meas.pec_flags[m]    = 0U;
			live_debug.meas.ow_cell_mask[m] = 0U;
			live_debug.meas.ow_ntc_mask[m]  = 0U;
		}
	}

	/* precharge + contactor feedbacks */
	live_debug.contactors.precharge_state      = Precharge_GetState();
	live_debug.contactors.vcu_request          = Precharge_GetVcuRequest();
	live_debug.contactors.vcu_request_age_ms   = Precharge_GetVcuRequestAgeMs();
	live_debug.contactors.hv_on_mismatch_count = Precharge_GetMismatchCount();
	live_debug.contactors.sdc_closed = (HAL_GPIO_ReadPin(MCU_SDC_FB_GPIO_Port, MCU_SDC_FB_Pin) == GPIO_PIN_SET) ? 1U : 0U;
	live_debug.contactors.air_pos   = read_contactor_state(MCU_AIR_positivo_FB_GPIO_Port, MCU_AIR_positivo_FB_Pin);
	live_debug.contactors.air_neg   = read_contactor_state(MCU_AIR_negativo_FB_GPIO_Port, MCU_AIR_negativo_FB_Pin);
	live_debug.contactors.precharge = read_contactor_state(MCU_PRE_FB_GPIO_Port, MCU_PRE_FB_Pin);
	live_debug.contactors.discharge = read_contactor_state(MCU_DISCH_FB_GPIO_Port, MCU_DISCH_FB_Pin);

	/* board level : fans + MCU analog */
	live_debug.board.fan_pwm = Get_Fan_PWM();

	const AnalogReadings_t *analog = AnalogReadings_Get();
	if (analog != NULL) {
		live_debug.board.mcu_temp_c         = analog->mcu_temp_c;
		live_debug.board.vdda               = analog->vdda;
		live_debug.board.ams_master_current = analog->ams_master_current;
	}

	/* CAN housekeeping */
	live_debug.can.can1_started        = CAN_IsStarted(&hcan1);
	live_debug.can.can2_started        = CAN_IsStarted(&hcan2);
	live_debug.can.can1_tx_queue_depth = CanTx_GetQueueDepth(&hcan1);
	live_debug.can.can2_tx_queue_depth = CanTx_GetQueueDepth(&hcan2);

	/* CAN status + erros por barramento */
	live_debug.can.can1_state    = CAN_GetState(&hcan1);
	live_debug.can.can2_state    = CAN_GetState(&hcan2);
	live_debug.can.can1_bus_off  = CAN_GetBusOff(&hcan1);
	live_debug.can.can2_bus_off  = CAN_GetBusOff(&hcan2);
	live_debug.can.can1_hw_error = CAN_GetHwError(&hcan1);
	live_debug.can.can2_hw_error = CAN_GetHwError(&hcan2);
	live_debug.can.can1_tx_error_counter = CAN_GetTEC(&hcan1);
	live_debug.can.can2_tx_error_counter = CAN_GetTEC(&hcan2);
	live_debug.can.can1_rx_error_counter = CAN_GetREC(&hcan1);
	live_debug.can.can2_rx_error_counter = CAN_GetREC(&hcan2);
	live_debug.can.can1_last_error_code  = CAN_GetLEC(&hcan1);
	live_debug.can.can2_last_error_code  = CAN_GetLEC(&hcan2);

	/* cell balancing */
	static const char *bal_stage_names[] = { "ROUGH", "FINE", "END" };
	static const char *bal_phase_names[] = { "INIT", "APPLY", "ON_TIME", "STOP_DISCHARGE", "SETTLE", "START_AVG", "WAIT_AVG", "READ_AVG", "COMPUTE", "OW" };

	live_debug.balancing.active        = (AMS_State == BALANCING) ? 1U : 0U;
	live_debug.balancing.target_min_mV = global_min_mV;

	uint8_t stage_idx = (uint8_t) balanceStage;
	live_debug.balancing.stage_name = (stage_idx < 3U) ? bal_stage_names[stage_idx] : "?";

	uint8_t phase_idx = Balancing_GetPhase();
	live_debug.balancing.phase_name = (phase_idx < 10U) ? bal_phase_names[phase_idx] : "?";

	uint8_t  total_on     = 0U;
	uint16_t bal_vmax     = 0U;
	uint16_t bal_vmin     = 0xFFFFU;
	uint16_t worst_delta  = 0U;
	uint8_t  worst_slave  = 0U;
	uint8_t  worst_cell   = 0U;

	for (uint8_t m = 0; m < LIVE_DEBUG_BAL_MAX_IC; m++) {

		if (m >= slaves_found) {
			live_debug.balancing.dcc_mask_per_ic[m] = 0U;
			live_debug.balancing.cells_per_ic[m]    = 0U;
			continue;
		}

		/* mascara calculada no ciclo (a mesma da 0x706). O registo DCC em si
		 * passa ~10% do ciclo a 0 (settle/medicao/OW) e enganava a amostra */
		uint16_t mask = (uint16_t) (Balancing_GetMask(m) & 0x0FFFU);
		live_debug.balancing.dcc_mask_per_ic[m] = mask;

		uint8_t on_count = 0U;
		for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
			if ((mask >> i) & 0x01U) {
				on_count++;
			}

			uint16_t mV = cell_code_to_mV(IC[m].cell.c_codes[i]);

			/* janela de plausibilidade: ignorar canais abertos/lixo */
			if ((mV < 2500U) || (mV > 4500U)) {
				continue;
			}

			if (mV > bal_vmax) {
				bal_vmax = mV;
			}
			if (mV < bal_vmin) {
				bal_vmin = mV;
			}

			if ((global_min_mV != 0U) && (global_min_mV != 0xFFFFU) && (mV > global_min_mV)) {
				uint16_t delta = (uint16_t) (mV - global_min_mV);
				if (delta > worst_delta) {
					worst_delta = delta;
					worst_slave = m + 1U;
					worst_cell  = i + 1U;
				}
			}
		}

		live_debug.balancing.cells_per_ic[m] = on_count;
		total_on += on_count;
	}

	if (bal_vmin == 0xFFFFU) {
		bal_vmin = 0U;
		bal_vmax = 0U;
	}

	live_debug.balancing.cells_discharging = total_on;
	live_debug.balancing.pack_vmax_mV      = bal_vmax;
	live_debug.balancing.pack_vmin_mV      = bal_vmin;
	live_debug.balancing.pack_delta_mV     = (uint16_t) (bal_vmax - bal_vmin);
	live_debug.balancing.worst_delta_mV    = worst_delta;
	live_debug.balancing.worst_slave       = worst_slave;
	live_debug.balancing.worst_cell        = worst_cell;

	live_debug.json.dma_errors   = uart2DmaErrors;
	live_debug.json.dma_restarts = uart2DmaRestarts;
	live_debug.json.bt_reboots   = rn4871Reboots;

	LiveDebug_SendJson();
}

/* ===========================================================================
 * JSON pelo UART2 (BT)
 *
 * Uma linha por chamada, so' com o que esta na struct live_debug acima.
 * Tudo inteiro (mV, mA, 0.1 C...): sem %f, que e' lento e pesado no
 * newlib-nano, e o brain_loop tem ~65 ms de WWDG. Estados saem pelo nome.
 * Se a trama nao couber no buffer ou no ring buffer do UART2 e' descartada
 * inteira e conta em json.frames_dropped (nunca sai JSON cortado).
 * =========================================================================== */

/* tipico ~3,1 KB; pior caso absoluto (64 faults, todos os campos no maximo
 * de digitos) ~5,9 KB -> cabe sempre. A 115200 baud 5,9 KB demoram 0,5 s */
#define LIVE_DEBUG_JSON_BUF 6144

static char json_buf[LIVE_DEBUG_JSON_BUF];
static int  json_len;   /* -1 = a trama nao coube no buffer */

static void j(const char *fmt, ...) {

	if (json_len < 0) {
		return;
	}

	int room = (int) sizeof(json_buf) - json_len;

	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(&json_buf[json_len], (size_t) room, fmt, ap);
	va_end(ap);

	json_len = ((n < 0) || (n >= room)) ? -1 : (json_len + n);
}

/* float -> inteiro (v * scale) arredondado; NaN/Inf/lixo -> 0 */
static long fx(float v, float scale) {
	float s = v * scale;

	if (!((s > -2.0e9f) && (s < 2.0e9f))) {
		return 0;
	}

	return (long) (s + ((s >= 0.0f) ? 0.5f : -0.5f));
}

static const char* ams_state_name(AMSStates_t s) {
	static const char *n[] = { "BALANCING", "CHARGING", "IDLE", "ONMISSION", "STARTUP", "DISCHARGE_TEST", "RESET_ISA", "FAULT" };
	return ((unsigned) s < (sizeof(n) / sizeof(n[0]))) ? n[s] : "?";
}

static const char* precharge_state_name(PrechargeState_t s) {
	static const char *n[] = { "START", "OPEN_ALL", "SWITCH_HVNEG", "WAIT_FOR_AIR_NEG_TO_CLOSE", "CHECKING_AIR_NEG_IS_CLOSED", "SWITCH_PRECHARGE", "WAIT_FOR_PRECHARGE_TO_CLOSE",
			"CHECKING_PRECHARGE_IS_CLOSED", "VERIFY_CURRENT", "VERIFY_BUS_VOLT", "SWITCH_HVPOS", "WAIT_FOR_AIR_POS_TO_CLOSE", "CHECKING_AIR_POS_IS_CLOSED", "TURN_OFF_PRECHARGE",
			"WAIT_FOR_PRECHARGE_TO_OPEN", "CHECKING_PRECHARGE_IS_OPEN", "HV_ON", "WRONG", "KILL", "RX_CAN" };
	return ((unsigned) s < (sizeof(n) / sizeof(n[0]))) ? n[s] : "?";
}

static const char* charger_state_name(uint8_t s) {
	static const char *n[] = { "WAIT_HV", "PRESTART_STOP", "CHARGING", "STOPPING", "DONE" };
	return (s < (sizeof(n) / sizeof(n[0]))) ? n[s] : "?";
}

static const char* adbms_state_name(adbms_result_state s) {
	static const char *n[] = { "END", "ONGOING", "START" };
	return ((unsigned) s < (sizeof(n) / sizeof(n[0]))) ? n[s] : "?";
}

static void j_ivt(const char *key, const live_debug_ivt_t *v) {
	j("\"%s\":{\"current_mA\":%ld,\"voltage_mV\":%ld,\"power_W\":%ld,\"charge_As\":%ld,\"temp_dC\":%ld,\"rx_age_ms\":%lu},", key, (long) v->current_mA, (long) v->u1_voltage_mV,
			(long) v->power_W, (long) v->coulombs_As, (long) v->temp_dC, (unsigned long) v->rx_age_ms);
}

static void j_can(const char *key, uint8_t started, uint8_t state, uint8_t bus_off, uint8_t tec, uint8_t rec, uint8_t lec, uint32_t hw_error, uint16_t tx_queue) {
	j("\"%s\":{\"started\":%u,\"state\":%u,\"bus_off\":%u,\"tx_errors\":%u,\"rx_errors\":%u,\"last_error\":%u,\"hw_error\":%lu,\"tx_queue\":%u},", key, started, state, bus_off, tec, rec, lec,
			(unsigned long) hw_error, tx_queue);
}

/* "key":[a,b,...] de um array por slave (12) */
static void j_slave_u16(const char *key, const uint16_t *v) {
	j("\"%s\":[", key);
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		j("%u%s", v[m], (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "]");
	}
}

static void LiveDebug_SendJson(void) {

	/* reboot do RN4871 em curso: um JSON no meio do "$$$"/"R,1" estragava os
	 * comandos e durante o reboot perdia-se de qualquer forma. Salta este */
	if (RN4871_IsBusy()) {
		return;
	}

	const live_debug_t *d = &live_debug;
	json_len = 0;

	j("{\"uptime_ms\":%lu,", (unsigned long) d->brain.runtime_ms);

	j("\"bms\":{\"state\":\"%s\",\"prev_state\":\"%s\",\"runtime_s\":%lu,\"wwdg_reset\":%u,\"active_faults\":%u},", ams_state_name(d->brain.ams_state),
			ams_state_name(d->brain.ams_previous_state), (unsigned long) d->brain.runtime_s, d->brain.wwdg_reset, d->brain.active_fault_count);

	/* todos os faults ativos pelo nome (a mascara cobre os 64, a lista da struct so 8) */
	j("\"ams_error\":{\"state\":\"%s\",\"active\":%u,\"permanent\":%u,\"fault_count\":%u,\"fault_mask_lo\":%lu,\"fault_mask_hi\":%lu,\"faults\":[", d->ams_error.state_name,
			d->ams_error.active, d->ams_error.permanent, d->ams_error.fault_count, (unsigned long) d->ams_error.fault_mask_lo, (unsigned long) d->ams_error.fault_mask_hi);
	uint8_t first = 1;
	for (uint8_t code = 0; code < FAULT_COUNT; code++) {
		uint32_t word = (code < 32) ? d->ams_error.fault_mask_lo : d->ams_error.fault_mask_hi;
		if ((word & (1UL << (code % 32))) != 0) {
			j("%s\"%s\"", first ? "" : ",", FaultManager_GetName((FaultCode_t) code));
			first = 0;
		}
	}
	j("]},");

	j("\"precharge\":{\"state\":\"%s\",\"vcu_request\":%d,\"vcu_request_age_ms\":%lu,\"hv_on_mismatch_count\":%u,\"sdc_closed\":%u,"
			"\"air_pos\":%u,\"air_neg\":%u,\"precharge_relay\":%u,\"discharge\":%u},", precharge_state_name(d->contactors.precharge_state), d->contactors.vcu_request,
			(unsigned long) d->contactors.vcu_request_age_ms, d->contactors.hv_on_mismatch_count, d->contactors.sdc_closed, d->contactors.air_pos, d->contactors.air_neg,
			d->contactors.precharge, d->contactors.discharge);

	j_ivt("isa_pack", &d->ivt_can1);
	j_ivt("isa_handcart", &d->ivt_can2);

	j("\"soc\":{\"percent_x100\":%ld,\"used_charge_As\":%ld,\"ready\":%u},", fx(d->soc.percent, 100.0f), (long) d->soc.used_charge_As, d->soc.ready);

	j("\"charger\":{\"state\":\"%s\",\"handcart_switch\":%u,\"handcart_age_ms\":%lu,\"status_ok\":%u,\"output_voltage_dV\":%u,\"output_current_dA\":%u,"
			"\"hw_failure\":%u,\"temp_otp\":%u,\"input_voltage_fault\":%u,\"starting_state_fault\":%u,\"comm_timeout\":%u,\"temp_C\":%d,\"requested_current_raw\":%u},",
			charger_state_name(d->charger.state), d->charger.handcart_switch, (unsigned long) d->charger.handcart_age_ms, d->charger.status_ok, d->charger.output_voltage_dV,
			d->charger.output_current_dA, d->charger.hw_failure, d->charger.temp_otp, d->charger.input_voltage_fault, d->charger.starting_state_fault, d->charger.comm_timeout,
			d->charger.temp_C, d->charger.requested_current_raw);

	j("\"pack\":{\"slaves_found\":%u,\"adbms_state\":\"%s\",\"any_pec_error\":%u,\"cell_max_mV\":%u,\"cell_min_mV\":%u,\"temp_max_cC\":%d,\"temp_min_cC\":%d,\"voltage_sum_mV\":%lu},",
			d->adbms.slaves_found, adbms_state_name(d->adbms.adbms_state), d->adbms.any_pec_error, d->adbms.pack_vmax_mV, d->adbms.pack_vmin_mV, d->adbms.pack_tmax_cC,
			d->adbms.pack_tmin_cC, (unsigned long) d->adbms.pack_voltage_sum_mV);

	j("\"balancing\":{\"active\":%u,\"stage\":\"%s\",\"phase\":\"%s\",\"target_mV\":%u,\"cells_discharging\":%u,\"cell_max_mV\":%u,\"cell_min_mV\":%u,\"delta_mV\":%u,"
			"\"worst_delta_mV\":%u,\"worst_slave\":%u,\"worst_cell\":%u,", d->balancing.active, d->balancing.stage_name ? d->balancing.stage_name : "?",
			d->balancing.phase_name ? d->balancing.phase_name : "?", d->balancing.target_min_mV, d->balancing.cells_discharging, d->balancing.pack_vmax_mV,
			d->balancing.pack_vmin_mV, d->balancing.pack_delta_mV, d->balancing.worst_delta_mV, d->balancing.worst_slave, d->balancing.worst_cell);
	j_slave_u16("mask", d->balancing.dcc_mask_per_ic);
	j("},");

	j("\"board\":{\"fan_pwm\":%u,\"mcu_temp_dC\":%ld,\"vdda_mV\":%ld,\"master_current_mA\":%ld},", d->board.fan_pwm, fx(d->board.mcu_temp_c, 10.0f), fx(d->board.vdda, 1000.0f),
			fx(d->board.ams_master_current, 1000.0f));

	j_can("can1", d->can.can1_started, d->can.can1_state, d->can.can1_bus_off, d->can.can1_tx_error_counter, d->can.can1_rx_error_counter, d->can.can1_last_error_code,
			d->can.can1_hw_error, d->can.can1_tx_queue_depth);
	j_can("can2", d->can.can2_started, d->can.can2_state, d->can.can2_bus_off, d->can.can2_tx_error_counter, d->can.can2_rx_error_counter, d->can.can2_last_error_code,
			d->can.can2_hw_error, d->can.can2_tx_queue_depth);

	/* contadores das tramas ANTERIORES (esta ainda nao foi enviada) */
	j("\"json\":{\"sent\":%lu,\"dropped\":%lu,\"last_len\":%u,\"dma_errors\":%lu,\"dma_restarts\":%lu,\"bt_reboots\":%lu},", (unsigned long) d->json.frames_sent,
			(unsigned long) d->json.frames_dropped, d->json.last_len, (unsigned long) d->json.dma_errors, (unsigned long) d->json.dma_restarts,
			(unsigned long) d->json.bt_reboots);

	/* por slave: [slave][celula] e [slave][ntc] */
	j("\"cell_mV\":[");
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		for (uint8_t c = 0; c < LIVE_DEBUG_MEAS_CELLS; c++) {
			j("%s%d", (c == 0) ? "[" : ",", d->meas.cell_mV[m][c]);
		}
		j("]%s", (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "],");
	}

	j("\"ntc_dC\":[");
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		for (uint8_t n = 0; n < LIVE_DEBUG_MEAS_NTC; n++) {
			j("%s%ld", (n == 0) ? "[" : ",", fx(d->meas.ntc_c[m][n], 10.0f));
		}
		j("]%s", (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "],");
	}

	j("\"die_dC\":[");
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		j("%d%s", d->meas.die_dC[m], (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "],");
	}

	j("\"pec_flags\":[");
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		j("%u%s", d->meas.pec_flags[m], (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "],");
	}

	j_slave_u16("ow_cell_mask", d->meas.ow_cell_mask);

	j(",\"ow_ntc_mask\":[");
	for (uint8_t m = 0; m < LIVE_DEBUG_MEAS_IC; m++) {
		j("%u%s", d->meas.ow_ntc_mask[m], (m + 1 < LIVE_DEBUG_MEAS_IC) ? "," : "]");
	}

	j("}\r\n");

	if ((json_len > 0) && (uart2Write(json_buf, json_len) == json_len)) {
		live_debug.json.frames_sent++;
		live_debug.json.last_len = (uint16_t) json_len;
	} else {
		live_debug.json.frames_dropped++;
	}
}
