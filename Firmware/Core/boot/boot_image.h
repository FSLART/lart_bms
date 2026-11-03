/*
 * boot_image.h
 *
 * As imagens que o bootloader aceita, no formato da ACU (mcu_boot):
 *
 *   +0x000  cabecalho MCUboot, 512 B (magic 0x96F3B83D, hdr_size 512, sem
 *           TLVs protegidos nem flags, versao major.minor.revision+build)
 *   +0x200  aplicacao ligada a 0x08010200 (vetores: SP na RAM, PC na imagem)
 *   +0x400  descritor da aplicacao (boot_app_info_t): placa e funcionalidades
 *   ...     TLV info (0x6907, total) + TLV 0x10 com o SHA-256 de cabecalho+app
 *
 * Tambem se aceita uma imagem "RAW" no EXEC: cabecalho apagado (0xFF) e a
 * aplicacao gravada diretamente pelo ST-LINK (CubeIDE). Nao tem hash; vale
 * pelos vetores e pelo descritor, e o bootloader nunca mexe nela sozinho.
 */

#ifndef BOOT_IMAGE_H_
#define BOOT_IMAGE_H_

#include "boot_layout.h"

#define BOOT_IMAGE_MAGIC     0x96F3B83Du
#define BOOT_TLV_INFO_MAGIC  0x6907u
#define BOOT_TLV_SHA256      0x0010u
#define BOOT_TLV_TOTAL       40u      // info (4) + cabecalho do TLV (4) + SHA-256 (32)
#define BOOT_IMAGE_MIN       (BOOT_HEADER_SIZE + 8u + BOOT_TLV_TOTAL)   // 560, como a ACU

typedef struct {
	uint32_t magic;
	uint32_t load_address;
	uint16_t header_size;
	uint16_t protect_tlv_size;
	uint32_t image_size;           // so a aplicacao
	uint32_t flags;
	uint8_t  version_major;
	uint8_t  version_minor;
	uint16_t version_revision;
	uint32_t version_build;
	uint32_t pad;
} boot_image_header_t;             // 32 B, o struct image_header do MCUboot

// fica na aplicacao em 0x08010400 (secao .app_info do linker, ver boot_app_info.c)
typedef struct {
	uint32_t magic;                // BOOT_APP_INFO_MAGIC
	uint32_t board;                // BOOT_BOARD_ID
	uint8_t  version_major;
	uint8_t  version_minor;
	uint16_t version_revision;
	uint32_t version_build;
	uint32_t features;             // BOOT_APP_FEATURE_*
	uint32_t reserved[3];
} boot_app_info_t;                 // 32 B

typedef enum {
	BOOT_IMAGE_NONE = 0,           // nada que arranque
	BOOT_IMAGE_PACKAGED = 1,       // cabecalho MCUboot + TLV, SHA-256 verificado
	BOOT_IMAGE_RAW = 2             // cabecalho apagado, gravada pelo debugger
} boot_image_kind_t;

typedef struct {
	uint8_t  kind;                 // boot_image_kind_t
	uint8_t  error;                // BOOT_ERR_* (0 = valida)
	uint32_t detail;               // para o 0x586: offset na flash ou o PC rejeitado
	uint32_t length;               // bytes ocupados na slot (PACKAGED: cabecalho + app + TLV; RAW: ate a ultima palavra gravada)
	uint32_t image_size;           // so a aplicacao
	uint32_t id;                   // 4 primeiros bytes do SHA-256 (PACKAGED), 0 nas RAW
	uint32_t sp;
	uint32_t pc;
	uint32_t features;             // do descritor
	uint8_t  version_major;
	uint8_t  version_minor;
	uint16_t version_revision;
	uint32_t version_build;
} boot_image_t;

_Static_assert(sizeof(boot_image_header_t) == 32u, "cabecalho MCUboot");
_Static_assert(sizeof(boot_app_info_t) == 32u, "descritor da aplicacao");

// a imagem corre sempre do EXEC: SP no topo da RAM da aplicacao, PC dentro da aplicacao
bool BootImage_VectorsValid(uint32_t sp, uint32_t pc, uint32_t image_size);

/* Verifica a imagem de uma slot (EXEC ou BACKUP). expected_length != 0 obriga
 * a esse comprimento total. PACKAGED: cabecalho, TLV, vetores, descritor e
 * SHA-256. RAW (so com allow_raw): cabecalho apagado, vetores e descritor.
 * Devolve BOOT_ERR_NONE ou o erro (tambem em image->error) */
uint8_t BootImage_Check(uint32_t slot, uint32_t slot_size, uint32_t expected_length, bool allow_raw, boot_image_t *image);

// cabecalho apagado (512 B a 0xFF)
bool BootImage_HeaderErased(uint32_t slot);

// cabecalho para guardar no BACKUP uma imagem RAW (versao do descritor)
void BootImage_MakeHeader(uint8_t header[BOOT_HEADER_SIZE], uint32_t image_size, const boot_image_t *source);

// TLV com o SHA-256 de cabecalho+app (40 B)
void BootImage_MakeTlv(uint8_t tlv[BOOT_TLV_TOTAL], const uint8_t digest[32]);

// versao do cabecalho de uma slot sem a validar (major << 24 | minor << 16 | revision), 0 se nao houver
uint32_t BootImage_PeekVersion(uint32_t slot);

#endif /* BOOT_IMAGE_H_ */
