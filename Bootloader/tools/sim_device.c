/*
 * sim_device.c
 *
 * O bootloader do BMS como biblioteca para o PC (test_can_upload.py): a
 * logica real (bl_core, bl_protocol, Core/boot) com a flash num array. O
 * teste em Python liga-a a um bus CAN virtual e corre o can_upload.py contra
 * ela, trama a trama.
 */

#include "bl_core.h"
#include "bl_protocol.h"
#include "bl_port.h"
#include "boot_shared.h"
#include <stdio.h>
#include <string.h>

uint8_t boot_test_flash[BOOT_FLASH_SIZE];
boot_shared_t boot_test_shared;

static bl_core_t core;
static uint32_t now_ms;

uint32_t BlPort_Millis(void) {

	return now_ms++;
}

void BlPort_Write(const char *text, uint32_t length) {

	(void) text;
	(void) length;
}

void BlPort_DiagFlush(void) {
}

bool BootFlash_EraseSector(uint32_t address) {

	int32_t sector = BootLayout_Sector(address);

	if ((sector < 2) || (BootLayout_SectorAddress(sector) != address)) {
		return false;
	}
	memset(&boot_test_flash[address - BOOT_FLASH_BASE], 0xFF, BootLayout_SectorSize(sector));
	return true;
}

bool BootFlash_Program(uint32_t address, const void *data, uint32_t length) {

	const uint8_t *bytes = (const uint8_t*) data;
	uint8_t *cell = &boot_test_flash[address - BOOT_FLASH_BASE];

	if (((address & 3u) != 0u) || ((length & 3u) != 0u) || (address < BOOT_JOURNAL_A_ADDRESS)) {
		return false;
	}
	for (uint32_t index = 0; index < length; index++) {
		cell[index] &= bytes[index];
	}
	return memcmp(cell, data, length) == 0;
}

/* ================== API para o Python ================== */

void sim_erase(void) {

	memset(boot_test_flash, 0xFF, sizeof(boot_test_flash));
	memset(&boot_test_shared, 0, sizeof(boot_test_shared));
	memset(&bl_diag, 0, sizeof(bl_diag));
}

// o ST-LINK: grava a aplicacao em 0x08010200 (o setor 4 e o 5 apagados antes)
int sim_debugger_flash(const uint8_t *app, uint32_t size) {

	if ((size == 0u) || (size > (BOOT_EXEC_SIZE - BOOT_HEADER_SIZE))) {
		return -1;
	}
	(void) BootFlash_EraseSector(BOOT_EXEC_ADDRESS);
	(void) BootFlash_EraseSector(BOOT_EXEC_ADDRESS + 0x10000u);
	memcpy(&boot_test_flash[BOOT_APP_ADDRESS - BOOT_FLASH_BASE], app, size);
	return 0;
}

// arranque do bootloader; requested = o pedido da aplicacao na RAM
void sim_start(int requested) {

	memset(&bl_diag, 0, sizeof(bl_diag));
	BlCore_Start(&core, requested ? (BL_RESET_SOFT | BL_RESET_PIN) : BL_RESET_POR, requested != 0, 500, 500);
	BlProtocol_Init(now_ms);
}

int sim_frame(int bus, const uint8_t *data, int dlc, uint8_t *reply) {

	return BlProtocol_Frame(&core, (uint8_t) bus, data, (uint8_t) dlc, reply);
}

int sim_reset_requested(void) {

	return bl_session.reset_requested ? 1 : 0;
}

// proxima trama de diagnostico de um bus (-1 = nada)
int sim_diag(int bus, uint32_t *id, uint8_t *data) {

	int kind = BlDiag_Next((uint8_t) bus, id, data);

	if (kind >= 0) {
		BlDiag_Sent((uint8_t) bus, kind);
	}
	return kind;
}

/* Um arranque sem uploader: devolve major << 16 | minor << 8 | run, ou -1
 * se ficar no bootloader. reset_flags como o RCC->CSR */
int sim_boot(uint32_t reset_flags) {

	memset(&bl_diag, 0, sizeof(bl_diag));
	BlCore_Start(&core, reset_flags, false, 500, 500);
	if (!BlCore_CanBoot(&core) || !BlCore_PrepareBoot(&core)) {
		return -1;
	}
	return (int) (((uint32_t) core.exec.version_major << 16) | ((uint32_t) core.exec.version_minor << 8) | core.run);
}
