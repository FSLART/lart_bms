/*
 * test_system.c - testes do BMS inteiro no PC, sem hardware.
 *
 * Codigo real: brain.c e todos os modulos (config, EEPROM A/B, precarga,
 * carregador, ISA, SOC, AMS_ERROR, CAN, RN4871, consola, JSON). A cadeia
 * ADBMS e simulada (fake_chain.c) e a placa tambem (fake_board.c: pinos,
 * EEPROM 24FC08, CAN, UART e o proprio RN4871).
 * Correr com Tests/run_tests.sh.
 */

#include "fake_chain.h"
#include "fake_board.h"
#include "bms_config.h"
#include "config_store.h"
#include "faults.h"
#include "precharge.h"
#include "charger.h"
#include "soc.h"
#include "console.h"
#include "live_json.h"
#include "powertrain_t26.h"
#include "handcart_t26.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ================== AJUDAS ================== */

// arranque com a EEPROM que estiver na placa falsa
static void start(void) {

	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(700);
}

static void boot(void) {

	fake_reset();
	start();
}

// "reiniciar": a EEPROM fica, o resto volta ao inicio
static void reboot_keep_eeprom(void) {

	uint8_t saved[BSP_EEPROM_SIZE];

	memcpy(saved, board.eeprom, sizeof(saved));
	fake_reset();
	memcpy(board.eeprom, saved, sizeof(saved));
	start();
}

static void console_uart(const char *line) {

	char copy[256];

	snprintf(copy, sizeof(copy), "%s", line);
	Console_Execute(CONSOLE_UART, copy);
}

static bool output_has(uint8_t link, const char *text) {

	return strstr(fake_uart_output(link), text) != NULL;
}

static void vcu_request(uint8_t request) {

	uint8_t data[1] = { request };
	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, false, data, 1);
}

static void handcart_switch(uint8_t on) {

	uint8_t data[1] = { on };
	fake_can_inject(CAN_BUS_2, HANDCART_T26_HANDCART_SWITCH_FEEDBACK_FRAME_ID, false, data, 1);
}

static void charger_status(bool fault) {

	struct handcart_t26_charger_status_p1000_t message = { 0 };
	uint8_t data[8];

	message.output_voltage = 5300;
	message.output_current = 55;
	message.starting_state_fault = fault ? 1u : 0u;
	message.charger_temperature = 75;   // 35 C
	handcart_t26_charger_status_p1000_pack(data, &message, sizeof(data));
	fake_can_inject(CAN_BUS_2, HANDCART_T26_CHARGER_STATUS_P1000_FRAME_ID, true, data, 8);
}

// corre ate a condicao, em passos de 1 ms; devolve os ms ou limit_ms
static uint32_t ms_until_state(precharge_state_t state, uint32_t limit_ms) {

	for (uint32_t elapsed = 0; elapsed < limit_ms; elapsed++) {
		if (bms.precharge.state == state) {
			return elapsed;
		}
		run_ms(1);
	}
	return limit_ms;
}

/* ================== CONFIG ================== */

static void test_config_table(void) {

	bms_config_t config;

	Config_SetDefaults(&config);
	CHECK(Config_Validate(&config) == NULL);
	CHECK(Config_Clamp(&config) == 0u);   // os defaults estao todos dentro das gamas
	CHECK(Config_PayloadSize(config_param_count) <= CONFIG_BANK_PAYLOAD_MAX);

	for (uint16_t first = 0; first < config_param_count; first++) {
		CHECK(config_params[first].count > 0u);
		CHECK(Config_Find(config_params[first].key) == first);   // nomes unicos
	}

	// a tabela cobre o struct: cada campo de config tem um parametro
	CHECK(Config_Find("cell_max_mV") >= 0);
	CHECK(Config_Find("soc_curve_mV") >= 0);
	CHECK(Config_Find("bt_name") >= 0);
	CHECK(Config_Find("ams_error_enable") >= 0);
	CHECK(Config_Find("can1_kbps") >= 0);
}

static void test_config_text(void) {

	bms_config_t config;
	char text[200];
	int cell_max = Config_Find("cell_max_mV");
	int curve = Config_Find("soc_curve_mV");
	int name = Config_Find("bt_name");
	int ams = Config_Find("ams_error_enable");
	int mask = Config_Find("contactor_check_mask");

	Config_SetDefaults(&config);

	CHECK(Config_ParseValue(&config, (uint16_t) cell_max, -1, "4180") == NULL);
	CHECK(config.cell_max_mV == 4180);
	CHECK(Config_ParseValue(&config, (uint16_t) cell_max, -1, "9000") != NULL);   // fora da gama
	CHECK(config.cell_max_mV == 4180);
	CHECK(Config_ParseValue(&config, (uint16_t) cell_max, -1, "41x") != NULL);
	CHECK(Config_ParseValue(&config, (uint16_t) cell_max, -1, "0042") != NULL);    // 42: fora da gama (nao e octal)

	CHECK(Config_ParseValue(&config, (uint16_t) curve, 3, "3300") == NULL);
	CHECK(config.soc_curve_mV[3] == 3300);
	CHECK(Config_ParseValue(&config, (uint16_t) curve, -1, "2500,2600") != NULL);  // faltam valores
	CHECK(Config_ParseValue(&config, (uint16_t) curve, -1, "2500,2600,2700,2800,2900,3000,3100,3200,3300,3400,3500,3600") == NULL);
	CHECK(config.soc_curve_mV[11] == 3600);
	(void) Config_FormatValue(&config, (uint16_t) curve, text, sizeof(text));
	CHECK(strcmp(text, "2500,2600,2700,2800,2900,3000,3100,3200,3300,3400,3500,3600") == 0);

	CHECK(Config_ParseValue(&config, (uint16_t) name, -1, "LART BMS 2") == NULL);
	CHECK(strcmp(config.bt_name, "LART BMS 2") == 0);
	CHECK(Config_ParseValue(&config, (uint16_t) name, -1, "a$$$b") != NULL);        // o RN4871 entrava em modo de comando
	CHECK(Config_ParseValue(&config, (uint16_t) name, -1, "nome,com,virgula") != NULL);

	CHECK(Config_ParseValue(&config, (uint16_t) ams, -1, "+PEC_ERROR") == NULL);
	CHECK(config.ams_error_enable.pec_error);
	CHECK(Config_ParseValue(&config, (uint16_t) ams, -1, "-pec_error") == NULL);
	CHECK(!config.ams_error_enable.pec_error);
	CHECK(Config_ParseValue(&config, (uint16_t) ams, -1, "+NAO_EXISTE") != NULL);

	CHECK(Config_ParseValue(&config, (uint16_t) mask, -1, "0x0F") == NULL);
	CHECK(config.contactor_check_mask == 0x0F);
	(void) Config_FormatValue(&config, (uint16_t) mask, text, sizeof(text));
	CHECK(strcmp(text, "0xF") == 0);

	// incoerente: o maximo abaixo do aviso
	Config_SetDefaults(&config);
	config.cell_max_mV = 4100;
	CHECK(Config_Validate(&config) != NULL);
	Config_SetDefaults(&config);
	config.ntc_disabled_mask[4] = 0x003F;   // slave 5 sem NTC: FS EV 5.8.3
	CHECK(Config_Validate(&config) != NULL);
	Config_SetDefaults(&config);
	config.can1_kbps = 333;
	CHECK(Config_Validate(&config) != NULL);
}

static void test_config_image(void) {

	static uint8_t image[CONFIG_BANK_SIZE];
	bms_config_t config;
	bms_config_t loaded;
	uint32_t sequence;

	Config_SetDefaults(&config);
	config.cell_max_mV = 4190;
	config.temp_compensation_adjust_mV[2] = -15;
	config.ams_error_enable.pec_error = true;
	strcpy(config.bt_name, "TESTE");

	uint16_t length = ConfigStore_BuildImage(&config, 7, image, sizeof(image));
	CHECK(length > 0u);
	CHECK(ConfigStore_ParseImage(image, length, &loaded, &sequence));
	CHECK(sequence == 7u);
	CHECK(loaded.cell_max_mV == 4190);
	CHECK(loaded.temp_compensation_adjust_mV[2] == -15);   // com sinal
	CHECK(loaded.ams_error_enable.pec_error);
	CHECK(strcmp(loaded.bt_name, "TESTE") == 0);

	// um bit trocado em qualquer lado: o CRC apanha
	image[40] ^= 0x10u;
	CHECK(!ConfigStore_ParseImage(image, length, &loaded, &sequence));
	image[40] ^= 0x10u;

	// EEPROM vazia
	memset(image, 0xFF, sizeof(image));
	CHECK(!ConfigStore_ParseImage(image, sizeof(image), &loaded, &sequence));

	// firmware antigo com menos parametros: le-se, os novos ficam no default
	length = ConfigStore_BuildImage(&config, 3, image, sizeof(image));
	uint16_t old_count = (uint16_t) (config_param_count - 2u);
	uint16_t old_payload = Config_PayloadSize(old_count);
	uint32_t schema = Config_SchemaCrc(old_count);
	image[8] = (uint8_t) old_count;
	image[9] = (uint8_t) (old_count >> 8);
	image[10] = (uint8_t) old_payload;
	image[11] = (uint8_t) (old_payload >> 8);
	for (uint8_t byte_index = 0; byte_index < 4u; byte_index++) {
		image[12 + byte_index] = (uint8_t) (schema >> (8u * byte_index));
	}
	uint32_t crc = Config_Crc32(image, CONFIG_BANK_HEADER + old_payload, 0);
	for (uint8_t byte_index = 0; byte_index < 4u; byte_index++) {
		image[CONFIG_BANK_HEADER + old_payload + byte_index] = (uint8_t) (crc >> (8u * byte_index));
	}
	CHECK(ConfigStore_ParseImage(image, (uint16_t) (CONFIG_BANK_HEADER + old_payload + 4u), &loaded, &sequence));
	CHECK(loaded.cell_max_mV == 4190);
	CHECK(loaded.ams_error_min_on_ms == config_defaults.ams_error_min_on_ms);

	// parametro mudado de tipo (esquema diferente): rejeitado
	image[12] ^= 0x01u;
	crc = Config_Crc32(image, CONFIG_BANK_HEADER + old_payload, 0);
	for (uint8_t byte_index = 0; byte_index < 4u; byte_index++) {
		image[CONFIG_BANK_HEADER + old_payload + byte_index] = (uint8_t) (crc >> (8u * byte_index));
	}
	CHECK(!ConfigStore_ParseImage(image, (uint16_t) (CONFIG_BANK_HEADER + old_payload + 4u), &loaded, &sequence));
}

/* ================== EEPROM ================== */

static void test_eeprom_blank_uses_defaults(void) {

	boot();

	CHECK(!bms.config.loaded);
	CHECK(bms.faults.config_invalid);
	CHECK(bms.config.cell_max_mV == 4200);
	CHECK(bms.eeprom.present);
	CHECK(!bms.faults.eeprom_error);
}

static void save_and_wait(void) {

	console_uart("save");
	for (uint32_t elapsed = 0; (elapsed < 2000u) && ConfigStore_Busy(); elapsed++) {
		run_ms(1);
	}
}

static void test_eeprom_save_banks(void) {

	boot();
	fake_uart_clear(UART_LINK_1);

	console_uart("set cell_max_mV 4190");
	CHECK(output_has(UART_LINK_1, "#OK cell_max_mV=4190"));
	CHECK(bms.eeprom.dirty);
	CHECK(bms.config.cell_max_mV == 4200);   // so a copia mudou

	save_and_wait();
	CHECK(bms.eeprom.state == EEPROM_DONE);
	CHECK(bms.eeprom.bank == 0u);            // primeiro no banco A
	CHECK(bms.eeprom.sequence == 1u);
	CHECK(bms.config.cell_max_mV == 4190);   // e aplicado a quente
	CHECK(bms.config.loaded);
	CHECK(!bms.faults.config_invalid);
	CHECK(board.eeprom_max_page_bytes <= BSP_EEPROM_PAGE);
	CHECK(output_has(UART_LINK_1, "#SAVE ok sequencia=1 banco=A"));

	console_uart("set cell_max_mV 4180");
	save_and_wait();
	CHECK(bms.eeprom.bank == 1u);            // depois no B, o A fica como estava
	CHECK(bms.eeprom.sequence == 2u);

	reboot_keep_eeprom();
	CHECK(bms.config.loaded);
	CHECK(bms.config.cell_max_mV == 4180);   // o mais recente
	CHECK(bms.eeprom.bank == 1u);
	CHECK(!bms.faults.config_invalid);

	// banco B estragado: volta ao A
	board.eeprom[CONFIG_BANK_SIZE + 30u] ^= 0xFFu;
	reboot_keep_eeprom();
	CHECK(bms.config.loaded);
	CHECK(bms.config.cell_max_mV == 4190);
	CHECK(bms.eeprom.bank == 0u);
}

static void test_eeprom_power_cut_mid_write(void) {

	boot();
	console_uart("set cell_max_mV 4190");
	save_and_wait();
	CHECK(bms.eeprom.bank == 0u);

	// o segundo save comeca no banco B e a energia vai-se a meio
	console_uart("set cell_max_mV 4170");
	console_uart("save");
	run_ms(40);
	CHECK(ConfigStore_Busy());
	reboot_keep_eeprom();

	CHECK(bms.config.loaded);
	CHECK(bms.config.cell_max_mV == 4190);   // a config anterior, intacta
	CHECK(bms.eeprom.bank == 0u);
}

static void test_eeprom_save_needs_hv_off(void) {

	boot();
	vcu_request(1);
	run_ms(100);
	CHECK(!bms.system.hv_safe);

	fake_uart_clear(UART_LINK_1);
	console_uart("set cell_max_mV 4190");
	console_uart("save");
	CHECK(output_has(UART_LINK_1, "#ERR save: HV ligada"));
	CHECK(!ConfigStore_Busy());
	CHECK(bms.config.cell_max_mV == 4200);
}

static void test_eeprom_reboot_params(void) {

	boot();
	fake_uart_clear(UART_LINK_1);
	console_uart("set slaves 10");
	CHECK(output_has(UART_LINK_1, "#OK slaves=10"));
	save_and_wait();
	CHECK(bms.eeprom.state == EEPROM_DONE);
	CHECK(bms.config.slaves == 12);           // topologia so muda ao reiniciar
	CHECK(bms.eeprom.reboot_pending);

	reboot_keep_eeprom();
	CHECK(bms.config.slaves == 10);
	CHECK(bms.faults.slave_missing);          // a cadeia falsa continua com 12
}

/* ================== CONSOLA ================== */

static void test_console_commands(void) {

	boot();
	fake_uart_clear(UART_LINK_1);

	console_uart("get cell_min_mV");
	CHECK(output_has(UART_LINK_1, "#CFG cell_min_mV=2500 mV [2000..4000]"));
	console_uart("get soc_curve_mV[11]");
	CHECK(output_has(UART_LINK_1, "#CFG soc_curve_mV[11]=4200"));
	console_uart("set nao_existe 1");
	CHECK(output_has(UART_LINK_1, "#ERR parametro desconhecido"));
	console_uart("set cell_min_mV 1");
	CHECK(output_has(UART_LINK_1, "#ERR cell_min_mV: fora da gama [2000..4000]"));
	console_uart("status");
	CHECK(output_has(UART_LINK_1, "#STATUS ams=IDLE pre=KILL"));
	console_uart("version");
	CHECK(output_has(UART_LINK_1, "#VERSION fw=13"));

	// get all sai uma linha por parametro, sem bloquear
	fake_uart_clear(UART_LINK_1);
	console_uart("get all");
	run_ms(50);
	CHECK(output_has(UART_LINK_1, "#CFG slaves=12"));
	CHECK(output_has(UART_LINK_1, "#CFG ams_error_min_on_ms=500"));
	CHECK(output_has(UART_LINK_1, "#END"));

	// diff e revert
	fake_uart_clear(UART_LINK_1);
	console_uart("set temp_max_dC 550");
	console_uart("diff");
	run_ms(20);
	CHECK(output_has(UART_LINK_1, "#DIFF temp_max_dC=550"));
	console_uart("revert");
	CHECK(ConfigStore_Edit()->temp_max_dC == 600);

	// apply sem gravar
	console_uart("set temp_max_dC 550");
	console_uart("apply");
	CHECK(bms.config.temp_max_dC == 550);
	CHECK(!ConfigStore_Busy());

	// pedidos por linha completa, pelo UART
	fake_uart_clear(UART_LINK_1);
	fake_uart_inject(UART_LINK_1, "get bal_delta_mV\r\n");
	run_ms(5);
	CHECK(output_has(UART_LINK_1, "#CFG bal_delta_mV=20"));
}

static void test_console_reboot_and_bootloader(void) {

	boot();
	console_uart("reboot");
	run_ms(300);
	CHECK(board.reset_requested);

	boot();
	vcu_request(1);
	run_ms(50);
	fake_uart_clear(UART_LINK_1);
	console_uart("bootloader");
	CHECK(output_has(UART_LINK_1, "#ERR HV ligada"));
	run_ms(300);
	CHECK(!board.bootloader_requested);
}

/* ================== RN4871 ================== */

static void test_rn4871_sets_name_and_streams(void) {

	boot();
	run_ms(6000);   // arranque do modulo, GN, SN, reboot, GN outra vez

	CHECK(bms.bt.state == BT_DATA);
	CHECK(strcmp(board.rn_name, "LART Accumulator") == 0);
	CHECK(strcmp(bms.bt.name, "LART Accumulator") == 0);
	CHECK(bms.bt.rn4871_reboots == 1u);       // so o do nome
	CHECK(bms.bt.rn4871_alive);

	fake_uart_clear(UART_LINK_2);
	run_ms(1100);
	CHECK(output_has(UART_LINK_2, "{\"uptime_ms\":"));     // JSON a 1 Hz pelo BLE
	CHECK(!output_has(UART_LINK_2, "$"));

	// comandos pelo BLE, e o status do proprio modulo
	fake_uart_inject(UART_LINK_2, "%CONNECT,1,AABBCCDDEEFF%");
	run_ms(5);
	CHECK(bms.bt.connected);
	fake_uart_clear(UART_LINK_2);
	fake_uart_inject(UART_LINK_2, "get bal_delta_mV\n");
	run_ms(5);
	CHECK(output_has(UART_LINK_2, "#CFG bal_delta_mV=20"));
	fake_uart_inject(UART_LINK_2, "%DISCONNECT%");
	run_ms(5);
	CHECK(!bms.bt.connected);
}

static void test_rn4871_probe_and_reboot(void) {

	boot();
	run_ms(6000);
	CHECK(bms.bt.state == BT_DATA);

	uint32_t probes = bms.bt.probes;
	run_ms(31000);
	CHECK(bms.bt.probes > probes);            // a vigia de 30 s
	CHECK(bms.bt.probe_failures == 0u);

	board.rn_dead = true;                     // o modulo deixa de responder
	uint32_t reboots = bms.bt.rn4871_reboots;
	run_ms(32000);
	CHECK(bms.bt.probe_failures > 0u);
	CHECK(bms.bt.rn4871_reboots > reboots);
}

static void test_ble_pin(void) {

	boot();
	run_ms(6000);
	console_uart("set bt_pin 1234");
	console_uart("apply");
	CHECK(bms.config.bt_pin == 1234);

	fake_uart_clear(UART_LINK_2);
	fake_uart_inject(UART_LINK_2, "set cell_max_mV 4190\n");
	run_ms(5);
	CHECK(output_has(UART_LINK_2, "#ERR bloqueado"));
	CHECK(ConfigStore_Edit()->cell_max_mV == 4200);

	fake_uart_inject(UART_LINK_2, "unlock 1111\n");
	run_ms(5);
	CHECK(output_has(UART_LINK_2, "#ERR pin"));
	fake_uart_inject(UART_LINK_2, "unlock 1234\nset cell_max_mV 4190\n");
	run_ms(5);
	CHECK(output_has(UART_LINK_2, "#OK cell_max_mV=4190"));

	// ao desligar volta a trancar
	fake_uart_inject(UART_LINK_2, "%DISCONNECT%set cell_max_mV 4180\n");
	run_ms(5);
	CHECK(ConfigStore_Edit()->cell_max_mV == 4190);

	// pelo cabo nunca pede PIN
	console_uart("set cell_max_mV 4170");
	CHECK(ConfigStore_Edit()->cell_max_mV == 4170);
}

/* ================== PRECARGA ================== */

static void test_precharge_full_sequence(void) {

	boot();
	CHECK(bms.precharge.state == KILL);
	CHECK(bms.system.hv_safe);
	CHECK(!board.air_neg && !board.air_pos && !board.precharge);

	vcu_request(1);
	uint32_t ms = ms_until_state(HV_ON, 15000);
	CHECK(ms < 15000u);
	CHECK(ms >= 3000u);                       // precharge_min_ms
	CHECK(board.air_neg && board.air_pos && !board.precharge);
	CHECK(bms.precharge.hv_on_count == 1u);
	CHECK(bms.system.state == AMS_ONMISSION);
	CHECK(!bms.system.hv_safe);
	CHECK(!bms.faults.contactor_mismatch && !bms.faults.precharge_failure);

	can_frame_t frame;
	run_ms(150);
	CHECK(fake_can_last(CAN_BUS_1, POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_FRAME_ID, &frame));
	CHECK(frame.data[2] == (uint8_t) HV_ON || frame.data[4] == (uint8_t) HV_ON || frame.data[1] == (uint8_t) HV_ON);

	vcu_request(0);
	run_ms(20);
	CHECK(bms.precharge.state == KILL);
	CHECK(!board.air_neg && !board.air_pos);
	run_ms(300);
	CHECK(bms.system.hv_safe);
}

static void test_precharge_welded_air(void) {

	boot();
	board.air_pos_welded = true;
	run_ms(300);
	CHECK(!bms.system.hv_safe);               // AIR+ fechado com tudo aberto

	vcu_request(1);
	run_ms(1000);
	CHECK(bms.faults.contactor_mismatch);
	CHECK(bms.faults.precharge_failure);
	CHECK(bms.precharge.state == KILL);
	CHECK(!board.air_neg);
}

static void test_precharge_air_neg_dead(void) {

	boot();
	board.air_neg_dead = true;
	vcu_request(1);
	run_ms(1000);
	CHECK(bms.faults.contactor_mismatch);
	CHECK(bms.faults.precharge_failure);
	CHECK(bms.precharge.state == KILL);
	CHECK(bms.precharge.hv_on_count == 0u);
	CHECK(bms.precharge.expected_mask == 0x09u);   // AIR- e descarga pedidos
	CHECK((bms.precharge.actual_mask & 0x01u) == 0u);

	run_ms(1000);
	CHECK(!bms.faults.contactor_mismatch);         // limpa com tudo aberto
	CHECK(bms.faults.precharge_failure);           // este fica ate a proxima tentativa
}

static void test_precharge_bus_never_rises(void) {

	boot();
	board.isa_pack_bus_mV = 0;               // o bus nao sobe (fusivel, resistencia aberta)
	vcu_request(1);
	run_ms(9000);
	CHECK(bms.precharge.state == VERIFY_BUS_VOLT);
	CHECK(board.air_neg && board.precharge && !board.air_pos);
	run_ms(2000);
	CHECK(bms.faults.precharge_timeout);
	CHECK(bms.precharge.state == KILL);
	CHECK(!board.air_pos);
}

static void test_precharge_sdc_opens(void) {

	boot();
	vcu_request(1);
	CHECK(ms_until_state(HV_ON, 15000) < 15000u);

	board.sdc_closed = false;                // alguem carregou num botao de emergencia
	run_ms(400);
	CHECK(bms.precharge.state == KILL);
	CHECK(!bms.faults.contactor_mismatch);   // nao e um erro dos contactores
	CHECK(bms.faults.sdc_open);
	CHECK(!board.air_neg && !board.air_pos);

	// sem SDC nao arranca
	vcu_request(1);
	run_ms(500);
	CHECK(bms.precharge.state == KILL);
}

static void test_precharge_vcu_lockout(void) {

	boot();
	vcu_request(1);
	run_ms(50);
	CHECK(bms.precharge.state != KILL);
	vcu_request(0);
	run_ms(50);
	CHECK(bms.precharge.state == KILL);
	vcu_request(1);
	run_ms(50);
	CHECK(bms.precharge.state == KILL);       // 10 s de lockout
	run_ms(10000);
	vcu_request(1);
	run_ms(50);
	CHECK(bms.precharge.state != KILL);
}

/* ================== AMS_ERROR ================== */

static void test_ams_error_boot_and_faults(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	brain_start();
	CHECK(board.ams_error);                   // fail-safe desde o primeiro instante
	run_ms(700);
	CHECK(!board.ams_error);

	// OV abre a linha e os contactores, mesmo com a mascara a zero (obrigatorio)
	console_uart("set ams_error_enable 0");
	console_uart("apply");
	vcu_request(1);
	CHECK(ms_until_state(HV_ON, 15000) < 15000u);
	chain.cell_mV[2][3] = 4300;
	run_ms(300);
	CHECK(bms.faults.cell_overvoltage);
	CHECK(board.ams_error);
	CHECK(!board.air_neg && !board.air_pos);
	CHECK(bms.precharge.state == KILL);
	CHECK(bms.system.state == AMS_FAULT);

	// limpa quando a condicao passa, com o tempo minimo
	chain.cell_mV[2][3] = 3700;
	run_ms(250);
	CHECK(!bms.faults.cell_overvoltage);
	run_ms(500);
	CHECK(!board.ams_error);
}

static void test_ams_error_optional_sources(void) {

	boot();
	board.isa_pack_present = false;           // ISA do pack calada: obrigatorio
	run_ms(800);
	CHECK(bms.faults.isa_pack_timeout);
	CHECK(board.ams_error);
	board.isa_pack_present = true;
	run_ms(800);
	CHECK(!board.ams_error);

	// o PEC nao abre a linha por omissao, mas pode passar a abrir
	chain.pec_type = Status;
	chain.pec_always[4] = true;
	run_ms(400);
	CHECK(bms.faults.pec_error);
	CHECK(!bms.ams_error.sources.pec_error);
	console_uart("set ams_error_enable +PEC_ERROR");
	console_uart("apply");
	run_ms(50);
	CHECK(bms.ams_error.sources.pec_error);
	CHECK(board.ams_error);
}

/* ================== ISA / SOC ================== */

static void test_isa_overcurrent(void) {

	boot();
	board.isa_pack_current_mA = -350000;     // 350 A a descarregar (> 300 A)
	run_ms(200);
	CHECK(!bms.faults.overcurrent);           // ainda dentro dos 400 ms
	run_ms(400);
	CHECK(bms.faults.overcurrent);
	CHECK(board.ams_error);
	board.isa_pack_current_mA = -10000;
	run_ms(100);
	CHECK(!bms.faults.overcurrent);
}

static void test_soc(void) {

	boot();
	CHECK(bms.soc.ready);
	// 3700 mV na curva por omissao: entre 70 % (3741) e 60 % (3622)
	CHECK(bms.soc.initial_percent_x100 > 6000u && bms.soc.initial_percent_x100 < 7000u);

	uint16_t before = bms.soc.percent_x100;
	board.isa_pack_current_mA = -135000;     // 135 A: 13,5 Ah em 6 min = 10 %... aqui 10 s
	run_ms(10000);
	int32_t used_As = bms.soc.used_As;
	CHECK(used_As >= 1340 && used_As <= 1360);
	// 1350 As de 13,5 Ah (48600 As) = 2,78 %
	CHECK(((int32_t) before - (int32_t) bms.soc.percent_x100) >= 270 && ((int32_t) before - (int32_t) bms.soc.percent_x100) <= 285);

	CHECK(Soc_PercentFromOcv(&bms.config, 2500) == 0u);
	CHECK(Soc_PercentFromOcv(&bms.config, 4200) == 10000u);
	CHECK(Soc_PercentFromOcv(&bms.config, 3537) == 5000u);
	// a carregar a tensao lida esta acima da OCV: 4,5 A por celula x 10,4 mOhm a 20 C ~ 47 mV
	uint16_t ocv = Soc_EstimateOcv(&bms.config, 3800, 13500, 200);
	CHECK(ocv >= 3750 && ocv <= 3756);
}

/* ================== CARREGAMENTO ================== */

static void charge_until(charger_state_t state, uint32_t limit_ms) {

	for (uint32_t elapsed = 0; (elapsed < limit_ms) && (bms.charger.state != state); elapsed += 100u) {
		handcart_switch(1);
		charger_status(false);
		run_ms(100);
	}
}

static void test_charging_session(void) {

	boot();
	board.isa_handcart_present = true;

	charge_until(CHARGER_CHARGING, 20000);
	CHECK(bms.charger.state == CHARGER_CHARGING);
	CHECK(bms.precharge.state == HV_ON);
	CHECK(bms.precharge.source == PRECHARGE_SOURCE_HANDCART);
	CHECK(bms.system.state == AMS_CHARGING);
	CHECK(bms.charger.req_enable);

	can_frame_t frame;
	charge_until(CHARGER_DONE, 1200);
	CHECK(fake_can_last(CAN_BUS_2, HANDCART_T26_BMS_CHARGING_REQUEST_P1000_FRAME_ID, &frame));
	CHECK(frame.extended);
	CHECK(frame.data[4] == 0u);               // Control = 0: a carregar
	CHECK(((frame.data[0] << 8) | frame.data[1]) == 6000u);   // 600,0 V

	// celula maxima no fim de carga -> paragem ordenada
	for (uint8_t cell = 0; cell < 12u; cell++) {
		chain.cell_mV[0][cell] = 4160;
	}
	board.isa_handcart_current_mA = 0;
	charge_until(CHARGER_DONE, 3000);
	CHECK(bms.charger.state == CHARGER_DONE);
	CHECK(bms.charger.end_reason == CHARGE_END_COMPLETE);
	CHECK(bms.precharge.state == KILL);
	CHECK(fake_can_last(CAN_BUS_2, HANDCART_T26_BMS_CHARGING_REQUEST_P1000_FRAME_ID, &frame));
	CHECK(frame.data[4] == 1u);               // Control = 1: parado

	// latch: so o switch OFF -> ON volta a carregar
	charge_until(CHARGER_CHARGING, 3000);
	CHECK(bms.charger.state == CHARGER_DONE);
}

static void test_charger_fault_debounce(void) {

	boot();
	board.isa_handcart_present = true;
	charge_until(CHARGER_CHARGING, 20000);
	CHECK(bms.charger.state == CHARGER_CHARGING);

	// uma trama com falha nao corta (o firmware antigo cortava)
	charger_status(true);
	run_ms(100);
	charger_status(false);
	run_ms(100);
	CHECK(bms.charger.state == CHARGER_CHARGING);

	// falha que dura -> corte
	for (uint8_t frames = 0; frames < 10u; frames++) {
		handcart_switch(1);
		charger_status(true);
		run_ms(100);
	}
	CHECK(bms.charger.state == CHARGER_DONE);
	CHECK(bms.charger.end_reason == CHARGE_END_CHARGER_FAULT);
	CHECK(bms.precharge.state == KILL);
}

static void test_charger_switch_off(void) {

	boot();
	board.isa_handcart_present = true;
	charge_until(CHARGER_CHARGING, 20000);
	CHECK(bms.charger.state == CHARGER_CHARGING);

	board.isa_handcart_current_mA = 5500;    // ainda a passar corrente
	charge_until(CHARGER_DONE, 200);
	for (uint8_t frames = 0; frames < 10u; frames++) {
		handcart_switch(0);
		charger_status(false);
		run_ms(100);
	}
	CHECK(bms.charger.state == CHARGER_STOPPING);
	CHECK(bms.precharge.state == HV_ON);      // nao abre com corrente

	board.isa_handcart_current_mA = 100;
	for (uint8_t frames = 0; frames < 5u; frames++) {
		handcart_switch(0);
		run_ms(100);
	}
	CHECK(bms.charger.state == CHARGER_DONE);
	CHECK(bms.charger.end_reason == CHARGE_END_SWITCH_OFF);
	CHECK(bms.precharge.state == KILL);
}

/* ================== CAN ================== */

static void test_can_telemetry(void) {

	boot();
	fake_can_clear();
	run_ms(1000);

	CHECK(fake_can_sent(CAN_BUS_1, POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID) >= 9u);   // 100 ms
	CHECK(fake_can_sent(CAN_BUS_1, POWERTRAIN_T26_AMS_SDC_FEEDBACK_FRAME_ID) >= 9u);
	CHECK(fake_can_sent(CAN_BUS_1, 0x600u) >= 3u);                                   // 250 ms
	CHECK(fake_can_sent(CAN_BUS_1, 0x653u) >= 3u);                                   // slave 12
	CHECK(fake_can_sent(CAN_BUS_1, 0x654u) == 0u);                                   // nao ha slave 13 no DBC
	CHECK(fake_can_sent(CAN_BUS_2, POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID) == 0u);  // CAN2 sem carregamento

	can_frame_t frame;
	CHECK(fake_can_last(CAN_BUS_1, 0x600u, &frame));
	CHECK((frame.data[0] | (frame.data[1] << 8)) == 3700u);   // celula 1 do slave 1, em mV

	struct powertrain_t26_master_msc_id_1_t master;
	CHECK(fake_can_last(CAN_BUS_1, POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID, &frame));
	CHECK(powertrain_t26_master_msc_id_1_unpack(&master, frame.data, frame.dlc) == 0);
	CHECK(master.master_state == (uint8_t) AMS_IDLE);
	CHECK(master.master_firmware_version == BMS_FIRMWARE_VERSION);
}

static void test_bootloader_over_can(void) {

	uint8_t request[1] = { 1 };

	boot();
	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID, false, request, 1);
	run_ms(300);
	CHECK(!board.bootloader_requested);       // no CAN1 nao (bootloader_can_mask = CAN2)

	fake_can_inject(CAN_BUS_2, POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID, false, request, 1);
	run_ms(300);
	CHECK(board.bootloader_requested);
}

/* ================== JSON ================== */

static void test_live_json(void) {

	static char line[LIVE_JSON_BUFFER];
	int depth = 0;
	bool balanced = true;
	bool in_string = false;

	boot();
	uint16_t length = LiveJson_Build(line, sizeof(line), CONSOLE_UART, HAL_GetTick());

	CHECK(length > 1000u);
	CHECK(line[0] == '{');
	CHECK(strcmp(&line[length - 3u], "}\r\n") == 0);
	CHECK(strchr(line, '$') == NULL);
	for (uint16_t index = 0; index < length; index++) {
		char character = line[index];
		if (character == '"') {
			in_string = !in_string;
		} else if (!in_string && ((character == '{') || (character == '['))) {
			depth++;
		} else if (!in_string && ((character == '}') || (character == ']'))) {
			depth--;
			balanced = balanced && (depth >= 0);
		}
	}
	CHECK(balanced && (depth == 0) && !in_string);

	// as chaves que a BMS_UI le (backend/decode/live_json.py)
	const char *keys[] = { "\"bms\":{\"state\":\"IDLE\"", "\"ams_error\":{", "\"precharge\":{\"state\":\"KILL\"", "\"isa_pack\":{", "\"isa_handcart\":{",
			"\"soc\":{", "\"charger\":{\"state\":\"WAIT_HV\"", "\"pack\":{\"slaves_found\":12", "\"balancing\":{", "\"board\":{", "\"can1\":{",
			"\"can2\":{", "\"json\":{\"sent\":", "\"cell_mV\":[[3700,", "\"ntc_dC\":[[", "\"die_dC\":[", "\"pec_flags\":[", "\"ow_cell_mask\":[",
			"\"ow_ntc_mask\":[" };
	for (size_t key = 0; key < (sizeof(keys) / sizeof(keys[0])); key++) {
		if (strstr(line, keys[key]) == NULL) {
			printf("  falta %s\n", keys[key]);
			CHECK(false);
		}
	}

	// o JSON sai pelo cabo a 1 Hz
	fake_uart_clear(UART_LINK_1);
	run_ms(1100);
	CHECK(output_has(UART_LINK_1, "{\"uptime_ms\":"));

	// evento da linha AMS_ERROR
	fake_uart_clear(UART_LINK_1);
	chain.cell_mV[0][0] = 4300;
	run_ms(300);
	CHECK(output_has(UART_LINK_1, "AMS_ERROR line -> ERROR"));
}

/* ================== MAIN ================== */

typedef struct {
	const char *name;
	void (*run)(void);
} test_t;

#define TEST(test_function) { #test_function, test_function }

int main(void) {

	const test_t tests[] = {
		TEST(test_config_table),
		TEST(test_config_text),
		TEST(test_config_image),
		TEST(test_eeprom_blank_uses_defaults),
		TEST(test_eeprom_save_banks),
		TEST(test_eeprom_power_cut_mid_write),
		TEST(test_eeprom_save_needs_hv_off),
		TEST(test_eeprom_reboot_params),
		TEST(test_console_commands),
		TEST(test_console_reboot_and_bootloader),
		TEST(test_rn4871_sets_name_and_streams),
		TEST(test_rn4871_probe_and_reboot),
		TEST(test_ble_pin),
		TEST(test_precharge_full_sequence),
		TEST(test_precharge_welded_air),
		TEST(test_precharge_air_neg_dead),
		TEST(test_precharge_bus_never_rises),
		TEST(test_precharge_sdc_opens),
		TEST(test_precharge_vcu_lockout),
		TEST(test_ams_error_boot_and_faults),
		TEST(test_ams_error_optional_sources),
		TEST(test_isa_overcurrent),
		TEST(test_soc),
		TEST(test_charging_session),
		TEST(test_charger_fault_debounce),
		TEST(test_charger_switch_off),
		TEST(test_can_telemetry),
		TEST(test_bootloader_over_can),
		TEST(test_live_json),
	};

	for (size_t test_index = 0; test_index < sizeof(tests) / sizeof(tests[0]); test_index++) {
		int failures_before = failures;
		tests[test_index].run();
		printf("%s %s\n", (failures == failures_before) ? "ok    " : "FALHOU", tests[test_index].name);
	}

	printf("\n%d verificacoes, %d falhadas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
