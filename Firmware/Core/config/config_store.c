/*
 * config_store.c
 *
 * Ver config_store.h. Dono de bms.config, bms.eeprom, faults.config_invalid
 * e faults.eeprom_error.
 */

#include "config_store.h"
#include "bsp.h"
#include <string.h>

#define WRITE_RETRIES    3u
#define READY_TIMEOUT_MS 20u    // tWC do 24FC08 e 5 ms; com folga para o I2C

_Static_assert((2u * CONFIG_BANK_SIZE) <= BSP_EEPROM_SIZE, "dois bancos na EEPROM");

static bms_config_t edit;                       // copia que a consola altera
static uint8_t image[CONFIG_BANK_SIZE];         // banco a gravar / lido
static uint16_t image_length;
static uint8_t target_bank;
static uint8_t write_retries;
static bool adbms_check_ok = true;

static void update_config_fault(void) {

	bms.faults.config_invalid = !bms.config.loaded || !adbms_check_ok;
}

static uint16_t bank_address(uint8_t bank) {

	return (uint16_t) (bank * CONFIG_BANK_SIZE);
}

static void put_u16(uint8_t *out, uint16_t value) {

	out[0] = (uint8_t) value;
	out[1] = (uint8_t) (value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value) {

	for (uint8_t byte_index = 0; byte_index < 4u; byte_index++) {
		out[byte_index] = (uint8_t) (value >> (8u * byte_index));
	}
}

static uint16_t get_u16(const uint8_t *in) {

	return (uint16_t) (in[0] | (in[1] << 8));
}

static uint32_t get_u32(const uint8_t *in) {

	return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

/* ================== IMAGEM DE UM BANCO ================== */

_Static_assert(CONFIG_BANK_PAYLOAD_MAX >= 400u, "folga para parametros novos");

uint16_t ConfigStore_BuildImage(const bms_config_t *config, uint32_t sequence, uint8_t *out, uint16_t size) {

	uint16_t payload_length = Config_PayloadSize(config_param_count);
	uint16_t total = (uint16_t) (CONFIG_BANK_HEADER + payload_length + CONFIG_BANK_CRC);

	if ((payload_length > CONFIG_BANK_PAYLOAD_MAX) || (total > size)) {
		return 0;
	}

	put_u16(&out[0], CONFIG_MAGIC);
	out[2] = CONFIG_SCHEMA_VERSION;
	out[3] = 0;
	put_u32(&out[4], sequence);
	put_u16(&out[8], config_param_count);
	put_u16(&out[10], payload_length);
	put_u32(&out[12], Config_SchemaCrc(config_param_count));

	if (Config_Serialize(config, &out[CONFIG_BANK_HEADER], payload_length) != payload_length) {
		return 0;
	}

	put_u32(&out[CONFIG_BANK_HEADER + payload_length], Config_Crc32(out, CONFIG_BANK_HEADER + payload_length, 0));
	return total;
}

/* true = banco bom: o config sai com os defaults, por cima o que estava
 * gravado, gamas aplicadas e coerencia verificada */
bool ConfigStore_ParseImage(const uint8_t *in, uint16_t size, bms_config_t *config, uint32_t *sequence) {

	if ((size < (CONFIG_BANK_HEADER + CONFIG_BANK_CRC)) || (get_u16(&in[0]) != CONFIG_MAGIC) || (in[2] != CONFIG_SCHEMA_VERSION)) {
		return false;
	}

	uint16_t param_count = get_u16(&in[8]);
	uint16_t payload_length = get_u16(&in[10]);

	if ((payload_length > CONFIG_BANK_PAYLOAD_MAX) || ((CONFIG_BANK_HEADER + payload_length + CONFIG_BANK_CRC) > size)) {
		return false;
	}
	if (Config_Crc32(in, CONFIG_BANK_HEADER + payload_length, 0) != get_u32(&in[CONFIG_BANK_HEADER + payload_length])) {
		return false;
	}

	// gravado por um firmware com mais parametros (mais novo): nao se le
	if ((param_count == 0u) || (param_count > config_param_count) || (Config_SchemaCrc(param_count) != get_u32(&in[12]))) {
		return false;
	}

	Config_SetDefaults(config);
	if (!Config_Deserialize(config, &in[CONFIG_BANK_HEADER], payload_length, param_count)) {
		return false;
	}

	(void) Config_Clamp(config);
	if (Config_Validate(config) != NULL) {
		return false;
	}

	*sequence = get_u32(&in[4]);
	return true;
}

/* ================== ARRANQUE ================== */

static bool read_bank(uint8_t bank, bms_config_t *config, uint32_t *sequence) {

	uint16_t address = bank_address(bank);

	if (!Bsp_EepromRead(address, image, CONFIG_BANK_HEADER)) {
		bms.eeprom.i2c_errors++;
		return false;
	}

	uint16_t payload_length = get_u16(&image[10]);

	if ((get_u16(&image[0]) != CONFIG_MAGIC) || (payload_length > CONFIG_BANK_PAYLOAD_MAX)) {
		return false;   // vazio (0xFF) ou nunca gravado
	}

	uint16_t rest = (uint16_t) (payload_length + CONFIG_BANK_CRC);

	if (!Bsp_EepromRead((uint16_t) (address + CONFIG_BANK_HEADER), &image[CONFIG_BANK_HEADER], rest)) {
		bms.eeprom.i2c_errors++;
		return false;
	}

	return ConfigStore_ParseImage(image, (uint16_t) (CONFIG_BANK_HEADER + rest), config, sequence);
}

void ConfigStore_Load(void) {

	bms_config_t candidate;
	uint32_t sequence = 0;
	bool found = false;

	memset(&bms.eeprom, 0, sizeof(bms.eeprom));
	bms.eeprom.bank = 0xFFu;
	Config_SetDefaults(&bms.config);

	bms.eeprom.present = Bsp_EepromPresent();

	for (uint8_t bank = 0; bms.eeprom.present && (bank < 2u); bank++) {

		uint32_t bank_sequence;

		if (!read_bank(bank, &candidate, &bank_sequence)) {
			continue;
		}

		// a mais recente; a comparacao com sinal aguenta a volta do contador
		if (!found || ((int32_t) (bank_sequence - sequence) > 0)) {
			bms.config = candidate;
			sequence = bank_sequence;
			bms.eeprom.bank = bank;
			found = true;
		}
	}

	bms.config.loaded = found;
	bms.config.version = CONFIG_SCHEMA_VERSION;
	bms.config.sequence = found ? sequence : 0u;
	bms.eeprom.sequence = bms.config.sequence;
	bms.faults.eeprom_error = !bms.eeprom.present || (bms.eeprom.i2c_errors > 0u);

	edit = bms.config;
	update_config_fault();
}

void ConfigStore_SetAdbmsCheck(bool adbms_ok) {

	adbms_check_ok = adbms_ok;
	update_config_fault();
}

/* ================== EDICAO ================== */

bms_config_t* ConfigStore_Edit(void) {

	return &edit;
}

void ConfigStore_MarkDirty(void) {

	bms.eeprom.dirty = true;
	bms.eeprom.reboot_pending = Config_RebootPending(&bms.config, &edit);
}

bool ConfigStore_Busy(void) {

	return (bms.eeprom.state == EEPROM_WRITING) || (bms.eeprom.state == EEPROM_VERIFY);
}

const char* ConfigStore_Apply(void) {

	const char *problem = Config_Validate(&edit);

	if (problem != NULL) {
		return problem;
	}
	if (!bms.system.hv_safe) {
		return "HV ligada";
	}

	Config_ApplyLive(&bms.config, &edit);
	bms.eeprom.reboot_pending = Config_RebootPending(&bms.config, &edit);
	return NULL;
}

void ConfigStore_Revert(void) {

	edit = bms.config;
	bms.eeprom.dirty = false;
	bms.eeprom.reboot_pending = false;
}

void ConfigStore_Defaults(void) {

	Config_SetDefaults(&edit);
	ConfigStore_MarkDirty();
}

const char* ConfigStore_Save(void) {

	if (ConfigStore_Busy()) {
		return "EEPROM ocupada";
	}
	if (!bms.eeprom.present) {
		return "EEPROM ausente";
	}

	const char *problem = ConfigStore_Apply();

	if (problem != NULL) {
		return problem;
	}

	image_length = ConfigStore_BuildImage(&edit, bms.eeprom.sequence + 1u, image, sizeof(image));
	if (image_length == 0u) {
		return "config nao cabe no banco";
	}

	target_bank = (bms.eeprom.bank == 0u) ? 1u : 0u;
	write_retries = 0;
	bms.eeprom.progress = 0;
	bms.eeprom.step_ms = 0;
	bms.eeprom.state = EEPROM_WRITING;
	return NULL;
}

/* ================== GRAVACAO EM FUNDO ================== */

static void finish(bool ok) {

	if (ok) {
		bms.eeprom.state = EEPROM_DONE;
		bms.eeprom.bank = target_bank;
		bms.eeprom.sequence++;
		bms.eeprom.writes++;
		bms.eeprom.dirty = false;
		bms.config.loaded = true;
		bms.config.sequence = bms.eeprom.sequence;
		bms.faults.eeprom_error = false;
		update_config_fault();
	} else {
		bms.eeprom.state = EEPROM_FAILED;
		bms.eeprom.failures++;
		bms.faults.eeprom_error = true;
		Bsp_EepromRecover();
	}
}

void ConfigStore_Step(uint32_t now_ms) {

	switch (bms.eeprom.state) {

	case EEPROM_WRITING: {

		// a escrita anterior ainda esta a decorrer dentro do chip (tWC)
		if ((bms.eeprom.progress > 0u) && !Bsp_EepromReady()) {
			if ((now_ms - bms.eeprom.step_ms) > READY_TIMEOUT_MS) {
				finish(false);
			}
			return;
		}

		if (bms.eeprom.progress >= image_length) {
			bms.eeprom.state = EEPROM_VERIFY;
			return;
		}

		uint16_t address = (uint16_t) (bank_address(target_bank) + bms.eeprom.progress);
		uint16_t chunk = (uint16_t) (BSP_EEPROM_PAGE - (address % BSP_EEPROM_PAGE));

		if (chunk > (image_length - bms.eeprom.progress)) {
			chunk = (uint16_t) (image_length - bms.eeprom.progress);
		}

		if (Bsp_EepromWritePage(address, &image[bms.eeprom.progress], (uint8_t) chunk)) {
			bms.eeprom.progress = (uint16_t) (bms.eeprom.progress + chunk);
			bms.eeprom.step_ms = now_ms;
			write_retries = 0;
		} else {
			bms.eeprom.i2c_errors++;
			if (++write_retries > WRITE_RETRIES) {
				finish(false);
			} else {
				Bsp_EepromRecover();
			}
		}
		break;
	}

	case EEPROM_VERIFY: {

		bms_config_t check;
		uint32_t sequence;
		static uint8_t readback[CONFIG_BANK_SIZE];

		if (!Bsp_EepromReady()) {
			if ((now_ms - bms.eeprom.step_ms) > READY_TIMEOUT_MS) {
				finish(false);
			}
			return;
		}

		bool ok = Bsp_EepromRead(bank_address(target_bank), readback, image_length) && (memcmp(readback, image, image_length) == 0)
				&& ConfigStore_ParseImage(readback, image_length, &check, &sequence);

		finish(ok);
		break;
	}

	case EEPROM_IDLE:
	case EEPROM_DONE:
	case EEPROM_FAILED:
	default:
		break;
	}
}
