/*
 * boot_layout.h
 *
 * Mapa da flash do STM32F412RE (512 KB, um so banco) e tudo o que o
 * bootloader (pasta Bootloader/) e a aplicacao tem de combinar. Mudar alguma
 * coisa aqui obriga a regravar os dois.
 *
 *   0x08000000  S0+S1   32 KB  bootloader
 *   0x08008000  S2      16 KB  journal A (estado do update, boot_journal.h)
 *   0x0800C000  S3      16 KB  journal B
 *   0x08010000  S4+S5  192 KB  EXEC: a imagem que corre. Cabecalho MCUboot de
 *                              512 B e a aplicacao ligada a 0x08010200
 *   0x08040000  S6+S7  256 KB  BACKUP: copia do ultimo firmware bom
 *
 * O F412 nao tem remap de bancos (o L476 da ACU tem): a aplicacao corre
 * sempre do EXEC e o "dual bank" e feito por copia. Antes de apagar o EXEC
 * para um update o bootloader guarda o firmware bom no BACKUP; se o novo nao
 * arrancar, nao se confirmar ou for pedido, copia o BACKUP de volta.
 *
 * RAM: os ultimos 32 B (0x2003FFE0) ficam fora dos dois linkers e passam
 * informacao entre a aplicacao e o bootloader (boot_shared.h).
 */

#ifndef BOOT_LAYOUT_H_
#define BOOT_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ================== FLASH ================== */

#define BOOT_FLASH_BASE        0x08000000u
#define BOOT_FLASH_SIZE        0x00080000u

#define BOOT_LOADER_ADDRESS    0x08000000u
#define BOOT_LOADER_SIZE       0x00008000u

#define BOOT_JOURNAL_A_ADDRESS 0x08008000u
#define BOOT_JOURNAL_B_ADDRESS 0x0800C000u
#define BOOT_JOURNAL_SIZE      0x00004000u

#define BOOT_EXEC_ADDRESS      0x08010000u
#define BOOT_EXEC_SIZE         0x00030000u
#define BOOT_BACKUP_ADDRESS    0x08040000u
#define BOOT_BACKUP_SIZE       0x00040000u

#define BOOT_HEADER_SIZE       0x200u                                   // cabecalho MCUboot
#define BOOT_APP_ADDRESS       (BOOT_EXEC_ADDRESS + BOOT_HEADER_SIZE)   // vetores da aplicacao (0x08010200)
#define BOOT_APP_INFO_OFFSET   0x200u                                   // descritor: 0x200 depois dos vetores
#define BOOT_IMAGE_MAX         BOOT_EXEC_SIZE                           // imagem completa: cabecalho + app + TLV

/* ================== RAM ================== */

#define BOOT_RAM_ADDRESS       0x20000000u
#define BOOT_RAM_SIZE          0x00040000u
#define BOOT_SHARED_ADDRESS    0x2003FFE0u                              // ultimos 32 B, fora dos linkers
#define BOOT_STACK_TOP         BOOT_SHARED_ADDRESS

/* ================== VERSOES ================== */

#define BOOT_VERSION_MAJOR     1u
#define BOOT_VERSION_MINOR     0u
#define BOOT_VERSION_PATCH     0u
#define BOOT_VERSION_PACKED    ((BOOT_VERSION_MAJOR << 16) | (BOOT_VERSION_MINOR << 8) | BOOT_VERSION_PATCH)
#define BOOT_PROTOCOL_VERSION  1u      // o protocolo CAN da ACU (mcu_boot docs/CAN_PROTOCOL.md)
#define BOOT_BOARD_TYPE        2u      // 0x582 BoardType: 1 = ACU (L476), 2 = BMS master (F412)

/* ================== DESCRITOR DA APLICACAO ================== */

// boot_image.h: o bootloader so aceita e arranca firmware feito para esta placa
#define BOOT_APP_INFO_MAGIC      0x4F464E49u   // "INFO"
#define BOOT_BOARD_ID            0x31534D42u   // "BMS1"
#define BOOT_APP_FEATURE_IWDG    0x01u         // refresca o IWDG: o bootloader arma-o nas imagens em teste
#define BOOT_APP_FEATURE_CONFIRM 0x02u         // confirma-se no journal depois de correr bem

/* ================== POLITICA DO UPDATE ================== */

#define BOOT_MAX_FAILURES      3u       // resets do watchdog de uma imagem em teste antes de voltar ao backup
#define BOOT_MAX_ATTEMPTS      10u      // arranques sem confirmar (cortes de energia incluidos)
#define BOOT_IWDG_TIMEOUT_MS   4000u    // IWDG armado pelo bootloader numa imagem em teste

/* ================== ESTADOS ================== */

// estado do EXEC guardado no journal
typedef enum {
	BOOT_EXEC_NONE = 0,       // nada registado: vale o que estiver na flash
	BOOT_EXEC_UPLOADING = 1,  // apagado para um upload, incompleto ate ao END
	BOOT_EXEC_PENDING = 2,    // imagem nova validada, em teste ate a aplicacao a confirmar
	BOOT_EXEC_CONFIRMED = 3,  // confirmada pela aplicacao ou reposta do backup
	BOOT_EXEC_RESTORING = 4,  // a copiar o BACKUP para o EXEC
	BOOT_EXEC_REJECTED = 5    // a aplicacao pediu para voltar ao backup
} boot_exec_state_t;

typedef enum {
	BOOT_BACKUP_NONE = 0,
	BOOT_BACKUP_WRITING = 1,  // a meio da copia: nao serve
	BOOT_BACKUP_VALID = 2
} boot_backup_state_t;

// estado efetivo da imagem que o bootloader arrancou (passa para a aplicacao)
typedef enum {
	BOOT_RUN_UNKNOWN = 0,     // a aplicacao nao veio do bootloader (debugger) ou o bootloader e antigo
	BOOT_RUN_CONFIRMED = 1,   // instalada por CAN e confirmada (ou reposta do backup)
	BOOT_RUN_PENDING = 2,     // em teste: a aplicacao confirma ao fim de boot_confirm_s
	BOOT_RUN_EXTERNAL = 3     // gravada pelo ST-LINK/CubeProgrammer: o bootloader nao mexe nela
} boot_run_t;

// o ultimo acontecimento (journal e area partilhada)
typedef enum {
	BOOT_EVENT_NONE = 0,
	BOOT_EVENT_BACKUP = 1,      // firmware bom copiado para o BACKUP
	BOOT_EVENT_UPLOAD = 2,      // upload comecado (EXEC apagado)
	BOOT_EVENT_INSTALLED = 3,   // imagem nova validada, em teste
	BOOT_EVENT_TRIAL = 4,       // arranque de uma imagem em teste
	BOOT_EVENT_CONFIRMED = 5,   // a aplicacao confirmou-se
	BOOT_EVENT_ROLLBACK = 6,    // BACKUP reposto no EXEC (reason diz porque)
	BOOT_EVENT_REJECTED = 7,    // a aplicacao pediu para voltar ao BACKUP
	BOOT_EVENT_ACCEPTED = 8     // imagem em teste aceite por nao haver backup para onde voltar
} boot_event_t;

/* Codigos de erro: 0..21 sao os da ACU (0x586 BCI_BOOT_ERROR, 0x587 Reason),
 * 22.. sao do BMS. Tambem sao o "reason" do rollback */
enum {
	BOOT_ERR_NONE = 0,
	BOOT_ERR_HEADER = 1,
	BOOT_ERR_TLV = 2,
	BOOT_ERR_HASH = 3,
	BOOT_ERR_VECTORS = 4,
	BOOT_ERR_CRC = 5,
	BOOT_ERR_FRAME = 6,
	BOOT_ERR_STATE = 7,
	BOOT_ERR_SIZE = 8,
	BOOT_ERR_OFFSET = 9,
	BOOT_ERR_FLASH_READ = 10,
	BOOT_ERR_FLASH_WRITE = 11,
	BOOT_ERR_FLASH_ERASE = 12,
	BOOT_ERR_FLASH_UNLOCK = 13,
	BOOT_ERR_FLASH_LOCK = 14,
	BOOT_ERR_CAN_QUEUE = 15,
	BOOT_ERR_CAN_TIMEOUT = 16,
	BOOT_ERR_HANDOFF = 17,
	BOOT_ERR_FLASH_CONFIG = 18,
	BOOT_ERR_INACTIVITY = 19,
	BOOT_ERR_RECORD = 20,
	BOOT_ERR_FLASH_OPERATION = 21,
	BOOT_ERR_BOARD = 22,         // descritor da aplicacao em falta ou de outra placa
	BOOT_ERR_CRASHED = 23,       // a imagem em teste reiniciou pelo watchdog BOOT_MAX_FAILURES vezes
	BOOT_ERR_UNCONFIRMED = 24,   // BOOT_MAX_ATTEMPTS arranques sem se confirmar
	BOOT_ERR_MANUAL = 25,        // pedido pela aplicacao ("boot rollback" na consola)
	BOOT_ERR_INCOMPLETE = 26,    // upload ou reposicao interrompidos
	BOOT_ERR_NO_IMAGE = 27       // nada arrancavel e sem backup: fica no bootloader
};

/* ================== SETORES ================== */

static inline int32_t BootLayout_Sector(uint32_t address) {

	if ((address < BOOT_FLASH_BASE) || (address >= (BOOT_FLASH_BASE + BOOT_FLASH_SIZE))) {
		return -1;
	}

	uint32_t offset = address - BOOT_FLASH_BASE;

	if (offset < 0x10000u) {
		return (int32_t) (offset / 0x4000u);               // S0..S3, 16 KB
	}
	if (offset < 0x20000u) {
		return 4;                                          // S4, 64 KB
	}
	return (int32_t) (5u + ((offset - 0x20000u) / 0x20000u));   // S5..S7, 128 KB
}

static inline uint32_t BootLayout_SectorAddress(int32_t sector) {

	if (sector <= 4) {
		return BOOT_FLASH_BASE + ((uint32_t) sector * 0x4000u);
	}
	return BOOT_FLASH_BASE + 0x20000u + ((uint32_t) (sector - 5) * 0x20000u);
}

static inline uint32_t BootLayout_SectorSize(int32_t sector) {

	return (sector < 4) ? 0x4000u : ((sector == 4) ? 0x10000u : 0x20000u);
}

/* ================== LEITURA DA FLASH ================== */

// nos testes no PC a flash e um array (boot_test_flash) com o mesmo mapa
#ifdef BOOT_HOST_TEST
extern uint8_t boot_test_flash[BOOT_FLASH_SIZE];
#define BOOT_FLASH_PTR(address) ((const uint8_t *) &boot_test_flash[(uint32_t) (address) - BOOT_FLASH_BASE])
#else
#define BOOT_FLASH_PTR(address) ((const uint8_t *) (uintptr_t) (address))
#endif

static inline uint32_t BootFlash_ReadWord(uint32_t address) {

	uint32_t value;

	memcpy(&value, BOOT_FLASH_PTR(address), sizeof(value));
	return value;
}

/* Escrita na flash: cada lado tem a sua (Bootloader/Src/bl_flash.c com
 * registos, a aplicacao no bsp.c com o HAL e so programa, os testes uma flash
 * simulada). Palavras de 32 bits alinhadas; o apagar e de um setor inteiro */
bool BootFlash_EraseSector(uint32_t sector_address);
bool BootFlash_Program(uint32_t address, const void *data, uint32_t length);

#endif /* BOOT_LAYOUT_H_ */
