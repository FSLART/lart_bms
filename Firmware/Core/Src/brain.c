/*
 * brain.c
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 */

#include "main.h"
#include "brain.h"
#include "adBms_Application.h"

#include "math.h"
#include "stdio.h"
#include <stdbool.h>

/* ================== LOCAL DEFINES / TYPES ================== */

// Snapshot global do BMS (ver brain.h); arranca tudo a zero
bms_t bms;

/* Config ate haver EEPROM: valores da pagina Configuracao da UI.
 * Os marcados "por definir" nao existem na UI */
static const bms_config_t config_defaults = {
	.loaded = false,
	.version = 1,
	.slaves = 12,
	.cells_per_slave = 12,
	.ntcs_per_slave = 6,
	.ntc_disabled_mask = { [2] = 0x000C },   // slave 3: NTC 3 e 4 (harness partido)
	.cell_min_mV = 2500,
	.cell_low_warn_mV = 2800,
	.cell_high_warn_mV = 4150,
	.cell_max_mV = 4200,
	.cell_open_wire_mV = 2300,
	.temp_min_dC = -200,        // por definir
	.temp_low_warn_dC = -100,   // por definir
	.temp_high_warn_dC = 500,
	.temp_max_dC = 600,         // FS 2027 EV 5.8.7: maximo 60 C
	.charge_voltage_dV = 6000,
	.charge_current_dA = 60,
	.charge_open_mA = 500,
	.bal_min_cell_mV = 3000,
	.bal_max_cell_mV = 4250,
	.bal_delta_mV = 20,
	.bal_die_stop_dC = 850,
	.bal_pulse_ms = 1500,
	.fan_accu_cells = { .start_dC = 300, .full_dC = 550, .pwm_min_pct = 20, .pwm_max_pct = 100 },
	.fan_accu_lv = { .start_dC = 400, .full_dC = 700, .pwm_min_pct = 20, .pwm_max_pct = 100 },   // por definir
	.precharge_timeout_ms = 10000,
	.precharge_bus_percentage = 95,
	.precharge_air_wait_ms = 500,
	.capacity_mAh = 13500,
	.soc_fit_degree = 4,
	.soc_curve_pct = { 0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 },
	.soc_curve_mV = { 2500, 2806, 3010, 3248, 3384, 3469, 3537, 3622, 3741, 3894, 4064, 4200 },
	.temp_compensation_temp_C = { -20, -10, 0, 10, 20, 30, 40, 50, 60, 70 },
	.temp_compensation_resistance_mOhm = { 176, 158, 140, 122, 104, 86, 68, 50, 32, 20 },   // 0,1 mOhm
	// o que a FS 2027 EV 5.8 obriga a abrir o SDC
	.ams_error_enable = {
		.cell_overvoltage = true,
		.cell_undervoltage = true,
		.cell_open_wire = true,
		.overtemperature = true,
		.undertemperature = true,
		.ntc_open_wire = true,
		.slave_missing = true,
		.adbms_timeout = true,
		.overcurrent = true,
		.isa_pack_timeout = true,
	},
};

//Heatbeat trigger flag
bool toggleHeartbeat = false;

// Brain INITILIAZATION
void brain_start(void) {

	// Watchdog Fault Acknoledge
	if (watchdog_flag & RCC_CSR_WWDGRSTF) {

	}

	bms.config = config_defaults;   // TODO: ler da EEPROM

	bool adbms_cfg_ok = ADBMS_Init();
	bms.faults.config_invalid = !bms.config.loaded || !adbms_cfg_ok;
}

void brain_loop(void) {

	uint32_t start_us = adBmsMicros();

	ADBMS_Run();

	// orcamento do WWDG: ~65 ms por brain_loop
	bms.system.uptime_ms = HAL_GetTick();
	bms.system.loop_us = adBmsMicros() - start_us;
	if (bms.system.loop_us > bms.system.loop_max_us) {
		bms.system.loop_max_us = bms.system.loop_us;
	}
}

// Timer interrupt callback: o HAL chama isto a partir do TIMx_IRQHandler do CubeMX
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {

	if (htim->Instance == TIM6) {
		ADBMS_Tick();   // tick de 200 ms da cadeia ADBMS
	}
}

// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

	heartbeat();
}

void heartbeat(void) {

	static uint16_t ticks = 0;
	static uint16_t period = 700;   // start period

	//toggleHeartbeat = false;
	if (++ticks >= period) {

		ticks = 0;

		toggleHeartbeat = true;

		// halve the period
		period >>= 1;

		// reset when reaching 0 or 1
		if (period < 1) {
			period = 1000;
		}
	}

}
