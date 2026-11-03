/*
 * boot_names.c
 *
 * Ver boot_names.h. Os nomes dos erros sao os VAL_ do bootloader.dbc.
 */

#include "boot_names.h"
#include "boot_layout.h"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const char *const error_names[] = {
	"NONE", "HEADER", "TLV", "HASH", "VECTORS", "CRC", "BAD_FRAME", "BAD_STATE", "BAD_SIZE", "BAD_OFFSET",
	"FLASH_READ", "FLASH_WRITE", "FLASH_ERASE", "FLASH_UNLOCK", "FLASH_LOCK", "CAN_QUEUE", "CAN_TIMEOUT", "HANDOFF",
	"FLASH_CONFIG", "INACTIVITY", "RECORD", "FLASH_OPERATION", "BOARD", "CRASHED", "UNCONFIRMED", "MANUAL",
	"INCOMPLETE", "NO_IMAGE"
};

static const char *const run_names[] = { "UNKNOWN", "CONFIRMED", "PENDING", "EXTERNAL" };

static const char *const event_names[] = { "NONE", "BACKUP", "UPLOAD", "INSTALLED", "TRIAL", "CONFIRMED", "ROLLBACK", "REJECTED", "ACCEPTED" };

static const char *const exec_names[] = { "NONE", "UPLOADING", "PENDING", "CONFIRMED", "RESTORING", "REJECTED" };

static const char *const backup_names[] = { "NONE", "WRITING", "VALID" };

_Static_assert(COUNT(error_names) == (BOOT_ERR_NO_IMAGE + 1), "um nome por erro");
_Static_assert(COUNT(event_names) == (BOOT_EVENT_ACCEPTED + 1), "um nome por acontecimento");
_Static_assert(COUNT(exec_names) == (BOOT_EXEC_REJECTED + 1), "um nome por estado do EXEC");

const char* BootNames_Error(uint8_t error) {

	return (error < COUNT(error_names)) ? error_names[error] : "?";
}

const char* BootNames_Run(uint8_t run) {

	return (run < COUNT(run_names)) ? run_names[run] : "?";
}

const char* BootNames_Event(uint8_t event) {

	return (event < COUNT(event_names)) ? event_names[event] : "?";
}

const char* BootNames_ExecState(uint8_t state) {

	return (state < COUNT(exec_names)) ? exec_names[state] : "?";
}

const char* BootNames_BackupState(uint8_t state) {

	return (state < COUNT(backup_names)) ? backup_names[state] : "?";
}
