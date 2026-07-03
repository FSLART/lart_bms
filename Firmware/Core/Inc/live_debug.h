/*
 * live_debug.h
 *
 *  Live debug snapshot: one global tree of the most important runtime values,
 *  copied from existing structs/getters. Add `live_debug` to the STM32CubeIDE
 *  Live Expressions window to watch the whole system in one entry.
 *
 *  All values are read-only mirrors. Nothing here drives the firmware.
 */

#ifndef INC_LIVE_DEBUG_H_
#define INC_LIVE_DEBUG_H_

#include <stdint.h>
#include <stdbool.h>

#include "main.h"         // HAL types (TIM_HandleTypeDef, GPIO_TypeDef, ...)
#include "brain.h"        // AMSStates_t
#include "precharge.h"    // PrechargeState_t
#include "adbms_main.h"   // adbms_result_state

/* --- brain.c : AMS master state machine + housekeeping --- */
typedef struct {
	AMSStates_t ams_state;           // AMS_State
	AMSStates_t ams_previous_state;  // AMS_Previous_State
	uint32_t    runtime_s;           // getRuntimeSeconds()
	uint8_t     active_fault_count;  // count_active_faults()
	uint32_t    watchdog_flag;       // RCC->CSR captured at boot
} live_debug_brain_t;

/* --- adbms driver : slaves + pack overalls --- */
typedef struct {
	uint8_t             slaves_found;
	adbms_result_state  adbms_state;      // adbms_current_state
	uint8_t             any_pec_error;    // anyPecError
	uint16_t            pack_vmax_mV;     // highest cell in pack
	uint16_t            pack_vmin_mV;     // lowest cell in pack
	int16_t             pack_tmax_cC;     // highest NTC, 0.01 C units
	int16_t             pack_tmin_cC;     // lowest NTC, 0.01 C units
	uint32_t            pack_voltage_sum_mV;  // sum of every cell
} live_debug_adbms_t;

/* --- ISA IVT-S current sensor --- */
typedef struct {
	int32_t current_mA;      // ivt.iBatt
	int32_t u1_voltage_mV;   // ivt.vBatt  (U1)
	int32_t power_W;         // ivt.power
	int32_t coulombs_As;     // ivt.coulombs_As
	int32_t temp_dC;         // ivt.temp, 0.1 C units
} live_debug_ivt_t;

/* --- precharge + contactor feedbacks (1 = closed/LOW feedback) --- */
typedef struct {
	PrechargeState_t precharge_state;   // Precharge_GetState()
	uint8_t air_pos;
	uint8_t air_neg;
	uint8_t precharge;
	uint8_t discharge;
} live_debug_contactors_t;

/* --- board level : fans + MCU analog --- */
typedef struct {
	uint8_t fan_pwm;             // Get_Fan_PWM(), 0..255
	float   mcu_temp_c;          // AnalogReadings_Get()->mcu_temp_c
	float   vdda;                // AnalogReadings_Get()->vdda
	float   ams_master_current;  // AnalogReadings_Get()->ams_master_current
} live_debug_board_t;

/* --- CAN housekeeping --- */
typedef struct {
	uint8_t  can1_started;
	uint8_t  can2_started;
	uint16_t can1_tx_queue_depth;
	uint16_t can2_tx_queue_depth;
} live_debug_can_t;

typedef struct {
	live_debug_brain_t      brain;
	live_debug_adbms_t      adbms;
	live_debug_ivt_t        ivt;
	live_debug_contactors_t contactors;
	live_debug_board_t      board;
	live_debug_can_t        can;
} live_debug_t;

extern live_debug_t live_debug;

/* Refresh the whole snapshot from live firmware state. Call periodically. */
void LiveDebug_Update(void);

#endif /* INC_LIVE_DEBUG_H_ */
