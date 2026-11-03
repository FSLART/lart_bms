/*
 * live_json.c
 *
 * Ver live_json.h. Escrita a mao, sem printf: o newlib-nano e lento com
 * printf e o brain_loop tem ~65 ms de WWDG. ~3,5 kB por linha com 12 slaves.
 */

#include "live_json.h"
#include "console.h"
#include "uart_link.h"
#include "rn4871.h"
#include "faults.h"
#include "ams_error.h"
#include "precharge.h"
#include "charger.h"
#include "boot_ctrl.h"
#include "boot_names.h"
#include <string.h>

#define MAX_DEPTH 8u

typedef struct {
	char *out;
	uint16_t size;
	uint16_t length;
	bool overflow;
	uint8_t depth;
	bool comma[MAX_DEPTH];
} json_t;

static char buffer[LIVE_JSON_BUFFER];
static uint32_t last_ms[CONSOLE_COUNT];
static uint32_t transitions_seen;

/* ================== ESCRITOR ================== */

static void put_char(json_t *json, char character) {

	if (json->length < (json->size - 3u)) {   // espaco para o \r\n e o terminador
		json->out[json->length++] = character;
	} else {
		json->overflow = true;
	}
}

static void put_string(json_t *json, const char *text) {

	put_char(json, '"');
	for (; *text != '\0'; text++) {
		if ((*text == '"') || (*text == '\\')) {
			put_char(json, '\\');
		}
		put_char(json, ((unsigned char) *text < ' ') ? '?' : *text);
	}
	put_char(json, '"');
}

static void put_uint(json_t *json, uint32_t value) {

	char digits[10];
	uint8_t count = 0;

	do {
		digits[count++] = (char) ('0' + (value % 10u));
		value /= 10u;
	} while (value != 0u);

	while (count > 0u) {
		put_char(json, digits[--count]);
	}
}

static void put_int(json_t *json, int32_t value) {

	if (value < 0) {
		put_char(json, '-');
		put_uint(json, (uint32_t) (-(int64_t) value));
	} else {
		put_uint(json, (uint32_t) value);
	}
}

static void separator(json_t *json) {

	if (json->comma[json->depth]) {
		put_char(json, ',');
	}
	json->comma[json->depth] = true;
}

static void key(json_t *json, const char *name) {

	separator(json);
	if (name != NULL) {
		put_string(json, name);
		put_char(json, ':');
	}
}

static void json_open(json_t *json, const char *name, char bracket) {

	key(json, name);
	put_char(json, bracket);
	if (json->depth < (MAX_DEPTH - 1u)) {
		json->depth++;
	}
	json->comma[json->depth] = false;
}

static void json_close(json_t *json, char bracket) {

	put_char(json, bracket);
	if (json->depth > 0u) {
		json->depth--;
	}
}

static void field_int(json_t *json, const char *name, int32_t value) {

	key(json, name);
	put_int(json, value);
}

static void field_uint(json_t *json, const char *name, uint32_t value) {

	key(json, name);
	put_uint(json, value);
}

static void field_bool(json_t *json, const char *name, bool value) {

	field_uint(json, name, value ? 1u : 0u);
}

static void field_str(json_t *json, const char *name, const char *value) {

	key(json, name);
	put_string(json, value);
}

/* ================== BLOCOS ================== */

static const char* adbms_state_name(adbms_state_t state) {

	static const char *const names[] = { "INIT", "WAKE_LOW", "WAKE_HIGH", "WAIT_TICK", "DETECT", "CONFIG", "CELL_READ", "OPEN_WIRE", "BALANCING", "TEMP_READ", "FAULT" };

	return ((unsigned) state <= (unsigned) ADBMS_FAULT) ? names[state] : "?";
}

static const char* balancing_name(balancing_stage_t stage) {

	static const char *const names[] = { "OFF", "ROUGH", "FINE", "END" };

	return ((unsigned) stage <= (unsigned) BALANCING_END) ? names[stage] : "?";
}

static const char* eeprom_state_name(eeprom_state_t state) {

	static const char *const names[] = { "IDLE", "WRITING", "VERIFY", "DONE", "FAILED" };

	return ((unsigned) state <= (unsigned) EEPROM_FAILED) ? names[state] : "?";
}

static void fault_names(json_t *json, const char *name, uint64_t mask) {

	json_open(json, name, '[');
	for (uint8_t fault_index = 0; fault_index < FAULT_COUNT; fault_index++) {
		if (((mask >> fault_index) & 1u) != 0u) {
			key(json, NULL);
			put_string(json, Faults_Name(fault_index));
		}
	}
	json_close(json, ']');
}

static void isa_block(json_t *json, const char *name, const bms_isa_t *isa, uint8_t bus_channel) {

	json_open(json, name, '{');
	field_int(json, "current_mA", isa->current_mA);
	field_int(json, "voltage_mV", isa->voltage_mV[bus_channel]);
	field_int(json, "u1_mV", isa->voltage_mV[0]);
	field_int(json, "u2_mV", isa->voltage_mV[1]);
	field_int(json, "u3_mV", isa->voltage_mV[2]);
	field_int(json, "power_W", isa->power_W);
	field_int(json, "charge_As", isa->charge_As);
	field_int(json, "energy_Wh", isa->energy_Wh);
	field_int(json, "temp_dC", isa->temp_dC);
	field_uint(json, "rx_age_ms", isa->age_ms);
	field_uint(json, "errors", isa->error_flags);
	field_bool(json, "alive", isa->alive);
	json_close(json, '}');
}

static void can_block(json_t *json, const char *name, const bms_can_t *can) {

	json_open(json, name, '{');
	field_bool(json, "started", can->started);
	field_uint(json, "kbps", can->kbps);
	field_bool(json, "bus_off", can->bus_off);
	field_bool(json, "error_passive", can->error_passive);
	field_uint(json, "tx_errors", can->tx_errors);
	field_uint(json, "rx_errors", can->rx_errors);
	field_uint(json, "last_error", can->last_error_code);
	field_uint(json, "tx_queue", can->tx_queue_depth);
	field_uint(json, "tx_frames", can->tx_frames);
	field_uint(json, "rx_frames", can->rx_frames);
	field_uint(json, "tx_dropped", can->tx_dropped);
	field_uint(json, "rx_dropped", can->rx_dropped);
	json_close(json, '}');
}

uint16_t LiveJson_Build(char *out, uint16_t size, uint8_t console, uint32_t now_ms) {

	json_t json = { .out = out, .size = size };
	const bms_config_t *config = &bms.config;
	const bms_link_t *link = (console == CONSOLE_BLE) ? &bms.uart2 : &bms.uart1;
	uint8_t slaves = config->slaves;
	uint8_t bus_channel = ((config->isa_bus_channel >= 1u) && (config->isa_bus_channel <= 3u)) ? (uint8_t) (config->isa_bus_channel - 1u) : 0u;

	json_open(&json, NULL, '{');
	field_uint(&json, "uptime_ms", bms.system.uptime_ms);

	json_open(&json, "bms", '{');
	field_str(&json, "state", brain_state_name(bms.system.state));
	field_str(&json, "prev_state", brain_state_name(bms.system.previous_state));
	field_uint(&json, "runtime_s", bms.system.uptime_ms / 1000u);
	field_bool(&json, "wwdg_reset", bms.system.wwdg_reset);
	field_uint(&json, "active_faults", bms.ams_error.fault_count);
	field_bool(&json, "hv_safe", bms.system.hv_safe);
	field_uint(&json, "loop_us", bms.system.loop_us);
	field_uint(&json, "loop_max_us", bms.system.loop_max_us);
	field_uint(&json, "fw", BMS_FIRMWARE_VERSION);
	json_close(&json, '}');

	json_open(&json, "ams_error", '{');
	field_str(&json, "state", bms.ams_error.active ? "ERROR" : "OK");
	field_bool(&json, "active", bms.ams_error.active);
	field_uint(&json, "permanent", 0u);   // o latch e em hardware
	field_bool(&json, "boot_hold", bms.ams_error.boot_hold);
	field_uint(&json, "fault_count", bms.ams_error.fault_count);
	field_uint(&json, "fault_mask_lo", (uint32_t) bms.ams_error.faults_mask);
	field_uint(&json, "fault_mask_hi", (uint32_t) (bms.ams_error.faults_mask >> 32));
	field_uint(&json, "ams_mask_lo", (uint32_t) bms.ams_error.sources_mask);
	fault_names(&json, "faults", bms.ams_error.faults_mask);
	fault_names(&json, "sources", bms.ams_error.sources_mask);
	json_close(&json, '}');

	const bms_precharge_t *precharge = &bms.precharge;

	json_open(&json, "precharge", '{');
	field_str(&json, "state", Precharge_StateName(precharge->state));
	field_uint(&json, "source", precharge->source);
	field_int(&json, "vcu_request", precharge->vcu_request);
	field_uint(&json, "vcu_request_age_ms", precharge->vcu_request_age_ms);
	field_uint(&json, "hv_on_mismatch_count", precharge->mismatch_count);
	field_bool(&json, "sdc_closed", precharge->sdc_closed);
	field_bool(&json, "air_pos", precharge->air_pos_fb);
	field_bool(&json, "air_neg", precharge->air_neg_fb);
	field_bool(&json, "precharge_relay", precharge->precharge_fb);
	field_bool(&json, "discharge", precharge->discharge_fb);
	field_bool(&json, "cmd_air_pos", precharge->trigger_air_pos_cmd);
	field_bool(&json, "cmd_air_neg", precharge->trigger_air_neg_cmd);
	field_bool(&json, "cmd_precharge", precharge->trigger_precharge_cmd);
	field_uint(&json, "expected", precharge->expected_mask);
	field_uint(&json, "actual", precharge->actual_mask);
	field_int(&json, "bus_mV", precharge->bus_voltage_mV);
	field_uint(&json, "state_ms", precharge->state_ms);
	field_uint(&json, "hv_on_count", precharge->hv_on_count);
	json_close(&json, '}');

	isa_block(&json, "isa_pack", &bms.isa_pack, bus_channel);
	isa_block(&json, "isa_handcart", &bms.isa_handcart, 0u);

	json_open(&json, "soc", '{');
	field_uint(&json, "percent_x100", bms.soc.percent_x100);
	field_int(&json, "used_charge_As", bms.soc.used_As);
	field_bool(&json, "ready", bms.soc.ready);
	field_uint(&json, "initial_percent_x100", bms.soc.initial_percent_x100);
	field_uint(&json, "ocv_mV", bms.soc.ocv_mV);
	field_uint(&json, "anchors", bms.soc.anchors);
	json_close(&json, '}');

	const bms_charger_t *charger = &bms.charger;

	json_open(&json, "charger", '{');
	field_str(&json, "state", Charger_StateName(charger->state));
	field_str(&json, "end", Charger_EndName(charger->end_reason));
	field_bool(&json, "handcart_switch", charger->switch_on);
	field_uint(&json, "handcart_age_ms", charger->switch_seen ? (now_ms - charger->switch_rx_ms) : UINT32_MAX);
	field_bool(&json, "status_ok", charger->charger_alive);
	field_uint(&json, "output_voltage_dV", charger->out_voltage_dV);
	field_uint(&json, "output_current_dA", charger->out_current_dA);
	field_bool(&json, "hw_failure", charger->hw_failure);
	field_bool(&json, "temp_otp", charger->over_temp);
	field_bool(&json, "input_voltage_fault", charger->input_voltage_fault);
	field_bool(&json, "starting_state_fault", charger->starting_state_fault);
	field_bool(&json, "comm_timeout", charger->comm_timeout);
	field_int(&json, "temp_C", charger->temp_C);
	field_uint(&json, "requested_current_raw", charger->req_current_dA);
	field_uint(&json, "requested_voltage_dV", charger->req_voltage_dV);
	field_bool(&json, "request_enable", charger->req_enable);
	json_close(&json, '}');

	const bms_cells_t *cells = &bms.cells;
	bool any_pec = false;

	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		any_pec = any_pec || bms.pec.last_read[slave_index];
	}

	json_open(&json, "pack", '{');
	field_uint(&json, "slaves_found", cells->slaves_found);
	field_str(&json, "adbms_state", adbms_state_name(bms.adbms.state));
	field_bool(&json, "any_pec_error", any_pec);
	field_uint(&json, "cell_max_mV", cells->cell_max_mV);
	field_uint(&json, "cell_min_mV", cells->cell_min_mV);
	field_uint(&json, "cell_avg_mV", cells->cell_avg_mV);
	field_uint(&json, "cell_delta_mV", cells->cell_delta_mV);
	field_int(&json, "temp_max_cC", cells->temp_max_dC * 10);
	field_int(&json, "temp_min_cC", cells->temp_min_dC * 10);
	field_uint(&json, "voltage_sum_mV", bms.accu.voltage_sum_mV);
	field_bool(&json, "cells_valid", cells->cells_valid);
	field_bool(&json, "temps_valid", cells->temps_valid);
	field_uint(&json, "cycles", bms.adbms.cycles);
	field_uint(&json, "cycle_us", bms.adbms.cycle_us);
	field_uint(&json, "overruns", bms.adbms.overruns);
	field_uint(&json, "pec_total", bms.pec.total);
	field_uint(&json, "spi_errors", bms.adbms.spi_errors);
	field_uint(&json, "config_errors", bms.adbms.config_errors);
	json_close(&json, '}');

	const bms_balancing_t *balancing = &bms.balancing;

	json_open(&json, "balancing", '{');
	field_bool(&json, "active", (balancing->stage == BALANCING_ROUGH) || (balancing->stage == BALANCING_FINE));
	field_str(&json, "stage", balancing_name(balancing->stage));
	field_uint(&json, "target_mV", balancing->target_mV);
	field_uint(&json, "cells_discharging", balancing->cells_discharging);
	field_uint(&json, "worst_delta_mV", balancing->worst_delta_mV);
	field_uint(&json, "worst_slave", balancing->worst_slave);
	field_uint(&json, "worst_cell", balancing->worst_cell);
	field_bool(&json, "die_hold", balancing->die_ot_hold);
	json_open(&json, "mask", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		key(&json, NULL);
		put_uint(&json, balancing->dcc_mask[slave_index]);
	}
	json_close(&json, ']');
	json_close(&json, '}');

	json_open(&json, "board", '{');
	field_uint(&json, "fan_pwm", bms.fan_cells.pwm);
	field_uint(&json, "fan_lv_pwm", bms.fan_lv.pwm);
	field_int(&json, "mcu_temp_dC", bms.board.mcu_temp_dC);
	field_uint(&json, "vdda_mV", bms.board.vdda_mV);
	field_uint(&json, "master_current_mA", bms.board.current_mA);
	json_close(&json, '}');

	can_block(&json, "can1", &bms.can1);
	can_block(&json, "can2", &bms.can2);

	json_open(&json, "eeprom", '{');
	field_bool(&json, "present", bms.eeprom.present);
	field_bool(&json, "loaded", config->loaded);
	field_uint(&json, "bank", bms.eeprom.bank);
	field_uint(&json, "sequence", bms.eeprom.sequence);
	field_str(&json, "state", eeprom_state_name(bms.eeprom.state));
	field_bool(&json, "dirty", bms.eeprom.dirty);
	field_bool(&json, "reboot_pending", bms.eeprom.reboot_pending);
	field_uint(&json, "writes", bms.eeprom.writes);
	field_uint(&json, "failures", bms.eeprom.failures);
	json_close(&json, '}');

	char backup_version[16];

	BootCtrl_VersionText(bms.boot.backup_version, backup_version, sizeof(backup_version));
	json_open(&json, "boot", '{');
	field_bool(&json, "bootloader", bms.boot.bootloader);
	field_uint(&json, "bl_version", bms.boot.boot_version);
	field_str(&json, "run", BootNames_Run(bms.boot.run));
	field_str(&json, "event", BootNames_Event(bms.boot.event));
	field_uint(&json, "attempts", bms.boot.attempts);
	field_uint(&json, "failures", bms.boot.failures);
	field_uint(&json, "confirm_in_s", bms.boot.confirm_left_s);
	field_bool(&json, "iwdg", bms.boot.iwdg);
	field_bool(&json, "backup", bms.boot.backup_valid);
	field_str(&json, "backup_version", bms.boot.backup_valid ? backup_version : "");
	field_uint(&json, "updates", bms.boot.updates);
	field_uint(&json, "rollbacks", bms.boot.rollbacks);
	field_str(&json, "last_reason", BootNames_Error(bms.boot.last_reason));
	json_close(&json, '}');

	json_open(&json, "bt", '{');
	field_str(&json, "state", Rn4871_StateName(bms.bt.state));
	field_bool(&json, "connected", bms.bt.connected);
	field_bool(&json, "alive", bms.bt.rn4871_alive);
	field_uint(&json, "probes", bms.bt.probes);
	field_uint(&json, "probe_failures", bms.bt.probe_failures);
	json_close(&json, '}');

	// contadores das linhas ANTERIORES; sent avanca um por linha (sequencia para a UI)
	json_open(&json, "json", '{');
	field_uint(&json, "sent", link->json_sent);
	field_uint(&json, "dropped", link->json_dropped);
	field_uint(&json, "last_len", link->last_json_bytes);
	field_uint(&json, "dma_errors", link->dma_errors);
	field_uint(&json, "dma_restarts", link->dma_restarts);
	field_uint(&json, "bt_reboots", bms.bt.rn4871_reboots);
	json_close(&json, '}');

	// por slave
	json_open(&json, "cell_mV", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		json_open(&json, NULL, '[');
		for (uint8_t cell_index = 0; cell_index < config->cells_per_slave; cell_index++) {
			key(&json, NULL);
			put_uint(&json, cells->cell_mV[slave_index][cell_index]);
		}
		json_close(&json, ']');
	}
	json_close(&json, ']');

	json_open(&json, "ntc_dC", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		json_open(&json, NULL, '[');
		for (uint8_t ntc_index = 0; ntc_index < config->ntcs_per_slave; ntc_index++) {
			key(&json, NULL);
			put_int(&json, cells->ntc_dC[slave_index][ntc_index]);
		}
		json_close(&json, ']');
	}
	json_close(&json, ']');

	json_open(&json, "die_dC", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		key(&json, NULL);
		put_int(&json, cells->die_dC[slave_index]);
	}
	json_close(&json, ']');

	json_open(&json, "pec_flags", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		key(&json, NULL);
		put_uint(&json, bms.pec.flags[slave_index]);
	}
	json_close(&json, ']');

	json_open(&json, "ow_cell_mask", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		uint32_t mask = 0;
		for (uint8_t cell_index = 0; cell_index < config->cells_per_slave; cell_index++) {
			mask |= cells->cell_open_wire[slave_index][cell_index] ? (1u << cell_index) : 0u;
		}
		key(&json, NULL);
		put_uint(&json, mask);
	}
	json_close(&json, ']');

	json_open(&json, "ow_ntc_mask", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		uint32_t mask = 0;
		for (uint8_t ntc_index = 0; ntc_index < config->ntcs_per_slave; ntc_index++) {
			mask |= cells->ntc_open_wire[slave_index][ntc_index] ? (1u << ntc_index) : 0u;
		}
		key(&json, NULL);
		put_uint(&json, mask);
	}
	json_close(&json, ']');

	// idade dos dados de cada slave: com dados velhos os numeros acima nao valem
	json_open(&json, "cells_age_ms", '[');
	for (uint8_t slave_index = 0; slave_index < slaves; slave_index++) {
		uint32_t valid_ms = bms.adbms.cells_valid_ms[slave_index];
		key(&json, NULL);
		put_uint(&json, (valid_ms != 0u) ? (now_ms - valid_ms) : UINT32_MAX);
	}
	json_close(&json, ']');

	json_close(&json, '}');

	if (json.overflow) {
		return 0;
	}

	out[json.length++] = '\r';
	out[json.length++] = '\n';
	out[json.length] = '\0';
	return json.length;
}

/* ================== ENVIO ================== */

void LiveJson_Init(void) {

	memset(last_ms, 0, sizeof(last_ms));
	transitions_seen = bms.ams_error.transitions;
}

void LiveJson_Step(uint32_t now_ms) {

	// eventos da linha AMS_ERROR (a BMS_UI regista estas linhas)
	if (bms.ams_error.transitions != transitions_seen) {
		const char *event = bms.ams_error.active ? "AMS_ERROR line -> ERROR\r\n" : "AMS_ERROR line -> OK\r\n";
		transitions_seen = bms.ams_error.transitions;
		(void) Console_Print(CONSOLE_UART, event);
		(void) Console_Print(CONSOLE_BLE, event);
	}

	for (uint8_t console = 0; console < CONSOLE_COUNT; console++) {

		int32_t period_ms = Console_LivePeriod(console);
		bool requested = Console_TakeJsonRequest(console);
		bool due = requested || ((period_ms > 0) && ((now_ms - last_ms[console]) >= (uint32_t) period_ms));

		if (!due) {
			continue;
		}
		if ((console == CONSOLE_BLE) && !Rn4871_DataMode()) {
			continue;   // o RN4871 esta em modo de comando: o JSON espera
		}
		last_ms[console] = now_ms;

		bms_link_t *link = (console == CONSOLE_BLE) ? &bms.uart2 : &bms.uart1;
		uint16_t length = LiveJson_Build(buffer, sizeof(buffer), console, now_ms);
		bool sent = (length > 0u) && Console_Print(console, buffer);

		if (sent) {
			link->json_sent++;
			link->last_json_bytes = length;
		} else {
			link->json_dropped++;
		}
	}
}
