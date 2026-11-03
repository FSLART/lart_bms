/*
 * test_config.c - a configuracao, parametro a parametro: texto (set/get da
 * consola), gamas, coerencia, a imagem de um banco da EEPROM e a gravacao em
 * fundo com as falhas do I2C (EEPROM ausente, sem ACK, escrita errada).
 * Correr com Tests/run_tests.sh.
 */

#include "fake_chain.h"
#include "fake_board.h"
#include "bms_config.h"
#include "config_store.h"
#include "faults.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int param(const char *key) {

	int index = Config_Find(key);

	if (index < 0) {
		printf("  parametro %s nao existe\n", key);
		failures++;
		return 0;
	}
	return index;
}

static const char* parse(bms_config_t *config, const char *key, int element, const char *text) {

	return Config_ParseValue(config, (uint16_t) param(key), element, text);
}

static bool is(const char *text, const char *expected) {

	return (text != NULL) && (expected != NULL) && (strcmp(text, expected) == 0);
}

static const char* format(const bms_config_t *config, const char *key) {

	static char out[200];

	(void) Config_FormatValue(config, (uint16_t) param(key), out, sizeof(out));
	return out;
}

static void boot(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(700);
}

/* ================== TABELA ================== */

static void test_defaults_and_table(void) {

	bms_config_t config;

	Config_SetDefaults(&config);
	CHECK(Config_Validate(&config) == NULL);
	CHECK(Config_Clamp(&config) == 0u);
	CHECK(config_param_count == 102u);

	// nomes unicos, gamas coerentes e os defaults dentro delas
	for (uint16_t index = 0; index < config_param_count; index++) {
		const cfg_param_t *entry = &config_params[index];

		CHECK(Config_Find(entry->key) == (int) index);
		CHECK(entry->count > 0u);
		if ((entry->type != CFG_STR) && (entry->type != CFG_FAULTS)) {
			CHECK(entry->min <= entry->max);
			for (uint8_t element = 0; element < entry->count; element++) {
				int32_t value;
				CHECK(Config_GetElement(&config, index, element, &value));
				CHECK((value >= entry->min) && (value <= entry->max));
			}
		}
	}

	// o parametro novo do bootloader esta no fim (regra da EEPROM)
	CHECK(Config_Find("boot_confirm_s") == (int) (config_param_count - 1u));
	CHECK(config.boot_confirm_s == 30u);
	CHECK(config.bootloader_can_mask == 0x03u);
	CHECK(Config_Find("nao_existe") == -1);
	CHECK(Config_Find("") == -1);
}

static void test_get_set_element(void) {

	bms_config_t config;
	int32_t value;
	uint16_t kbps = (uint16_t) param("can1_kbps");
	uint16_t name = (uint16_t) param("bt_name");
	uint16_t masks = (uint16_t) param("ntc_disabled_mask");

	Config_SetDefaults(&config);
	CHECK(Config_GetElement(&config, kbps, 0, &value) && (value == 500));
	CHECK(!Config_GetElement(&config, kbps, 1, &value));         // escalar: so o elemento 0
	CHECK(!Config_GetElement(&config, name, 0, &value));         // texto nao e numerico
	CHECK(!Config_GetElement(&config, config_param_count, 0, &value));

	CHECK(Config_SetElement(&config, kbps, 0, 1000) && (config.can1_kbps == 1000u));
	CHECK(!Config_SetElement(&config, kbps, 0, 1001));           // fora da gama
	CHECK(!Config_SetElement(&config, kbps, 0, 124));
	CHECK(config.can1_kbps == 1000u);
	CHECK(Config_SetElement(&config, masks, 15, 0x3) && (config.ntc_disabled_mask[15] == 0x3u));
	CHECK(!Config_SetElement(&config, masks, 16, 0));
	CHECK(!Config_SetElement(&config, name, 0, 1));
}

/* ================== TEXTO ================== */

static void test_parse_numbers(void) {

	bms_config_t config;

	Config_SetDefaults(&config);

	// decimal, hexadecimal, espacos a volta, sinal
	CHECK(parse(&config, "can1_kbps", -1, "250") == NULL);
	CHECK(config.can1_kbps == 250u);
	CHECK(parse(&config, "can1_kbps", -1, "0x1F4") == NULL);
	CHECK(config.can1_kbps == 500u);
	CHECK(parse(&config, "can1_kbps", -1, "  1000  ") == NULL);
	CHECK(config.can1_kbps == 1000u);
	CHECK(parse(&config, "board_current_offset_mA", -1, "-150") == NULL);
	CHECK(config.board_current_offset_mA == -150);
	CHECK(parse(&config, "board_current_offset_mA", -1, "0100") == NULL);   // zero a esquerda nao e octal
	CHECK(config.board_current_offset_mA == 100);

	// erros: nada se muda
	CHECK(is(parse(&config, "can1_kbps", -1, "124"), "fora da gama"));
	CHECK(is(parse(&config, "board_current_offset_mA", -1, "-2001"), "fora da gama"));
	CHECK(is(parse(&config, "can1_kbps", -1, "abc"), "valor invalido"));
	CHECK(is(parse(&config, "can1_kbps", -1, "500x"), "valor invalido"));
	CHECK(is(parse(&config, "can1_kbps", -1, ""), "valor invalido"));
	CHECK(is(parse(&config, "can1_kbps", 1, "500"), "indice invalido"));
	CHECK(config.can1_kbps == 1000u);
	CHECK(is(Config_ParseValue(&config, config_param_count, -1, "1"), "parametro desconhecido"));

	// bools: on/off/true/false/0/1
	CHECK(parse(&config, "can2_mirror_charging", -1, "off") == NULL);
	CHECK(!config.can2_mirror_charging);
	CHECK(parse(&config, "can2_mirror_charging", -1, "true") == NULL);
	CHECK(config.can2_mirror_charging);
	CHECK(parse(&config, "can2_mirror_charging", -1, "false") == NULL);
	CHECK(!config.can2_mirror_charging);
	CHECK(parse(&config, "can2_mirror_charging", -1, "on") == NULL);
	CHECK(config.can2_mirror_charging);
	CHECK(parse(&config, "can2_mirror_charging", -1, "0") == NULL);
	CHECK(!config.can2_mirror_charging);
	CHECK(is(parse(&config, "can2_mirror_charging", -1, "2"), "fora da gama"));
	CHECK(is(parse(&config, "can2_mirror_charging", -1, "on!"), "valor invalido"));
	CHECK(is(parse(&config, "can2_mirror_charging", -1, "talvez"), "valor invalido"));
}

static void test_parse_tables(void) {

	bms_config_t config;
	char text[200];

	Config_SetDefaults(&config);

	// um elemento
	CHECK(parse(&config, "ntc_disabled_mask", 3, "0x10") == NULL);
	CHECK(config.ntc_disabled_mask[3] == 0x10u);
	CHECK(is(parse(&config, "ntc_disabled_mask", 16, "1"), "indice invalido"));

	// a tabela toda: exatamente count valores
	strcpy(text, "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16");
	CHECK(parse(&config, "ntc_disabled_mask", -1, text) == NULL);
	CHECK((config.ntc_disabled_mask[0] == 1u) && (config.ntc_disabled_mask[15] == 16u));
	CHECK(is(parse(&config, "ntc_disabled_mask", -1, "1,2,3"), "numero de valores"));
	CHECK(is(parse(&config, "ntc_disabled_mask", -1, "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17"), "numero de valores"));
	CHECK(is(parse(&config, "ntc_disabled_mask", -1, "1;2"), "numero de valores"));
	CHECK(is(parse(&config, "ntc_disabled_mask", -1, ",1"), "numero de valores"));
	CHECK(config.ntc_disabled_mask[15] == 16u);   // um erro nao deixa a tabela a meio

	// espacos entre os valores
	CHECK(parse(&config, "soc_curve_pct", -1, "0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100") == NULL);
	CHECK((config.soc_curve_pct[1] == 5u) && (config.soc_curve_pct[11] == 100u));
}

static void test_parse_text_and_faults(void) {

	bms_config_t config;

	Config_SetDefaults(&config);

	// nome do RN4871: 1..20 caracteres, sem virgula, % nem $
	CHECK(parse(&config, "bt_name", -1, "LART BMS 26") == NULL);
	CHECK(strcmp(config.bt_name, "LART BMS 26") == 0);
	CHECK(is(parse(&config, "bt_name", -1, ""), "tamanho invalido"));
	CHECK(is(parse(&config, "bt_name", -1, "123456789012345678901"), "tamanho invalido"));
	CHECK(parse(&config, "bt_name", -1, "12345678901234567890") == NULL);
	CHECK(is(parse(&config, "bt_name", -1, "a,b"), "caracter invalido"));
	CHECK(is(parse(&config, "bt_name", -1, "a%b"), "caracter invalido"));
	CHECK(is(parse(&config, "bt_name", -1, "$$$"), "caracter invalido"));
	CHECK(is(parse(&config, "bt_name", 1, "x"), "indice invalido"));
	CHECK(strcmp(config.bt_name, "12345678901234567890") == 0);

	// faults: mascara, ou +NOME/-NOME
	bms_faults_t *enable = &config.ams_error_enable;
	int soc_low = Faults_FindByName("SOC_LOW");
	int sdc_open = Faults_FindByName("sdc_open");

	CHECK((soc_low >= 0) && (sdc_open >= 0));
	CHECK(parse(&config, "ams_error_enable", -1, "0") == NULL);
	CHECK(Faults_ToMask(enable) == 0u);
	CHECK(parse(&config, "ams_error_enable", -1, "+SOC_LOW,+sdc_open") == NULL);
	CHECK(Faults_ToMask(enable) == ((1ull << soc_low) | (1ull << sdc_open)));
	CHECK(parse(&config, "ams_error_enable", -1, "-SOC_LOW") == NULL);
	CHECK(Faults_ToMask(enable) == (1ull << sdc_open));
	CHECK(parse(&config, "ams_error_enable", -1, "0x3") == NULL);
	CHECK(Faults_ToMask(enable) == 0x3u);
	CHECK(is(parse(&config, "ams_error_enable", -1, "+NAO_EXISTE"), "fault desconhecido"));
	CHECK(is(parse(&config, "ams_error_enable", -1, "+SOC_LOW lixo"), "valor invalido"));
	CHECK(is(parse(&config, "ams_error_enable", -1, "0x1G"), "valor invalido"));
	CHECK(is(parse(&config, "ams_error_enable", -1, "nada"), "valor invalido"));
	CHECK(is(parse(&config, "ams_error_enable", -1, "0xFFFFFFFFFFFFFFFF"), "fora da gama"));
	CHECK(is(parse(&config, "ams_error_enable", 1, "0"), "indice invalido"));
	CHECK(Faults_ToMask(enable) == 0x3u);
}

static void test_format(void) {

	bms_config_t config;
	char small[4];
	uint16_t kbps = (uint16_t) param("can1_kbps");

	Config_SetDefaults(&config);
	CHECK(strcmp(format(&config, "can1_kbps"), "500") == 0);
	CHECK(strcmp(format(&config, "bootloader_can_mask"), "0x3") == 0);   // hexadecimal
	CHECK(strcmp(format(&config, "bt_name"), config.bt_name) == 0);
	config.board_current_offset_mA = -42;
	CHECK(strcmp(format(&config, "board_current_offset_mA"), "-42") == 0);

	Faults_FromMask(0x80000003ull, &config.ams_error_enable);
	CHECK(strcmp(format(&config, "ams_error_enable"), "0x0000000080000003") == 0);

	memset(config.ntc_disabled_mask, 0, sizeof(config.ntc_disabled_mask));
	config.ntc_disabled_mask[1] = 0x20;
	CHECK(strcmp(format(&config, "ntc_disabled_mask"), "0x0,0x20,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0") == 0);
	CHECK(strncmp(format(&config, "soc_curve_pct"), "0,", 2) == 0);

	// buffer pequeno: corta e termina sempre
	CHECK(Config_FormatValue(&config, kbps, small, sizeof(small)) == 3u);
	CHECK(strcmp(small, "500") == 0);
	CHECK(Config_FormatValue(&config, (uint16_t) param("soc_curve_pct"), small, sizeof(small)) == 3u);
	CHECK(small[3] == '\0');
	CHECK(Config_FormatValue(&config, (uint16_t) param("bt_name"), small, sizeof(small)) == 3u);
	CHECK(Config_FormatValue(&config, kbps, small, 0) == 0u);
	CHECK(Config_FormatValue(&config, config_param_count, small, sizeof(small)) == 0u);
}

/* ================== COERENCIA E GAMAS ================== */

static void test_validate_rules(void) {

	bms_config_t config;

	Config_SetDefaults(&config);
	config.slaves = 0;
	CHECK(is(Config_Validate(&config), "topologia fora dos limites"));
	Config_SetDefaults(&config);
	config.cells_per_slave = BMS_MAX_CELLS + 1u;
	CHECK(is(Config_Validate(&config), "topologia fora dos limites"));
	Config_SetDefaults(&config);
	config.ntcs_per_slave = BMS_MAX_NTCS + 1u;
	CHECK(is(Config_Validate(&config), "topologia fora dos limites"));

	// FS 2027 EV 5.8.3: 6 NTC por 12 celulas e o minimo e 30 %: com 3 desligados num slave nao
	Config_SetDefaults(&config);
	config.ntc_disabled_mask[4] = 0x07;
	CHECK(is(Config_Validate(&config), "menos de 30% das celulas com NTC (FS EV 5.8.3)"));
	config.ntc_disabled_mask[4] = 0x03;   // 4 de 12 = 33 %
	CHECK(Config_Validate(&config) == NULL);
	config.ntc_disabled_mask[11] = 0xFFC0u;   // bits acima dos NTC que existem nao contam
	CHECK(Config_Validate(&config) == NULL);

	Config_SetDefaults(&config);
	config.cell_open_wire_mV = config.cell_min_mV;
	CHECK(is(Config_Validate(&config), "celula: open_wire < min < low_warn <= high_warn < max"));
	Config_SetDefaults(&config);
	config.cell_high_warn_mV = config.cell_max_mV;
	CHECK(is(Config_Validate(&config), "celula: open_wire < min < low_warn <= high_warn < max"));
	Config_SetDefaults(&config);
	config.cell_low_warn_mV = config.cell_high_warn_mV;   // igual pode
	CHECK(Config_Validate(&config) == NULL);

	Config_SetDefaults(&config);
	config.temp_min_dC = config.temp_low_warn_dC;
	CHECK(is(Config_Validate(&config), "temperatura: min < low_warn <= high_warn < max"));

	Config_SetDefaults(&config);
	config.bal_min_cell_mV = config.bal_max_cell_mV;
	CHECK(is(Config_Validate(&config), "balanceamento: bal_min_cell_mV < bal_max_cell_mV"));

	Config_SetDefaults(&config);
	config.fan_accu_cells.full_dC = config.fan_accu_cells.start_dC;
	CHECK(is(Config_Validate(&config), "ventoinhas: start < full e pwm_min <= pwm_max"));
	Config_SetDefaults(&config);
	config.fan_accu_lv.pwm_min_pct = (uint8_t) (config.fan_accu_lv.pwm_max_pct + 1u);
	CHECK(is(Config_Validate(&config), "ventoinhas: start < full e pwm_min <= pwm_max"));

	Config_SetDefaults(&config);
	config.precharge_air_wait_ms = config.precharge_timeout_ms;
	CHECK(is(Config_Validate(&config), "precarga: air_wait e min tem de ser < timeout"));
	Config_SetDefaults(&config);
	config.precharge_min_ms = config.precharge_timeout_ms;
	CHECK(is(Config_Validate(&config), "precarga: air_wait e min tem de ser < timeout"));

	Config_SetDefaults(&config);
	config.soc_curve_pct[5] = config.soc_curve_pct[4];
	CHECK(is(Config_Validate(&config), "curva do SOC tem de ser crescente"));
	Config_SetDefaults(&config);
	config.soc_curve_mV[11] = config.soc_curve_mV[10];
	CHECK(is(Config_Validate(&config), "curva do SOC tem de ser crescente"));

	Config_SetDefaults(&config);
	config.temp_compensation_temp_C[3] = config.temp_compensation_temp_C[2];
	CHECK(is(Config_Validate(&config), "compensacao: temperaturas crescentes"));

	Config_SetDefaults(&config);
	config.can2_kbps = 300;
	CHECK(is(Config_Validate(&config), "CAN: 125, 250, 500 ou 1000 kbit/s"));

	Config_SetDefaults(&config);
	config.bt_name[0] = '\0';
	CHECK(is(Config_Validate(&config), "bt_name vazio"));
}

static void test_clamp(void) {

	bms_config_t config;

	Config_SetDefaults(&config);
	config.can1_kbps = 2000;                        // acima da gama
	config.board_current_offset_mA = -30000;        // abaixo
	*(uint8_t*) &config.can2_mirror_charging = 7;   // bool com lixo (EEPROM)
	strcpy(config.bt_name, "a,b");                  // virgula
	Faults_FromMask(UINT64_MAX, &config.ams_error_enable);

	CHECK(Config_Clamp(&config) == 4u);
	CHECK(config.can1_kbps == 1000u);
	CHECK(config.board_current_offset_mA == -2000);
	CHECK(*(uint8_t*) &config.can2_mirror_charging == 1u);
	CHECK(strcmp(config.bt_name, config_defaults.bt_name) == 0);
	CHECK(Faults_ToMask(&config.ams_error_enable) == ((FAULT_COUNT >= 64u) ? UINT64_MAX : ((1ull << FAULT_COUNT) - 1u)));
	CHECK(Config_Clamp(&config) == 0u);

	// texto sem terminador (EEPROM com lixo): fica terminado
	Config_SetDefaults(&config);
	memset(config.bt_name, 'A', sizeof(config.bt_name));
	CHECK(Config_Clamp(&config) == 0u);
	CHECK(strlen(config.bt_name) == (sizeof(config.bt_name) - 1u));
}

/* ================== EEPROM ================== */

static void test_serialize_roundtrip(void) {

	bms_config_t config;
	bms_config_t back;
	uint8_t payload[600];
	uint16_t length = Config_PayloadSize(config_param_count);

	CHECK(length <= CONFIG_BANK_PAYLOAD_MAX);
	CHECK(Config_PayloadSize(0) == 0u);
	CHECK(Config_PayloadSize(1000) == length);

	Config_SetDefaults(&config);
	config.can1_kbps = 1000;
	config.board_current_offset_mA = -321;
	config.can2_mirror_charging = false;
	config.ntc_disabled_mask[7] = 0x22;
	strcpy(config.bt_name, "BMS-X");
	Faults_FromMask(0x0000000180000001ull, &config.ams_error_enable);
	config.boot_confirm_s = 77;

	CHECK(Config_Serialize(&config, payload, length) == length);
	CHECK(Config_Serialize(&config, payload, (uint16_t) (length - 1u)) == 0u);

	Config_SetDefaults(&back);
	CHECK(Config_Deserialize(&back, payload, length, config_param_count));
	for (uint16_t index = 0; index < config_param_count; index++) {
		CHECK(Config_ParamEqual(&config, &back, index));
	}

	// little-endian: o can1_kbps (u16) e o primeiro byte que muda
	CHECK(!Config_Deserialize(&back, payload, (uint16_t) (length - 1u), config_param_count));
	CHECK(!Config_Deserialize(&back, payload, length, (uint16_t) (config_param_count + 1u)));

	// uma EEPROM de um firmware com menos parametros: os que faltam ficam com o default
	uint16_t older = (uint16_t) (config_param_count - 1u);

	Config_SetDefaults(&back);
	CHECK(Config_Deserialize(&back, payload, Config_PayloadSize(older), older));
	CHECK(back.boot_confirm_s == config_defaults.boot_confirm_s);
	CHECK(back.can1_kbps == 1000u);

	// CRC-32 (zlib) e o CRC do esquema
	CHECK(Config_Crc32((const uint8_t*) "123456789", 9, 0) == 0xCBF43926u);
	CHECK(Config_SchemaCrc(older) != Config_SchemaCrc(config_param_count));
	CHECK(Config_SchemaCrc(config_param_count) == Config_SchemaCrc(5000));
}

static void test_apply_live_and_reboot_pending(void) {

	bms_config_t active;
	bms_config_t edited;

	Config_SetDefaults(&active);
	edited = active;
	CHECK(!Config_RebootPending(&active, &edited));

	edited.can1_kbps = 250;            // REBOOT
	edited.live_period_ms = 2500;      // a quente
	CHECK(Config_RebootPending(&active, &edited));
	Config_ApplyLive(&active, &edited);
	CHECK(active.live_period_ms == 2500u);
	CHECK(active.can1_kbps == 500u);
	CHECK(Config_RebootPending(&active, &edited));

	// faults compara-se pela mascara (bools normalizados)
	Faults_FromMask(0x5u, &active.ams_error_enable);
	Faults_FromMask(0x5u, &edited.ams_error_enable);
	CHECK(Config_ParamEqual(&active, &edited, (uint16_t) param("ams_error_enable")));
	Faults_FromMask(0x4u, &edited.ams_error_enable);
	CHECK(!Config_ParamEqual(&active, &edited, (uint16_t) param("ams_error_enable")));
}

static void test_bank_image(void) {

	bms_config_t config;
	bms_config_t back;
	uint8_t image[CONFIG_BANK_SIZE];
	uint32_t sequence;

	Config_SetDefaults(&config);
	config.cell_max_mV = 4190;
	uint16_t length = ConfigStore_BuildImage(&config, 41, image, sizeof(image));

	CHECK(length == (CONFIG_BANK_HEADER + Config_PayloadSize(config_param_count) + CONFIG_BANK_CRC));
	CHECK((image[0] == 0x42u) && (image[1] == 0x4Cu) && (image[2] == CONFIG_SCHEMA_VERSION));
	CHECK(ConfigStore_ParseImage(image, length, &back, &sequence));
	CHECK((sequence == 41u) && (back.cell_max_mV == 4190u));
	CHECK(ConfigStore_BuildImage(&config, 1, image, (uint16_t) (length - 1u)) == 0u);

	length = ConfigStore_BuildImage(&config, 41, image, sizeof(image));
	CHECK(!ConfigStore_ParseImage(image, 10, &back, &sequence));             // curta
	image[length - 1u] ^= 0xFFu;
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));         // CRC
	image[length - 1u] ^= 0xFFu;
	image[0] ^= 1u;
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));         // magic
	image[0] ^= 1u;
	image[2] = CONFIG_SCHEMA_VERSION + 1u;
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));         // esquema antigo/novo
	image[2] = CONFIG_SCHEMA_VERSION;
	CHECK(ConfigStore_ParseImage(image, length, &back, &sequence));

	// helper: muda um campo do cabecalho e refaz o CRC
#define REWRITE(offset, value16) do { \
		image[offset] = (uint8_t) (value16); image[(offset) + 1] = (uint8_t) ((value16) >> 8); \
		uint32_t crc = Config_Crc32(image, (size_t) (length - CONFIG_BANK_CRC), 0); \
		for (int b = 0; b < 4; b++) image[length - CONFIG_BANK_CRC + b] = (uint8_t) (crc >> (8 * b)); \
	} while (0)

	REWRITE(8, (uint16_t) (config_param_count + 1u));                     // de um firmware mais novo
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));
	REWRITE(8, 0u);
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));
	REWRITE(8, config_param_count);
	image[12] ^= 0x10u;                                                    // CRC do esquema
	REWRITE(8, config_param_count);
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));
	image[12] ^= 0x10u;
	REWRITE(8, config_param_count);
	CHECK(ConfigStore_ParseImage(image, length, &back, &sequence));

	// payload anunciado maior que o banco
	uint16_t saved_length = (uint16_t) (image[10] | (image[11] << 8));

	image[10] = 0xFF;
	image[11] = 0x01;
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));
	image[10] = (uint8_t) saved_length;
	image[11] = (uint8_t) (saved_length >> 8);
#undef REWRITE

	// gravada fora da gama: o clamp corrige; incoerente: rejeitada
	config.can1_kbps = 999;   // fora da gama nao (125..1000), mas invalido: so 125/250/500/1000
	length = ConfigStore_BuildImage(&config, 3, image, sizeof(image));
	CHECK(!ConfigStore_ParseImage(image, length, &back, &sequence));
	Config_SetDefaults(&config);
	config.bal_delta_mV = 60000;   // muito acima da gama: fica no maximo
	length = ConfigStore_BuildImage(&config, 3, image, sizeof(image));
	CHECK(ConfigStore_ParseImage(image, length, &back, &sequence));
	CHECK(back.bal_delta_mV == (uint16_t) config_params[param("bal_delta_mV")].max);
}

static void write_bank(uint8_t bank, const bms_config_t *config, uint32_t sequence) {

	uint8_t image[CONFIG_BANK_SIZE];
	uint16_t length = ConfigStore_BuildImage(config, sequence, image, sizeof(image));

	memcpy(&board.eeprom[bank * CONFIG_BANK_SIZE], image, length);
}

static void test_load_banks(void) {

	bms_config_t a;
	bms_config_t b;

	Config_SetDefaults(&a);
	Config_SetDefaults(&b);
	a.cell_max_mV = 4180;
	b.cell_max_mV = 4190;

	// so o banco A
	fake_board_reset();
	write_bank(0, &a, 5);
	ConfigStore_Load();
	CHECK(bms.config.loaded && (bms.eeprom.bank == 0u) && (bms.config.cell_max_mV == 4180u) && (bms.config.sequence == 5u));
	CHECK(!bms.faults.eeprom_error);

	// os dois: vale a sequencia mais alta
	write_bank(1, &b, 6);
	ConfigStore_Load();
	CHECK((bms.eeprom.bank == 1u) && (bms.config.cell_max_mV == 4190u) && (bms.eeprom.sequence == 6u));

	// a volta do contador: 0 e mais recente que 0xFFFFFFFF
	write_bank(0, &a, 0xFFFFFFFFu);
	write_bank(1, &b, 0u);
	ConfigStore_Load();
	CHECK(bms.eeprom.bank == 1u);

	// banco B estragado: fica o A
	write_bank(0, &a, 7);
	write_bank(1, &b, 8);
	board.eeprom[CONFIG_BANK_SIZE + 40u] ^= 0xFFu;
	ConfigStore_Load();
	CHECK((bms.eeprom.bank == 0u) && (bms.config.cell_max_mV == 4180u));

	// EEPROM vazia: defaults, sem erro
	fake_board_reset();
	ConfigStore_Load();
	CHECK(!bms.config.loaded && (bms.eeprom.bank == 0xFFu) && bms.eeprom.present && !bms.faults.eeprom_error);
	CHECK(bms.config.cell_max_mV == config_defaults.cell_max_mV);

	// EEPROM que nao responde ao I2C: defaults e erro
	board.eeprom_present = false;
	ConfigStore_Load();
	CHECK(!bms.eeprom.present && bms.faults.eeprom_error && !bms.config.loaded);

	// le o cabecalho mas falha a leitura do resto: erro de I2C
	fake_board_reset();
	write_bank(0, &a, 9);
	board.eeprom_fail_reads = true;
	ConfigStore_Load();
	CHECK((bms.eeprom.i2c_errors == 2u) && bms.faults.eeprom_error && !bms.config.loaded);
}

/* ================== GRAVACAO ================== */

static void wait_eeprom(uint32_t limit_ms) {

	for (uint32_t elapsed = 0; (elapsed < limit_ms) && ConfigStore_Busy(); elapsed++) {
		run_ms(1);
	}
}

static void test_save_flow(void) {

	boot();
	CHECK(bms.system.hv_safe);

	bms_config_t *edit = ConfigStore_Edit();

	// a copia de edicao nao mexe na ativa ate apply/save
	edit->live_period_ms = 3000;
	ConfigStore_MarkDirty();
	CHECK(bms.eeprom.dirty && !bms.eeprom.reboot_pending);
	CHECK(bms.config.live_period_ms != 3000u);
	ConfigStore_Revert();
	CHECK(!bms.eeprom.dirty && (edit->live_period_ms == bms.config.live_period_ms));

	// incoerente: nem apply nem save
	edit->bal_min_cell_mV = edit->bal_max_cell_mV;
	CHECK(is(ConfigStore_Apply(), "balanceamento: bal_min_cell_mV < bal_max_cell_mV"));
	CHECK(is(ConfigStore_Save(), "balanceamento: bal_min_cell_mV < bal_max_cell_mV"));
	ConfigStore_Revert();

	// save: banco B (o A esta vazio, o atual e "nenhum" -> grava no 0? o outro do que esta em uso)
	edit->can1_kbps = 250;   // REBOOT
	edit->live_period_ms = 1500;
	ConfigStore_MarkDirty();
	CHECK(bms.eeprom.reboot_pending);
	CHECK(ConfigStore_Save() == NULL);
	CHECK(ConfigStore_Busy());
	CHECK(is(ConfigStore_Save(), "EEPROM ocupada"));
	CHECK(bms.config.live_period_ms == 1500u);   // o que muda a quente ja mudou
	CHECK(bms.config.can1_kbps == 500u);
	wait_eeprom(2000);
	CHECK(bms.eeprom.state == EEPROM_DONE);
	CHECK((bms.eeprom.sequence == 1u) && (bms.eeprom.writes == 1u) && !bms.eeprom.dirty && bms.config.loaded);
	CHECK(board.eeprom_max_page_bytes <= 16u);
	uint8_t first_bank = bms.eeprom.bank;

	// a seguinte vai para o outro banco, e o arranque le a mais recente
	edit->live_period_ms = 1600;
	CHECK(ConfigStore_Save() == NULL);
	wait_eeprom(2000);
	CHECK((bms.eeprom.state == EEPROM_DONE) && (bms.eeprom.bank != first_bank) && (bms.eeprom.sequence == 2u));
	ConfigStore_Load();
	CHECK((bms.config.live_period_ms == 1600u) && (bms.config.can1_kbps == 250u));

	// defaults na copia (sem mexer na ativa)
	ConfigStore_Defaults();
	CHECK(bms.eeprom.dirty && (edit->live_period_ms == config_defaults.live_period_ms));
	CHECK(bms.config.live_period_ms == 1600u);
	CHECK(bms.eeprom.reboot_pending);   // can1_kbps volta a 500 na copia

	// com a HV ligada nada se aplica
	bms.system.hv_safe = false;
	CHECK(is(ConfigStore_Apply(), "HV ligada"));
	CHECK(is(ConfigStore_Save(), "HV ligada"));
	bms.system.hv_safe = true;

	// sem EEPROM
	bms.eeprom.present = false;
	CHECK(is(ConfigStore_Save(), "EEPROM ausente"));
}

static void test_save_failures(void) {

	// o I2C nao aceita escritas: tenta, recupera o barramento e desiste
	boot();
	board.eeprom_fail_writes = true;
	CHECK(ConfigStore_Save() == NULL);
	wait_eeprom(500);
	CHECK(bms.eeprom.state == EEPROM_FAILED);
	CHECK((bms.eeprom.failures == 1u) && bms.faults.eeprom_error);
	CHECK(bms.eeprom.i2c_errors >= 4u);
	CHECK(board.eeprom_recoveries >= 4u);
	CHECK(!bms.config.loaded);   // a config anterior continua a valer

	// a escrita interna nunca acaba (sem ACK): timeout
	boot();
	CHECK(ConfigStore_Save() == NULL);
	run_ms(2);
	board.eeprom_stuck_busy = true;
	wait_eeprom(1000);
	CHECK((bms.eeprom.state == EEPROM_FAILED) && bms.faults.eeprom_error);

	// escreve outra coisa: a verificacao apanha e o banco antigo continua a ser o bom
	boot();
	CHECK(ConfigStore_Save() == NULL);
	wait_eeprom(2000);
	CHECK(bms.eeprom.state == EEPROM_DONE);
	uint8_t good_bank = bms.eeprom.bank;

	board.eeprom_corrupt_writes = true;
	CHECK(ConfigStore_Save() == NULL);
	wait_eeprom(2000);
	CHECK(bms.eeprom.state == EEPROM_FAILED);
	board.eeprom_corrupt_writes = false;
	ConfigStore_Load();
	CHECK(bms.config.loaded && (bms.eeprom.bank == good_bank) && (bms.eeprom.sequence == 1u));

	// a leitura da verificacao falha
	boot();
	CHECK(ConfigStore_Save() == NULL);
	while (bms.eeprom.state == EEPROM_WRITING) {
		run_ms(1);
	}
	board.eeprom_fail_reads = true;
	wait_eeprom(1000);
	CHECK(bms.eeprom.state == EEPROM_FAILED);

	// verificacao com a EEPROM ocupada para sempre: timeout
	boot();
	CHECK(ConfigStore_Save() == NULL);
	while (bms.eeprom.state == EEPROM_WRITING) {
		run_ms(1);
	}
	board.eeprom_stuck_busy = true;
	wait_eeprom(1000);
	CHECK(bms.eeprom.state == EEPROM_FAILED);
}

int main(void) {

	test_defaults_and_table();
	test_get_set_element();
	test_parse_numbers();
	test_parse_tables();
	test_parse_text_and_faults();
	test_format();
	test_validate_rules();
	test_clamp();
	test_serialize_roundtrip();
	test_apply_live_and_reboot_pending();
	test_bank_image();
	test_load_banks();
	test_save_flow();
	test_save_failures();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
