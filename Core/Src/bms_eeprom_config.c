/*
 * bms_eeprom_config.c
 *
 *  Created on: Nov 2, 2025
 *      Author: jpser
 */

#include "bms_eeprom_config.h"

BmsEepromConfig masterCfg = { 0 };

bool eeprom_write_uint8(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint8_t value) {
	bool ok = false;
	if (!EE24_Write(eeprom_id, eeprom_address, &value, 1, 100)) {
		ok = false;
		RAISE_ERROR(FAULT_EEPROM_WRITE_ERROR);

	} else {
		KILL_ERROR(FAULT_EEPROM_WRITE_ERROR);
		ok = true;
	}
	return ok;
}

bool eeprom_read_uint8(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint8_t *out) {
	bool ok = false;
	if (!EE24_Read(eeprom_id, eeprom_address, out, 1, 100)) {
		ok = false;
		RAISE_ERROR(FAULT_EEPROM_READ_ERROR);

	} else {
		KILL_ERROR(FAULT_EEPROM_READ_ERROR);
		ok = true;
	}
	return ok;
}

bool eeprom_write_uint16(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint16_t value) {
	/* Store as little-endian: low byte first, then high byte */
	uint8_t bytes[2];
	bytes[0] = (uint8_t) (value & 0xFF);
	bytes[1] = (uint8_t) (value >> 8);

	bool ok = false;
	if (!EE24_Write(eeprom_id, eeprom_address, bytes, 2, 100)) {
		ok = false;
		RAISE_ERROR(FAULT_EEPROM_WRITE_ERROR);

	} else {
		KILL_ERROR(FAULT_EEPROM_WRITE_ERROR);
		ok = true;
	}
	return ok;
}

bool eeprom_read_uint16(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint16_t *out) {
	uint8_t bytes[2];
	if (!EE24_Read(eeprom_id, eeprom_address, bytes, 2, 100)) {
		RAISE_ERROR(FAULT_EEPROM_READ_ERROR);
		return false;
	}
	*out = (uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8);
	KILL_ERROR(FAULT_EEPROM_READ_ERROR);
	return true;
}

bool eeprom_write_uint32(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint32_t value) {
	/* Store as little-endian: lowest byte first */
	uint8_t bytes[4];
	bytes[0] = (uint8_t) (value & 0xFF);
	bytes[1] = (uint8_t) ((value >> 8) & 0xFF);
	bytes[2] = (uint8_t) ((value >> 16) & 0xFF);
	bytes[3] = (uint8_t) ((value >> 24) & 0xFF);
	bool ok = false;
	if (!EE24_Write(eeprom_id, eeprom_address, bytes, 4, 100)) {
		ok = false;
		RAISE_ERROR(FAULT_EEPROM_WRITE_ERROR);

	} else {
		KILL_ERROR(FAULT_EEPROM_WRITE_ERROR);
		ok = true;
	}
	return ok;
}

bool eeprom_read_uint32(EE24_HandleTypeDef *eeprom_id, uint32_t eeprom_address, uint32_t *out) {
	uint8_t bytes[4];
	if (!EE24_Read(eeprom_id, eeprom_address, bytes, 4, 100)) {
		RAISE_ERROR(FAULT_EEPROM_READ_ERROR);
		return false;
	}
	*out = (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8) | ((uint32_t) bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
	KILL_ERROR(FAULT_EEPROM_READ_ERROR);
	return true;
}

/* ─────────────────────────────────────────────────────────────────────────────
 PUBLIC FUNCTIONS
 ───────────────────────────────────────────────────────────────────────────── */

void BmsConfig_LoadDefaults(void) {

	/* Topology */
	masterCfg.total_ic = 2;
	masterCfg.cell_count = 12;
	masterCfg.aux_count = 6;
	masterCfg.raux_count = 6;

	/* Flags */
	masterCfg.use_filtered_cells = 0; /* filtered ADC channel not working yet */
	masterCfg.bypass_discharge = 1; /* bypass discharge feedback check      */
	masterCfg.bypass_checks = 0; /* do not bypass all checks             */
	masterCfg.output_mode = 1; /* 1 = DCC (plain on/off balancing)     */
	masterCfg.min_pwm_to_enable = 1;

	/* Voltage thresholds (millivolts) */
	masterCfg.min_cell_mV = 2800; /* protect cells below this voltage     */
	masterCfg.max_cell_mV = 4200; /* sanity check upper limit             */
	masterCfg.ov_threshold_mV = 4100; /* over-voltage trip point              */
	masterCfg.uv_threshold_mV = 2900; /* under-voltage trip point             */
	masterCfg.owc_threshold_mV = 2000; /* cell open-wire detection threshold   */
	masterCfg.min_rough_balancing_mV = 100; /* start rough balancing above 100 mV delta */
	masterCfg.deadband_mV = 5; /* ignore fluctuations smaller than this */
	masterCfg.fullscale_mV = 200; /* delta at which PWM reaches maximum   */

	/* Timing (milliseconds) */
	masterCfg.balance_on_time_ms = 940;
	masterCfg.balance_settle_ms = 10;
	masterCfg.avg_conv_wait_ms = 10;
	masterCfg.owa_threshold_mV = 50000; /* aux open-wire threshold              */
	masterCfg.precharge_lockout_ms = 15000;
	masterCfg.contactor_delay_ms = 250;
	masterCfg.feedback_debounce_ms = 250;
}

/* ─────────────────────────────────────────────────────────────────────────────*/

bool BmsConfig_IsProgrammed(EE24_HandleTypeDef *eeprom_id) {
	uint16_t estang = 0;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_CHECKSUM, &estang)) {
		return false;
	}
	return (estang == EEPROM_CHECKSUM_VALUE);
}

/* ─────────────────────────────────────────────────────────────────────────────*/

bool BmsConfig_Write(EE24_HandleTypeDef *eeprom_id) {

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 0 – Header + topology */
	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_CHECKSUM, EEPROM_CHECKSUM_VALUE))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_VERSION, EEPROM_CONFIG_VERSION))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_TOTAL_IC, masterCfg.total_ic))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_CELL_COUNT, masterCfg.cell_count))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_AUX_COUNT, masterCfg.aux_count))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_RAUX_COUNT, masterCfg.raux_count))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_USE_FILTERED_CELLS, masterCfg.use_filtered_cells))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_BYPASS_DISCHARGE, masterCfg.bypass_discharge))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_BYPASS_CHECKS, masterCfg.bypass_checks))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_OUTPUT_MODE, masterCfg.output_mode))
		return false;

	if (!eeprom_write_uint8(eeprom_id, EEPROM_ADDRESS_MIN_PWM_TO_ENABLE, masterCfg.min_pwm_to_enable))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 1 – Voltage thresholds */
	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_MIN_CELL_MV, masterCfg.min_cell_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_MAX_CELL_MV, masterCfg.max_cell_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_OV_THRESHOLD_MV, masterCfg.ov_threshold_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_UV_THRESHOLD_MV, masterCfg.uv_threshold_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_OWC_THRESHOLD_MV, masterCfg.owc_threshold_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_MIN_ROUGH_BAL_MV, masterCfg.min_rough_balancing_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_DEADBAND_MV, masterCfg.deadband_mV))
		return false;

	if (!eeprom_write_uint16(eeprom_id, EEPROM_ADDRESS_FULLSCALE_MV, masterCfg.fullscale_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 2 – Timing + OWA */
	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_BALANCE_ON_TIME_MS, masterCfg.balance_on_time_ms))
		return false;

	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_BALANCE_SETTLE_MS, masterCfg.balance_settle_ms))
		return false;

	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_AVG_CONV_WAIT_MS, masterCfg.avg_conv_wait_ms))
		return false;

	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_OWA_THRESHOLD_MV, masterCfg.owa_threshold_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 3 – More timing */
	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_PRECHARGE_LOCKOUT_MS, masterCfg.precharge_lockout_ms))
		return false;

	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_CONTACTOR_DELAY_MS, masterCfg.contactor_delay_ms))
		return false;

	if (!eeprom_write_uint32(eeprom_id, EEPROM_ADDRESS_FEEDBACK_DEBOUNCE_MS, masterCfg.feedback_debounce_ms))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	return true;
}

/* ─────────────────────────────────────────────────────────────────────────────*/

bool BmsConfig_Read(EE24_HandleTypeDef *eeprom_id) {
	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 0 – Header + topology */
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_TOTAL_IC, &masterCfg.total_ic))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_CELL_COUNT, &masterCfg.cell_count))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_AUX_COUNT, &masterCfg.aux_count))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_RAUX_COUNT, &masterCfg.raux_count))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_USE_FILTERED_CELLS, &masterCfg.use_filtered_cells))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_BYPASS_DISCHARGE, &masterCfg.bypass_discharge))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_BYPASS_CHECKS, &masterCfg.bypass_checks))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_OUTPUT_MODE, &masterCfg.output_mode))
		return false;
	if (!eeprom_read_uint8(eeprom_id, EEPROM_ADDRESS_MIN_PWM_TO_ENABLE, &masterCfg.min_pwm_to_enable))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 1 – Voltage thresholds */
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_MIN_CELL_MV, &masterCfg.min_cell_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_MAX_CELL_MV, &masterCfg.max_cell_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_OV_THRESHOLD_MV, &masterCfg.ov_threshold_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_UV_THRESHOLD_MV, &masterCfg.uv_threshold_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_OWC_THRESHOLD_MV, &masterCfg.owc_threshold_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_MIN_ROUGH_BAL_MV, &masterCfg.min_rough_balancing_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_DEADBAND_MV, &masterCfg.deadband_mV))
		return false;
	if (!eeprom_read_uint16(eeprom_id, EEPROM_ADDRESS_FULLSCALE_MV, &masterCfg.fullscale_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 2 – Timing + OWA */
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_BALANCE_ON_TIME_MS, &masterCfg.balance_on_time_ms))
		return false;
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_BALANCE_SETTLE_MS, &masterCfg.balance_settle_ms))
		return false;
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_AVG_CONV_WAIT_MS, &masterCfg.avg_conv_wait_ms))
		return false;
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_OWA_THRESHOLD_MV, &masterCfg.owa_threshold_mV))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Page 3 – More timing */
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_PRECHARGE_LOCKOUT_MS, &masterCfg.precharge_lockout_ms))
		return false;
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_CONTACTOR_DELAY_MS, &masterCfg.contactor_delay_ms))
		return false;
	if (!eeprom_read_uint32(eeprom_id, EEPROM_ADDRESS_FEEDBACK_DEBOUNCE_MS, &masterCfg.feedback_debounce_ms))
		return false;

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	return true;
}

/* ─────────────────────────────────────────────────────────────────────────────*/

void BmsConfig_ToBalanceConfig(balance_config_t *balancing_config) {

	balancing_config->min_rough_balacing_mV = masterCfg.min_rough_balancing_mV;
	balancing_config->deadband_mV = masterCfg.deadband_mV;
	balancing_config->min_cell_mV = masterCfg.min_cell_mV;
	balancing_config->max_cell_mV = masterCfg.max_cell_mV;
	balancing_config->use_filtered_cells = (masterCfg.use_filtered_cells != 0);
}

bool BmsConfig_Init(EE24_HandleTypeDef *eeprom_id, I2C_HandleTypeDef *hi2c) {
	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Step 1: test the I2C connection to the EEPROM chip */
	if (!EE24_Init(eeprom_id, hi2c, EE24_ADDRESS_DEFAULT)) {
		RAISE_ERROR(FAULT_EEPROM_READ_ERROR);
		return false; /* chip not responding - check wiring / I2C address */
	}

	BmsConfig_LoadDefaults();

	BmsConfig_Write(eeprom_id);

	/* Step 2: check if the chip has been programmed before */
	if (!BmsConfig_IsProgrammed(eeprom_id)) {

		/* Step 3: chip is blank - write the defaults so it is ready to read back */
		BmsConfig_LoadDefaults();

		if (!BmsConfig_Write(eeprom_id)) {
			RAISE_ERROR(FAULT_EEPROM_WRITE_ERROR);
			return false; /* write failed */
		}

		/* verify if the write actually stuck */
		if (!BmsConfig_IsProgrammed(eeprom_id)) {
			RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
			return false;
		}
	}

	//update watcdog
	HAL_WWDG_Refresh(&hwwdg);

	/* Step 4: read the stored config from EEPROM into masterCfg*/
	if (!BmsConfig_Read(eeprom_id)) {
		return false; /* read failed */
	}

	/* Step 5: apply the cinfigs throut the codet */
	//BmsConfig_ToBalanceConfig(cfg, eeprom_id);
	return true;
}

void BmsConfig_DumpEEPROM(EE24_HandleTypeDef *eeprom_id){

	/* 24FC08 = 8Kbit = 1024 bytes */
	const int eeprom_total_bytes = 1024;
	const int bytes_per_row = 16;

	uint8_t row[bytes_per_row];

	printfUI("\n\r===== EEPROM RAW HEX DUMP (24FC08, 1024 bytes) =====\n\r");
	printfUI("       00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F\n\r");
	printfUI("       -----------------------------------------------\n\r");

	for (uint32_t addr = 0; addr < eeprom_total_bytes; addr += bytes_per_row) {
		//update watcdog
		HAL_WWDG_Refresh(&hwwdg);

		/* Read one row from EEPROM */
		if (!EE24_Read(eeprom_id, addr, row, bytes_per_row, 200)) {
			printfUI("0x%03X  [READ ERROR]\n\r", (unsigned int) addr);
			continue;
		}

		/* Print address */
		printfUI("0x%03X  ", (unsigned int) addr);

		/* Print hex bytes, with a gap in the middle for readability */
		for (uint8_t i = 0; i < bytes_per_row; i++) {
			printfUI("%02X ", row[i]);
			if (i == 7) {
				printfUI(" "); /* extra space between the two groups of 8 */
			}
		}

		/* Print ASCII representation */
		printfUI(" |");
		for (uint8_t i = 0; i < bytes_per_row; i++) {
			/* Print the character if printable, otherwise a dot */
			if (row[i] >= 0x20 && row[i] <= 0x7E) {
				printfUI("%c", row[i]);
			} else {
				printfUI(".");
			}
		}
		printfUI("|\n\r");
	}

	printfUI("===== END OF DUMP =====\n\r");

}

void BmsConfig_DumpUART(void) {
	printfUI("===== EEPROM CONFIG DUMP =====\n\r");

	/* Topology */
	printfUI("[TOPOLOGY]\n\r");
	printfUI("  total_ic          : %u\n\r", masterCfg.total_ic);
	printfUI("  cell_count        : %u\n\r", masterCfg.cell_count);
	printfUI("  aux_count         : %u\n\r", masterCfg.aux_count);
	printfUI("  raux_count        : %u\n\r", masterCfg.raux_count);

	/* Flags */
	printfUI("[FLAGS]\n\r");
	printfUI("  use_filtered_cells: %u\n\r", masterCfg.use_filtered_cells);
	printfUI("  bypass_discharge  : %u\n\r", masterCfg.bypass_discharge);
	printfUI("  bypass_checks     : %u\n\r", masterCfg.bypass_checks);
	printfUI("  output_mode       : %u  (0=PWM 1=DCC)\n\r", masterCfg.output_mode);
	printfUI("  min_pwm_to_enable : %u\n\r", masterCfg.min_pwm_to_enable);

	/* Voltage thresholds */
	printfUI("[VOLTAGE THRESHOLDS] (mV)\n\r");
	printfUI("  min_cell_mV            : %u\n\r", masterCfg.min_cell_mV);
	printfUI("  max_cell_mV            : %u\n\r", masterCfg.max_cell_mV);
	printfUI("  ov_threshold_mV        : %u\n\r", masterCfg.ov_threshold_mV);
	printfUI("  uv_threshold_mV        : %u\n\r", masterCfg.uv_threshold_mV);
	printfUI("  owc_threshold_mV       : %u\n\r", masterCfg.owc_threshold_mV);
	printfUI("  min_rough_balancing_mV : %u\n\r", masterCfg.min_rough_balancing_mV);
	printfUI("  deadband_mV            : %u\n\r", masterCfg.deadband_mV);
	printfUI("  fullscale_mV           : %u\n\r", masterCfg.fullscale_mV);

	/* Timing */
	printfUI("[TIMING] (ms)\n\r");
	printfUI("  balance_on_time_ms   : %lu\n\r", masterCfg.balance_on_time_ms);
	printfUI("  balance_settle_ms    : %lu\n\r", masterCfg.balance_settle_ms);
	printfUI("  avg_conv_wait_ms     : %lu\n\r", masterCfg.avg_conv_wait_ms);
	printfUI("  owa_threshold_mV     : %lu\n\r", masterCfg.owa_threshold_mV);
	printfUI("  precharge_lockout_ms : %lu\n\r", masterCfg.precharge_lockout_ms);
	printfUI("  contactor_delay_ms   : %lu\n\r", masterCfg.contactor_delay_ms);
	printfUI("  feedback_debounce_ms : %lu\n\r", masterCfg.feedback_debounce_ms);

	printfUI("==============================\n\r");
}
