/*
 * bms_config.c
 *
 * Ver bms_config.h. Defaults e tabela de parametros.
 */

#include "bms_config.h"
#include "faults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Config de origem: valores da pagina Configuracao da UI (BMS_UI,
 * bms-settings.js) e do firmware antigo. Os marcados "por definir" nao
 * existem na UI nem tinham valor testado: confirmar no carro */
const bms_config_t config_defaults = {
	.loaded = false,
	.version = CONFIG_SCHEMA_VERSION,
	.sequence = 0,
	// topologia: TEK-26E, 144s3p, 12 slaves de 12 celulas e 6 NTC
	.slaves = 12,
	.cells_per_slave = 12,
	.ntcs_per_slave = 6,
	.cells_parallel = 3,
	.ntc_disabled_mask = { [2] = 0x000C },   // slave 3: NTC 3 e 4 (harness partido)
	// celulas: Molicel P45B
	.cell_min_mV = 2500,
	.cell_low_warn_mV = 2800,
	.cell_high_warn_mV = 4150,
	.cell_max_mV = 4200,
	.cell_open_wire_mV = 2300,
	.temp_min_dC = -200,        // por definir
	.temp_low_warn_dC = -100,   // por definir
	.temp_high_warn_dC = 500,
	.temp_max_dC = 600,         // FS 2027 EV 5.8.4: maximo 60 C
	// cadeia ADBMS
	.adbms_data_timeout_ms = 300,   // FS 2027 T 11.9 / EV 5.8.13: perda de medicao -> SDC em 500 ms
	.adbms_read_retries = 2,
	.adbms_missing_retries = 2,
	.adbms_dead_cycles = 2,
	.adbms_cth = 1,                 // CVT_8_1mV, o default do datasheet
	.ow_min_gain_pct = 70,
	.ow_conv_ms = 20,
	.ntc_beta = 3977,               // Amphenol NKA502C1
	.ntc_r25_ohm = 5000,
	.ntc_r_fixed_ohm = 10000,
	.ntc_aux_match_mV = 20,
	// pack
	.overcurrent_discharge_A = 300,   // por definir: dcl_max_a da UI; confirmar com a datasheet da P45B
	.overcurrent_charge_A = 60,       // por definir
	.overcurrent_ms = 400,            // FS 2027 EV 5.8.10: 500 ms para a corrente
	.pack_voltage_tolerance_pct = 10, // por definir
	.pack_voltage_mismatch_ms = 1000, // por definir
	.isa_pack_timeout_ms = 600,
	.isa_handcart_timeout_ms = 1000,
	.isa_bus_channel = 1,             // por definir: U da ISA do pack depois do AIR+
	.isa_pack_invert = false,
	.isa_handcart_invert = false,
	// carregamento: EV Europe OEM3 650-6, protocolo P1000
	.charge_voltage_dV = 6000,
	.charge_current_dA = 60,
	.charge_open_mA = 500,
	.charge_cut_current_mA = 15000,
	.charge_temp_max_dC = 500,
	.charge_settle_ms = 5000,
	.charge_prestart_ms = 5000,
	.charge_command_ms = 1000,
	.charge_status_timeout_ms = 3000,
	.charge_fault_debounce_ms = 500,
	.charge_bus_tolerance_pct = 20,
	.handcart_timeout_ms = 1000,
	// balanceamento
	.bal_enabled = true,
	.bal_min_cell_mV = 3000,
	.bal_max_cell_mV = 4250,
	.bal_delta_mV = 20,
	.bal_rough_mV = 100,
	.bal_die_stop_dC = 850,
	.bal_die_hold_s = 60,
	// ventoinhas
	.fan_accu_cells = { .start_dC = 300, .full_dC = 550, .pwm_min_pct = 20, .pwm_max_pct = 100 },
	.fan_accu_lv = { .start_dC = 400, .full_dC = 700, .pwm_min_pct = 20, .pwm_max_pct = 100 },   // por definir
	.fan_pwm_hz = 1000,               // ~ o que o TIM12 dava (977 Hz)
	.fan_hysteresis_dC = 50,
	// pre-carga
	.precharge_mode = 1,
	.precharge_timeout_ms = 10000,
	.precharge_bus_percentage = 95,
	.precharge_air_wait_ms = 500,
	.precharge_min_ms = 3000,         // o firmware antigo esperava 3 s fixos
	.precharge_end_current_mA = 0,
	.precharge_lockout_ms = 10000,
	.contactor_debounce_ms = 200,
	.contactor_mismatch_count = 3,
	.contactor_check_mask = 0x07,     // AIR-, PRE e AIR+; o da descarga estava sempre em bypass
	.vcu_timeout_ms = 0,
	// SOC
	.capacity_mAh = 13500,
	.soc_fit_degree = 4,
	.soc_curve_pct = { 0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 },
	.soc_curve_mV = { 2500, 2806, 3010, 3248, 3384, 3469, 3537, 3622, 3741, 3894, 4064, 4200 },
	.soc_rest_current_mA = 1000,
	.soc_rest_s = 1800,
	.soc_low_pct = 5,
	.temp_compensation_temp_C = { -20, -10, 0, 10, 20, 30, 40, 50, 60, 70 },
	.temp_compensation_adjust_mV = { 0 },
	.temp_compensation_resistance_mOhm = { 176, 158, 140, 122, 104, 86, 68, 50, 32, 20 },   // 0,1 mOhm
	// placa
	.board_current_mV_per_A = 264,    // MCS1802GS-05
	.board_current_offset_mA = 0,
	// CAN
	.can1_kbps = 500,
	.can2_kbps = 500,
	.can_master_ms = 100,
	.can_slaves_ms = 250,
	.can2_mirror_charging = true,
	.bootloader_can_mask = 0x03,      // 0x0DC nos dois bus (o bootloader CAN escuta nos dois); 0x601 so no CAN2
	// comunicacao externa
	.bt_name = "LART Accumulator",
	.live_period_ms = 1000,           // RN4871 sem flow control: ~3 kB/s e o maximo seguro
	.live_uart1_ms = 1000,
	.bt_probe_s = 30,
	.bt_reboot_s = 0,
	.bt_pin = 0,                      // sem PIN: qualquer telemovel ligado ao BLE pode mudar a config
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
	.ams_error_min_on_ms = 500,
	.boot_confirm_s = 30,
};

/* ================== TABELA ================== */

#define SIZE_CFG_BOOL   sizeof(bool)
#define SIZE_CFG_U8     1u
#define SIZE_CFG_I8     1u
#define SIZE_CFG_U16    2u
#define SIZE_CFG_I16    2u
#define SIZE_CFG_U32    4u
#define SIZE_CFG_STR    1u
#define SIZE_CFG_FAULTS sizeof(bms_faults_t)

#define FIELD_SIZE(field) sizeof(((bms_config_t *) 0)->field)
#define PARAM(key, field, type, min, max, flags, unit) \
	{ key, unit, (uint16_t) offsetof(bms_config_t, field), type, (uint8_t) (FIELD_SIZE(field) / SIZE_##type), min, max, flags }

#define REBOOT CFG_FLAG_REBOOT
#define HEX    CFG_FLAG_HEX

/* A ORDEM E A DA EEPROM: acrescentar so no fim (ver bms_config.h) */
const cfg_param_t config_params[] = {
	// topologia
	PARAM("slaves", slaves, CFG_U8, 1, BMS_MAX_SLAVES, REBOOT, ""),
	PARAM("cells_per_slave", cells_per_slave, CFG_U8, 1, BMS_MAX_CELLS, REBOOT, ""),
	PARAM("ntcs_per_slave", ntcs_per_slave, CFG_U8, 0, BMS_MAX_NTCS, REBOOT, ""),
	PARAM("cells_parallel", cells_parallel, CFG_U8, 1, 10, 0, ""),
	PARAM("ntc_disabled_mask", ntc_disabled_mask, CFG_U16, 0, 0x3FF, HEX, "bits"),
	// celulas
	PARAM("cell_min_mV", cell_min_mV, CFG_U16, 2000, 4000, 0, "mV"),
	PARAM("cell_low_warn_mV", cell_low_warn_mV, CFG_U16, 2000, 4000, 0, "mV"),
	PARAM("cell_high_warn_mV", cell_high_warn_mV, CFG_U16, 3000, 4500, 0, "mV"),
	PARAM("cell_max_mV", cell_max_mV, CFG_U16, 3000, 4500, 0, "mV"),
	PARAM("cell_open_wire_mV", cell_open_wire_mV, CFG_U16, 500, 3000, 0, "mV"),
	PARAM("temp_min_dC", temp_min_dC, CFG_I16, -400, 400, 0, "0.1C"),
	PARAM("temp_low_warn_dC", temp_low_warn_dC, CFG_I16, -400, 400, 0, "0.1C"),
	PARAM("temp_high_warn_dC", temp_high_warn_dC, CFG_I16, 0, 800, 0, "0.1C"),
	PARAM("temp_max_dC", temp_max_dC, CFG_I16, 0, 600, 0, "0.1C"),
	// cadeia ADBMS
	PARAM("adbms_data_timeout_ms", adbms_data_timeout_ms, CFG_U16, 250, 500, 0, "ms"),
	PARAM("adbms_read_retries", adbms_read_retries, CFG_U8, 0, 5, 0, ""),
	PARAM("adbms_missing_retries", adbms_missing_retries, CFG_U8, 0, 10, 0, ""),
	PARAM("adbms_dead_cycles", adbms_dead_cycles, CFG_U8, 1, 10, 0, ""),
	PARAM("adbms_cth", adbms_cth, CFG_U8, 0, 7, 0, "enum"),
	PARAM("ow_min_gain_pct", ow_min_gain_pct, CFG_U8, 10, 100, 0, "%"),
	PARAM("ow_conv_ms", ow_conv_ms, CFG_U8, 20, 60, 0, "ms"),
	PARAM("ntc_beta", ntc_beta, CFG_U16, 1000, 6000, 0, "K"),
	PARAM("ntc_r25_ohm", ntc_r25_ohm, CFG_U16, 100, 65000, 0, "ohm"),
	PARAM("ntc_r_fixed_ohm", ntc_r_fixed_ohm, CFG_U16, 100, 65000, 0, "ohm"),
	PARAM("ntc_aux_match_mV", ntc_aux_match_mV, CFG_U16, 1, 500, 0, "mV"),
	// pack
	PARAM("overcurrent_discharge_A", overcurrent_discharge_A, CFG_U16, 1, 1000, 0, "A"),
	PARAM("overcurrent_charge_A", overcurrent_charge_A, CFG_U16, 1, 500, 0, "A"),
	PARAM("overcurrent_ms", overcurrent_ms, CFG_U16, 0, 500, 0, "ms"),
	PARAM("pack_voltage_tolerance_pct", pack_voltage_tolerance_pct, CFG_U8, 1, 50, 0, "%"),
	PARAM("pack_voltage_mismatch_ms", pack_voltage_mismatch_ms, CFG_U16, 100, 10000, 0, "ms"),
	PARAM("isa_pack_timeout_ms", isa_pack_timeout_ms, CFG_U16, 100, 1000, 0, "ms"),
	PARAM("isa_handcart_timeout_ms", isa_handcart_timeout_ms, CFG_U16, 100, 5000, 0, "ms"),
	PARAM("isa_bus_channel", isa_bus_channel, CFG_U8, 1, 3, 0, "U"),
	PARAM("isa_pack_invert", isa_pack_invert, CFG_BOOL, 0, 1, 0, ""),
	PARAM("isa_handcart_invert", isa_handcart_invert, CFG_BOOL, 0, 1, 0, ""),
	// carregamento
	PARAM("charge_voltage_dV", charge_voltage_dV, CFG_U16, 0, 6500, 0, "0.1V"),
	PARAM("charge_current_dA", charge_current_dA, CFG_U16, 0, 140, 0, "0.1A"),
	PARAM("charge_open_mA", charge_open_mA, CFG_U16, 0, 5000, 0, "mA"),
	PARAM("charge_cut_current_mA", charge_cut_current_mA, CFG_U16, 1000, 30000, 0, "mA"),
	PARAM("charge_temp_max_dC", charge_temp_max_dC, CFG_I16, 0, 600, 0, "0.1C"),
	PARAM("charge_settle_ms", charge_settle_ms, CFG_U16, 0, 10000, 0, "ms"),
	PARAM("charge_prestart_ms", charge_prestart_ms, CFG_U16, 0, 30000, 0, "ms"),
	PARAM("charge_command_ms", charge_command_ms, CFG_U16, 100, 4000, 0, "ms"),
	PARAM("charge_status_timeout_ms", charge_status_timeout_ms, CFG_U16, 500, 10000, 0, "ms"),
	PARAM("charge_fault_debounce_ms", charge_fault_debounce_ms, CFG_U16, 0, 5000, 0, "ms"),
	PARAM("charge_bus_tolerance_pct", charge_bus_tolerance_pct, CFG_U8, 1, 50, 0, "%"),
	PARAM("handcart_timeout_ms", handcart_timeout_ms, CFG_U16, 200, 5000, 0, "ms"),
	// balanceamento
	PARAM("bal_enabled", bal_enabled, CFG_BOOL, 0, 1, 0, ""),
	PARAM("bal_min_cell_mV", bal_min_cell_mV, CFG_U16, 2000, 4000, 0, "mV"),
	PARAM("bal_max_cell_mV", bal_max_cell_mV, CFG_U16, 3000, 4500, 0, "mV"),
	PARAM("bal_delta_mV", bal_delta_mV, CFG_U16, 1, 200, 0, "mV"),
	PARAM("bal_rough_mV", bal_rough_mV, CFG_U16, 10, 500, 0, "mV"),
	PARAM("bal_die_stop_dC", bal_die_stop_dC, CFG_I16, 400, 1200, 0, "0.1C"),
	PARAM("bal_die_hold_s", bal_die_hold_s, CFG_U16, 0, 600, 0, "s"),
	// ventoinhas
	PARAM("fan_cells_start_dC", fan_accu_cells.start_dC, CFG_I16, 0, 900, 0, "0.1C"),
	PARAM("fan_cells_full_dC", fan_accu_cells.full_dC, CFG_I16, 0, 900, 0, "0.1C"),
	PARAM("fan_cells_pwm_min_pct", fan_accu_cells.pwm_min_pct, CFG_U8, 0, 100, 0, "%"),
	PARAM("fan_cells_pwm_max_pct", fan_accu_cells.pwm_max_pct, CFG_U8, 0, 100, 0, "%"),
	PARAM("fan_lv_start_dC", fan_accu_lv.start_dC, CFG_I16, 0, 900, 0, "0.1C"),
	PARAM("fan_lv_full_dC", fan_accu_lv.full_dC, CFG_I16, 0, 900, 0, "0.1C"),
	PARAM("fan_lv_pwm_min_pct", fan_accu_lv.pwm_min_pct, CFG_U8, 0, 100, 0, "%"),
	PARAM("fan_lv_pwm_max_pct", fan_accu_lv.pwm_max_pct, CFG_U8, 0, 100, 0, "%"),
	PARAM("fan_pwm_hz", fan_pwm_hz, CFG_U16, 100, 30000, REBOOT, "Hz"),
	PARAM("fan_hysteresis_dC", fan_hysteresis_dC, CFG_I16, 0, 200, 0, "0.1C"),
	// pre-carga e contactores
	PARAM("precharge_mode", precharge_mode, CFG_U8, 0, 1, 0, "enum"),
	PARAM("precharge_timeout_ms", precharge_timeout_ms, CFG_U16, 1000, 60000, 0, "ms"),
	PARAM("precharge_bus_percentage", precharge_bus_percentage, CFG_U8, 50, 100, 0, "%"),
	PARAM("precharge_air_wait_ms", precharge_air_wait_ms, CFG_U16, 50, 5000, 0, "ms"),
	PARAM("precharge_min_ms", precharge_min_ms, CFG_U16, 0, 30000, 0, "ms"),
	PARAM("precharge_end_current_mA", precharge_end_current_mA, CFG_U16, 0, 20000, 0, "mA"),
	PARAM("precharge_lockout_ms", precharge_lockout_ms, CFG_U16, 0, 60000, 0, "ms"),
	PARAM("contactor_debounce_ms", contactor_debounce_ms, CFG_U16, 0, 2000, 0, "ms"),
	PARAM("contactor_mismatch_count", contactor_mismatch_count, CFG_U8, 1, 20, 0, ""),
	PARAM("contactor_check_mask", contactor_check_mask, CFG_U8, 0, 0x0F, HEX, "bits"),
	PARAM("vcu_timeout_ms", vcu_timeout_ms, CFG_U16, 0, 10000, 0, "ms"),
	// SOC
	PARAM("capacity_mAh", capacity_mAh, CFG_U16, 1000, 65000, 0, "mAh"),
	PARAM("soc_fit_degree", soc_fit_degree, CFG_U8, 1, 7, 0, ""),
	PARAM("soc_curve_pct", soc_curve_pct, CFG_U8, 0, 100, 0, "%"),
	PARAM("soc_curve_mV", soc_curve_mV, CFG_U16, 2000, 4500, 0, "mV"),
	PARAM("soc_rest_current_mA", soc_rest_current_mA, CFG_U16, 0, 10000, 0, "mA"),
	PARAM("soc_rest_s", soc_rest_s, CFG_U16, 0, 65000, 0, "s"),
	PARAM("soc_low_pct", soc_low_pct, CFG_U8, 0, 50, 0, "%"),
	PARAM("tempcomp_temp_C", temp_compensation_temp_C, CFG_I8, -40, 100, 0, "C"),
	PARAM("tempcomp_adjust_dmV", temp_compensation_adjust_mV, CFG_I16, -1000, 1000, 0, "0.1mV"),
	PARAM("tempcomp_resistance_dmOhm", temp_compensation_resistance_mOhm, CFG_U16, 0, 10000, 0, "0.1mOhm"),
	// placa master
	PARAM("board_current_mV_per_A", board_current_mV_per_A, CFG_U16, 10, 2000, 0, "mV/A"),
	PARAM("board_current_offset_mA", board_current_offset_mA, CFG_I16, -2000, 2000, 0, "mA"),
	// CAN
	PARAM("can1_kbps", can1_kbps, CFG_U16, 125, 1000, REBOOT, "kbit/s"),
	PARAM("can2_kbps", can2_kbps, CFG_U16, 125, 1000, REBOOT, "kbit/s"),
	PARAM("can_master_ms", can_master_ms, CFG_U16, 0, 10000, 0, "ms"),
	PARAM("can_slaves_ms", can_slaves_ms, CFG_U16, 0, 10000, 0, "ms"),
	PARAM("can2_mirror_charging", can2_mirror_charging, CFG_BOOL, 0, 1, 0, ""),
	PARAM("bootloader_can_mask", bootloader_can_mask, CFG_U8, 0, 3, HEX, "bits"),
	// comunicacao externa
	PARAM("bt_name", bt_name, CFG_STR, 0, 0, REBOOT, ""),
	PARAM("live_period_ms", live_period_ms, CFG_U16, 0, 10000, 0, "ms"),
	PARAM("live_uart1_ms", live_uart1_ms, CFG_U16, 0, 10000, 0, "ms"),
	PARAM("bt_probe_s", bt_probe_s, CFG_U16, 0, 3600, 0, "s"),
	PARAM("bt_reboot_s", bt_reboot_s, CFG_U16, 0, 3600, 0, "s"),
	PARAM("bt_pin", bt_pin, CFG_U16, 0, 9999, 0, ""),
	// AMS_ERROR
	PARAM("ams_error_enable", ams_error_enable, CFG_FAULTS, 0, 0, HEX, "faults"),
	PARAM("ams_error_min_on_ms", ams_error_min_on_ms, CFG_U16, 0, 10000, 0, "ms"),
	// bootloader CAN
	PARAM("boot_confirm_s", boot_confirm_s, CFG_U16, 5, 600, 0, "s"),
};

const uint16_t config_param_count = (uint16_t) (sizeof(config_params) / sizeof(config_params[0]));

/* ================== AJUDAS ================== */

static uint8_t memory_size(uint8_t type) {

	switch (type) {
	case CFG_BOOL:
		return (uint8_t) sizeof(bool);
	case CFG_U16:
	case CFG_I16:
		return 2u;
	case CFG_U32:
		return 4u;
	case CFG_FAULTS:
		return (uint8_t) sizeof(bms_faults_t);
	default:
		return 1u;
	}
}

// bytes de um elemento na EEPROM (bool num byte, faults como mascara de 64 bits)
static uint8_t storage_size(uint8_t type) {

	switch (type) {
	case CFG_U16:
	case CFG_I16:
		return 2u;
	case CFG_U32:
		return 4u;
	case CFG_FAULTS:
		return 8u;
	default:
		return 1u;
	}
}

static uint8_t* element_ptr(const bms_config_t *config, const cfg_param_t *param, uint8_t element) {

	return (uint8_t*) config + param->offset + ((size_t) element * memory_size(param->type));
}

static bool is_numeric(uint8_t type) {

	return (type != CFG_STR) && (type != CFG_FAULTS);
}

static int32_t read_element(const bms_config_t *config, const cfg_param_t *param, uint8_t element) {

	const uint8_t *field = element_ptr(config, param, element);

	switch (param->type) {
	case CFG_BOOL:
		return *(const bool*) field ? 1 : 0;
	case CFG_U8:
		return *field;
	case CFG_I8:
		return *(const int8_t*) field;
	case CFG_U16:
		return *(const uint16_t*) field;
	case CFG_I16:
		return *(const int16_t*) field;
	case CFG_U32:
		return (int32_t) *(const uint32_t*) field;
	default:
		return 0;
	}
}

static void write_element(bms_config_t *config, const cfg_param_t *param, uint8_t element, int32_t value) {

	uint8_t *field = element_ptr(config, param, element);

	switch (param->type) {
	case CFG_BOOL:
		*(bool*) field = (value != 0);
		break;
	case CFG_U8:
		*field = (uint8_t) value;
		break;
	case CFG_I8:
		*(int8_t*) field = (int8_t) value;
		break;
	case CFG_U16:
		*(uint16_t*) field = (uint16_t) value;
		break;
	case CFG_I16:
		*(int16_t*) field = (int16_t) value;
		break;
	case CFG_U32:
		*(uint32_t*) field = (uint32_t) value;
		break;
	default:
		break;
	}
}

static uint64_t faults_valid_bits(void) {

	return (FAULT_COUNT >= 64u) ? UINT64_MAX : ((1ull << FAULT_COUNT) - 1u);
}

// RN4871: o nome vai dentro de um comando "SN,<nome>\r", sem virgulas nem %
static bool name_char_ok(char character) {

	return (character >= ' ') && (character <= '~') && (character != ',') && (character != '%') && (character != '$');
}

/* ================== DEFAULTS, GAMAS E COERENCIA ================== */

void Config_SetDefaults(bms_config_t *config) {

	*config = config_defaults;
}

uint16_t Config_Clamp(bms_config_t *config) {

	uint16_t changed = 0;

	for (uint16_t param_index = 0; param_index < config_param_count; param_index++) {

		const cfg_param_t *param = &config_params[param_index];

		if (param->type == CFG_STR) {

			char *text = (char*) element_ptr(config, param, 0);
			bool ok = (text[0] != '\0');

			text[param->count - 1u] = '\0';
			for (uint8_t char_index = 0; ok && (text[char_index] != '\0'); char_index++) {
				ok = name_char_ok(text[char_index]);
			}
			if (!ok) {
				memcpy(text, element_ptr(&config_defaults, param, 0), param->count);
				changed++;
			}
			continue;
		}

		if (param->type == CFG_FAULTS) {

			bms_faults_t *faults = (bms_faults_t*) element_ptr(config, param, 0);
			uint64_t mask = Faults_ToMask(faults) & faults_valid_bits();
			Faults_FromMask(mask, faults);   // tambem normaliza bools com lixo
			continue;
		}

		for (uint8_t element = 0; element < param->count; element++) {

			// um bool com lixo (vindo da EEPROM) le-se pelo byte, nunca como bool
			if (param->type == CFG_BOOL) {
				uint8_t raw = *element_ptr(config, param, element);
				if (raw > 1u) {
					changed++;
				}
				write_element(config, param, element, (raw != 0u) ? 1 : 0);
				continue;
			}

			int32_t value = read_element(config, param, element);
			int32_t clamped = (value < param->min) ? param->min : ((value > param->max) ? param->max : value);

			if (clamped != value) {
				changed++;
			}
			write_element(config, param, element, clamped);
		}
	}

	return changed;
}

static bool valid_kbps(uint16_t kbps) {

	return (kbps == 125u) || (kbps == 250u) || (kbps == 500u) || (kbps == 1000u);
}

static bool fan_ok(const bms_fan_cfg_t *fan) {

	return (fan->start_dC < fan->full_dC) && (fan->pwm_min_pct <= fan->pwm_max_pct);
}

const char* Config_Validate(const bms_config_t *config) {

	if ((config->slaves == 0u) || (config->slaves > BMS_MAX_SLAVES) || (config->cells_per_slave == 0u) || (config->cells_per_slave > BMS_MAX_CELLS)
			|| (config->ntcs_per_slave > BMS_MAX_NTCS)) {
		return "topologia fora dos limites";
	}

	// FS 2027 EV 5.8.3: temperatura de pelo menos 30 % das celulas
	for (uint8_t slave_index = 0; slave_index < config->slaves; slave_index++) {

		uint16_t disabled_ntcs = config->ntc_disabled_mask[slave_index] & (uint16_t) ((1u << config->ntcs_per_slave) - 1u);
		uint32_t enabled_ntcs = (uint32_t) config->ntcs_per_slave - (uint32_t) __builtin_popcount(disabled_ntcs);

		if ((enabled_ntcs * 100u) < ((uint32_t) config->cells_per_slave * 30u)) {
			return "menos de 30% das celulas com NTC (FS EV 5.8.3)";
		}
	}

	if (!((config->cell_open_wire_mV < config->cell_min_mV) && (config->cell_min_mV < config->cell_low_warn_mV)
			&& (config->cell_low_warn_mV <= config->cell_high_warn_mV) && (config->cell_high_warn_mV < config->cell_max_mV))) {
		return "celula: open_wire < min < low_warn <= high_warn < max";
	}
	if (!((config->temp_min_dC < config->temp_low_warn_dC) && (config->temp_low_warn_dC <= config->temp_high_warn_dC)
			&& (config->temp_high_warn_dC < config->temp_max_dC))) {
		return "temperatura: min < low_warn <= high_warn < max";
	}
	if (config->bal_min_cell_mV >= config->bal_max_cell_mV) {
		return "balanceamento: bal_min_cell_mV < bal_max_cell_mV";
	}
	if (!fan_ok(&config->fan_accu_cells) || !fan_ok(&config->fan_accu_lv)) {
		return "ventoinhas: start < full e pwm_min <= pwm_max";
	}
	if ((config->precharge_air_wait_ms >= config->precharge_timeout_ms) || (config->precharge_min_ms >= config->precharge_timeout_ms)) {
		return "precarga: air_wait e min tem de ser < timeout";
	}

	for (uint8_t point = 1; point < CFG_SOC_POINTS; point++) {
		if ((config->soc_curve_pct[point] <= config->soc_curve_pct[point - 1u]) || (config->soc_curve_mV[point] <= config->soc_curve_mV[point - 1u])) {
			return "curva do SOC tem de ser crescente";
		}
	}
	for (uint8_t point = 1; point < CFG_TEMPCOMP_POINTS; point++) {
		if (config->temp_compensation_temp_C[point] <= config->temp_compensation_temp_C[point - 1u]) {
			return "compensacao: temperaturas crescentes";
		}
	}

	if (!valid_kbps(config->can1_kbps) || !valid_kbps(config->can2_kbps)) {
		return "CAN: 125, 250, 500 ou 1000 kbit/s";
	}
	if (config->bt_name[0] == '\0') {
		return "bt_name vazio";
	}

	return NULL;
}

/* ================== ACESSO POR NOME ================== */

int Config_Find(const char *key) {

	for (uint16_t param_index = 0; param_index < config_param_count; param_index++) {
		if (strcmp(config_params[param_index].key, key) == 0) {
			return param_index;
		}
	}

	return -1;
}

bool Config_GetElement(const bms_config_t *config, uint16_t param_index, uint8_t element, int32_t *value) {

	if ((param_index >= config_param_count) || !is_numeric(config_params[param_index].type) || (element >= config_params[param_index].count)) {
		return false;
	}

	*value = read_element(config, &config_params[param_index], element);
	return true;
}

bool Config_SetElement(bms_config_t *config, uint16_t param_index, uint8_t element, int32_t value) {

	if ((param_index >= config_param_count) || !is_numeric(config_params[param_index].type) || (element >= config_params[param_index].count)) {
		return false;
	}

	const cfg_param_t *param = &config_params[param_index];

	if ((value < param->min) || (value > param->max)) {
		return false;
	}

	write_element(config, param, element, value);
	return true;
}

size_t Config_FormatValue(const bms_config_t *config, uint16_t param_index, char *out, size_t size) {

	const cfg_param_t *param = &config_params[param_index];
	size_t length = 0;

	if ((size == 0u) || (param_index >= config_param_count)) {
		return 0;
	}
	out[0] = '\0';

	if ((param->type == CFG_STR) || (param->type == CFG_FAULTS)) {

		int written;

		if (param->type == CFG_STR) {
			written = snprintf(out, size, "%s", (const char*) element_ptr(config, param, 0));
		} else {
			uint64_t mask = Faults_ToMask((const bms_faults_t*) element_ptr(config, param, 0));
			written = snprintf(out, size, "0x%08lX%08lX", (unsigned long) (mask >> 32), (unsigned long) (mask & 0xFFFFFFFFu));
		}
		if (written < 0) {
			return 0;
		}
		return ((size_t) written < size) ? (size_t) written : (size - 1u);
	}

	for (uint8_t element = 0; (element < param->count) && (length < size); element++) {

		int32_t value = read_element(config, param, element);
		int written;

		if ((param->flags & CFG_FLAG_HEX) != 0u) {
			written = snprintf(out + length, size - length, "%s0x%lX", (element > 0u) ? "," : "", (unsigned long) value);
		} else {
			written = snprintf(out + length, size - length, "%s%ld", (element > 0u) ? "," : "", (long) value);
		}
		if (written < 0) {
			break;
		}
		length += (size_t) written;
	}

	return (length < size) ? length : (size - 1u);
}

static bool parse_int(const char *text, const char **end, int32_t *value) {

	while (*text == ' ') {
		text++;
	}

	if ((strncmp(text, "on", 2) == 0) || (strncmp(text, "true", 4) == 0)) {
		*value = 1;
		*end = text + ((text[1] == 'n') ? 2 : 4);
		return true;
	}
	if ((strncmp(text, "off", 3) == 0) || (strncmp(text, "false", 5) == 0)) {
		*value = 0;
		*end = text + ((text[1] == 'f') ? 3 : 5);
		return true;
	}

	// decimal, ou hexadecimal com 0x (um zero a esquerda nao passa a octal)
	bool hex = (text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X'));
	char *stop;
	long parsed = strtol(text, &stop, hex ? 16 : 10);

	if (stop == text) {
		return false;
	}

	*value = (int32_t) parsed;
	*end = stop;
	return true;
}

static const char* parse_faults(bms_faults_t *faults, const char *text) {

	while (*text == ' ') {
		text++;
	}

	// +NOME / -NOME, separados por virgulas
	if ((*text == '+') || (*text == '-')) {

		uint64_t mask = Faults_ToMask(faults);

		while ((*text == '+') || (*text == '-')) {

			bool add = (*text == '+');
			char name[32];
			size_t length = 0;

			text++;
			while ((*text != '\0') && (*text != ',') && (*text != ' ') && (length < (sizeof(name) - 1u))) {
				name[length++] = *text++;
			}
			name[length] = '\0';

			int fault_index = Faults_FindByName(name);
			if (fault_index < 0) {
				return "fault desconhecido";
			}
			mask = add ? (mask | (1ull << fault_index)) : (mask & ~(1ull << fault_index));

			while ((*text == ',') || (*text == ' ')) {
				text++;
			}
		}

		if (*text != '\0') {
			return "valor invalido";
		}
		Faults_FromMask(mask, faults);
		return NULL;
	}

	bool hex = (text[0] == '0') && ((text[1] == 'x') || (text[1] == 'X'));
	char *stop;
	unsigned long long mask = strtoull(text, &stop, hex ? 16 : 10);

	if ((stop == text) || (*stop != '\0')) {
		return "valor invalido";
	}
	if ((mask & ~faults_valid_bits()) != 0u) {
		return "fora da gama";
	}

	Faults_FromMask((uint64_t) mask, faults);
	return NULL;
}

const char* Config_ParseValue(bms_config_t *config, uint16_t param_index, int element, const char *text) {

	if (param_index >= config_param_count) {
		return "parametro desconhecido";
	}

	const cfg_param_t *param = &config_params[param_index];

	if (param->type == CFG_STR) {

		size_t length = strlen(text);

		if (element > 0) {
			return "indice invalido";
		}
		if ((length == 0u) || (length >= param->count)) {
			return "tamanho invalido";
		}
		for (size_t char_index = 0; char_index < length; char_index++) {
			if (!name_char_ok(text[char_index])) {
				return "caracter invalido";
			}
		}
		memset(element_ptr(config, param, 0), 0, param->count);
		memcpy(element_ptr(config, param, 0), text, length);
		return NULL;
	}

	if (param->type == CFG_FAULTS) {
		return (element > 0) ? "indice invalido" : parse_faults((bms_faults_t*) element_ptr(config, param, 0), text);
	}

	if (element >= param->count) {
		return "indice invalido";
	}

	// um so elemento
	if ((element >= 0) || (param->count == 1u)) {

		int32_t value;
		const char *end;

		if (!parse_int(text, &end, &value)) {
			return "valor invalido";
		}
		while (*end == ' ') {
			end++;
		}
		if (*end != '\0') {
			return "valor invalido";
		}
		return Config_SetElement(config, param_index, (uint8_t) ((element >= 0) ? element : 0), value) ? NULL : "fora da gama";
	}

	// tabela inteira: exatamente count valores separados por virgulas
	int32_t values[BMS_MAX_SLAVES];
	const char *cursor = text;

	_Static_assert(BMS_MAX_SLAVES >= CFG_SOC_POINTS, "values[] tem de levar a maior tabela");
	_Static_assert(BMS_MAX_SLAVES >= CFG_TEMPCOMP_POINTS, "values[] tem de levar a maior tabela");

	for (uint8_t value_index = 0; value_index < param->count; value_index++) {

		const char *end;

		if (!parse_int(cursor, &end, &values[value_index])) {
			return "numero de valores";
		}
		if ((values[value_index] < param->min) || (values[value_index] > param->max)) {
			return "fora da gama";
		}
		while (*end == ' ') {
			end++;
		}
		if (value_index + 1u < param->count) {
			if (*end != ',') {
				return "numero de valores";
			}
			cursor = end + 1;
		} else if (*end != '\0') {
			return "numero de valores";
		}
	}

	for (uint8_t value_index = 0; value_index < param->count; value_index++) {
		write_element(config, param, value_index, values[value_index]);
	}
	return NULL;
}

bool Config_ParamEqual(const bms_config_t *first, const bms_config_t *second, uint16_t param_index) {

	const cfg_param_t *param = &config_params[param_index];

	if (param->type == CFG_FAULTS) {
		return Faults_ToMask((const bms_faults_t*) element_ptr(first, param, 0)) == Faults_ToMask((const bms_faults_t*) element_ptr(second, param, 0));
	}

	return memcmp(element_ptr(first, param, 0), element_ptr(second, param, 0), (size_t) param->count * memory_size(param->type)) == 0;
}

/* ================== EEPROM ================== */

uint16_t Config_PayloadSize(uint16_t param_count) {

	uint32_t size = 0;

	for (uint16_t param_index = 0; (param_index < param_count) && (param_index < config_param_count); param_index++) {
		size += (uint32_t) config_params[param_index].count * storage_size(config_params[param_index].type);
	}

	return (uint16_t) size;
}

uint16_t Config_Serialize(const bms_config_t *config, uint8_t *out, uint16_t size) {

	uint16_t length = 0;

	if (Config_PayloadSize(config_param_count) > size) {
		return 0;
	}

	for (uint16_t param_index = 0; param_index < config_param_count; param_index++) {

		const cfg_param_t *param = &config_params[param_index];

		if (param->type == CFG_STR) {
			memcpy(&out[length], element_ptr(config, param, 0), param->count);
			length += param->count;
			continue;
		}

		if (param->type == CFG_FAULTS) {
			uint64_t mask = Faults_ToMask((const bms_faults_t*) element_ptr(config, param, 0));
			for (uint8_t byte_index = 0; byte_index < 8u; byte_index++) {
				out[length++] = (uint8_t) (mask >> (8u * byte_index));
			}
			continue;
		}

		for (uint8_t element = 0; element < param->count; element++) {

			uint32_t value = (uint32_t) read_element(config, param, element);

			for (uint8_t byte_index = 0; byte_index < storage_size(param->type); byte_index++) {
				out[length++] = (uint8_t) (value >> (8u * byte_index));
			}
		}
	}

	return length;
}

static int32_t sign_extend(uint32_t raw, uint8_t type) {

	switch (type) {
	case CFG_I8:
		return (int8_t) raw;
	case CFG_I16:
		return (int16_t) raw;
	default:
		return (int32_t) raw;
	}
}

bool Config_Deserialize(bms_config_t *config, const uint8_t *data, uint16_t size, uint16_t param_count) {

	uint16_t length = 0;

	if ((param_count > config_param_count) || (Config_PayloadSize(param_count) != size)) {
		return false;
	}

	for (uint16_t param_index = 0; param_index < param_count; param_index++) {

		const cfg_param_t *param = &config_params[param_index];

		if (param->type == CFG_STR) {
			memcpy(element_ptr(config, param, 0), &data[length], param->count);
			length += param->count;
			continue;
		}

		if (param->type == CFG_FAULTS) {
			uint64_t mask = 0;
			for (uint8_t byte_index = 0; byte_index < 8u; byte_index++) {
				mask |= (uint64_t) data[length++] << (8u * byte_index);
			}
			Faults_FromMask(mask, (bms_faults_t*) element_ptr(config, param, 0));
			continue;
		}

		for (uint8_t element = 0; element < param->count; element++) {

			uint32_t raw = 0;

			for (uint8_t byte_index = 0; byte_index < storage_size(param->type); byte_index++) {
				raw |= (uint32_t) data[length++] << (8u * byte_index);
			}

			if (param->type == CFG_BOOL) {
				*element_ptr(config, param, element) = (uint8_t) raw;   // o clamp normaliza e conta
			} else {
				write_element(config, param, element, sign_extend(raw, param->type));
			}
		}
	}

	return true;
}

uint32_t Config_Crc32(const uint8_t *data, size_t size, uint32_t crc) {

	crc = ~crc;

	for (size_t byte_index = 0; byte_index < size; byte_index++) {

		crc ^= data[byte_index];

		for (uint8_t bit = 0; bit < 8u; bit++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}

	return ~crc;
}

uint32_t Config_SchemaCrc(uint16_t param_count) {

	uint32_t crc = 0;

	for (uint16_t param_index = 0; (param_index < param_count) && (param_index < config_param_count); param_index++) {

		const cfg_param_t *param = &config_params[param_index];
		uint8_t descriptor[2] = { param->type, param->count };

		crc = Config_Crc32((const uint8_t*) param->key, strlen(param->key), crc);
		crc = Config_Crc32(descriptor, sizeof(descriptor), crc);
	}

	return crc;
}

/* ================== A QUENTE ================== */

void Config_ApplyLive(bms_config_t *active, const bms_config_t *edited) {

	for (uint16_t param_index = 0; param_index < config_param_count; param_index++) {

		const cfg_param_t *param = &config_params[param_index];

		if ((param->flags & CFG_FLAG_REBOOT) == 0u) {
			memcpy(element_ptr(active, param, 0), element_ptr(edited, param, 0), (size_t) param->count * memory_size(param->type));
		}
	}
}

bool Config_RebootPending(const bms_config_t *active, const bms_config_t *edited) {

	for (uint16_t param_index = 0; param_index < config_param_count; param_index++) {
		if (((config_params[param_index].flags & CFG_FLAG_REBOOT) != 0u) && !Config_ParamEqual(active, edited, param_index)) {
			return true;
		}
	}

	return false;
}
