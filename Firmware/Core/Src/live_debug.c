/*
 * live_debug.c
 *
 *  Fills the live_debug snapshot from existing firmware state. See live_debug.h.
 */

#include "live_debug.h"

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

/* Externs owned by other translation units */
extern uint8_t slaves_found;     // adBms_Application.c
extern uint8_t anyPecError;      // adBms6830GenericType.c

live_debug_t live_debug = { 0 };

void LiveDebug_Update(void) {

	/* brain / AMS state machine */
	live_debug.brain.ams_state          = AMS_State;
	live_debug.brain.ams_previous_state = AMS_Previous_State;
	live_debug.brain.runtime_s          = getRuntimeSeconds();
	live_debug.brain.active_fault_count = count_active_faults();
	live_debug.brain.watchdog_flag      = watchdog_flag;

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

	/* ISA IVT-S do handcart (CAN2) - corrente de carga, separada do pack */
	live_debug.ivt_can2.current_mA    = IVT_GetCurrentCan2_mA();
	live_debug.ivt_can2.u1_voltage_mV = IVT_GetPackVoltageCan2_mV();
	live_debug.ivt_can2.power_W       = IVT_GetPowerCan2_W();
	live_debug.ivt_can2.coulombs_As   = IVT_GetCoulombsCan2_As();
	live_debug.ivt_can2.temp_dC       = IVT_GetTemperatureCan2_dC();

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
				live_debug.meas.ntc_c[m][n] = getTemperatureCAN(SLAVE[m].raux.ra_codes[n]);
			} else {
				live_debug.meas.ntc_c[m][n] = 0.0f;
			}
		}
	}

	/* precharge + contactor feedbacks */
	live_debug.contactors.precharge_state = Precharge_GetState();
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
	static const char *bal_phase_names[] = { "INIT", "APPLY", "ON_TIME", "STOP_DISCHARGE", "SETTLE", "START_AVG", "WAIT_AVG", "READ_AVG", "COMPUTE" };

	live_debug.balancing.active        = (AMS_State == BALANCING) ? 1U : 0U;
	live_debug.balancing.target_min_mV = global_min_mV;

	uint8_t stage_idx = (uint8_t) balanceStage;
	live_debug.balancing.stage_name = (stage_idx < 3U) ? bal_stage_names[stage_idx] : "?";

	uint8_t phase_idx = Balancing_GetPhase();
	live_debug.balancing.phase_name = (phase_idx < 9U) ? bal_phase_names[phase_idx] : "?";

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

		uint16_t mask = (uint16_t) (IC[m].tx_cfgb.dcc & 0x0FFFU);
		live_debug.balancing.dcc_mask_per_ic[m] = mask;

		uint8_t on_count = 0U;
		for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
			if ((mask >> i) & 0x01U) {
				on_count++;
			}

			uint16_t mV = cell_code_to_mV(IC[m].acell.ac_codes[i]);

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
}
