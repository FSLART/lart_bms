/*
 * test_comms.c - a comunicacao externa, comando a comando: a consola (cabo e
 * BLE, todos os comandos e os erros), o RN4871 (todos os estados, modulo
 * morto, calado, a dar erro), o CAN que o test_system nao cobre (VCU viva,
 * tramas estranhas, NTC desligados, indices dos faults) e o JSON (buffer
 * pequeno, ligacao cheia, RN4871 em modo de comando).
 * Correr com Tests/run_tests.sh.
 */

#include "fake_chain.h"
#include "fake_board.h"
#include "bms_config.h"
#include "config_store.h"
#include "faults.h"
#include "precharge.h"
#include "console.h"
#include "rn4871.h"
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

static void boot(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(700);
}

static void uart(const char *line) {

	char copy[256];

	snprintf(copy, sizeof(copy), "%s", line);
	Console_Execute(CONSOLE_UART, copy);
}

static void ble(const char *line) {

	char copy[256];

	snprintf(copy, sizeof(copy), "%s", line);
	Console_Execute(CONSOLE_BLE, copy);
}

static bool out1(const char *text) {

	return strstr(fake_uart_output(UART_LINK_1), text) != NULL;
}

static bool out2(const char *text) {

	return strstr(fake_uart_output(UART_LINK_2), text) != NULL;
}

static uint32_t count_lines(const char *text, const char *prefix) {

	uint32_t count = 0;

	for (const char *cursor = strstr(text, prefix); cursor != NULL; cursor = strstr(cursor + 1, prefix)) {
		count++;
	}
	return count;
}

// o RN4871 chega ao modo de dados (nome verificado)
static void ble_ready(void) {

	for (uint32_t elapsed = 0; (elapsed < 10000u) && !Rn4871_DataMode(); elapsed += 10u) {
		run_ms(10);
	}
	CHECK(Rn4871_DataMode());
}

/* ================== CONSOLA ================== */

static void test_console_status_get_set(void) {

	boot();
	fake_uart_clear(UART_LINK_1);
	uart("status");
	CHECK(out1("#STATUS ams=IDLE pre=KILL chg=WAIT_HV hv_safe=1 ams_error=0"));
	CHECK(out1("cfg=defaults"));

	// get: um parametro, um elemento, erros
	fake_uart_clear(UART_LINK_1);
	uart("get can1_kbps");
	CHECK(out1("#CFG can1_kbps=500 kbit/s [125..1000] R"));
	uart("get soc_curve_pct[11]");
	CHECK(out1("#CFG soc_curve_pct[11]=100"));
	uart("GET bt_name");   // o comando aceita maiusculas
	CHECK(out1("#CFG bt_name="));
	uart("get soc_curve_pct[12]");
	CHECK(out1("#ERR indice invalido"));
	uart("get bt_name[0]");
	CHECK(out1("#ERR indice invalido"));
	uart("get naoexiste");
	CHECK(out1("#ERR parametro desconhecido: naoexiste"));
	uart("get soc_curve_pct[x]");
	CHECK(out1("#ERR parametro desconhecido: soc_curve_pct[x]"));
	uart("get soc_curve_pct[-1]");
	CHECK(out1("#ERR parametro desconhecido"));
	uart("get soc_curve_pct[1]x");
	CHECK(out1("#ERR parametro desconhecido"));

	// get all: uma linha por parametro, servidas pelo Console_Step
	fake_uart_clear(UART_LINK_1);
	uart("get all");
	run_ms(50);
	CHECK(count_lines(fake_uart_output(UART_LINK_1), "#CFG ") == config_param_count);
	CHECK(out1("#END 102 parametros"));
	fake_uart_clear(UART_LINK_1);
	uart("get");
	run_ms(50);
	CHECK(out1("#END 102 parametros"));

	// set: valor, erros, e o '$' nunca sai (o RN4871 entrava em modo de comando)
	fake_uart_clear(UART_LINK_1);
	uart("set live_period_ms 1500");
	CHECK(out1("#OK live_period_ms=1500 ms [0..10000]"));
	CHECK(bms.eeprom.dirty);
	uart("set live_period_ms");
	CHECK(out1("#ERR falta o valor"));
	uart("set live_period_ms 99999");
	CHECK(out1("#ERR live_period_ms: fora da gama [0..10000]"));
	uart("set $$$ 1");
	CHECK(out1("#ERR parametro desconhecido: ___"));
	CHECK(strchr(fake_uart_output(UART_LINK_1), '$') == NULL);
	uart("set soc_curve_pct[2] 7");
	CHECK(out1("#OK soc_curve_pct="));
	CHECK(ConfigStore_Edit()->soc_curve_pct[2] == 7u);
	uart("set bt_name   nome com espacos   ");
	CHECK(strcmp(ConfigStore_Edit()->bt_name, "nome com espacos") == 0);

	// diff: so o que mudou na copia
	fake_uart_clear(UART_LINK_1);
	uart("diff");
	run_ms(20);
	CHECK(out1("#DIFF live_period_ms=1500"));
	CHECK(out1("#DIFF soc_curve_pct="));
	CHECK(out1("#DIFF bt_name=nome com espacos"));
	CHECK(count_lines(fake_uart_output(UART_LINK_1), "#DIFF ") == 3u);
	CHECK(out1("#END diff"));

	// revert / defaults
	fake_uart_clear(UART_LINK_1);
	uart("revert");
	CHECK(out1("#OK revert"));
	CHECK(!bms.eeprom.dirty && (ConfigStore_Edit()->live_period_ms == bms.config.live_period_ms));
	uart("defaults");
	CHECK(out1("#OK defaults na copia"));
}

static void test_console_apply_save(void) {

	boot();
	fake_uart_clear(UART_LINK_1);

	// apply a quente; com parametros R avisa
	uart("set live_uart1_ms 2000");
	uart("apply");
	CHECK(out1("#OK apply"));
	CHECK(bms.config.live_uart1_ms == 2000u);
	uart("set can2_kbps 250");
	uart("apply");
	CHECK(out1("#OK apply (ha parametros que so mudam depois de reiniciar)"));
	CHECK(bms.config.can2_kbps == 500u);

	uart("set bal_min_cell_mV 3600");
	uart("set bal_max_cell_mV 3500");
	uart("apply");
	CHECK(out1("#ERR apply: balanceamento"));
	uart("save");
	CHECK(out1("#ERR save: balanceamento"));
	uart("revert");

	// save: a resposta final chega quando a EEPROM acaba
	fake_uart_clear(UART_LINK_1);
	uart("save");
	CHECK(out1("#OK save: a gravar (sequencia 1)"));
	run_ms(500);
	CHECK(out1("#SAVE ok sequencia=1 banco=A"));

	// falha a gravar
	fake_uart_clear(UART_LINK_1);
	board.eeprom_fail_writes = true;
	uart("save");
	run_ms(500);
	CHECK(out1("#SAVE falhou"));
	board.eeprom_fail_writes = false;

	// ocupada: o dump espera (nao le a meio de uma escrita)
	fake_uart_clear(UART_LINK_1);
	uart("save");
	uart("eeprom");
	CHECK(out1("#ERR EEPROM ocupada"));
	run_ms(500);
}

static void test_console_listings(void) {

	boot();

	// dump da EEPROM: 64 linhas de 16 bytes
	fake_uart_clear(UART_LINK_1);
	uart("eeprom");
	run_ms(100);
	CHECK(count_lines(fake_uart_output(UART_LINK_1), "#EE ") == 64u);
	CHECK(out1("#EE 000 FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF"));
	CHECK(out1("#EE 3F0 "));
	CHECK(out1("#END eeprom"));

	// a EEPROM deixa de responder a meio
	fake_uart_clear(UART_LINK_1);
	uart("eeprom");
	board.eeprom_fail_reads = true;
	run_ms(20);
	CHECK(out1("#ERR leitura da EEPROM"));
	board.eeprom_fail_reads = false;

	// faults: so os ativos, e se seguram o AMS_ERROR
	boot();
	chain.cell_mV[0][0] = 4300;   // sobretensao
	run_ms(2000);
	fake_uart_clear(UART_LINK_1);
	uart("faults");
	run_ms(20);
	CHECK(out1("#FAULTS ativos="));
	CHECK(out1("#FAULT CELL_OVERVOLTAGE ams=1"));
	CHECK(out1("#END faults"));
}

static void test_console_misc_commands(void) {

	boot();
	fake_uart_clear(UART_LINK_1);

	uart("version");
	CHECK(out1("#VERSION fw=13 schema=1 params=102"));
	uart("help");
	CHECK(count_lines(fake_uart_output(UART_LINK_1), "#HELP ") == 3u);
	uart("xyz");
	CHECK(out1("#ERR comando desconhecido (help)"));

	// ventoinhas
	uart("fan cells max");
	CHECK(out1("#OK fan cells max") && bms.fan_cells.bypass_on);
	uart("fan lv max");
	CHECK(bms.fan_lv.bypass_on);
	run_ms(250);   // as ventoinhas andam a cada 200 ms
	CHECK((bms.fan_cells.pwm == 255u) && (bms.fan_lv.pwm == 255u));
	uart("fan cells auto");
	CHECK(!bms.fan_cells.bypass_on);
	uart("fan x max");
	CHECK(out1("#ERR fan <cells|lv> <max|auto>"));
	uart("fan cells meio");
	CHECK(out1("#ERR fan <cells|lv> <max|auto>"));

	// live: periodo desta ligacao
	CHECK(Console_LivePeriod(CONSOLE_UART) == (int32_t) bms.config.live_uart1_ms);
	uart("live 250");
	CHECK(out1("#OK live=250") && (Console_LivePeriod(CONSOLE_UART) == 250));
	uart("live off");
	CHECK(Console_LivePeriod(CONSOLE_UART) == 0);
	bms.config.live_uart1_ms = 0;
	uart("live on");
	CHECK(Console_LivePeriod(CONSOLE_UART) == 1000);   // config a 0: 1 s
	bms.config.live_uart1_ms = 700;
	uart("live on");
	CHECK(Console_LivePeriod(CONSOLE_UART) == 700);
	uart("live 50");
	CHECK(out1("#ERR live <on|off|100..60000>"));
	uart("live 60001");
	CHECK(Console_LivePeriod(CONSOLE_UART) == 700);
	CHECK(Console_LivePeriod(CONSOLE_BLE) == (int32_t) bms.config.live_period_ms);

	// json: uma linha ja
	uart("live off");
	fake_uart_clear(UART_LINK_1);
	uart("json");
	CHECK(Console_TakeJsonRequest(CONSOLE_UART));
	CHECK(!Console_TakeJsonRequest(CONSOLE_UART));
	uart("json");
	run_ms(5);
	CHECK(out1("{\"uptime_ms\":"));

	// bt
	uart("bt reboot");
	CHECK(out1("#OK bt reboot"));
	uart("bt nada");
	CHECK(out1("#ERR bt reboot"));

	// reboot / bootloader: so com a HV desligada
	uart("reboot");
	CHECK(Console_TakeRebootRequest() && !Console_TakeRebootRequest());
	uart("bootloader");
	CHECK(Console_TakeBootloaderRequest() && !Console_TakeBootloaderRequest());
	CHECK(!Console_TakeRollbackRequest());

	// boot sem argumentos e com um errado
	fake_uart_clear(UART_LINK_1);
	uart("boot");
	CHECK(out1("#BOOT bootloader=nao "));
	uart("boot xpto");
	CHECK(out1("#ERR boot [confirm|rollback]"));
	uart("boot confirm");
	CHECK(out1("#ERR boot confirm: a imagem nao esta em teste (UNKNOWN)"));
}

static void test_console_line_input(void) {

	boot();
	fake_uart_clear(UART_LINK_1);

	// pelo UART1: linhas com \r, \n ou \r\n, caracteres de controlo ignorados
	fake_uart_inject(UART_LINK_1, "ver\x01sion\r\n\r\nstatus\n");
	run_ms(5);
	CHECK(out1("#VERSION fw="));
	CHECK(out1("#STATUS "));

	// linha comprida: erro, e a seguinte volta a funcionar
	char long_line[300];

	memset(long_line, 'a', sizeof(long_line));
	long_line[sizeof(long_line) - 2u] = '\n';
	long_line[sizeof(long_line) - 1u] = '\0';
	fake_uart_clear(UART_LINK_1);
	fake_uart_inject(UART_LINK_1, long_line);
	fake_uart_inject(UART_LINK_1, "help\n");
	run_ms(5);
	CHECK(out1("#ERR linha demasiado comprida"));
	CHECK(out1("#HELP "));

	// uma linha so de espacos nao faz nada
	fake_uart_clear(UART_LINK_1);
	uart("   ");
	CHECK(fake_uart_output(UART_LINK_1)[0] == '\0');
}

static void test_console_ble_pin(void) {

	boot();
	ble_ready();

	// com PIN: o que muda alguma coisa pede unlock; ler nao
	bms.config.bt_pin = 4321;
	fake_uart_clear(UART_LINK_2);
	ble("set live_period_ms 900");
	CHECK(out2("#ERR bloqueado: unlock <pin>"));
	ble("boot rollback");
	CHECK(out2("#ERR bloqueado"));
	ble("get live_period_ms");
	CHECK(out2("#CFG live_period_ms="));
	ble("boot");
	CHECK(out2("#BOOT "));

	// 3 PIN errados: 10 s de espera, mesmo com o certo
	ble("unlock 1");
	ble("unlock 2");
	ble("unlock 3");
	CHECK(out2("#ERR pin"));
	fake_uart_clear(UART_LINK_2);
	ble("unlock 4321");
	CHECK(out2("#ERR espera"));
	run_ms(10100);
	fake_uart_clear(UART_LINK_2);
	ble("unlock 4321");
	CHECK(out2("#OK unlocked"));
	ble("set live_period_ms 900");
	CHECK(out2("#OK live_period_ms=900"));

	// a ligacao cai: o PIN volta a ser pedido
	fake_uart_inject(UART_LINK_2, "%DISCONNECT%");
	run_ms(5);
	fake_uart_clear(UART_LINK_2);
	ble("set live_period_ms 800");
	CHECK(out2("#ERR bloqueado"));

	// pelo cabo nunca ha PIN
	fake_uart_clear(UART_LINK_1);
	uart("set live_period_ms 800");
	CHECK(out1("#OK live_period_ms=800"));

	// PIN a 0: o unlock aceita qualquer coisa
	bms.config.bt_pin = 0;
	fake_uart_clear(UART_LINK_2);
	ble("unlock 99");
	CHECK(out2("#OK unlocked"));
}

static void test_console_ble_listing_waits_for_data_mode(void) {

	boot();
	ble_ready();

	// uma listagem pelo BLE com o RN4871 em modo de comando (a vigia) espera por ele
	bms.config.bt_probe_s = 1;
	for (uint32_t elapsed = 0; (elapsed < 3000u) && (bms.bt.state != BT_PROBE_ENTER); elapsed++) {
		run_ms(1);
	}
	CHECK(bms.bt.state == BT_PROBE_ENTER);
	size_t mark = strlen(fake_uart_output(UART_LINK_2));
	ble("get all");
	run_ms(20);
	CHECK(strstr(fake_uart_output(UART_LINK_2) + mark, "#CFG ") == NULL);
	ble_ready();
	run_ms(50);
	CHECK(count_lines(fake_uart_output(UART_LINK_2) + mark, "#CFG ") == config_param_count);

	// um reboot do modulo corta a ligacao: a listagem pendente vai fora
	bms.config.bt_probe_s = 0;
	ble("bt reboot");
	run_ms(5);
	mark = strlen(fake_uart_output(UART_LINK_2));
	ble("get all");
	ble_ready();
	run_ms(50);
	CHECK(strstr(fake_uart_output(UART_LINK_2) + mark, "#CFG ") == NULL);
}

/* ================== RN4871 ================== */

static void test_rn4871_dead_module(void) {

	boot();
	board.rn_dead = true;
	bms.config.bt_probe_s = 0;
	Rn4871_Init(HAL_GetTick());

	// 1,5 s de arranque e 3 tentativas de "$$$" de 1 s: segue em modo de dados sem o dar como vivo
	run_ms(1400);
	CHECK(bms.bt.state == BT_BOOT);
	run_ms(1700);
	CHECK(bms.bt.state == BT_ENTER_CMD);
	run_ms(3200);
	CHECK((bms.bt.state == BT_DATA) && !bms.bt.rn4871_alive);
	CHECK(strcmp(Rn4871_StateName(bms.bt.state), "DATA") == 0);
	CHECK(strcmp(Rn4871_StateName((bt_state_t) 99), "?") == 0);

	// a vigia: sem resposta reinicia-o as cegas ("$$$", 200 ms, "R,1")
	bms.config.bt_probe_s = 2;
	run_ms(2200);
	CHECK(bms.bt.probes == 1u);
	run_ms(1100);
	CHECK((bms.bt.probe_failures == 1u) && (bms.bt.state == BT_REBOOT));
	run_ms(300);
	CHECK(bms.bt.rn4871_reboots == 1u);
	run_ms(1000);   // R,1 + 2,5 s sem %REBOOT%: volta ao arranque
	CHECK(bms.bt.state == BT_BOOT);

	// sem modo de dados nada sai pelo BLE
	CHECK(!Rn4871_Write("x", 1));
}

static void test_rn4871_silent_and_errors(void) {

	// entra em modo de comando mas nao responde ao GN: timeout -> sair -> dados
	boot();
	board.rn_silent = true;
	Rn4871_Init(HAL_GetTick());
	run_ms(1700);
	CHECK(bms.bt.state == BT_GET_NAME);
	run_ms(1100);
	CHECK(bms.bt.state == BT_EXIT_CMD);
	run_ms(1100);
	CHECK(bms.bt.state == BT_DATA);

	// o SN da erro: nao reinicia o modulo
	boot();
	board.rn_sn_error = true;
	Rn4871_Init(HAL_GetTick());
	run_ms(3000);
	CHECK(bms.bt.state == BT_DATA);
	CHECK(bms.bt.rn4871_reboots == 0u);
	CHECK(strcmp(board.rn_name, "RN4871-1234") == 0);

	// o nome ja e o certo: nao ha SN nem reboot
	boot();
	strcpy(board.rn_name, bms.config.bt_name);
	Rn4871_Init(HAL_GetTick());
	run_ms(3000);
	CHECK((bms.bt.state == BT_DATA) && (bms.bt.rn4871_reboots == 0u) && bms.bt.rn4871_alive);
	CHECK(strcmp(bms.bt.name, bms.config.bt_name) == 0);

	// um nome comprido demais no modulo e cortado ao tamanho do bms.bt.name
	boot();
	strcpy(board.rn_name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	Rn4871_Init(HAL_GetTick());
	for (uint32_t elapsed = 0; (elapsed < 3000u) && (bms.bt.name[0] == '\0'); elapsed++) {
		run_ms(1);
	}
	CHECK(strlen(bms.bt.name) == (CFG_NAME_LEN - 1u));
	CHECK(strncmp(bms.bt.name, "ABCDEFGHIJKLMNOPQRST", CFG_NAME_LEN - 1u) == 0);
	ble_ready();   // e depois do SN + reboot o nome e o da config
	CHECK(strcmp(bms.bt.name, bms.config.bt_name) == 0);
}

static void test_rn4871_status_tokens(void) {

	boot();
	ble_ready();

	fake_uart_inject(UART_LINK_2, "%CONNECT,1,AABBCCDDEEFF%");
	run_ms(2);
	CHECK(bms.bt.connected);
	fake_uart_inject(UART_LINK_2, "%DISCONNECT%");
	run_ms(2);
	CHECK(!bms.bt.connected);
	fake_uart_inject(UART_LINK_2, "%STREAM_OPEN%");
	run_ms(2);
	CHECK(bms.bt.connected);

	// %...% comprido demais nao e um status: o texto segue para a consola
	fake_uart_clear(UART_LINK_2);
	fake_uart_inject(UART_LINK_2, "%0123456789012345678901234567890123456789012345678901234567\nversion\n");
	run_ms(5);
	CHECK(out2("#VERSION fw="));

	// o modulo reinicia sozinho: volta a verificar o nome
	fake_uart_inject(UART_LINK_2, "%REBOOT%");
	run_ms(2);
	CHECK(!bms.bt.connected);
	CHECK(bms.bt.state == BT_BOOT);
	ble_ready();

	// texto da aplicacao sem '$'
	CHECK(Rn4871_Write("ok", 2));
	CHECK(!Rn4871_Write("a$b", 3));
}

static void test_rn4871_requested_and_periodic_reboot(void) {

	boot();
	ble_ready();
	uint32_t reboots = bms.bt.rn4871_reboots;

	Rn4871_RequestReboot();
	run_ms(2);
	CHECK(bms.bt.state == BT_REBOOT);
	ble_ready();
	CHECK(bms.bt.rn4871_reboots == (reboots + 1u));

	// reboot periodico as cegas (bt_reboot_s)
	bms.config.bt_probe_s = 0;
	bms.config.bt_reboot_s = 3;
	reboots = bms.bt.rn4871_reboots;
	run_ms(3100);
	CHECK(bms.bt.rn4871_reboots == (reboots + 1u));
	bms.config.bt_reboot_s = 0;
	ble_ready();
}

static void test_rn4871_tx_never_idle(void) {

	// a vigia nao manda "$$$" no meio de um envio: desiste e tenta mais tarde
	boot();
	ble_ready();
	bms.config.bt_probe_s = 1;
	board.uart_tx_busy[UART_LINK_2] = true;
	uint32_t probes = bms.bt.probes;

	run_ms(1100);
	CHECK(bms.bt.state == BT_PROBE_ENTER);
	run_ms(2100);
	CHECK((bms.bt.state == BT_DATA) && (bms.bt.probes == probes));

	// o reboot as cegas manda na mesma ao fim de 2 s
	Rn4871_RequestReboot();
	run_ms(2);
	CHECK(bms.bt.state == BT_REBOOT);
	run_ms(2300);
	CHECK(bms.bt.state != BT_REBOOT);
	board.uart_tx_busy[UART_LINK_2] = false;

	// resposta enorme em modo de comando: fica so com o fim (sem rebentar o buffer)
	boot();
	board.rn_silent = true;
	Rn4871_Init(HAL_GetTick());
	run_ms(1700);
	CHECK(bms.bt.state == BT_GET_NAME);
	for (uint8_t index = 0; index < 5u; index++) {
		fake_uart_inject(UART_LINK_2, "0123456789012345678901234567890123456789");
	}
	fake_uart_inject(UART_LINK_2, "NOMELONGO\r\nCMD> ");
	run_ms(5);
	CHECK(bms.bt.state != BT_GET_NAME);
}

/* ================== CAN ================== */

static void test_can_rx_corner_cases(void) {

	uint8_t request[1] = { 1 };
	uint8_t data[8] = { 0 };

	boot();

	// no CAN1 so ha IDs de 11 bits: uma trama de 29 bits com o ID do pedido de precarga e ignorada
	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, true, request, 1);
	run_ms(50);
	CHECK(bms.precharge.vcu_request < 0);

	// um ID que o CAN2 nao conhece nao faz nada
	fake_can_inject(CAN_BUS_2, 0x123, false, data, 8);
	run_ms(5);

	// VCU viva (0x750): com vcu_timeout_ms, o silencio da VCU a meio da precarga e um fault
	bms.config.vcu_timeout_ms = 300;
	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_VCU_STATES_FRAME_ID, false, data, 8);
	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, false, request, 1);
	run_ms(100);
	CHECK(bms.precharge.state != KILL);
	CHECK(!bms.faults.vcu_timeout);
	for (uint8_t round = 0; round < 3u; round++) {
		fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_VCU_STATES_FRAME_ID, false, data, 8);
		fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, false, request, 1);
		run_ms(100);
	}
	CHECK(!bms.faults.vcu_timeout);
	run_ms(400);
	CHECK(bms.faults.vcu_timeout || (bms.precharge.state == KILL));
}

static void test_can_display_and_fault_indexes(void) {

	can_frame_t frame;

	boot();

	// NTC desligados no slave 1: no CAN herdam o anterior; os primeiros sem anterior vao a 0
	bms.config.ntc_disabled_mask[0] = 0x03;
	fake_can_clear();
	run_ms(1500);
	CHECK(fake_can_last(CAN_BUS_1, POWERTRAIN_T26_SLAVE_01_TEMPERATURE_ID_1_FRAME_ID, &frame));

	struct powertrain_t26_slave_01_temperature_id_1_t temps;

	CHECK(powertrain_t26_slave_01_temperature_id_1_unpack(&temps, frame.data, frame.dlc) == 0);
	CHECK((temps.temperature_value_1 == 0u) && (temps.temperature_value_2 == 0u));
	CHECK((temps.temperature_value_3 > 2000u) && (temps.temperature_value_3 < 3000u));   // ~25 C em 0,01 C

	// sobretemperatura: o 0x703 diz qual slave e qual NTC
	bms.config.ntc_disabled_mask[0] = 0;
	chain.gpio_mV[4][2] = fake_ntc_mV(70.0);
	run_ms(2500);
	CHECK(bms.faults.overtemperature);
	fake_can_clear();
	run_ms(1000);
	CHECK(fake_can_last(CAN_BUS_1, POWERTRAIN_T26_MASTER_MSC_ID_2_FRAME_ID, &frame));

	struct powertrain_t26_master_msc_id_2_t faults;

	CHECK(powertrain_t26_master_msc_id_2_unpack(&faults, frame.data, frame.dlc) == 0);
	CHECK(faults.fault1_code == (uint8_t) (Faults_FindByName("OVERTEMPERATURE") + 1));
	CHECK(faults.fault1_index_type == 5u);    // slave 5 (a contar de 1)
	CHECK(faults.fault1_index_value == 3u);   // NTC 3
}

/* ================== JSON ================== */

static void test_json_overflow_and_links(void) {

	static char small[512];
	static char line[LIVE_JSON_BUFFER];

	boot();

	// buffer pequeno: nao sai meia linha
	CHECK(LiveJson_Build(small, sizeof(small), CONSOLE_UART, HAL_GetTick()) == 0u);
	CHECK(LiveJson_Build(line, sizeof(line), CONSOLE_UART, HAL_GetTick()) > 1000u);

	// ligacao cheia: conta como perdida
	board.uart_reject[UART_LINK_1] = true;
	uint32_t dropped = bms.uart1.json_dropped;

	uart("json");
	run_ms(5);
	CHECK(bms.uart1.json_dropped == (dropped + 1u));
	board.uart_reject[UART_LINK_1] = false;

	// o RN4871 em modo de comando: o JSON do BLE espera
	ble_ready();
	ble("bt reboot");
	run_ms(5);
	uint32_t sent = bms.uart2.json_sent;

	ble("json");
	run_ms(20);
	CHECK(bms.uart2.json_sent == sent);
	ble_ready();
	run_ms(20);
	CHECK(bms.uart2.json_sent > sent);

	// a linha AMS_ERROR sai nas duas ligacoes quando muda
	fake_uart_clear(UART_LINK_1);
	chain.cell_mV[1][1] = 4300;
	run_ms(1500);
	CHECK(out1("AMS_ERROR line -> ERROR"));
}

int main(void) {

	fake_flash_erase_all();

	test_console_status_get_set();
	test_console_apply_save();
	test_console_listings();
	test_console_misc_commands();
	test_console_line_input();
	test_console_ble_pin();
	test_console_ble_listing_waits_for_data_mode();
	test_rn4871_dead_module();
	test_rn4871_silent_and_errors();
	test_rn4871_status_tokens();
	test_rn4871_requested_and_periodic_reboot();
	test_rn4871_tx_never_idle();
	test_can_rx_corner_cases();
	test_can_display_and_fault_indexes();
	test_json_overflow_and_links();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
