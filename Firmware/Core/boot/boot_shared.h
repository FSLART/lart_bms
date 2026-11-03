/*
 * boot_shared.h
 *
 * Os ultimos 32 B da RAM (0x2003FFE0), fora dos linkers da aplicacao e do
 * bootloader, sobrevivem a um reset (nao a um corte de energia). Servem para
 * os dois falarem:
 *
 *   aplicacao -> bootloader  "fica no bootloader" (request = 0xB00710AD em
 *                            0x2003FFFC, a mesma palavra e o mesmo sitio da
 *                            ACU) e os bitrates CAN da config
 *   bootloader -> aplicacao  porque arrancou (RCC->CSR, que o bootloader
 *                            limpa), o estado da imagem (em teste ou
 *                            confirmada), o que fez neste arranque e a versao
 */

#ifndef BOOT_SHARED_H_
#define BOOT_SHARED_H_

#include "boot_layout.h"

#define BOOT_SHARED_MAGIC   0x4853424Cu   // "LBSH"
#define BOOT_REQUEST_MAGIC  0xB00710ADu   // ficar no bootloader (o bootloader consome-a)

#define BOOT_SHARED_FLAG_IWDG    0x01u    // o bootloader armou o IWDG: a aplicacao tem de o refrescar
#define BOOT_SHARED_FLAG_BACKUP  0x02u    // ha um backup valido para onde voltar
#define BOOT_SHARED_FLAG_WINDOW  0x04u    // o bootloader esperou pelo uploader antes de arrancar

typedef struct {
	uint32_t magic;           // BOOT_SHARED_MAGIC: os campos ate check valem
	uint16_t can1_kbps;       // bitrates (aplicacao -> bootloader; o bootloader devolve os que usou)
	uint16_t can2_kbps;
	uint32_t reset_flags;     // RCC->CSR do ultimo reset, antes de o bootloader o limpar
	uint8_t  run;             // boot_run_t da imagem arrancada
	uint8_t  event;           // boot_event_t: o que o bootloader fez neste arranque
	uint8_t  attempts;        // arranques da imagem em teste, com este
	uint8_t  flags;           // BOOT_SHARED_FLAG_*
	uint32_t boot_version;    // BOOT_VERSION_PACKED do bootloader
	uint32_t exec_id;         // id da imagem arrancada (0 nas RAW)
	uint32_t check;           // CRC-32 dos 24 B anteriores
	uint32_t request;         // 0x2003FFFC: BOOT_REQUEST_MAGIC
} boot_shared_t;

_Static_assert(sizeof(boot_shared_t) == 32u, "area partilhada: os ultimos 32 B da RAM");

#ifdef BOOT_HOST_TEST
extern boot_shared_t boot_test_shared;
#define BOOT_SHARED (&boot_test_shared)
#else
#define BOOT_SHARED ((volatile boot_shared_t *) BOOT_SHARED_ADDRESS)
#endif

bool BootShared_Valid(const boot_shared_t *shared);
void BootShared_Seal(boot_shared_t *shared);   // mete o magic e o check

// copia da area partilhada (a RAM e volatile: le-se uma vez)
void BootShared_Read(boot_shared_t *out);
void BootShared_Write(const boot_shared_t *in);

#endif /* BOOT_SHARED_H_ */
