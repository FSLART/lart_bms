/*
 * bl_core.c
 *
 * Ver bl_core.h. Toda a escrita na flash passa por aqui (e pelo bl_protocol
 * no DATA). A ordem das escritas e o que garante que um corte de energia em
 * qualquer ponto deixa sempre um firmware arrancavel:
 *
 *   BEGIN  journal BACKUP=WRITING -> apaga e copia o BACKUP -> verifica ->
 *          journal BACKUP=VALID -> journal EXEC=UPLOADING -> apaga o EXEC
 *   END    valida o EXEC (SHA-256) -> journal EXEC=PENDING
 *   boot   EXEC em teste: journal +1 arranque (+1 falha se o anterior foi watchdog)
 *   repor  journal EXEC=RESTORING -> apaga e copia o EXEC -> verifica ->
 *          journal EXEC=CONFIRMED
 *
 * Um EXEC que nao passa a verificacao (corte a meio do upload ou da
 * reposicao) e reposto do BACKUP no arranque seguinte; um BACKUP so conta
 * depois do registo VALID.
 */

#include "bl_core.h"
#include "bl_log.h"
#include "bl_port.h"
#include "bl_protocol.h"
#include "boot_hash.h"
#include "boot_names.h"

#define COPY_CHUNK         256u
#define JOURNAL_FOR_UPLOAD 8u     // registos livres antes de uma sessao (BEGIN, END, arranques, confirmacao)

static uint32_t align4(uint32_t value) {

	return (value + 3u) & ~3u;
}

static bool write_state(bl_core_t *core, boot_record_t *record) {

	record->can1_kbps = core->can1_kbps;
	record->can2_kbps = core->can2_kbps;

	if (BootJournal_Write(&core->journal, record, true)) {
		return true;
	}

	uint32_t address = BootJournal_SectorAddress(core->journal.active) + ((uint32_t) core->journal.next * BOOT_RECORD_SIZE);

	BlDiag_Error(BOOT_ERR_RECORD, 0, BL_SOURCE_BOOT, address - BOOT_FLASH_BASE);
	BlLog("journal: a escrita falhou");
	return false;
}

// apaga os setores que tocam em [address, address + length)
static bool erase_range(uint32_t address, uint32_t length) {

	int32_t first = BootLayout_Sector(address);
	int32_t last = BootLayout_Sector(address + length - 1u);

	for (int32_t sector = first; sector <= last; sector++) {
		BlPort_DiagFlush();
		if (!BootFlash_EraseSector(BootLayout_SectorAddress(sector))) {
			return false;
		}
	}
	return true;
}

// flash para flash por um buffer na RAM; length multiplo de 4
static bool copy_slot(uint32_t destination, uint32_t source, uint32_t length) {

	uint8_t buffer[COPY_CHUNK];

	for (uint32_t offset = 0; offset < length; offset += COPY_CHUNK) {

		uint32_t chunk = ((length - offset) < COPY_CHUNK) ? (length - offset) : COPY_CHUNK;

		memcpy(buffer, BOOT_FLASH_PTR(source + offset), chunk);
		if (!BootFlash_Program(destination + offset, buffer, chunk)) {
			return false;
		}
	}
	return true;
}

static void log_image(const char *slot, const boot_image_t *image, uint8_t error) {

	if (error != BOOT_ERR_NONE) {
		BlLog("%s: invalido (%s, 0x%08x)", slot, BootNames_Error(error), image->detail);
		return;
	}
	BlLog("%s: %s v%u.%u.%u+%u, %u B, id %08x", slot, (image->kind == BOOT_IMAGE_RAW) ? "RAW" : "MCUboot", image->version_major, image->version_minor,
			image->version_revision, image->version_build, image->length, image->id);
}

/* ================== ARRANQUE ================== */

bool BlCore_WindowReset(uint32_t reset_flags) {

	if ((reset_flags & BL_RESET_WATCHDOGS) != 0u) {
		return true;
	}
	// so o pino (debugger ou botao): um POR, BOR ou reset por software tambem levantam o PINRSTF
	return ((reset_flags & BL_RESET_PIN) != 0u) && ((reset_flags & (BL_RESET_POR | BL_RESET_BOR | BL_RESET_SOFT)) == 0u);
}

static void need_restore(bl_core_t *core, uint8_t reason) {

	boot_image_header_t header;

	core->reason = reason;

	// a versao da imagem posta de lado, tal como esta no cabecalho (sem garantias)
	memcpy(&header, BOOT_FLASH_PTR(BOOT_EXEC_ADDRESS), sizeof(header));
	core->rejected_known = (header.magic == BOOT_IMAGE_MAGIC);
	if (core->rejected_known) {
		core->rejected_major = header.version_major;
		core->rejected_minor = header.version_minor;
		core->rejected_revision = header.version_revision;
		core->rejected_build = header.version_build;
	}
	core->rejected_id = core->journal.state.exec_id;

	if (core->backup_ok) {
		core->action = BL_ACTION_RESTORE;
	} else if (core->exec_ok) {
		core->action = BL_ACTION_BOOT;   // sem backup: arranca o que ha
	} else {
		core->action = BL_ACTION_STAY;
	}
}

void BlCore_Start(bl_core_t *core, uint32_t reset_flags, bool requested, uint16_t can1_kbps, uint16_t can2_kbps) {

	memset(core, 0, sizeof(*core));
	core->reset_flags = reset_flags;
	core->requested = requested;
	core->can1_kbps = can1_kbps;
	core->can2_kbps = can2_kbps;

	BootJournal_Load(&core->journal);

	const boot_record_t *state = &core->journal.state;

	BlLog("journal: %s, EXEC %s, BACKUP %s, %u updates, %u rollbacks", core->journal.found ? "ok" : "vazio", BootNames_ExecState(state->exec_state),
			BootNames_BackupState(state->backup_state), state->updates, state->rollbacks);

	// BACKUP: so depois do registo VALID e se for a imagem que o journal diz
	if (state->backup_state == BOOT_BACKUP_VALID) {
		uint8_t error = BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, state->backup_length, false, &core->backup);

		if ((error == BOOT_ERR_NONE) && ((core->backup.id != state->backup_id) || (core->backup.length > BOOT_EXEC_SIZE))) {
			error = BOOT_ERR_HASH;
		}
		core->backup_ok = (error == BOOT_ERR_NONE);
		log_image("BACKUP", &core->backup, error);
	}

	// EXEC: MCUboot (SHA-256) ou RAW (gravada pelo ST-LINK)
	uint8_t error = BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, true, &core->exec);
	bool managed = (error == BOOT_ERR_NONE) && (core->exec.kind == BOOT_IMAGE_PACKAGED) && (core->exec.id == state->exec_id)
			&& (core->exec.length == state->exec_length);

	log_image("EXEC", &core->exec, error);
	core->exec_ok = (error == BOOT_ERR_NONE);
	core->run = core->exec_ok ? BOOT_RUN_EXTERNAL : BOOT_RUN_UNKNOWN;
	core->action = core->exec_ok ? BL_ACTION_BOOT : BL_ACTION_STAY;

	switch (state->exec_state) {

	case BOOT_EXEC_UPLOADING:
	case BOOT_EXEC_RESTORING:
		// a meio: so uma RAW (o debugger gravou por cima) arranca sem mais
		if (!core->exec_ok || (core->exec.kind != BOOT_IMAGE_RAW)) {
			need_restore(core, BOOT_ERR_INCOMPLETE);
		}
		break;

	case BOOT_EXEC_PENDING:
	case BOOT_EXEC_CONFIRMED:
	case BOOT_EXEC_REJECTED:
		if (managed) {
			core->run = (state->exec_state == BOOT_EXEC_PENDING) ? BOOT_RUN_PENDING : BOOT_RUN_CONFIRMED;
			if (state->exec_state == BOOT_EXEC_REJECTED) {
				need_restore(core, BOOT_ERR_MANUAL);
			}
		} else if (!core->exec_ok) {
			need_restore(core, error);   // a imagem que la estava estragou-se
		}
		// valida mas outra: gravada a mao por cima, fica EXTERNAL
		break;

	default:
		if (!core->exec_ok) {
			need_restore(core, error);
		}
		break;
	}

	// imagem em teste: o arranque anterior acabou no watchdog?
	if ((core->action == BL_ACTION_BOOT) && (core->run == BOOT_RUN_PENDING)) {

		core->crashed = (state->attempts > 0u) && ((reset_flags & BL_RESET_WATCHDOGS) != 0u);

		uint32_t failures = (uint32_t) state->failures + (core->crashed ? 1u : 0u);

		if (failures >= BOOT_MAX_FAILURES) {
			need_restore(core, BOOT_ERR_CRASHED);
		} else if (state->attempts >= BOOT_MAX_ATTEMPTS) {
			need_restore(core, BOOT_ERR_UNCONFIRMED);
		}
	}

	if (core->action == BL_ACTION_STAY) {
		if (core->reason == BOOT_ERR_NONE) {
			core->reason = BOOT_ERR_NO_IMAGE;
		}
		core->entry_flags |= BL_ENTRY_RECOVERY;
	}
	if (core->action == BL_ACTION_RESTORE) {
		core->entry_flags |= BL_ENTRY_ROLLBACK;
	}
	if (requested) {
		core->entry_flags |= BL_ENTRY_RAM;
	}

	static const char *const actions[] = { "arrancar o EXEC", "repor o BACKUP e arrancar", "ficar no bootloader" };

	BlLog("decisao: %s (%s, %s)%s", actions[core->action], BootNames_Run(core->run), BootNames_Error(core->reason), requested ? ", pedido na RAM" : "");
	BlDiag_FromCore(core);
}

bool BlCore_CanBoot(const bl_core_t *core) {

	return core->action != BL_ACTION_STAY;
}

/* ================== REPOR O BACKUP ================== */

static bool restore(bl_core_t *core) {

	boot_record_t record = core->journal.state;
	uint32_t length = core->backup.length;

	BlLog("rollback (%s): a repor o BACKUP v%u.%u.%u no EXEC", BootNames_Error(core->reason), core->backup.version_major, core->backup.version_minor,
			core->backup.version_revision);

	// sem este registo nao faz mal: um EXEC a meio falha a verificacao e repete-se
	record.exec_state = BOOT_EXEC_RESTORING;
	record.event = BOOT_EVENT_ROLLBACK;
	record.reason = core->reason;
	(void) write_state(core, &record);

	core->exec_ok = false;
	if (!erase_range(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE) || !copy_slot(BOOT_EXEC_ADDRESS, BOOT_BACKUP_ADDRESS, align4(length))) {
		BlLog("rollback: erro na flash");
		return false;
	}

	uint8_t error = BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, length, false, &core->exec);

	if ((error != BOOT_ERR_NONE) || (core->exec.id != core->backup.id)) {
		BlDiag_Error((error != BOOT_ERR_NONE) ? error : BOOT_ERR_HASH, 1, BL_SOURCE_BOOT, core->exec.detail);
		BlLog("rollback: a copia nao confere");
		return false;
	}

	record = core->journal.state;
	record.exec_state = BOOT_EXEC_CONFIRMED;
	record.exec_length = length;
	record.exec_id = core->backup.id;
	record.attempts = 0;
	record.failures = 0;
	record.rollbacks++;
	record.event = BOOT_EVENT_ROLLBACK;
	record.reason = core->reason;
	(void) write_state(core, &record);

	core->exec_ok = true;
	core->run = BOOT_RUN_CONFIRMED;
	core->action = BL_ACTION_BOOT;
	core->event = BOOT_EVENT_ROLLBACK;
	BlLog("rollback: EXEC reposto");
	return true;
}

bool BlCore_PrepareBoot(bl_core_t *core) {

	if (core->action == BL_ACTION_STAY) {
		return false;
	}

	if (core->action == BL_ACTION_RESTORE) {
		bool restored = restore(core);

		BlDiag_FromCore(core);
		if (!restored) {
			core->action = BL_ACTION_STAY;
			core->entry_flags |= BL_ENTRY_RECOVERY;
			BlDiag_FromCore(core);
			return false;
		}
	} else if ((core->reason != BOOT_ERR_NONE) && core->exec_ok
			&& ((core->run == BOOT_RUN_PENDING) || (core->journal.state.exec_state == BOOT_EXEC_REJECTED))) {
		// queria voltar ao backup mas nao ha: a imagem que ha passa a ser a boa
		boot_record_t record = core->journal.state;

		record.exec_state = BOOT_EXEC_CONFIRMED;
		record.attempts = 0;
		record.failures = 0;
		record.event = BOOT_EVENT_ACCEPTED;
		record.reason = core->reason;
		(void) write_state(core, &record);
		core->run = BOOT_RUN_CONFIRMED;
		core->event = BOOT_EVENT_ACCEPTED;
		BlLog("sem BACKUP para onde voltar (%s): a imagem fica", BootNames_Error(core->reason));
	}

	if (core->run == BOOT_RUN_PENDING) {
		// espaco para a confirmacao da aplicacao (ela nao pode apagar setores)
		if (BootJournal_Free(&core->journal) <= BOOT_JOURNAL_RESERVE) {
			(void) BootJournal_Compact(&core->journal);
		}

		boot_record_t record = core->journal.state;

		if (record.attempts < 255u) {
			record.attempts++;
		}
		if (core->crashed && (record.failures < 255u)) {
			record.failures++;
		}
		record.event = BOOT_EVENT_TRIAL;
		record.reason = core->crashed ? BOOT_ERR_CRASHED : BOOT_ERR_NONE;
		(void) write_state(core, &record);   // sem registo arranca na mesma

		core->event = BOOT_EVENT_TRIAL;
		core->iwdg = (core->exec.features & BOOT_APP_FEATURE_IWDG) != 0u;
		BlLog("imagem em teste: arranque %u de %u, falhas %u de %u", record.attempts, BOOT_MAX_ATTEMPTS, record.failures, BOOT_MAX_FAILURES);
	}

	return core->exec_ok;
}

/* ================== UPLOAD ================== */

static bool same_image(const bl_core_t *core, bool raw) {

	if (raw) {
		// o BACKUP de uma RAW tem um cabecalho gerado e o mesmo conteudo depois dele
		return (core->backup.image_size == core->exec.image_size)
				&& (memcmp(BOOT_FLASH_PTR(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE), BOOT_FLASH_PTR(BOOT_EXEC_ADDRESS + BOOT_HEADER_SIZE), core->exec.image_size) == 0);
	}
	return (core->backup.length == core->exec.length) && (memcmp(BOOT_FLASH_PTR(BOOT_BACKUP_ADDRESS), BOOT_FLASH_PTR(BOOT_EXEC_ADDRESS), core->exec.length) == 0);
}

static uint8_t backup_exec(bl_core_t *core) {

	const boot_image_t *exec = &core->exec;
	bool good = core->exec_ok && ((core->run == BOOT_RUN_CONFIRMED) || (core->run == BOOT_RUN_EXTERNAL));

	if (!good) {
		BlLog("BEGIN: o EXEC nao e firmware confirmado, o BACKUP fica (%s)", core->backup_ok ? "valido" : "vazio");
		return BOOT_ERR_NONE;
	}

	bool raw = (exec->kind == BOOT_IMAGE_RAW);
	uint32_t length = raw ? (BOOT_HEADER_SIZE + exec->image_size + BOOT_TLV_TOTAL) : exec->length;

	if (length > BOOT_EXEC_SIZE) {
		BlLog("BEGIN: a imagem RAW nao cabe no EXEC com cabecalho, sem backup");
		return BOOT_ERR_NONE;
	}
	if (core->backup_ok && same_image(core, raw)) {
		BlLog("BEGIN: o BACKUP ja tem este firmware");
		return BOOT_ERR_NONE;
	}

	boot_record_t record = core->journal.state;

	record.backup_state = BOOT_BACKUP_WRITING;
	record.event = BOOT_EVENT_BACKUP;
	record.reason = BOOT_ERR_NONE;
	if (!write_state(core, &record)) {
		return BOOT_ERR_RECORD;
	}
	core->backup_ok = false;

	BlLog("BEGIN: a guardar o firmware atual no BACKUP (%u B)", length);
	if (!erase_range(BOOT_BACKUP_ADDRESS, length)) {
		return BOOT_ERR_FLASH_ERASE;
	}

	bool copied;

	if (raw) {
		// a RAW nao tem cabecalho nem hash: fazem-se aqui
		uint8_t header[BOOT_HEADER_SIZE];

		BootImage_MakeHeader(header, exec->image_size, exec);
		copied = BootFlash_Program(BOOT_BACKUP_ADDRESS, header, BOOT_HEADER_SIZE)
				&& copy_slot(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE, BOOT_EXEC_ADDRESS + BOOT_HEADER_SIZE, exec->image_size);
		if (copied) {
			boot_sha256_t sha;
			uint8_t digest[BOOT_SHA256_SIZE];
			uint8_t tlv[BOOT_TLV_TOTAL];

			BootSha256_Init(&sha);
			BootSha256_Update(&sha, BOOT_FLASH_PTR(BOOT_BACKUP_ADDRESS), BOOT_HEADER_SIZE + exec->image_size);
			BootSha256_Final(&sha, digest);
			BootImage_MakeTlv(tlv, digest);
			copied = BootFlash_Program(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE + exec->image_size, tlv, BOOT_TLV_TOTAL);
		}
	} else {
		copied = copy_slot(BOOT_BACKUP_ADDRESS, BOOT_EXEC_ADDRESS, align4(length));
	}
	if (!copied) {
		return BOOT_ERR_FLASH_WRITE;
	}

	uint8_t error = BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, length, false, &core->backup);

	if ((error == BOOT_ERR_NONE) && !raw && (core->backup.id != exec->id)) {
		error = BOOT_ERR_HASH;
	}
	if (error != BOOT_ERR_NONE) {
		BlDiag_Error(error, 2, BL_SOURCE_VALIDATION, core->backup.detail);
		BlLog("BEGIN: a copia no BACKUP nao confere (%s)", BootNames_Error(error));
		return error;
	}

	record = core->journal.state;
	record.backup_state = BOOT_BACKUP_VALID;
	record.backup_length = length;
	record.backup_id = core->backup.id;
	record.event = BOOT_EVENT_BACKUP;
	record.reason = BOOT_ERR_NONE;
	if (!write_state(core, &record)) {
		return BOOT_ERR_RECORD;
	}
	core->backup_ok = true;
	log_image("BACKUP", &core->backup, BOOT_ERR_NONE);
	return BOOT_ERR_NONE;
}

uint8_t BlCore_BeginUpload(bl_core_t *core, uint32_t length) {

	if (BootJournal_Free(&core->journal) < JOURNAL_FOR_UPLOAD) {
		(void) BootJournal_Compact(&core->journal);
	}

	uint8_t error = backup_exec(core);

	if (error != BOOT_ERR_NONE) {
		return error;
	}

	boot_record_t record = core->journal.state;

	record.exec_state = BOOT_EXEC_UPLOADING;
	record.exec_length = length;
	record.exec_id = 0;
	record.attempts = 0;
	record.failures = 0;
	record.event = BOOT_EVENT_UPLOAD;
	record.reason = BOOT_ERR_NONE;
	if (!write_state(core, &record)) {
		return BOOT_ERR_RECORD;
	}

	// daqui ate ao END o EXEC nao arranca: um reset repoe o BACKUP
	memset(&core->exec, 0, sizeof(core->exec));
	core->exec_ok = false;
	core->run = BOOT_RUN_UNKNOWN;
	core->reason = BOOT_ERR_INCOMPLETE;
	core->action = core->backup_ok ? BL_ACTION_RESTORE : BL_ACTION_STAY;
	BlDiag_ValidBanks(core);   // a decisao do arranque fica no diagnostico ate ao reset, como na ACU

	BlLog("BEGIN: a apagar o EXEC para %u B", length);
	if (!erase_range(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE)) {
		return BOOT_ERR_FLASH_ERASE;
	}
	return BOOT_ERR_NONE;
}

uint8_t BlCore_FinishUpload(bl_core_t *core, uint32_t length) {

	uint8_t error = BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, length, false, &core->exec);

	if (error != BOOT_ERR_NONE) {
		BlDiag_Error(error, 1, BL_SOURCE_VALIDATION, core->exec.detail);
		BlLog("END: imagem rejeitada (%s, 0x%08x)", BootNames_Error(error), core->exec.detail);
		memset(&core->exec, 0, sizeof(core->exec));
		return error;
	}

	boot_record_t record = core->journal.state;

	record.exec_state = BOOT_EXEC_PENDING;
	record.exec_length = length;
	record.exec_id = core->exec.id;
	record.attempts = 0;
	record.failures = 0;
	record.updates++;
	record.event = BOOT_EVENT_INSTALLED;
	record.reason = BOOT_ERR_NONE;
	if (!write_state(core, &record)) {
		return BOOT_ERR_RECORD;
	}

	core->exec_ok = true;
	core->run = BOOT_RUN_PENDING;
	core->action = BL_ACTION_BOOT;
	core->reason = BOOT_ERR_NONE;
	core->crashed = false;
	BlDiag_ValidBanks(core);
	BlLog("END: v%u.%u.%u+%u instalada (id %08x), em teste a partir do proximo arranque", core->exec.version_major, core->exec.version_minor,
			core->exec.version_revision, core->exec.version_build, core->exec.id);
	return BOOT_ERR_NONE;
}
