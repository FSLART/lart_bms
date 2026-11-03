/*
 * console.c
 *
 * Ver console.h. As listagens grandes (get all, diff, faults, eeprom) saem
 * uma linha de cada vez, so quando ha espaco no ring do UART, para nunca
 * empurrar o JSON para fora nem bloquear o brain_loop.
 */

#include "console.h"
#include "uart_link.h"
#include "rn4871.h"
#include "config_store.h"
#include "faults.h"
#include "ams_error.h"
#include "precharge.h"
#include "charger.h"
#include "bsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LINE_MAX          192u
#define REPLY_MAX         224u
#define JOB_MIN_FREE      512u    // bytes livres no ring antes de mais uma linha de listagem
#define PIN_ATTEMPTS      3u
#define PIN_LOCKOUT_MS    10000u
#define EEPROM_DUMP_LINE  16u

typedef enum {
	JOB_NONE = 0,
	JOB_CONFIG,
	JOB_DIFF,
	JOB_FAULTS,
	JOB_EEPROM
} console_job_t;

typedef struct {
	char line[LINE_MAX];
	uint8_t length;
	bool overflow;
	bool unlocked;
	uint8_t pin_failures;
	uint32_t pin_lockout_ms;
	console_job_t job;
	uint16_t job_index;
	int32_t live_period_ms;    // -1 = o da config
	bool json_request;
} console_t;

static console_t consoles[CONSOLE_COUNT];
static bool reboot_requested;
static bool bootloader_requested;
static int8_t save_console = -1;          // quem pediu o save (para o resultado)
static eeprom_state_t eeprom_seen;

/* ================== SAIDA ================== */

bool Console_Print(uint8_t console, const char *text) {

	uint16_t length = (uint16_t) strlen(text);

	if (console == CONSOLE_BLE) {
		return Rn4871_Write(text, length);
	}
	return UartLink_Write(UART_LINK_1, text, length);
}

static bool reply(uint8_t console, const char *format, ...) {

	char text[REPLY_MAX];
	va_list arguments;

	va_start(arguments, format);
	int length = vsnprintf(text, sizeof(text) - 2u, format, arguments);
	va_end(arguments);

	if (length < 0) {
		return false;
	}
	if ((size_t) length > (sizeof(text) - 3u)) {
		length = (int) (sizeof(text) - 3u);
	}

	// nunca devolver um '$' pelo BLE: "$$$" mete o RN4871 em modo de comando
	for (int index = 0; index < length; index++) {
		if (text[index] == '$') {
			text[index] = '_';
		}
	}
	text[length++] = '\r';
	text[length++] = '\n';
	text[length] = '\0';

	return Console_Print(console, text);
}

static uint16_t free_space(uint8_t console) {

	if ((console == CONSOLE_BLE) && !Rn4871_DataMode()) {
		return 0;
	}
	return UartLink_Free((console == CONSOLE_BLE) ? UART_LINK_2 : UART_LINK_1);
}

/* ================== AJUDAS ================== */

static char* skip_spaces(char *text) {

	while ((*text == ' ') || (*text == '\t')) {
		text++;
	}
	return text;
}

static char* next_word(char **cursor) {

	char *word = skip_spaces(*cursor);
	char *end = word;

	while ((*end != '\0') && (*end != ' ') && (*end != '\t')) {
		end++;
	}
	if (*end != '\0') {
		*end++ = '\0';
	}
	*cursor = end;
	return word;
}

static void trim_end(char *text) {

	size_t length = strlen(text);

	while ((length > 0u) && ((text[length - 1u] == ' ') || (text[length - 1u] == '\t'))) {
		text[--length] = '\0';
	}
}

static void lowercase(char *text) {

	for (; *text != '\0'; text++) {
		*text = (char) tolower((unsigned char) *text);
	}
}

static bool locked(uint8_t console) {

	return (console == CONSOLE_BLE) && (bms.config.bt_pin != 0u) && !consoles[console].unlocked;
}

/* "nome" ou "nome[i]": indice na tabela e elemento (-1 = tudo) */
static int find_param(char *key, int *element) {

	char *bracket = strchr(key, '[');

	*element = -1;
	if (bracket != NULL) {
		char *stop;
		long index = strtol(bracket + 1, &stop, 10);
		if ((*stop != ']') || (index < 0) || (stop[1] != '\0')) {
			return -1;
		}
		*element = (int) index;
		*bracket = '\0';
	}

	return Config_Find(key);
}

static void print_param(uint8_t console, const char *prefix, const bms_config_t *config, uint16_t param_index) {

	const cfg_param_t *param = &config_params[param_index];
	char value[160];

	(void) Config_FormatValue(config, param_index, value, sizeof(value));

	if ((param->type == CFG_STR) || (param->type == CFG_FAULTS)) {
		(void) reply(console, "#%s %s=%s%s", prefix, param->key, value, ((param->flags & CFG_FLAG_REBOOT) != 0u) ? " R" : "");
	} else {
		(void) reply(console, "#%s %s=%s %s [%ld..%ld]%s", prefix, param->key, value, param->unit, (long) param->min, (long) param->max,
				((param->flags & CFG_FLAG_REBOOT) != 0u) ? " R" : "");
	}
}

/* ================== COMANDOS ================== */

static void command_status(uint8_t console) {

	(void) reply(console, "#STATUS ams=%s pre=%s chg=%s hv_safe=%d ams_error=%d faults=%u cfg=%s seq=%lu dirty=%d reboot=%d eeprom=%d bt=%s ble=%d up=%lus",
			brain_state_name(bms.system.state), Precharge_StateName(bms.precharge.state), Charger_StateName(bms.charger.state), bms.system.hv_safe ? 1 : 0, bms.ams_error.active ? 1 : 0,
			bms.ams_error.fault_count, bms.config.loaded ? "eeprom" : "defaults", (unsigned long) bms.config.sequence, bms.eeprom.dirty ? 1 : 0,
			bms.eeprom.reboot_pending ? 1 : 0, bms.eeprom.present ? 1 : 0, Rn4871_StateName(bms.bt.state), bms.bt.connected ? 1 : 0,
			(unsigned long) (bms.system.uptime_ms / 1000u));
}

static void command_get(uint8_t console, char *arguments) {

	char *key = next_word(&arguments);
	int element;

	if ((strcmp(key, "all") == 0) || (key[0] == '\0')) {
		consoles[console].job = JOB_CONFIG;
		consoles[console].job_index = 0;
		return;
	}

	int param_index = find_param(key, &element);

	if (param_index < 0) {
		(void) reply(console, "#ERR parametro desconhecido: %s", key);
		return;
	}

	if (element >= 0) {
		int32_t value;
		if (!Config_GetElement(ConfigStore_Edit(), (uint16_t) param_index, (uint8_t) element, &value)) {
			(void) reply(console, "#ERR indice invalido");
			return;
		}
		(void) reply(console, "#CFG %s[%d]=%ld", config_params[param_index].key, element, (long) value);
		return;
	}

	print_param(console, "CFG", ConfigStore_Edit(), (uint16_t) param_index);
}

static void command_set(uint8_t console, char *arguments) {

	char *key = next_word(&arguments);
	char *value = skip_spaces(arguments);
	int element;

	trim_end(value);

	int param_index = find_param(key, &element);

	if (param_index < 0) {
		(void) reply(console, "#ERR parametro desconhecido: %s", key);
		return;
	}
	if (value[0] == '\0') {
		(void) reply(console, "#ERR falta o valor");
		return;
	}

	const char *problem = Config_ParseValue(ConfigStore_Edit(), (uint16_t) param_index, element, value);

	if (problem != NULL) {
		const cfg_param_t *param = &config_params[param_index];
		(void) reply(console, "#ERR %s: %s [%ld..%ld]", param->key, problem, (long) param->min, (long) param->max);
		return;
	}

	ConfigStore_MarkDirty();
	print_param(console, "OK", ConfigStore_Edit(), (uint16_t) param_index);
}

static void command_save(uint8_t console) {

	const char *problem = ConfigStore_Save();

	if (problem != NULL) {
		(void) reply(console, "#ERR save: %s", problem);
		return;
	}

	save_console = (int8_t) console;
	(void) reply(console, "#OK save: a gravar (sequencia %lu)", (unsigned long) (bms.eeprom.sequence + 1u));
}

static void command_unlock(uint8_t console, char *arguments, uint32_t now_ms) {

	console_t *state = &consoles[console];
	long pin = strtol(skip_spaces(arguments), NULL, 10);

	if ((state->pin_failures >= PIN_ATTEMPTS) && ((now_ms - state->pin_lockout_ms) < PIN_LOCKOUT_MS)) {
		(void) reply(console, "#ERR espera");
		return;
	}
	if ((bms.config.bt_pin == 0u) || (pin == (long) bms.config.bt_pin)) {
		state->unlocked = true;
		state->pin_failures = 0;
		(void) reply(console, "#OK unlocked");
		return;
	}

	state->pin_failures++;
	state->pin_lockout_ms = now_ms;
	(void) reply(console, "#ERR pin");
}

static void command_fan(uint8_t console, char *arguments) {

	char *which = next_word(&arguments);
	char *mode = next_word(&arguments);
	bms_fan_t *fan = (strcmp(which, "lv") == 0) ? &bms.fan_lv : ((strcmp(which, "cells") == 0) ? &bms.fan_cells : NULL);

	if ((fan == NULL) || ((strcmp(mode, "max") != 0) && (strcmp(mode, "auto") != 0))) {
		(void) reply(console, "#ERR fan <cells|lv> <max|auto>");
		return;
	}

	fan->bypass_on = (strcmp(mode, "max") == 0);
	(void) reply(console, "#OK fan %s %s", which, mode);
}

static void command_live(uint8_t console, char *arguments) {

	char *mode = next_word(&arguments);

	if (strcmp(mode, "on") == 0) {
		uint16_t configured = (console == CONSOLE_BLE) ? bms.config.live_period_ms : bms.config.live_uart1_ms;
		consoles[console].live_period_ms = (configured > 0u) ? configured : 1000;
	} else if (strcmp(mode, "off") == 0) {
		consoles[console].live_period_ms = 0;
	} else {
		long period = strtol(mode, NULL, 10);
		if ((period < 100) || (period > 60000)) {
			(void) reply(console, "#ERR live <on|off|100..60000>");
			return;
		}
		consoles[console].live_period_ms = period;
	}

	(void) reply(console, "#OK live=%ld", (long) consoles[console].live_period_ms);
}

static void command_help(uint8_t console) {

	(void) reply(console, "#HELP status | get <nome|all> | set <nome>[i] <valor> | diff | apply | save | revert | defaults");
	(void) reply(console, "#HELP faults | live <on|off|ms> | json | fan <cells|lv> <max|auto> | bt reboot | eeprom");
	(void) reply(console, "#HELP version | reboot | bootloader | unlock <pin>");
}

void Console_Execute(uint8_t console, char *line) {

	uint32_t now_ms = HAL_GetTick();
	char *cursor = line;
	char *command = next_word(&cursor);

	lowercase(command);

	if (command[0] == '\0') {
		return;
	}

	// o que altera alguma coisa pede o PIN no BLE
	bool changes = (strcmp(command, "set") == 0) || (strcmp(command, "apply") == 0) || (strcmp(command, "save") == 0) || (strcmp(command, "revert") == 0)
			|| (strcmp(command, "defaults") == 0) || (strcmp(command, "reboot") == 0) || (strcmp(command, "bootloader") == 0) || (strcmp(command, "fan") == 0)
			|| (strcmp(command, "bt") == 0);

	if (changes && locked(console)) {
		(void) reply(console, "#ERR bloqueado: unlock <pin>");
		return;
	}

	if (strcmp(command, "status") == 0) {
		command_status(console);
	} else if (strcmp(command, "get") == 0) {
		command_get(console, cursor);
	} else if (strcmp(command, "set") == 0) {
		command_set(console, cursor);
	} else if (strcmp(command, "diff") == 0) {
		consoles[console].job = JOB_DIFF;
		consoles[console].job_index = 0;
	} else if (strcmp(command, "apply") == 0) {
		const char *problem = ConfigStore_Apply();
		if (problem != NULL) {
			(void) reply(console, "#ERR apply: %s", problem);
		} else {
			(void) reply(console, "#OK apply%s", bms.eeprom.reboot_pending ? " (ha parametros que so mudam depois de reiniciar)" : "");
		}
	} else if (strcmp(command, "save") == 0) {
		command_save(console);
	} else if (strcmp(command, "revert") == 0) {
		ConfigStore_Revert();
		(void) reply(console, "#OK revert");
	} else if (strcmp(command, "defaults") == 0) {
		ConfigStore_Defaults();
		(void) reply(console, "#OK defaults na copia (apply/save para usar)");
	} else if (strcmp(command, "faults") == 0) {
		consoles[console].job = JOB_FAULTS;
		consoles[console].job_index = 0;
		(void) reply(console, "#FAULTS ativos=%u ams_error=%d", bms.ams_error.fault_count, bms.ams_error.active ? 1 : 0);
	} else if (strcmp(command, "live") == 0) {
		command_live(console, cursor);
	} else if (strcmp(command, "json") == 0) {
		consoles[console].json_request = true;
	} else if (strcmp(command, "fan") == 0) {
		command_fan(console, cursor);
	} else if (strcmp(command, "bt") == 0) {
		char *what = next_word(&cursor);
		if (strcmp(what, "reboot") == 0) {
			Rn4871_RequestReboot();
			(void) reply(console, "#OK bt reboot");
		} else {
			(void) reply(console, "#ERR bt reboot");
		}
	} else if (strcmp(command, "eeprom") == 0) {
		if (ConfigStore_Busy()) {
			(void) reply(console, "#ERR EEPROM ocupada");
		} else {
			consoles[console].job = JOB_EEPROM;
			consoles[console].job_index = 0;
		}
	} else if (strcmp(command, "version") == 0) {
		(void) reply(console, "#VERSION fw=%u schema=%u params=%u payload=%u build=%s %s", BMS_FIRMWARE_VERSION, CONFIG_SCHEMA_VERSION, config_param_count,
				Config_PayloadSize(config_param_count), __DATE__, __TIME__);
	} else if ((strcmp(command, "reboot") == 0) || (strcmp(command, "bootloader") == 0)) {
		if (!bms.system.hv_safe || Charger_SessionActive()) {
			(void) reply(console, "#ERR HV ligada");
		} else {
			bool bootloader = (strcmp(command, "bootloader") == 0);
			reboot_requested = !bootloader;
			bootloader_requested = bootloader;
			(void) reply(console, "#OK %s", command);
		}
	} else if (strcmp(command, "unlock") == 0) {
		command_unlock(console, cursor, now_ms);
	} else if (strcmp(command, "help") == 0) {
		command_help(console);
	} else {
		(void) reply(console, "#ERR comando desconhecido (help)");
	}
}

/* ================== LISTAGENS ================== */

static bool job_step(uint8_t console) {

	console_t *state = &consoles[console];

	switch (state->job) {

	case JOB_CONFIG:
		if (state->job_index >= config_param_count) {
			(void) reply(console, "#END %u parametros", config_param_count);
			return false;
		}
		print_param(console, "CFG", ConfigStore_Edit(), state->job_index++);
		return true;

	case JOB_DIFF:
		while ((state->job_index < config_param_count) && Config_ParamEqual(&bms.config, ConfigStore_Edit(), state->job_index)) {
			state->job_index++;
		}
		if (state->job_index >= config_param_count) {
			(void) reply(console, "#END diff");
			return false;
		}
		print_param(console, "DIFF", ConfigStore_Edit(), state->job_index++);
		return true;

	case JOB_FAULTS: {
		uint64_t enabled = AmsError_EnabledMask();
		while ((state->job_index < FAULT_COUNT) && (((bms.ams_error.faults_mask >> state->job_index) & 1u) == 0u)) {
			state->job_index++;
		}
		if (state->job_index >= FAULT_COUNT) {
			(void) reply(console, "#END faults");
			return false;
		}
		(void) reply(console, "#FAULT %s ams=%d", Faults_Name((uint8_t) state->job_index), (int) ((enabled >> state->job_index) & 1u));
		state->job_index++;
		return true;
	}

	case JOB_EEPROM: {
		uint8_t bytes[EEPROM_DUMP_LINE];
		char text[EEPROM_DUMP_LINE * 3u + 1u];

		if (state->job_index >= BSP_EEPROM_SIZE) {
			(void) reply(console, "#END eeprom");
			return false;
		}
		if (ConfigStore_Busy() || !Bsp_EepromRead(state->job_index, bytes, EEPROM_DUMP_LINE)) {
			(void) reply(console, "#ERR leitura da EEPROM");
			return false;
		}
		for (uint8_t index = 0; index < EEPROM_DUMP_LINE; index++) {
			(void) snprintf(&text[index * 3u], 4, "%02X ", bytes[index]);
		}
		text[(EEPROM_DUMP_LINE * 3u) - 1u] = '\0';
		(void) reply(console, "#EE %03X %s", state->job_index, text);
		state->job_index = (uint16_t) (state->job_index + EEPROM_DUMP_LINE);
		return true;
	}

	case JOB_NONE:
	default:
		return false;
	}
}

/* ================== ENTRADA ================== */

void Console_Init(void) {

	memset(consoles, 0, sizeof(consoles));
	for (uint8_t console = 0; console < CONSOLE_COUNT; console++) {
		consoles[console].live_period_ms = -1;
	}
	reboot_requested = false;
	bootloader_requested = false;
	save_console = -1;
	eeprom_seen = bms.eeprom.state;
}

void Console_LinkReset(uint8_t console) {

	if (console < CONSOLE_COUNT) {
		consoles[console].unlocked = false;
		consoles[console].length = 0;
		consoles[console].overflow = false;
		consoles[console].job = JOB_NONE;
	}
}

void Console_Feed(uint8_t console, char character) {

	console_t *state = &consoles[console];

	if ((character == '\r') || (character == '\n')) {
		if (state->overflow) {
			(void) reply(console, "#ERR linha demasiado comprida");
		} else if (state->length > 0u) {
			state->line[state->length] = '\0';
			Console_Execute(console, state->line);
		}
		state->length = 0;
		state->overflow = false;
		return;
	}

	if ((unsigned char) character < ' ') {
		return;   // outros caracteres de controlo ignorados
	}
	if (state->length < (LINE_MAX - 1u)) {
		state->line[state->length++] = character;
	} else {
		state->overflow = true;
	}
}

void Console_Step(uint32_t now_ms) {

	uint8_t bytes[64];
	uint16_t count;

	(void) now_ms;

	// UART1 por cabo; o BLE chega pelo Rn4871_Step
	while ((count = UartLink_Read(UART_LINK_1, bytes, sizeof(bytes))) > 0u) {
		for (uint16_t index = 0; index < count; index++) {
			Console_Feed(CONSOLE_UART, (char) bytes[index]);
		}
	}

	for (uint8_t console = 0; console < CONSOLE_COUNT; console++) {
		while ((consoles[console].job != JOB_NONE) && (free_space(console) >= JOB_MIN_FREE)) {
			if (!job_step(console)) {
				consoles[console].job = JOB_NONE;
			}
		}
	}

	// resultado do save, para quem o pediu
	if ((bms.eeprom.state != eeprom_seen) && (save_console >= 0)) {
		if (bms.eeprom.state == EEPROM_DONE) {
			(void) reply((uint8_t) save_console, "#SAVE ok sequencia=%lu banco=%c%s", (unsigned long) bms.eeprom.sequence, (bms.eeprom.bank == 0u) ? 'A' : 'B',
					bms.eeprom.reboot_pending ? " (reiniciar para os parametros R)" : "");
			save_console = -1;
		} else if (bms.eeprom.state == EEPROM_FAILED) {
			(void) reply((uint8_t) save_console, "#SAVE falhou (a config anterior continua na EEPROM)");
			save_console = -1;
		}
	}
	eeprom_seen = bms.eeprom.state;
}

int32_t Console_LivePeriod(uint8_t console) {

	if (consoles[console].live_period_ms >= 0) {
		return consoles[console].live_period_ms;
	}
	return (console == CONSOLE_BLE) ? (int32_t) bms.config.live_period_ms : (int32_t) bms.config.live_uart1_ms;
}

bool Console_TakeJsonRequest(uint8_t console) {

	bool requested = consoles[console].json_request;

	consoles[console].json_request = false;
	return requested;
}

bool Console_TakeRebootRequest(void) {

	bool requested = reboot_requested;

	reboot_requested = false;
	return requested;
}

bool Console_TakeBootloaderRequest(void) {

	bool requested = bootloader_requested;

	bootloader_requested = false;
	return requested;
}
