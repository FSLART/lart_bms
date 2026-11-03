/*
 * bl_flash.c
 *
 * BootFlash_* do boot_layout.h com os registos do FLASH (RM0402 3.6):
 * palavras de 32 bits (PSIZE x32, 2,7..3,6 V), apagar por setor. O CPU fica
 * parado enquanto a flash trabalha (um so banco): o TIM2 continua a contar.
 * Nunca toca nos setores do proprio bootloader (S0, S1).
 */

#include "stm32f4xx.h"
#include "boot_layout.h"
#include "bl_protocol.h"

#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu
#define PSIZE_WORD      (2u << FLASH_CR_PSIZE_Pos)
#define FLASH_ERRORS    (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)
#define FIRST_WRITABLE  BOOT_JOURNAL_A_ADDRESS

static bool unlock(uint32_t offset) {

	if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
		FLASH->KEYR = FLASH_KEY1;
		FLASH->KEYR = FLASH_KEY2;
	}
	if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
		BlDiag_FlashError(BOOT_ERR_FLASH_UNLOCK, offset, FLASH->SR);
		return false;
	}
	while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
	}
	FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
	return true;
}

static void lock(void) {

	FLASH->CR = FLASH_CR_LOCK;
}

// como o FLASH_FlushCaches do HAL: depois de mexer na flash as caches podem ter o conteudo antigo
static void flush_caches(void) {

	uint32_t enabled = FLASH->ACR & (FLASH_ACR_ICEN | FLASH_ACR_DCEN);

	FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
	FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
	FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
	FLASH->ACR |= enabled;
}

bool BootFlash_EraseSector(uint32_t sector_address) {

	int32_t sector = BootLayout_Sector(sector_address);
	uint32_t offset = sector_address - BOOT_FLASH_BASE;

	if ((sector < 0) || (sector_address < FIRST_WRITABLE) || (BootLayout_SectorAddress(sector) != sector_address)) {
		BlDiag_FlashError(BOOT_ERR_FLASH_ERASE, offset, 0u);
		return false;
	}
	if (!unlock(offset)) {
		return false;
	}

	FLASH->CR = PSIZE_WORD | FLASH_CR_SER | ((uint32_t) sector << FLASH_CR_SNB_Pos);
	FLASH->CR |= FLASH_CR_STRT;
	while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
	}

	uint32_t errors = FLASH->SR & FLASH_ERRORS;

	lock();
	flush_caches();

	if (errors != 0u) {
		BlDiag_FlashError(BOOT_ERR_FLASH_ERASE, offset, errors);
		return false;
	}

	// confirma que ficou tudo a 0xFF
	uint32_t size = BootLayout_SectorSize(sector);

	for (uint32_t index = 0; index < size; index += 4u) {
		if (*(volatile const uint32_t*) (uintptr_t) (sector_address + index) != 0xFFFFFFFFu) {
			BlDiag_FlashError(BOOT_ERR_FLASH_ERASE, offset + index, 0u);
			return false;
		}
	}
	return true;
}

bool BootFlash_Program(uint32_t address, const void *data, uint32_t length) {

	const uint8_t *bytes = (const uint8_t*) data;
	uint32_t offset = address - BOOT_FLASH_BASE;

	if (((address & 3u) != 0u) || ((length & 3u) != 0u) || (address < FIRST_WRITABLE) || ((address + length) > (BOOT_FLASH_BASE + BOOT_FLASH_SIZE))) {
		BlDiag_FlashError(BOOT_ERR_FLASH_WRITE, offset, 0u);
		return false;
	}
	if (!unlock(offset)) {
		return false;
	}

	uint32_t errors = 0;

	FLASH->CR = PSIZE_WORD | FLASH_CR_PG;
	for (uint32_t index = 0; (index < length) && (errors == 0u); index += 4u) {

		uint32_t word;

		memcpy(&word, &bytes[index], sizeof(word));
		*(volatile uint32_t*) (uintptr_t) (address + index) = word;
		__DSB();
		while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
		}
		errors = FLASH->SR & FLASH_ERRORS;
	}
	lock();
	flush_caches();

	if ((errors != 0u) || (memcmp((const void*) (uintptr_t) address, data, length) != 0)) {
		BlDiag_FlashError(BOOT_ERR_FLASH_WRITE, offset, errors);
		return false;
	}
	return true;
}
