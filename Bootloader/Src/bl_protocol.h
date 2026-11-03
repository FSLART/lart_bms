/*
 * bl_protocol.h
 *
 * Protocolo CAN de upload da ACU (mcu_boot docs/CAN_PROTOCOL.md, versao 1),
 * igual trama a trama para servir o mesmo uploader:
 *
 *   0x602  pedido  INFO 0 | BEGIN 1 (comprimento u32) | DATA 2 (offset u24 + 4 B)
 *                  | END 3 (CRC-32 u32) | RESET 4. DLC 8, little-endian
 *   0x580  resposta comando|0x80, status, valor u32, protocolo 1, estado
 *                  (0 parado, 1 a receber, 2 validado e instalado)
 *   0x581  heartbeat 0x55, tick u32, versao do bootloader (a cada 500 ms)
 *   0x582..0x589  diagnostico (BCI_BOOT_INFO .. BCI_BOOT_REJECTED_VERSION)
 *
 * Diferencas no BMS: escuta no CAN1 e no CAN2 e responde no bus de onde veio
 * o pedido (uma sessao fica presa ao bus do BEGIN); os "bancos" do
 * diagnostico sao 1 = EXEC e 2 = BACKUP; ha erros 22..27 (boot_layout.h).
 */

#ifndef BL_PROTOCOL_H_
#define BL_PROTOCOL_H_

#include "bl_core.h"

#define BL_CAN_REQUEST_ID    0x602u
#define BL_CAN_RESPONSE_ID   0x580u
#define BL_CAN_HEARTBEAT_ID  0x581u
#define BL_CAN_DIAG_BASE_ID  0x582u   // 0x582..0x589

#define BL_INACTIVITY_MS     30000u   // sessao sem comandos -> reset (como a ACU)
#define BL_HEARTBEAT_MS      500u

enum { BL_CMD_INFO = 0, BL_CMD_BEGIN = 1, BL_CMD_DATA = 2, BL_CMD_END = 3, BL_CMD_RESET = 4 };
enum { BL_OK = 0, BL_BAD_FRAME = 1, BL_BAD_STATE = 2, BL_BAD_SIZE = 3, BL_BAD_OFFSET = 4, BL_FLASH_ERROR = 5, BL_BAD_IMAGE = 6 };
enum { BL_STAGE_IDLE = 0, BL_STAGE_ERASING = 1, BL_STAGE_RECEIVING = 2, BL_STAGE_VALIDATING = 3, BL_STAGE_COMMITTED = 4, BL_STAGE_FAILED = 5 };
enum { BL_SOURCE_VALIDATION = 1, BL_SOURCE_UPLOAD = 2, BL_SOURCE_FLASH = 3, BL_SOURCE_CAN = 4, BL_SOURCE_BOOT = 5 };
enum { BL_DIAG_INFO = 0, BL_DIAG_VERSION, BL_DIAG_DECISION, BL_DIAG_UPLOAD, BL_DIAG_ERROR, BL_DIAG_ROLLBACK, BL_DIAG_FLASH, BL_DIAG_REJECTED_VERSION, BL_DIAG_COUNT };

#define BL_BUS_COUNT 2u

// o que os diagnosticos mostram (os campos da ACU)
typedef struct {
	uint16_t pending[BL_BUS_COUNT];   // por bus: bit = BL_DIAG_* por enviar
	uint8_t  valid_banks;             // bit0 EXEC, bit1 BACKUP
	uint8_t  upload_stage;
	uint8_t  selected_bank;           // 1 = EXEC, 2 = reposto do BACKUP, 0 = nenhum
	uint8_t  entry_flags;
	uint8_t  rejected_bank;
	uint8_t  reject_reason;
	uint8_t  rejected_version_known;
	uint8_t  error_code;
	uint8_t  error_bank;
	uint8_t  error_command;
	uint8_t  error_source;
	uint8_t  current_command;
	uint8_t  version_major;           // da imagem que arranca
	uint8_t  version_minor;
	uint16_t version_revision;
	uint32_t version_build;
	uint8_t  rejected_major;
	uint8_t  rejected_minor;
	uint16_t rejected_revision;
	uint32_t rejected_build;
	uint32_t reset_flags;
	uint32_t rejected_generation;
	uint32_t received;
	uint32_t total;
	uint32_t error_detail;
	uint32_t flash_offset;
	uint32_t flash_error;
	uint32_t error_sequence;
} bl_diag_t;

typedef struct {
	bool     active;          // a receber
	bool     complete;        // END validado: espera o RESET
	uint8_t  bus;             // 0 = CAN1, 1 = CAN2
	uint32_t length;
	uint32_t received;
	uint32_t crc;             // zlib.crc32 do que ja chegou
	uint8_t  last[8];         // o ultimo DATA aceite (repeticao exata = ACK sem regravar)
	bool     reset_requested;
	uint32_t last_ok_ms;      // ultimo comando aceite
} bl_session_t;

extern bl_diag_t bl_diag;
extern bl_session_t bl_session;

void BlProtocol_Init(uint32_t now_ms);

/* Uma trama 0x602 do bus "bus". Escreve a resposta (8 B) e devolve o status.
 * Pode demorar segundos (BEGIN apaga e copia flash) */
uint8_t BlProtocol_Frame(bl_core_t *core, uint8_t bus, const uint8_t *data, uint8_t dlc, uint8_t reply[8]);

bool BlProtocol_Busy(void);   // sessao a meio ou instalada a espera do RESET

// diagnosticos: o estado do arranque (todos os campos para os dois bus)
void BlDiag_FromCore(const bl_core_t *core);
void BlDiag_ValidBanks(const bl_core_t *core);   // so os bancos validos (durante um upload)
void BlDiag_Snapshot(void);
void BlDiag_Error(uint8_t code, uint8_t bank, uint8_t source, uint32_t detail);
void BlDiag_FlashError(uint8_t code, uint32_t offset, uint32_t hardware_error);

// a proxima trama de diagnostico para "bus" (-1 = nada); BlDiag_Sent depois de a meter na fila
int BlDiag_Next(uint8_t bus, uint32_t *id, uint8_t data[8]);
void BlDiag_Sent(uint8_t bus, int kind);

void BlProtocol_Heartbeat(uint32_t now_ms, uint8_t data[8]);

#endif /* BL_PROTOCOL_H_ */
