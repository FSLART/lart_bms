/*
 * boot_ctrl.c
 *
 * Ver boot_ctrl.h. A aplicacao so ACRESCENTA registos ao journal (nunca
 * apaga setores: apagar 16 KB para o CPU ~0,3 s e o WWDG nao deixa); o
 * bootloader garante espaco antes de arrancar uma imagem em teste.
 */

#include "boot_ctrl.h"
#include "boot_image.h"
#include "boot_journal.h"
#include "boot_shared.h"
#include <stdio.h>
#include <string.h>

/* Descritor: o linker poe a secao .app_info 0x200 depois dos vetores
 * (0x08010400, ver STM32F412RETX_FLASH.ld). O uploader le daqui a versao */
__attribute__((section(".app_info"), used)) const boot_app_info_t boot_app_info = {
	.magic = BOOT_APP_INFO_MAGIC,
	.board = BOOT_BOARD_ID,
	.version_major = BMS_FIRMWARE_VERSION,
	.version_minor = BMS_FIRMWARE_MINOR,
	.version_revision = BMS_FIRMWARE_PATCH,
	.version_build = 0,
	.features = BOOT_APP_FEATURE_IWDG | BOOT_APP_FEATURE_CONFIRM,
	.reserved = { 0, 0, 0 }
};

static uint32_t start_ms;

uint32_t BootCtrl_ResetFlags(uint32_t rcc_csr) {

	boot_shared_t shared;

	BootShared_Read(&shared);
	return BootShared_Valid(&shared) ? shared.reset_flags : rcc_csr;
}

// o que o journal diz (contadores, backup)
static void read_journal(void) {

	boot_journal_t journal;

	BootJournal_Load(&journal);
	if (!journal.found) {
		return;
	}
	bms.boot.updates = journal.state.updates;
	bms.boot.rollbacks = journal.state.rollbacks;
	bms.boot.failures = journal.state.failures;
	bms.boot.last_reason = journal.state.reason;
	bms.boot.backup_valid = (journal.state.backup_state == BOOT_BACKUP_VALID);
	bms.boot.backup_version = bms.boot.backup_valid ? BootImage_PeekVersion(BOOT_BACKUP_ADDRESS) : 0u;
	if (bms.boot.run == BOOT_RUN_PENDING) {
		bms.boot.attempts = journal.state.attempts;
	}
}

void BootCtrl_Init(uint32_t now_ms) {

	boot_shared_t shared;

	start_ms = now_ms;
	memset(&bms.boot, 0, sizeof(bms.boot));

	BootShared_Read(&shared);
	if (BootShared_Valid(&shared)) {
		bms.boot.bootloader = true;
		bms.boot.run = shared.run;
		bms.boot.event = shared.event;
		bms.boot.attempts = shared.attempts;
		bms.boot.iwdg = (shared.flags & BOOT_SHARED_FLAG_IWDG) != 0u;
		bms.boot.boot_version = shared.boot_version;
		bms.boot.exec_id = shared.exec_id;
	}
	read_journal();

	/* Lido: a area passa a ter so os bitrates desta config. Um reset do
	 * watchdog leva o bootloader ao bitrate certo, e um arranque sem
	 * bootloader (debugger) ja nao le um estado antigo */
	memset(&shared, 0, sizeof(shared));
	shared.can1_kbps = bms.config.can1_kbps;
	shared.can2_kbps = bms.config.can2_kbps;
	BootShared_Seal(&shared);
	shared.request = 0u;
	BootShared_Write(&shared);
}

bool BootCtrl_Confirm(void) {

	boot_journal_t journal;

	if (bms.boot.run != BOOT_RUN_PENDING) {
		return false;
	}

	// so se o journal ainda falar desta imagem (outro upload pelo meio nao)
	BootJournal_Load(&journal);
	if (!journal.found || (journal.state.exec_state != BOOT_EXEC_PENDING) || (journal.state.exec_id != bms.boot.exec_id)) {
		bms.boot.confirm_failed = true;
		return false;
	}

	boot_record_t record = journal.state;

	record.exec_state = BOOT_EXEC_CONFIRMED;
	record.event = BOOT_EVENT_CONFIRMED;
	record.reason = BOOT_ERR_NONE;
	if (!BootJournal_Write(&journal, &record, false)) {
		bms.boot.confirm_failed = true;
		return false;
	}

	bms.boot.run = BOOT_RUN_CONFIRMED;
	bms.boot.confirmed_now = true;
	bms.boot.confirm_left_s = 0;
	read_journal();
	return true;
}

void BootCtrl_Step(uint32_t now_ms) {

	if ((bms.boot.run != BOOT_RUN_PENDING) || bms.boot.confirm_failed) {
		bms.boot.confirm_left_s = 0;
		return;
	}

	uint32_t elapsed_ms = now_ms - start_ms;
	uint32_t confirm_ms = (uint32_t) bms.config.boot_confirm_s * 1000u;

	if (elapsed_ms < confirm_ms) {
		bms.boot.confirm_left_s = (uint16_t) ((confirm_ms - elapsed_ms + 999u) / 1000u);
		return;
	}
	(void) BootCtrl_Confirm();
}

const char* BootCtrl_CanRollback(void) {

	boot_journal_t journal;

	BootJournal_Load(&journal);
	if (!journal.found || (journal.state.backup_state != BOOT_BACKUP_VALID)) {
		return "nao ha backup";
	}
	if (BootJournal_Free(&journal) == 0u) {
		return "journal cheio (reinicia primeiro)";
	}
	return NULL;
}

bool BootCtrl_Reject(void) {

	boot_journal_t journal;

	if (BootCtrl_CanRollback() != NULL) {
		return false;
	}
	BootJournal_Load(&journal);

	boot_record_t record = journal.state;

	record.exec_state = BOOT_EXEC_REJECTED;
	record.event = BOOT_EVENT_REJECTED;
	record.reason = BOOT_ERR_MANUAL;
	return BootJournal_Write(&journal, &record, false);
}

void BootCtrl_RequestBootloader(void) {

	boot_shared_t shared;

	// mantem o que o bootloader deixou e acrescenta os bitrates e o pedido
	BootShared_Read(&shared);
	if (!BootShared_Valid(&shared)) {
		memset(&shared, 0, sizeof(shared));
	}
	shared.can1_kbps = bms.config.can1_kbps;
	shared.can2_kbps = bms.config.can2_kbps;
	BootShared_Seal(&shared);
	shared.request = BOOT_REQUEST_MAGIC;
	BootShared_Write(&shared);
}

void BootCtrl_VersionText(uint32_t packed, char *out, uint8_t size) {

	(void) snprintf(out, size, "%u.%u.%u", (unsigned) (packed >> 24), (unsigned) ((packed >> 16) & 0xFFu), (unsigned) (packed & 0xFFFFu));
}
