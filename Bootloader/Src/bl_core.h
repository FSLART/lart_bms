/*
 * bl_core.h
 *
 * Decisoes do bootloader, sem hardware (testadas no PC):
 *
 *   arranque  le o journal, verifica o EXEC e o BACKUP (SHA-256) e decide:
 *             arrancar o EXEC, repor o BACKUP primeiro, ou ficar (recovery)
 *   teste     uma imagem nova fica "em teste": cada arranque conta; um reset
 *             do watchdog conta como falha. BOOT_MAX_FAILURES falhas ou
 *             BOOT_MAX_ATTEMPTS arranques sem a aplicacao se confirmar ->
 *             volta ao BACKUP
 *   upload    BEGIN guarda o firmware bom no BACKUP (se ainda la nao estiver)
 *             e apaga o EXEC; END valida a imagem e deixa-a em teste
 *
 * O que vale como "firmware bom" para guardar: o confirmado pela aplicacao,
 * o reposto do backup, ou o gravado a mao (ST-LINK). Uma imagem em teste
 * nunca vai para o BACKUP: o backup continua a ser o ultimo bom.
 */

#ifndef BL_CORE_H_
#define BL_CORE_H_

#include "boot_layout.h"
#include "boot_image.h"
#include "boot_journal.h"

// RCC->CSR do F412 (bits 25..31)
#define BL_RESET_BOR        (1u << 25)
#define BL_RESET_PIN        (1u << 26)
#define BL_RESET_POR        (1u << 27)
#define BL_RESET_SOFT       (1u << 28)
#define BL_RESET_IWDG       (1u << 29)
#define BL_RESET_WWDG       (1u << 30)
#define BL_RESET_LPWR       (1u << 31)
#define BL_RESET_WATCHDOGS  (BL_RESET_IWDG | BL_RESET_WWDG | BL_RESET_LPWR)

// 0x584 EntryFlags (o BMS nao tem botao: o bit 1 e a janela de arranque)
#define BL_ENTRY_RAM        0x01u
#define BL_ENTRY_WINDOW     0x02u
#define BL_ENTRY_RECOVERY   0x04u
#define BL_ENTRY_ROLLBACK   0x08u

typedef enum {
	BL_ACTION_BOOT = 0,       // arrancar o EXEC como esta
	BL_ACTION_RESTORE = 1,    // repor o BACKUP no EXEC e arrancar
	BL_ACTION_STAY = 2        // nada arrancavel: fica a espera de um upload
} bl_action_t;

typedef struct {
	uint32_t       reset_flags;
	bool           requested;      // pedido da aplicacao na RAM
	uint16_t       can1_kbps;      // os bitrates em uso (ficam no journal)
	uint16_t       can2_kbps;
	boot_journal_t journal;
	boot_image_t   exec;
	boot_image_t   backup;
	bool           exec_ok;        // o EXEC arranca tal como esta
	bool           backup_ok;      // o BACKUP tem a imagem que o journal diz, intacta
	uint8_t        run;            // boot_run_t do EXEC
	uint8_t        action;         // bl_action_t
	uint8_t        reason;         // BOOT_ERR_*: porque repor ou ficar
	bool           crashed;        // o arranque anterior da imagem em teste acabou no watchdog
	uint8_t        entry_flags;    // BL_ENTRY_*
	uint8_t        event;          // boot_event_t deste arranque (vai para a aplicacao)
	bool           iwdg;           // armar o IWDG antes de arrancar
	uint32_t       rejected_id;    // a imagem posta de lado no rollback (para o 0x587)
	uint8_t        rejected_major; // e a versao dela, se se soube
	uint8_t        rejected_minor;
	uint16_t       rejected_revision;
	uint32_t       rejected_build;
	bool           rejected_known;
} bl_core_t;

// janela para o uploader apanhar o bootloader: depois de um watchdog ou de um reset pelo pino
bool BlCore_WindowReset(uint32_t reset_flags);

void BlCore_Start(bl_core_t *core, uint32_t reset_flags, bool requested, uint16_t can1_kbps, uint16_t can2_kbps);

// ha alguma coisa para arrancar (o EXEC, ou o BACKUP depois de reposto)
bool BlCore_CanBoot(const bl_core_t *core);

/* Prepara o salto: repoe o BACKUP se for preciso e regista o arranque de uma
 * imagem em teste. false = nao ha o que arrancar (fica no bootloader) */
bool BlCore_PrepareBoot(bl_core_t *core);

// BEGIN: guarda o firmware bom, regista o upload e apaga o EXEC. BOOT_ERR_* (0 = pronto)
uint8_t BlCore_BeginUpload(bl_core_t *core, uint32_t length);

// END: valida o EXEC e deixa a imagem em teste. BOOT_ERR_*
uint8_t BlCore_FinishUpload(bl_core_t *core, uint32_t length);

#endif /* BL_CORE_H_ */
