/*
 * boot_journal.h
 *
 * Estado do update na flash, nos setores S2 e S3 (16 KB cada). Cada registo
 * de 48 B e uma fotografia COMPLETA do estado (nao um delta): vale o registo
 * valido com o maior "sequence". Escreve-se sempre no fim (a flash so passa
 * bits de 1 para 0), e um registo cortado a meio por falta de energia falha o
 * CRC e e ignorado: fica o anterior.
 *
 * Quando o setor ativo enche, o proximo registo vai para o outro setor
 * depois de o apagar; ate esse registo estar escrito, o antigo continua la.
 * So o bootloader apaga (o apagar de 16 KB para o CPU ~0,3 s, mais do que o
 * WWDG da aplicacao aguenta). A aplicacao so acrescenta o registo de
 * confirmacao, e o bootloader garante espaco para ele antes de a arrancar.
 *
 * Antes do bootloader estes setores tinham o firmware antigo (ligado a
 * 0x08000000): lixo, que nao passa o magic+CRC e e tratado como cheio.
 */

#ifndef BOOT_JOURNAL_H_
#define BOOT_JOURNAL_H_

#include "boot_layout.h"

#define BOOT_JOURNAL_MAGIC   0x4A534D42u   // "BMSJ"
#define BOOT_RECORD_SIZE     48u
#define BOOT_RECORDS         (BOOT_JOURNAL_SIZE / BOOT_RECORD_SIZE)   // 341 por setor
#define BOOT_JOURNAL_RESERVE 4u            // registos livres garantidos antes de arrancar uma imagem em teste

typedef struct {
	uint32_t magic;           // BOOT_JOURNAL_MAGIC
	uint32_t sequence;        // +1 por registo, nos dois setores
	uint8_t  exec_state;      // boot_exec_state_t
	uint8_t  backup_state;    // boot_backup_state_t
	uint8_t  attempts;        // arranques da imagem em teste
	uint8_t  failures;        // desses, quantos acabaram num reset do watchdog
	uint32_t exec_length;     // imagem no EXEC: comprimento total e id (SHA-256)
	uint32_t exec_id;
	uint32_t backup_length;   // imagem no BACKUP
	uint32_t backup_id;
	uint16_t can1_kbps;       // bitrates da ultima sessao (o bootloader usa-os num arranque a frio)
	uint16_t can2_kbps;
	uint16_t updates;         // imagens instaladas por CAN
	uint16_t rollbacks;       // vezes que o BACKUP foi reposto
	uint8_t  event;           // boot_event_t: o que levou a este registo
	uint8_t  reason;          // BOOT_ERR_* do rollback (ou do erro)
	uint16_t spare16;
	uint32_t spare32;
	uint32_t check;           // CRC-32 dos 44 B anteriores
} boot_record_t;

_Static_assert(sizeof(boot_record_t) == BOOT_RECORD_SIZE, "registo do journal");

typedef struct {
	boot_record_t state;      // o estado atual (zeros se nao houver nenhum registo)
	bool     found;           // ha pelo menos um registo valido
	uint8_t  active;          // setor em uso: 0 = A (S2), 1 = B (S3)
	uint16_t next;            // proximo registo livre no setor ativo (BOOT_RECORDS = cheio)
	uint32_t writes;          // registos escritos desde o Load
} boot_journal_t;

void BootJournal_Load(boot_journal_t *journal);

/* Acrescenta o estado (o journal mete magic, sequence e CRC e confirma a
 * escrita). Com may_erase passa para o outro setor quando o ativo enche */
bool BootJournal_Write(boot_journal_t *journal, const boot_record_t *state, bool may_erase);

// registos que ainda cabem no setor ativo
uint16_t BootJournal_Free(const boot_journal_t *journal);

// passa ja para o outro setor (apagado) com o estado atual
bool BootJournal_Compact(boot_journal_t *journal);

uint32_t BootJournal_SectorAddress(uint8_t sector);

#endif /* BOOT_JOURNAL_H_ */
