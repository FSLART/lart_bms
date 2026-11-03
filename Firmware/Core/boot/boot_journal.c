/*
 * boot_journal.c
 *
 * Ver boot_journal.h.
 */

#include "boot_journal.h"
#include "boot_hash.h"

#define CHECKED_BYTES (BOOT_RECORD_SIZE - 4u)

uint32_t BootJournal_SectorAddress(uint8_t sector) {

	return (sector == 0u) ? BOOT_JOURNAL_A_ADDRESS : BOOT_JOURNAL_B_ADDRESS;
}

static uint32_t record_address(uint8_t sector, uint16_t slot) {

	return BootJournal_SectorAddress(sector) + ((uint32_t) slot * BOOT_RECORD_SIZE);
}

static bool slot_erased(uint32_t address) {

	for (uint32_t offset = 0; offset < BOOT_RECORD_SIZE; offset += 4u) {
		if (BootFlash_ReadWord(address + offset) != 0xFFFFFFFFu) {
			return false;
		}
	}
	return true;
}

static bool record_valid(const boot_record_t *record) {

	return (record->magic == BOOT_JOURNAL_MAGIC) && (record->check == BootCrc32_Update(0, record, CHECKED_BYTES));
}

void BootJournal_Load(boot_journal_t *journal) {

	uint16_t next[2] = { 0, 0 };

	memset(journal, 0, sizeof(*journal));

	for (uint8_t sector = 0; sector < 2u; sector++) {
		for (uint16_t slot = 0; slot < BOOT_RECORDS; slot++) {

			uint32_t address = record_address(sector, slot);

			if (slot_erased(address)) {
				continue;
			}
			// usado (valido ou lixo): o proximo livre e depois deste
			next[sector] = (uint16_t) (slot + 1u);

			boot_record_t record;

			memcpy(&record, BOOT_FLASH_PTR(address), sizeof(record));
			if (record_valid(&record) && (!journal->found || (record.sequence > journal->state.sequence))) {
				journal->state = record;
				journal->found = true;
				journal->active = sector;
			}
		}
	}

	if (!journal->found) {
		// sem registos: o setor com mais espaco (os dois podem ter lixo do firmware antigo)
		journal->active = (next[1] < next[0]) ? 1u : 0u;
	}
	journal->next = next[journal->active];
}

uint16_t BootJournal_Free(const boot_journal_t *journal) {

	return (journal->next < BOOT_RECORDS) ? (uint16_t) (BOOT_RECORDS - journal->next) : 0u;
}

// escreve e confirma; um registo que falhe fica para tras como lixo
static bool program_record(uint8_t sector, uint16_t slot, const boot_record_t *record) {

	uint32_t address = record_address(sector, slot);

	if (!BootFlash_Program(address, record, BOOT_RECORD_SIZE)) {
		return false;
	}
	return memcmp(BOOT_FLASH_PTR(address), record, BOOT_RECORD_SIZE) == 0;
}

static bool switch_sector(boot_journal_t *journal, boot_record_t *record) {

	uint8_t other = (uint8_t) (journal->active ^ 1u);

	if (!BootFlash_EraseSector(BootJournal_SectorAddress(other))) {
		return false;
	}
	journal->active = other;
	journal->next = 0;

	// apagado agora: se nem o primeiro registo pegar, a flash esta mal
	bool written = program_record(other, 0, record);

	journal->next = 1;
	return written;
}

bool BootJournal_Write(boot_journal_t *journal, const boot_record_t *state, bool may_erase) {

	boot_record_t record = *state;

	record.magic = BOOT_JOURNAL_MAGIC;
	record.sequence = journal->found ? (journal->state.sequence + 1u) : 1u;
	record.check = BootCrc32_Update(0, &record, CHECKED_BYTES);

	bool written = false;

	// ate duas tentativas no setor ativo (uma palavra que nao pegou estraga so aquele registo)
	for (uint8_t attempt = 0; (attempt < 2u) && !written && (journal->next < BOOT_RECORDS); attempt++) {
		uint16_t slot = journal->next;

		journal->next = (uint16_t) (slot + 1u);
		written = program_record(journal->active, slot, &record);
	}

	if (!written) {
		if (!may_erase) {
			return false;
		}
		written = switch_sector(journal, &record);
	}

	if (written) {
		journal->state = record;
		journal->found = true;
		journal->writes++;
	}
	return written;
}

bool BootJournal_Compact(boot_journal_t *journal) {

	boot_record_t record = journal->state;

	record.magic = BOOT_JOURNAL_MAGIC;
	record.sequence = journal->found ? (journal->state.sequence + 1u) : 1u;
	record.check = BootCrc32_Update(0, &record, CHECKED_BYTES);

	if (!switch_sector(journal, &record)) {
		return false;
	}
	journal->state = record;
	journal->found = true;
	journal->writes++;
	return true;
}
