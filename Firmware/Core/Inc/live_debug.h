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

	/* status do periferico */
	uint8_t  can1_state;      // HAL_CAN_StateTypeDef
	uint8_t  can2_state;
	uint8_t  can1_bus_off;    // 1 = bus-off (parou de vez ate recovery)
	uint8_t  can2_bus_off;

	/* erros */
	uint32_t can1_hw_error;          // HAL_CAN_GetError() bitmask
	uint32_t can2_hw_error;
	uint8_t  can1_tx_error_counter;  // erros de TX; sobe = TX sem ACK. >=256 -> bus-off
	uint8_t  can2_tx_error_counter;
	uint8_t  can1_rx_error_counter;  // erros de RX
	uint8_t  can2_rx_error_counter;
	uint8_t  can1_last_error_code;   // 0 ok, 1 stuff, 2 form, 3 ACK, 4 bit-rec, 5 bit-dom, 6 CRC
	uint8_t  can2_last_error_code;
} live_debug_can_t;

/* --- cell balancing : whole snapshot from ac_codes + tx_cfgb.dcc --- */
#define LIVE_DEBUG_BAL_MAX_IC 12

typedef struct {
	uint8_t     active;                 // 1 = AMS_State == BALANCING
	const char *stage_name;             // "ROUGH" / "FINE" / "END"
	const char *phase_name;             // "INIT", "ON_TIME", "READ_AVG", ...
	uint16_t    target_min_mV;          // frozen global minimum (balance target)
	uint8_t     cells_discharging;      // total cells with DCC on, whole pack
	uint8_t     cells_per_ic[LIVE_DEBUG_BAL_MAX_IC];     // DCC count per slave
	uint16_t    dcc_mask_per_ic[LIVE_DEBUG_BAL_MAX_IC];  // raw DCC bitmask per slave
	uint16_t    pack_vmax_mV;           // highest valid cell (avg registers)
	uint16_t    pack_vmin_mV;           // lowest valid cell (avg registers)
	uint16_t    pack_delta_mV;          // vmax - vmin
	uint16_t    worst_delta_mV;         // biggest (cell - target), 0 = converged
	uint8_t     worst_slave;            // 1-based, 0 = none
	uint8_t     worst_cell;             // 1-based, 0 = none
} live_debug_balancing_t;

/* --- estado da linha AMS_ERROR + o que a disparou --------------------
 * state_name diz logo em que pe esta a linha; faults[] lista pelo nome os
 * faults ativos no registry, para identificar a causa sem decifrar mascaras */
#define LIVE_DEBUG_FAULT_LIST 8

typedef struct {
	uint8_t     active;        // 1 = linha AMS_ERROR em erro
	uint8_t     permanent;     // 1 = latch permanente (so power cycle limpa)
	const char *state_name;    // "OK" / "ERROR" / "ERROR_PERMANENT"
	uint8_t     fault_count;   // quantos faults ativos no fault manager
	uint32_t    fault_mask_lo; // mascara de faults ativos, bits 0..31
	uint32_t    fault_mask_hi; // mascara de faults ativos, bits 32..63
	const char *faults[LIVE_DEBUG_FAULT_LIST];  // nomes dos faults ativos
} live_debug_ams_error_t;

/* --- todas as celulas e todos os NTC, por slave (para Live Expressions) --- */
#define LIVE_DEBUG_MEAS_IC     12
#define LIVE_DEBUG_MEAS_CELLS  12
#define LIVE_DEBUG_MEAS_NTC    6

typedef struct {
	/* tensao de cada celula em mV [slave][celula]. Negativo = sentinela de
	 * reset/open-wire (0x8000), util para debug */
	int16_t cell_mV[LIVE_DEBUG_MEAS_IC][LIVE_DEBUG_MEAS_CELLS];
	/* temperatura de cada NTC em C [slave][ntc]. ~2 = NTC aberto, ~150 = curto */
	float   ntc_c[LIVE_DEBUG_MEAS_IC][LIVE_DEBUG_MEAS_NTC];
} live_debug_meas_t;

typedef struct {
	live_debug_brain_t      brain;
	live_debug_ams_error_t  ams_error;
	live_debug_adbms_t      adbms;
	live_debug_ivt_t        ivt_can1;   // ISA do pack (CAN1) - alimenta SOC
	live_debug_ivt_t        ivt_can2;   // ISA do handcart (CAN2) - carga/display
	live_debug_contactors_t contactors;
	live_debug_board_t      board;
	live_debug_can_t        can;
	live_debug_balancing_t  balancing;
	live_debug_meas_t       meas;
} live_debug_t;

extern live_debug_t live_debug;

/* Refresh the whole snapshot from live firmware state. Call periodically. */
void LiveDebug_Update(void);

#endif /* INC_LIVE_DEBUG_H_ */
