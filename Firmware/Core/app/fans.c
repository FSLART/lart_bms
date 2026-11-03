/*
 * fans.c
 *
 * Ver fans.h.
 */

#include "fans.h"
#include "bsp.h"

#define FAN_PERIOD_MS 200u

static uint32_t last_ms;

void Fans_Init(void) {

	bms.fan_cells.pwm = 0;
	bms.fan_lv.pwm = 0;
	Bsp_FanInit(bms.config.fan_pwm_hz);
	Bsp_SetFanPwm(0);
	last_ms = 0;
}

/* 0 abaixo de start (com histerese a descer), de pwm_min a pwm_max ate full */
uint8_t Fans_Ramp(const bms_fan_cfg_t *config, int16_t temp_dC, int16_t hysteresis_dC, bool *running) {

	if (!*running && (temp_dC >= config->start_dC)) {
		*running = true;
	} else if (*running && (temp_dC < (config->start_dC - hysteresis_dC))) {
		*running = false;
	}

	if (!*running) {
		return 0;
	}

	uint32_t pct;

	if (temp_dC >= config->full_dC) {
		pct = config->pwm_max_pct;
	} else if (temp_dC <= config->start_dC) {
		pct = config->pwm_min_pct;   // dentro da histerese fica no minimo
	} else {
		pct = config->pwm_min_pct
				+ ((uint32_t) (config->pwm_max_pct - config->pwm_min_pct) * (uint32_t) (temp_dC - config->start_dC)) / (uint32_t) (config->full_dC - config->start_dC);
	}

	return (uint8_t) ((pct * 255u) / 100u);
}

void Fans_Step(uint32_t now_ms) {

	if ((now_ms - last_ms) < FAN_PERIOD_MS) {
		return;
	}
	last_ms = now_ms;

	bms_fan_t *cells_fan = &bms.fan_cells;
	bms_fan_t *lv_fan = &bms.fan_lv;
	const bms_config_t *config = &bms.config;

	// celulas: sem NTC validos, no maximo
	if (bms.cells.temps_valid) {
		cells_fan->control_temp_dC = bms.cells.temp_max_dC;
		cells_fan->pwm = Fans_Ramp(&config->fan_accu_cells, cells_fan->control_temp_dC, config->fan_hysteresis_dC, &cells_fan->running);
	} else {
		cells_fan->running = true;
		cells_fan->pwm = 255;
	}

	// eletronica LV: die dos ADBMS e o proprio MCU
	int16_t lv_temp_dC = bms.board.mcu_temp_dC;

	if (bms.cells.temps_valid && (bms.cells.die_max_dC > lv_temp_dC)) {
		lv_temp_dC = bms.cells.die_max_dC;
	}
	lv_fan->control_temp_dC = lv_temp_dC;
	lv_fan->pwm = Fans_Ramp(&config->fan_accu_lv, lv_temp_dC, config->fan_hysteresis_dC, &lv_fan->running);

	if (cells_fan->bypass_on) {
		cells_fan->pwm = 255;
	}
	if (lv_fan->bypass_on) {
		lv_fan->pwm = 255;
	}

	Bsp_SetFanPwm(cells_fan->pwm);
}
