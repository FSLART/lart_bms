/*
 * bl_protocol.c
 *
 * Ver bl_protocol.h. O tratamento de cada comando segue o boot_update_frame()
 * da ACU (mcu_boot/Core/Src/boot_update.c): mesmos status, mesmas regras de
 * repeticao e os mesmos diagnosticos. No F412 o DATA grava logo a palavra de
 * 32 bits (o L476 juntava duas tramas por causa do ECC de 64 bits).
 */

#include "bl_protocol.h"
#include "bl_log.h"
#include "bl_port.h"
#include "boot_hash.h"
#include "boot_names.h"

#define PROGRESS_STEP 16384u

bl_diag_t bl_diag;
bl_session_t bl_session;

static void put_u16(uint8_t *out, uint32_t value) {

	out[0] = (uint8_t) value;
	out[1] = (uint8_t) (value >> 8);
}

static void put_u24(uint8_t *out, uint32_t value) {

	put_u16(out, value);
	out[2] = (uint8_t) (value >> 16);
}

static void put_u32(uint8_t *out, uint32_t value) {

	put_u24(out, value);
	out[3] = (uint8_t) (value >> 24);
}

static uint32_t get_u32(const uint8_t *in) {

	return (uint32_t) in[0] | ((uint32_t) in[1] << 8) | ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
}

/* ================== DIAGNOSTICO ================== */

static void mark(uint8_t kind) {

	for (uint8_t bus = 0; bus < BL_BUS_COUNT; bus++) {
		bl_diag.pending[bus] |= (uint16_t) (1u << kind);
	}
}

void BlDiag_Snapshot(void) {

	mark(BL_DIAG_INFO);
	mark(BL_DIAG_DECISION);
	mark(BL_DIAG_UPLOAD);
	if (bl_diag.selected_bank != 0u) {
		mark(BL_DIAG_VERSION);
	}
	if (bl_diag.error_code != BOOT_ERR_NONE) {
		mark(BL_DIAG_ERROR);
	}
	if (bl_diag.error_source == BL_SOURCE_FLASH) {
		mark(BL_DIAG_FLASH);
	}
	if (bl_diag.rejected_bank != 0u) {
		mark(BL_DIAG_ROLLBACK);
		if (bl_diag.rejected_version_known) {
			mark(BL_DIAG_REJECTED_VERSION);
		}
	}
}

void BlDiag_Error(uint8_t code, uint8_t bank, uint8_t source, uint32_t detail) {

	bl_diag.error_sequence++;
	bl_diag.error_code = code;
	bl_diag.error_bank = bank;
	bl_diag.error_command = bl_diag.current_command;
	bl_diag.error_source = source;
	bl_diag.error_detail = detail;
	mark(BL_DIAG_ERROR);
}

void BlDiag_FlashError(uint8_t code, uint32_t offset, uint32_t hardware_error) {

	BlDiag_Error(code, (offset < (BOOT_BACKUP_ADDRESS - BOOT_FLASH_BASE)) ? 1u : 2u, BL_SOURCE_FLASH, offset);
	bl_diag.flash_offset = offset;
	bl_diag.flash_error = hardware_error;
	mark(BL_DIAG_FLASH);
}

void BlDiag_ValidBanks(const bl_core_t *core) {

	bl_diag.valid_banks = (uint8_t) ((core->exec_ok ? 1u : 0u) | (core->backup_ok ? 2u : 0u));
	mark(BL_DIAG_INFO);
}

void BlDiag_FromCore(const bl_core_t *core) {

	const boot_image_t *selected = NULL;

	bl_diag.valid_banks = (uint8_t) ((core->exec_ok ? 1u : 0u) | (core->backup_ok ? 2u : 0u));
	bl_diag.entry_flags = core->entry_flags;
	bl_diag.reset_flags = core->reset_flags;

	if (core->exec_ok) {
		bl_diag.selected_bank = (core->event == BOOT_EVENT_ROLLBACK) ? 2u : 1u;
		selected = &core->exec;
	} else if (core->action == BL_ACTION_RESTORE) {
		bl_diag.selected_bank = 2u;
		selected = &core->backup;
	} else {
		bl_diag.selected_bank = 0u;
	}
	if (selected != NULL) {
		bl_diag.version_major = selected->version_major;
		bl_diag.version_minor = selected->version_minor;
		bl_diag.version_revision = selected->version_revision;
		bl_diag.version_build = selected->version_build;
	}

	// a imagem posta de lado (rollback ou recovery)
	if ((core->reason != BOOT_ERR_NONE) && ((core->action != BL_ACTION_BOOT) || (core->event == BOOT_EVENT_ROLLBACK))) {
		bl_diag.rejected_bank = 1u;
		bl_diag.reject_reason = core->reason;
		bl_diag.rejected_version_known = core->rejected_known ? 1u : 0u;
		bl_diag.rejected_major = core->rejected_major;
		bl_diag.rejected_minor = core->rejected_minor;
		bl_diag.rejected_revision = core->rejected_revision;
		bl_diag.rejected_build = core->rejected_build;
		bl_diag.rejected_generation = core->journal.state.updates;
	} else {
		bl_diag.rejected_bank = 0u;
		bl_diag.reject_reason = 0u;
		bl_diag.rejected_version_known = 0u;
	}

	BlDiag_Snapshot();
}

int BlDiag_Next(uint8_t bus, uint32_t *id, uint8_t data[8]) {

	uint16_t pending = bl_diag.pending[bus];
	int kind = 0;

	if (pending == 0u) {
		return -1;
	}
	while ((pending & (1u << kind)) == 0u) {
		kind++;
	}

	memset(data, 0, 8);
	*id = BL_CAN_DIAG_BASE_ID + (uint32_t) kind;

	switch (kind) {
	case BL_DIAG_INFO:
		data[0] = BOOT_PROTOCOL_VERSION;
		data[1] = BOOT_BOARD_TYPE;
		data[2] = bl_diag.valid_banks;
		data[3] = bl_diag.upload_stage;
		put_u32(&data[4], BOOT_IMAGE_MAX);
		break;
	case BL_DIAG_VERSION:
		data[0] = bl_diag.version_major;
		data[1] = bl_diag.version_minor;
		put_u16(&data[2], bl_diag.version_revision);
		put_u32(&data[4], bl_diag.version_build);
		break;
	case BL_DIAG_DECISION:
		data[0] = bl_diag.selected_bank;
		data[1] = bl_diag.entry_flags;
		data[2] = bl_diag.rejected_bank;
		data[3] = bl_diag.reject_reason;
		put_u32(&data[4], bl_diag.reset_flags);
		break;
	case BL_DIAG_UPLOAD:
		data[0] = 1u;   // o upload e sempre para o EXEC
		data[1] = bl_diag.upload_stage;
		put_u24(&data[2], bl_diag.received);
		put_u24(&data[5], bl_diag.total);
		break;
	case BL_DIAG_ERROR:
		data[0] = bl_diag.error_code;
		data[1] = bl_diag.error_bank;
		data[2] = bl_diag.error_command;
		data[3] = bl_diag.error_source;
		put_u32(&data[4], bl_diag.error_detail);
		break;
	case BL_DIAG_ROLLBACK:
		data[0] = bl_diag.rejected_bank;
		data[1] = bl_diag.selected_bank;
		data[2] = bl_diag.reject_reason;
		data[3] = bl_diag.rejected_version_known;
		put_u32(&data[4], bl_diag.rejected_generation);
		break;
	case BL_DIAG_FLASH:
		put_u32(&data[0], bl_diag.flash_offset);
		put_u32(&data[4], bl_diag.flash_error);
		break;
	case BL_DIAG_REJECTED_VERSION:
		data[0] = bl_diag.rejected_major;
		data[1] = bl_diag.rejected_minor;
		put_u16(&data[2], bl_diag.rejected_revision);
		put_u32(&data[4], bl_diag.rejected_build);
		break;
	default:
		bl_diag.pending[bus] = 0;
		return -1;
	}
	return kind;
}

void BlDiag_Sent(uint8_t bus, int kind) {

	if ((kind >= 0) && (kind < BL_DIAG_COUNT)) {
		bl_diag.pending[bus] &= (uint16_t) ~(1u << kind);
	}
}

void BlProtocol_Heartbeat(uint32_t now_ms, uint8_t data[8]) {

	data[0] = 0x55u;
	put_u32(&data[1], now_ms);
	data[5] = BOOT_VERSION_MAJOR;
	data[6] = BOOT_VERSION_MINOR;
	data[7] = BOOT_VERSION_PATCH;
}

/* ================== SESSAO ================== */

static void upload_status(uint8_t stage) {

	bl_diag.upload_stage = stage;
	bl_diag.received = bl_session.received;
	bl_diag.total = bl_session.length;
	mark(BL_DIAG_UPLOAD);
	BlPort_DiagFlush();
}

void BlProtocol_Init(uint32_t now_ms) {

	memset(&bl_session, 0, sizeof(bl_session));
	bl_session.last_ok_ms = now_ms;
	bl_diag.current_command = 0xFFu;   // arranque, nenhum comando
	bl_diag.upload_stage = BL_STAGE_IDLE;
}

bool BlProtocol_Busy(void) {

	return bl_session.active || bl_session.complete;
}

static uint8_t command_begin(bl_core_t *core, uint8_t bus, uint32_t length) {

	if (bl_session.complete) {
		return BL_BAD_STATE;   // ja instalada: so o RESET
	}
	if ((length < BOOT_IMAGE_MIN) || (length > BOOT_IMAGE_MAX)) {
		return BL_BAD_SIZE;
	}
	if (bl_session.active && (bl_session.received == 0u) && (length == bl_session.length)) {
		return BL_OK;   // BEGIN repetido (perdeu-se o ACK): nao se apaga outra vez
	}

	bl_session.active = false;
	bl_session.received = 0;
	bl_session.length = length;
	bl_session.bus = bus;
	upload_status(BL_STAGE_ERASING);

	uint32_t errors_before = bl_diag.error_sequence;
	uint8_t error = BlCore_BeginUpload(core, length);

	if (error != BOOT_ERR_NONE) {
		if (bl_diag.error_sequence == errors_before) {
			BlDiag_Error(error, 1u, BL_SOURCE_UPLOAD, length);
		}
		return BL_FLASH_ERROR;
	}

	bl_session.crc = 0;
	memset(bl_session.last, 0, sizeof(bl_session.last));
	bl_session.active = true;
	upload_status(BL_STAGE_RECEIVING);
	return BL_OK;
}

static uint8_t command_data(const uint8_t *data) {

	if (!bl_session.active) {
		return BL_BAD_STATE;
	}
	// so a repeticao exata do ultimo DATA leva ACK sem gravar outra vez
	if ((bl_session.received > 0u) && (memcmp(bl_session.last, data, 8) == 0)) {
		return BL_OK;
	}

	uint32_t offset = (uint32_t) data[1] | ((uint32_t) data[2] << 8) | ((uint32_t) data[3] << 16);

	if ((offset != bl_session.received) || (bl_session.received == bl_session.length)) {
		return BL_BAD_OFFSET;
	}

	uint32_t count = bl_session.length - bl_session.received;
	uint8_t word[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

	if (count > 4u) {
		count = 4u;
	}
	memcpy(word, &data[4], count);

	if (!BootFlash_Program(BOOT_EXEC_ADDRESS + bl_session.received, word, sizeof(word))) {
		bl_session.active = false;
		return BL_FLASH_ERROR;
	}

	bl_session.crc = BootCrc32_Update(bl_session.crc, &data[4], count);
	bl_session.received += count;
	memcpy(bl_session.last, data, 8);
	bl_diag.received = bl_session.received;

	if (((bl_session.received % PROGRESS_STEP) == 0u) || (bl_session.received == bl_session.length)) {
		upload_status(BL_STAGE_RECEIVING);
		BlLog("upload: %u/%u B", bl_session.received, bl_session.length);
	}
	return BL_OK;
}

static uint8_t command_end(bl_core_t *core, uint32_t crc) {

	if (bl_session.complete && (crc == bl_session.crc)) {
		return BL_OK;   // END repetido
	}
	if (!bl_session.active || (bl_session.received != bl_session.length)) {
		return BL_BAD_STATE;
	}

	upload_status(BL_STAGE_VALIDATING);
	if (crc != bl_session.crc) {
		BlDiag_Error(BOOT_ERR_CRC, 1u, BL_SOURCE_UPLOAD, bl_session.crc);
		BlLog("END: CRC do uploader 0x%08x, recebido 0x%08x", crc, bl_session.crc);
		return BL_BAD_IMAGE;
	}

	uint8_t error = BlCore_FinishUpload(core, bl_session.length);

	if (error == BOOT_ERR_RECORD) {
		bl_session.active = false;
		return BL_FLASH_ERROR;
	}
	if (error != BOOT_ERR_NONE) {
		return BL_BAD_IMAGE;
	}

	bl_session.active = false;
	bl_session.complete = true;
	upload_status(BL_STAGE_COMMITTED);
	return BL_OK;
}

uint8_t BlProtocol_Frame(bl_core_t *core, uint8_t bus, const uint8_t *data, uint8_t dlc, uint8_t reply[8]) {

	uint8_t status = BL_OK;
	uint8_t command = (dlc > 0u) ? data[0] : 0x7Fu;
	uint32_t value = 0;
	uint32_t errors_before = bl_diag.error_sequence;

	bl_diag.current_command = command;
	memset(reply, 0, 8);

	if (dlc != 8u) {
		status = BL_BAD_FRAME;
	} else if (BlProtocol_Busy() && (bus != bl_session.bus) && (command != BL_CMD_INFO)) {
		status = BL_BAD_STATE;   // ha uma sessao no outro bus
	} else {
		switch (command) {
		case BL_CMD_INFO:
			BlDiag_Snapshot();
			value = BOOT_IMAGE_MAX;
			break;
		case BL_CMD_BEGIN:
			status = command_begin(core, bus, get_u32(&data[1]));
			break;
		case BL_CMD_DATA:
			status = command_data(data);
			break;
		case BL_CMD_END:
			status = command_end(core, get_u32(&data[1]));
			break;
		case BL_CMD_RESET:
			bl_session.reset_requested = true;
			break;
		default:
			status = BL_BAD_FRAME;
			break;
		}
	}

	if (status != BL_OK) {
		// o erro mais especifico ja foi registado por quem falhou
		if (bl_diag.error_sequence == errors_before) {
			static const uint8_t codes[] = { BOOT_ERR_NONE, BOOT_ERR_FRAME, BOOT_ERR_STATE, BOOT_ERR_SIZE, BOOT_ERR_OFFSET, BOOT_ERR_FLASH_OPERATION, BOOT_ERR_HASH };
			uint32_t detail = bl_session.received;

			if (status == BL_BAD_FRAME) {
				detail = dlc;
			} else if (status == BL_BAD_SIZE) {
				detail = get_u32(&data[1]);
			} else if (status == BL_BAD_OFFSET) {
				detail = (uint32_t) data[1] | ((uint32_t) data[2] << 8) | ((uint32_t) data[3] << 16);
			}
			BlDiag_Error(codes[status], 1u, BL_SOURCE_UPLOAD, detail);
		}
		upload_status(((status == BL_BAD_IMAGE) || (status == BL_FLASH_ERROR) || (!bl_session.active && !bl_session.complete)) ? BL_STAGE_FAILED
				: (bl_session.complete ? BL_STAGE_COMMITTED : BL_STAGE_RECEIVING));
		BlLog("comando 0x%02x recusado no CAN%u: status %u, %u/%u B, DLC %u", command, bus + 1u, status, bl_session.received, bl_session.length, dlc);
	} else {
		bl_session.last_ok_ms = BlPort_Millis();
	}

	if (command != BL_CMD_INFO) {
		value = bl_session.received;
	}

	reply[0] = (uint8_t) (command | 0x80u);
	reply[1] = status;
	put_u32(&reply[2], value);
	reply[6] = BOOT_PROTOCOL_VERSION;
	reply[7] = bl_session.complete ? 2u : (bl_session.active ? 1u : 0u);
	return status;
}
