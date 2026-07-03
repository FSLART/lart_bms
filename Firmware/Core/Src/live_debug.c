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

	/* adbms driver + pack overalls */
	live_debug.adbms.slaves_found        = slaves_found;
	live_debug.adbms.adbms_state         = adbms_current_state;
	live_debug.adbms.any_pec_error       = anyPecError;
	live_debug.adbms.pack_vmax_mV        = g_pack_vmax_mV;
	live_debug.adbms.pack_vmin_mV        = g_pack_vmin_mV;
	live_debug.adbms.pack_tmax_cC        = g_pack_tmax_cC;
	live_debug.adbms.pack_tmin_cC        = g_pack_tmin_cC;
	live_debug.adbms.pack_voltage_sum_mV = g_pack_voltage_sum_mV;

	/* ISA IVT-S current sensor */
	live_debug.ivt.current_mA    = IVT_GetCurrent_mA();
	live_debug.ivt.u1_voltage_mV = IVT_GetPackVoltage_mV();
	live_debug.ivt.power_W       = IVT_GetPower_W();
	live_debug.ivt.coulombs_As   = IVT_GetCoulombs_As();
	live_debug.ivt.temp_dC       = IVT_GetTemperature_dC();

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
}
