/*
 * test_boot.c - o bootloader CAN no PC.
 *
 * Codigo real: Core/boot (journal, imagens, SHA-256, CRC-32, area
 * partilhada) e a logica do bootloader (Bootloader/Src/bl_core.c,
 * bl_protocol.c, bl_log.c). A flash e um array com as regras de uma NOR
 * (apagar a 0xFF por setor, programar so passa bits a 0) e pode "perder a
 * energia" a meio de qualquer escrita: os testes de corte confirmam que,
 * corte onde cortar, o arranque seguinte tem sempre um firmware que arranca.
 * Correr com Tests/run_tests.sh.
 */

#include "bl_core.h"
#include "bl_protocol.h"
#include "bl_port.h"
#include "bl_log.h"
#include "boot_hash.h"
#include "boot_image.h"
#include "boot_journal.h"
#include "boot_shared.h"
#include "boot_names.h"
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ================== FLASH E PORT FALSOS ================== */

uint8_t boot_test_flash[BOOT_FLASH_SIZE];
boot_shared_t boot_test_shared;

static uint32_t now_ms;
static long flash_ops;          // palavras programadas + setores apagados
static long cut_at = -1;        // corta a energia nesta operacao
static long fail_at = -1;       // a partir desta operacao a flash da erro (sem mudar nada)...
static long fail_count = 1;     // ...nestas seguidas
static jmp_buf power_cut;
static int overwrites;          // programar por cima de bits ja a 0 com outro valor (erro de logica)
static int verbose;
static char log_text[8192];      // o que o bootloader escreveu no UART
static uint32_t log_length;

uint32_t BlPort_Millis(void) {

	return now_ms;
}

void BlPort_Write(const char *text, uint32_t length) {

	if (verbose) {
		fwrite(text, 1, length, stdout);
	}
	if ((log_length + length) < sizeof(log_text)) {
		memcpy(&log_text[log_length], text, length);
		log_length += length;
		log_text[log_length] = 0;
	}
}

static bool flash_fails(void) {

	return (fail_at >= 0) && (flash_ops >= fail_at) && (flash_ops < (fail_at + fail_count));
}

void BlPort_DiagFlush(void) {
}

static uint8_t *flash_at(uint32_t address) {

	return &boot_test_flash[address - BOOT_FLASH_BASE];
}

static void maybe_cut(void) {

	if ((cut_at >= 0) && (flash_ops == cut_at)) {
		cut_at = -1;
		longjmp(power_cut, 1);
	}
	flash_ops++;
}

bool BootFlash_EraseSector(uint32_t address) {

	int32_t sector = BootLayout_Sector(address);

	if ((sector < 2) || (BootLayout_SectorAddress(sector) != address)) {
		return false;
	}

	uint32_t size = BootLayout_SectorSize(sector);
	bool fail = flash_fails();

	if ((cut_at >= 0) && (flash_ops == cut_at)) {
		// apagar a meio: metade do setor apagado, o resto como estava
		memset(flash_at(address), 0xFF, size / 2u);
	}
	maybe_cut();
	if (fail) {
		return false;
	}
	memset(flash_at(address), 0xFF, size);
	return true;
}

bool BootFlash_Program(uint32_t address, const void *data, uint32_t length) {

	const uint8_t *bytes = (const uint8_t*) data;

	if (((address & 3u) != 0u) || ((length & 3u) != 0u) || (address < BOOT_JOURNAL_A_ADDRESS)) {
		return false;
	}
	for (uint32_t index = 0; index < length; index += 4u) {

		uint8_t *cell = flash_at(address + index);
		bool fail = flash_fails();

		if ((cut_at >= 0) && (flash_ops == cut_at)) {
			cell[0] &= bytes[index];   // palavra a meio: so o primeiro byte pegou
		}
		maybe_cut();
		if (fail) {
			return false;
		}
		for (uint32_t byte = 0; byte < 4u; byte++) {
			if ((cell[byte] & bytes[index + byte]) != bytes[index + byte]) {
				overwrites++;
			}
			cell[byte] &= bytes[index + byte];
		}
	}
	return memcmp(flash_at(address), data, length) == 0;
}

/* ================== IMAGENS ================== */

#define APP_INFO_AT  BOOT_APP_INFO_OFFSET
#define FEATURES     (BOOT_APP_FEATURE_IWDG | BOOT_APP_FEATURE_CONFIRM)

static void put32(uint8_t *out, uint32_t value) {

	memcpy(out, &value, 4);
}

// aplicacao ligada a 0x08010200: vetores, descritor e "codigo" pseudo-aleatorio
static uint32_t make_app(uint8_t *out, uint32_t size, uint8_t major, uint8_t minor, uint32_t seed) {

	boot_app_info_t info = { BOOT_APP_INFO_MAGIC, BOOT_BOARD_ID, major, minor, 0, 0, FEATURES, { 0, 0, 0 } };
	uint32_t state = seed * 2654435761u + 1u;

	memset(out, 0, size);
	put32(&out[0], BOOT_STACK_TOP);
	put32(&out[4], (BOOT_APP_ADDRESS + 0x501u));
	for (uint32_t offset = 8; offset < 0x1C4u; offset += 4u) {
		put32(&out[offset], BOOT_APP_ADDRESS + 0x601u);
	}
	memcpy(&out[APP_INFO_AT], &info, sizeof(info));
	for (uint32_t offset = APP_INFO_AT + sizeof(info); offset < size; offset++) {
		state = (state * 1103515245u) + 12345u;
		out[offset] = (uint8_t) (state >> 16);
	}
	put32(&out[size - 4u], 0x12345678u ^ seed);   // a ultima palavra nunca e 0xFFFFFFFF
	return size;
}

// cabecalho MCUboot + aplicacao + TLV com o SHA-256 (o que o can_upload.py faz)
static uint32_t package(uint8_t *out, const uint8_t *app, uint32_t size, uint8_t major, uint8_t minor, uint16_t revision, uint32_t build) {

	boot_image_header_t header = { BOOT_IMAGE_MAGIC, 0, BOOT_HEADER_SIZE, 0, size, 0, major, minor, revision, build, 0 };
	boot_sha256_t sha;
	uint8_t digest[32];

	memset(out, 0, BOOT_HEADER_SIZE);
	memcpy(out, &header, sizeof(header));
	memcpy(&out[BOOT_HEADER_SIZE], app, size);
	BootSha256_Init(&sha);
	BootSha256_Update(&sha, out, BOOT_HEADER_SIZE + size);
	BootSha256_Final(&sha, digest);
	BootImage_MakeTlv(&out[BOOT_HEADER_SIZE + size], digest);
	return BOOT_HEADER_SIZE + size + BOOT_TLV_TOTAL;
}

// o CubeIDE/ST-LINK: apaga os setores que vai usar e grava a aplicacao em 0x08010200
static void debugger_flash(const uint8_t *app, uint32_t size) {

	int32_t first = BootLayout_Sector(BOOT_APP_ADDRESS);
	int32_t last = BootLayout_Sector(BOOT_APP_ADDRESS + size - 1u);

	for (int32_t sector = first; sector <= last; sector++) {
		memset(flash_at(BootLayout_SectorAddress(sector)), 0xFF, BootLayout_SectorSize(sector));
	}
	memcpy(flash_at(BOOT_APP_ADDRESS), app, size);
}

static void blank_device(void) {

	memset(boot_test_flash, 0xFF, sizeof(boot_test_flash));
	memset(&boot_test_shared, 0, sizeof(boot_test_shared));
	memset(&bl_diag, 0, sizeof(bl_diag));
	now_ms = 0;
	flash_ops = 0;
	cut_at = -1;
	fail_at = -1;
	fail_count = 1;
	overwrites = 0;
	log_length = 0;
	log_text[0] = 0;
}

/* ================== BOOTLOADER ================== */

static bl_core_t core;

typedef struct {
	bool    booted;      // saltou para a aplicacao
	uint8_t run;
	uint8_t event;
	uint8_t major;       // versao do que arrancou
	uint8_t minor;
	uint8_t reason;
	bool    iwdg;
} boot_result_t;

// um arranque do bootloader sem uploader: decide e "salta"
static boot_result_t power_on(uint32_t reset_flags) {

	boot_result_t result = { 0 };

	memset(&bl_diag, 0, sizeof(bl_diag));
	BlCore_Start(&core, reset_flags, false, 500, 500);
	BlProtocol_Init(now_ms);
	result.reason = core.reason;
	if (BlCore_CanBoot(&core) && BlCore_PrepareBoot(&core)) {
		result.booted = true;
		result.run = core.run;
		result.event = core.event;
		result.major = core.exec.version_major;
		result.minor = core.exec.version_minor;
		result.iwdg = core.iwdg;
	}
	return result;
}

// a aplicacao confirma-se (o que o boot_ctrl.c faz depois de boot_confirm_s)
static bool app_confirm(void) {

	boot_journal_t journal;

	BootJournal_Load(&journal);
	if (journal.state.exec_state != BOOT_EXEC_PENDING) {
		return false;
	}

	boot_record_t record = journal.state;

	record.exec_state = BOOT_EXEC_CONFIRMED;
	record.event = BOOT_EVENT_CONFIRMED;
	record.reason = BOOT_ERR_NONE;
	return BootJournal_Write(&journal, &record, false);
}

static void request_bytes(uint8_t out[8], uint8_t command, uint32_t value, const uint8_t *payload) {

	memset(out, 0, 8);
	out[0] = command;
	if (command == BL_CMD_DATA) {
		out[1] = (uint8_t) value;
		out[2] = (uint8_t) (value >> 8);
		out[3] = (uint8_t) (value >> 16);
		memcpy(&out[4], payload, 4);
	} else {
		put32(&out[1], value);
	}
}

static uint8_t send(uint8_t bus, uint8_t command, uint32_t value, const uint8_t *payload, uint8_t reply[8]) {

	uint8_t request[8];

	request_bytes(request, command, value, payload);
	return BlProtocol_Frame(&core, bus, request, 8, reply);
}

static uint32_t reply_value(const uint8_t reply[8]) {

	uint32_t value;

	memcpy(&value, &reply[2], 4);
	return value;
}

// sessao no bootloader (pedido da aplicacao na RAM)
static void enter_bootloader(void) {

	memset(&bl_diag, 0, sizeof(bl_diag));
	BlCore_Start(&core, BL_RESET_SOFT | BL_RESET_PIN, true, 500, 500);
	BlProtocol_Init(now_ms);
}

// INFO, BEGIN, DATA..., END como o uploader; devolve o status do ultimo comando
static uint8_t upload(const uint8_t *image, uint32_t length, uint32_t stop_after) {

	uint8_t reply[8];
	uint8_t status = send(0, BL_CMD_INFO, 0, NULL, reply);

	if ((status != BL_OK) || (reply_value(reply) != BOOT_IMAGE_MAX)) {
		return 0xEE;
	}
	status = send(0, BL_CMD_BEGIN, length, NULL, reply);
	if ((status != BL_OK) || (reply_value(reply) != 0u)) {
		return status;
	}
	for (uint32_t offset = 0; offset < length; offset += 4u) {

		uint8_t chunk[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		uint32_t count = ((length - offset) < 4u) ? (length - offset) : 4u;

		if (offset >= stop_after) {
			return 0xFD;   // o uploader foi-se embora
		}
		memcpy(chunk, &image[offset], count);
		status = send(0, BL_CMD_DATA, offset, chunk, reply);
		if ((status != BL_OK) || (reply_value(reply) != (offset + count))) {
			return status;
		}
		now_ms++;
	}
	status = send(0, BL_CMD_END, BootCrc32_Update(0, image, length), NULL, reply);
	if ((status == BL_OK) && ((reply[7] != 2u) || (reply_value(reply) != length))) {
		return 0xEC;
	}
	return status;
}

/* ================== TESTES ================== */

static uint8_t app_v1[24000], app_v2[30000], app_v3[20000], app_v4[26000];
static uint8_t image_v2[BOOT_EXEC_SIZE], image_v3[BOOT_EXEC_SIZE], image_v4[BOOT_EXEC_SIZE];
static uint32_t length_v2, length_v3, length_v4;
// pequenas para o teste dos cortes (um corte por escrita: o tempo cresce com o tamanho)
static uint8_t small_v2[3000], small_v3[2200];
static uint8_t small_image_v2[8192], small_image_v3[8192];
static uint32_t small_length_v2, small_length_v3;

static void build_images(void) {

	make_app(app_v1, sizeof(app_v1), 1, 0, 1);
	make_app(app_v2, sizeof(app_v2), 2, 0, 2);
	make_app(app_v3, sizeof(app_v3), 3, 0, 3);
	make_app(app_v4, sizeof(app_v4), 4, 0, 4);
	length_v2 = package(image_v2, app_v2, sizeof(app_v2), 2, 0, 0, 20);
	length_v3 = package(image_v3, app_v3, sizeof(app_v3), 3, 0, 0, 30);
	length_v4 = package(image_v4, app_v4, sizeof(app_v4), 4, 0, 0, 40);
	make_app(small_v2, sizeof(small_v2), 2, 1, 5);
	make_app(small_v3, sizeof(small_v3), 3, 1, 6);
	small_length_v2 = package(small_image_v2, small_v2, sizeof(small_v2), 2, 1, 0, 0);
	small_length_v3 = package(small_image_v3, small_v3, sizeof(small_v3), 3, 1, 0, 0);
}

static void test_hashes(void) {

	static const uint8_t abc_digest[32] = {
		0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
		0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
	};
	static const uint8_t two_block_digest[32] = {
		0x24, 0x8D, 0x6A, 0x61, 0xD2, 0x06, 0x38, 0xB8, 0xE5, 0xC0, 0x26, 0x93, 0x0C, 0x3E, 0x60, 0x39,
		0xA3, 0x3C, 0xE4, 0x59, 0x64, 0xFF, 0x21, 0x67, 0xF6, 0xEC, 0xED, 0xD4, 0x19, 0xDB, 0x06, 0xC1
	};
	static const uint8_t million_a_digest[32] = {
		0xCD, 0xC7, 0x6E, 0x5C, 0x99, 0x14, 0xFB, 0x92, 0x81, 0xA1, 0xC7, 0xE2, 0x84, 0xD7, 0x3E, 0x67,
		0xF1, 0x80, 0x9A, 0x48, 0xA4, 0x97, 0x20, 0x0E, 0x04, 0x6D, 0x39, 0xCC, 0xC7, 0x11, 0x2C, 0xD0
	};
	boot_sha256_t sha;
	uint8_t digest[32];
	const char *two_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

	BootSha256_Init(&sha);
	BootSha256_Update(&sha, "abc", 3);
	BootSha256_Final(&sha, digest);
	CHECK(memcmp(digest, abc_digest, 32) == 0);

	BootSha256_Init(&sha);
	BootSha256_Update(&sha, two_block, (uint32_t) strlen(two_block));
	BootSha256_Final(&sha, digest);
	CHECK(memcmp(digest, two_block_digest, 32) == 0);

	// um milhao de 'a' em pedacos irregulares
	static uint8_t a_block[1000];

	memset(a_block, 'a', sizeof(a_block));
	BootSha256_Init(&sha);
	for (uint32_t done = 0, step = 1; done < 1000000u; done += step, step = (step % 997u) + 1u) {
		if ((done + step) > 1000000u) {
			step = 1000000u - done;
		}
		BootSha256_Update(&sha, a_block, step);
	}
	BootSha256_Final(&sha, digest);
	CHECK(memcmp(digest, million_a_digest, 32) == 0);

	// zlib.crc32(b"123456789") e encadeado
	CHECK(BootCrc32_Update(0, "123456789", 9) == 0xCBF43926u);
	CHECK(BootCrc32_Update(BootCrc32_Update(0, "1234", 4), "56789", 5) == 0xCBF43926u);
	CHECK(BootCrc32_Update(0, "", 0) == 0u);
}

static void test_layout(void) {

	CHECK(BootLayout_Sector(BOOT_LOADER_ADDRESS) == 0);
	CHECK(BootLayout_Sector(BOOT_JOURNAL_A_ADDRESS) == 2);
	CHECK(BootLayout_Sector(BOOT_JOURNAL_B_ADDRESS) == 3);
	CHECK(BootLayout_Sector(BOOT_EXEC_ADDRESS) == 4);
	CHECK(BootLayout_Sector(BOOT_EXEC_ADDRESS + 0x10000u) == 5);
	CHECK(BootLayout_Sector(BOOT_BACKUP_ADDRESS) == 6);
	CHECK(BootLayout_Sector(BOOT_BACKUP_ADDRESS + BOOT_BACKUP_SIZE - 1u) == 7);
	CHECK(BootLayout_Sector(BOOT_FLASH_BASE + BOOT_FLASH_SIZE) == -1);
	CHECK(BootLayout_SectorAddress(5) == 0x08020000u);
	CHECK(BootLayout_SectorSize(4) == 0x10000u);
	CHECK(BootLayout_SectorSize(7) == 0x20000u);
	CHECK((BOOT_EXEC_ADDRESS + BOOT_EXEC_SIZE) == BOOT_BACKUP_ADDRESS);
	CHECK(BOOT_APP_ADDRESS == 0x08010200u);
	CHECK(BOOT_SHARED_ADDRESS + sizeof(boot_shared_t) == (BOOT_RAM_ADDRESS + BOOT_RAM_SIZE));
	CHECK((BOOT_SHARED_ADDRESS + offsetof(boot_shared_t, request)) == 0x2003FFFCu);   // o sitio da palavra da ACU
}

static void test_journal(void) {

	boot_journal_t journal;
	boot_record_t record;

	blank_device();
	BootJournal_Load(&journal);
	CHECK(!journal.found);
	CHECK(BootJournal_Free(&journal) == BOOT_RECORDS);

	memset(&record, 0, sizeof(record));
	record.exec_state = BOOT_EXEC_CONFIRMED;
	record.updates = 7;
	CHECK(BootJournal_Write(&journal, &record, false));
	BootJournal_Load(&journal);
	CHECK(journal.found && (journal.state.updates == 7) && (journal.state.sequence == 1u));

	// enche o setor A: o seguinte vai para o B (so com may_erase)
	for (uint32_t index = 1; index < BOOT_RECORDS; index++) {
		record.updates = (uint16_t) index;
		CHECK(BootJournal_Write(&journal, &record, false));
	}
	CHECK(BootJournal_Free(&journal) == 0u);
	record.updates = 999;
	CHECK(!BootJournal_Write(&journal, &record, false));
	CHECK(BootJournal_Write(&journal, &record, true));
	CHECK(journal.active == 1u);
	BootJournal_Load(&journal);
	CHECK((journal.state.updates == 999) && (journal.active == 1u) && (journal.next == 1u));

	// um registo cortado a meio fica para tras: vale o anterior, e o proximo vai depois dele
	uint32_t next = BootJournal_SectorAddress(1) + BOOT_RECORD_SIZE;

	memset(flash_at(next), 0x00, 20);
	BootJournal_Load(&journal);
	CHECK((journal.state.updates == 999) && (journal.next == 2u));
	record.updates = 1000;
	CHECK(BootJournal_Write(&journal, &record, false));
	BootJournal_Load(&journal);
	CHECK((journal.state.updates == 1000) && (journal.next == 3u));

	// setores com lixo (o firmware antigo estava ligado a 0x08000000): sem registos, o primeiro apaga
	blank_device();
	for (uint32_t index = 0; index < (2u * BOOT_JOURNAL_SIZE); index++) {
		boot_test_flash[(BOOT_JOURNAL_A_ADDRESS - BOOT_FLASH_BASE) + index] = (uint8_t) (index * 7u);
	}
	BootJournal_Load(&journal);
	CHECK(!journal.found && (BootJournal_Free(&journal) == 0u));
	CHECK(!BootJournal_Write(&journal, &record, false));
	CHECK(BootJournal_Write(&journal, &record, true));
	BootJournal_Load(&journal);
	CHECK(journal.found && (journal.state.updates == 1000));

	// compactar passa para o outro setor com o mesmo estado
	uint8_t before = journal.active;

	CHECK(BootJournal_Compact(&journal));
	BootJournal_Load(&journal);
	CHECK((journal.active != before) && (journal.state.updates == 1000) && (journal.next == 1u));
	CHECK(overwrites == 0);
}

static void test_image_checks(void) {

	boot_image_t image;

	blank_device();
	memcpy(flash_at(BOOT_BACKUP_ADDRESS), image_v2, length_v2);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, length_v2, false, &image) == BOOT_ERR_NONE);
	CHECK((image.kind == BOOT_IMAGE_PACKAGED) && (image.version_major == 2) && (image.version_build == 20u) && (image.length == length_v2));
	CHECK(image.features == FEATURES);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, length_v2 + 4u, false, &image) == BOOT_ERR_TLV);
	CHECK(BootImage_PeekVersion(BOOT_BACKUP_ADDRESS) == 0x02000000u);

	// um bit trocado no meio
	flash_at(BOOT_BACKUP_ADDRESS + 5000u)[0] ^= 0x10u;
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &image) == BOOT_ERR_HASH);
	flash_at(BOOT_BACKUP_ADDRESS + 5000u)[0] ^= 0x10u;

	// imagem de outra placa (a ACU, por exemplo)
	static uint8_t other[sizeof(app_v2)];
	static uint8_t other_image[BOOT_EXEC_SIZE];

	memcpy(other, app_v2, sizeof(other));
	put32(&other[APP_INFO_AT + 4u], 0x31554341u);   // "ACU1"
	uint32_t other_length = package(other_image, other, sizeof(other), 2, 0, 0, 0);

	memcpy(flash_at(BOOT_BACKUP_ADDRESS), other_image, other_length);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &image) == BOOT_ERR_BOARD);

	// ligada para 0x08000000 (o firmware antigo): o PC esta fora da imagem
	memcpy(other, app_v2, sizeof(other));
	put32(&other[4], 0x08000401u);
	other_length = package(other_image, other, sizeof(other), 2, 0, 0, 0);
	memcpy(flash_at(BOOT_BACKUP_ADDRESS), other_image, other_length);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &image) == BOOT_ERR_VECTORS);

	// a pilha no fim da RAM (sem os 32 B da area partilhada): recusada
	memcpy(other, app_v2, sizeof(other));
	put32(&other[0], 0x20040000u);
	other_length = package(other_image, other, sizeof(other), 2, 0, 0, 0);
	memcpy(flash_at(BOOT_BACKUP_ADDRESS), other_image, other_length);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &image) == BOOT_ERR_VECTORS);

	// RAW: cabecalho apagado, so com allow_raw
	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, false, &image) == BOOT_ERR_HEADER);
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, true, &image) == BOOT_ERR_NONE);
	CHECK((image.kind == BOOT_IMAGE_RAW) && (image.image_size == sizeof(app_v1)) && (image.version_major == 1));

	// lixo no cabecalho (o firmware antigo por baixo do bootloader)
	flash_at(BOOT_EXEC_ADDRESS)[3] = 0x12;
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, true, &image) == BOOT_ERR_HEADER);
}

static void test_fresh_device_boots_debugger_image(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));

	boot_result_t result = power_on(BL_RESET_POR | BL_RESET_PIN);

	CHECK(result.booted && (result.run == BOOT_RUN_EXTERNAL) && (result.major == 1));
	CHECK(!result.iwdg);
	CHECK(bl_diag.valid_banks == 1u);
	CHECK(!BlCore_WindowReset(BL_RESET_POR | BL_RESET_PIN));
	CHECK(!BlCore_WindowReset(BL_RESET_SOFT | BL_RESET_PIN));
	CHECK(BlCore_WindowReset(BL_RESET_PIN));
	CHECK(BlCore_WindowReset(BL_RESET_WWDG | BL_RESET_PIN));

	// nada na flash: fica no bootloader
	blank_device();
	result = power_on(BL_RESET_POR);
	CHECK(!result.booted && (core.action == BL_ACTION_STAY) && (core.reason == BOOT_ERR_HEADER));
	CHECK((core.entry_flags & BL_ENTRY_RECOVERY) != 0u);
}

static void test_protocol_rules(void) {

	uint8_t reply[8];
	uint8_t request[8];

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();

	CHECK(send(0, BL_CMD_INFO, 0, NULL, reply) == BL_OK);
	CHECK((reply[0] == 0x80u) && (reply_value(reply) == BOOT_IMAGE_MAX) && (reply[6] == 1u) && (reply[7] == 0u));

	// DLC errado e comando desconhecido
	request_bytes(request, BL_CMD_INFO, 0, NULL);
	CHECK(BlProtocol_Frame(&core, 0, request, 7, reply) == BL_BAD_FRAME);
	CHECK(send(0, 9, 0, NULL, reply) == BL_BAD_FRAME);
	CHECK(reply[0] == 0x89u);

	// sem BEGIN
	uint8_t word[4] = { 1, 2, 3, 4 };

	CHECK(send(0, BL_CMD_DATA, 0, word, reply) == BL_BAD_STATE);
	CHECK(send(0, BL_CMD_END, 0, NULL, reply) == BL_BAD_STATE);
	CHECK(bl_diag.error_code == BOOT_ERR_STATE);

	// tamanhos
	CHECK(send(0, BL_CMD_BEGIN, BOOT_IMAGE_MIN - 1u, NULL, reply) == BL_BAD_SIZE);
	CHECK(send(0, BL_CMD_BEGIN, BOOT_IMAGE_MAX + 1u, NULL, reply) == BL_BAD_SIZE);

	CHECK(send(0, BL_CMD_BEGIN, length_v2, NULL, reply) == BL_OK);
	CHECK((reply[7] == 1u) && (reply_value(reply) == 0u));
	long ops = flash_ops;

	// BEGIN repetido antes do primeiro DATA: nao apaga outra vez
	CHECK(send(0, BL_CMD_BEGIN, length_v2, NULL, reply) == BL_OK);
	CHECK(flash_ops == ops);

	// outro bus durante a sessao: so o INFO
	CHECK(send(1, BL_CMD_DATA, 0, image_v2, reply) == BL_BAD_STATE);
	CHECK(send(1, BL_CMD_INFO, 0, NULL, reply) == BL_OK);

	CHECK(send(0, BL_CMD_DATA, 0, image_v2, reply) == BL_OK);
	CHECK(reply_value(reply) == 4u);
	ops = flash_ops;
	// a repeticao exata do ultimo DATA (ACK perdido) nao grava
	CHECK(send(0, BL_CMD_DATA, 0, image_v2, reply) == BL_OK);
	CHECK((flash_ops == ops) && (reply_value(reply) == 4u));
	// fora de ordem
	CHECK(send(0, BL_CMD_DATA, 8, &image_v2[8], reply) == BL_BAD_OFFSET);
	CHECK((bl_diag.error_code == BOOT_ERR_OFFSET) && (bl_diag.error_detail == 8u));
	CHECK(send(0, BL_CMD_END, 0, NULL, reply) == BL_BAD_STATE);

	for (uint32_t offset = 4; offset < length_v2; offset += 4u) {
		uint8_t chunk[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		memcpy(chunk, &image_v2[offset], ((length_v2 - offset) < 4u) ? (length_v2 - offset) : 4u);
		if (send(0, BL_CMD_DATA, offset, chunk, reply) != BL_OK) {
			CHECK(false);
			break;
		}
	}
	CHECK(reply_value(reply) == length_v2);
	// CRC errado: recusado, a sessao fica
	CHECK(send(0, BL_CMD_END, 0x12345678u, NULL, reply) == BL_BAD_IMAGE);
	CHECK(bl_diag.error_code == BOOT_ERR_CRC);
	CHECK(send(0, BL_CMD_END, BootCrc32_Update(0, image_v2, length_v2), NULL, reply) == BL_OK);
	CHECK((reply[0] == 0x83u) && (reply[7] == 2u) && (reply_value(reply) == length_v2));
	// END repetido com o mesmo CRC; BEGIN depois de instalado nao
	CHECK(send(0, BL_CMD_END, BootCrc32_Update(0, image_v2, length_v2), NULL, reply) == BL_OK);
	CHECK(send(0, BL_CMD_BEGIN, length_v2, NULL, reply) == BL_BAD_STATE);
	CHECK(send(0, BL_CMD_RESET, 0, NULL, reply) == BL_OK);
	CHECK(bl_session.reset_requested && (reply[0] == 0x84u));
	CHECK(bl_diag.upload_stage == BL_STAGE_COMMITTED);

	// diagnosticos: as tramas 0x582..0x589 saem pelos dois bus
	uint32_t id;
	uint8_t data[8];
	int kind = BlDiag_Next(1, &id, data);

	CHECK((kind == BL_DIAG_INFO) && (id == 0x582u) && (data[0] == 1u) && (data[1] == BOOT_BOARD_TYPE));
	CHECK((data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t) data[7] << 24)) == BOOT_IMAGE_MAX);
	BlDiag_Sent(1, kind);
	CHECK(BlDiag_Next(1, &id, data) != BL_DIAG_INFO);

	uint8_t heartbeat[8];

	BlProtocol_Heartbeat(0x12345678u, heartbeat);
	CHECK((heartbeat[0] == 0x55u) && (heartbeat[1] == 0x78u) && (heartbeat[4] == 0x12u) && (heartbeat[5] == BOOT_VERSION_MAJOR));
	CHECK(overwrites == 0);
}

static void test_update_confirm_and_crash_rollback(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	CHECK(power_on(BL_RESET_POR).booted);

	// v2 por CAN: o v1 (RAW) vai para o BACKUP com cabecalho gerado
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);
	CHECK(core.journal.state.exec_state == BOOT_EXEC_PENDING);
	CHECK(core.journal.state.backup_state == BOOT_BACKUP_VALID);
	CHECK((core.journal.state.updates == 1u) && (core.journal.state.can1_kbps == 500u));

	boot_image_t backup;

	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &backup) == BOOT_ERR_NONE);
	CHECK((backup.version_major == 1) && (backup.image_size == sizeof(app_v1)));
	CHECK(memcmp(flash_at(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE), app_v1, sizeof(app_v1)) == 0);

	// primeiro arranque: em teste, com o IWDG
	boot_result_t result = power_on(BL_RESET_SOFT | BL_RESET_PIN);

	CHECK(result.booted && (result.run == BOOT_RUN_PENDING) && (result.major == 2) && result.iwdg);
	CHECK(core.journal.state.attempts == 1u);
	CHECK(app_confirm());
	result = power_on(BL_RESET_POR);
	CHECK(result.booted && (result.run == BOOT_RUN_CONFIRMED) && (result.major == 2) && !result.iwdg);
	CHECK(core.journal.state.attempts == 1u);   // confirmada: ja nao conta

	// v3: o v2 confirmado passa para o BACKUP
	enter_bootloader();
	CHECK(upload(image_v3, length_v3, ~0u) == BL_OK);
	CHECK(BootImage_Check(BOOT_BACKUP_ADDRESS, BOOT_BACKUP_SIZE, 0, false, &backup) == BOOT_ERR_NONE);
	CHECK((backup.version_major == 2) && (backup.length == length_v2));

	// o v3 rebenta: o WWDG reinicia-o tres vezes
	result = power_on(BL_RESET_SOFT | BL_RESET_PIN);
	CHECK(result.booted && (result.major == 3) && (result.run == BOOT_RUN_PENDING));
	result = power_on(BL_RESET_WWDG | BL_RESET_PIN);
	CHECK(result.booted && (result.major == 3) && (core.journal.state.failures == 1u));
	result = power_on(BL_RESET_IWDG | BL_RESET_PIN);
	CHECK(result.booted && (result.major == 3) && (core.journal.state.failures == 2u));
	result = power_on(BL_RESET_WWDG | BL_RESET_PIN);
	CHECK(result.booted && (result.major == 2) && (result.run == BOOT_RUN_CONFIRMED) && (result.event == BOOT_EVENT_ROLLBACK));
	CHECK(result.reason == BOOT_ERR_CRASHED);
	CHECK((core.journal.state.rollbacks == 1u) && (core.journal.state.exec_state == BOOT_EXEC_CONFIRMED));
	CHECK((bl_diag.rejected_bank == 1u) && (bl_diag.reject_reason == BOOT_ERR_CRASHED) && (bl_diag.selected_bank == 2u));
	CHECK(bl_diag.rejected_version_known && (bl_diag.rejected_major == 3));
	CHECK(memcmp(flash_at(BOOT_EXEC_ADDRESS), image_v2, length_v2) == 0);

	// e daqui para a frente arranca normal
	result = power_on(BL_RESET_POR);
	CHECK(result.booted && (result.major == 2) && (result.run == BOOT_RUN_CONFIRMED) && (result.event == BOOT_EVENT_NONE));
	CHECK(overwrites == 0);
}

static void test_power_cycles_and_unconfirmed(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());

	// v4: o BACKUP passa a v2. Desligar e ligar nao e falha da imagem...
	enter_bootloader();
	CHECK(upload(image_v4, length_v4, ~0u) == BL_OK);
	for (uint32_t boot = 1; boot <= BOOT_MAX_ATTEMPTS; boot++) {
		boot_result_t result = power_on(BL_RESET_POR | BL_RESET_PIN);
		CHECK(result.booted && (result.major == 4) && (result.run == BOOT_RUN_PENDING));
		CHECK(core.journal.state.failures == 0u);
	}
	// ...mas BOOT_MAX_ATTEMPTS arranques sem se confirmar e: volta ao v2
	boot_result_t result = power_on(BL_RESET_POR | BL_RESET_PIN);

	CHECK(result.booted && (result.major == 2) && (result.reason == BOOT_ERR_UNCONFIRMED));

	// atualizar com o BACKUP ja igual ao EXEC: nao se copia outra vez
	enter_bootloader();
	long ops = flash_ops;
	uint8_t reply[8];

	CHECK(send(0, BL_CMD_BEGIN, length_v4, NULL, reply) == BL_OK);
	// so os registos do journal + apagar o EXEC (2 setores), nada no BACKUP
	CHECK((flash_ops - ops) < 40);
	CHECK(overwrites == 0);
}

static void test_interrupted_upload_restores(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, 12000u) == 0xFD);   // o portatil ficou sem bateria a meio

	// 30 s depois o bootloader reinicia: o EXEC esta a meio e o v1 volta do BACKUP
	boot_result_t result = power_on(BL_RESET_SOFT | BL_RESET_PIN);

	CHECK(result.booted && (result.major == 1) && (result.event == BOOT_EVENT_ROLLBACK) && (result.reason == BOOT_ERR_INCOMPLETE));
	CHECK(result.run == BOOT_RUN_CONFIRMED);

	// o v1 reposto ja e um MCUboot (com o cabecalho gerado) e arranca sempre igual
	result = power_on(BL_RESET_POR);
	CHECK(result.booted && (result.major == 1) && (result.event == BOOT_EVENT_NONE));

	// sem backup nenhum (o EXEC nunca foi bom): fica no bootloader a espera
	blank_device();
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, 4000u) == 0xFD);
	result = power_on(BL_RESET_SOFT);
	CHECK(!result.booted && (core.action == BL_ACTION_STAY) && (result.reason == BOOT_ERR_INCOMPLETE));

	// e um upload completo resolve
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);
	result = power_on(BL_RESET_SOFT);
	CHECK(result.booted && (result.major == 2) && (result.run == BOOT_RUN_PENDING));
	CHECK(overwrites == 0);
}

static void test_manual_rollback_and_corruption(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());

	// "boot rollback" na consola: a aplicacao marca REJECTED e reinicia
	boot_journal_t journal;

	BootJournal_Load(&journal);
	boot_record_t record = journal.state;

	record.exec_state = BOOT_EXEC_REJECTED;
	record.event = BOOT_EVENT_REJECTED;
	CHECK(BootJournal_Write(&journal, &record, false));

	boot_result_t result = power_on(BL_RESET_SOFT | BL_RESET_PIN);

	CHECK(result.booted && (result.major == 1) && (result.reason == BOOT_ERR_MANUAL));

	// a flash do EXEC estraga-se (um bit): volta ao BACKUP
	flash_at(BOOT_EXEC_ADDRESS + 3000u)[0] ^= 0x01u;
	result = power_on(BL_RESET_POR);
	CHECK(result.booted && (result.major == 1) && (result.reason == BOOT_ERR_HASH) && (result.event == BOOT_EVENT_ROLLBACK));

	// o debugger grava por cima (CubeIDE): arranca a RAW, sem tentativas nem rollback
	enter_bootloader();
	CHECK(upload(image_v3, length_v3, ~0u) == BL_OK);   // v3 em teste
	debugger_flash(app_v4, sizeof(app_v4));
	for (uint32_t boot = 0; boot < 5u; boot++) {
		result = power_on(BL_RESET_WWDG | BL_RESET_PIN);
		CHECK(result.booted && (result.major == 4) && (result.run == BOOT_RUN_EXTERNAL) && !result.iwdg);
	}
	CHECK(overwrites == 0);
}

static void test_no_backup_accepts_image(void) {

	// nunca houve firmware bom: o v2 rebenta mas nao ha para onde voltar
	blank_device();
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(power_on(BL_RESET_WWDG).booted);
	CHECK(power_on(BL_RESET_WWDG).booted);

	boot_result_t result = power_on(BL_RESET_WWDG);

	CHECK(result.booted && (result.major == 2) && (result.event == BOOT_EVENT_ACCEPTED) && (result.run == BOOT_RUN_CONFIRMED));
	CHECK(core.journal.state.exec_state == BOOT_EXEC_CONFIRMED);

	// a seguir, um update guarda-o como backup
	enter_bootloader();
	CHECK(upload(image_v3, length_v3, ~0u) == BL_OK);
	CHECK(core.journal.state.backup_state == BOOT_BACKUP_VALID);
	CHECK(overwrites == 0);
}

static void test_journal_reserve_for_app(void) {

	// o journal quase cheio: antes de arrancar uma imagem em teste o bootloader abre espaco
	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(image_v2, length_v2, ~0u) == BL_OK);

	while (BootJournal_Free(&core.journal) > 2u) {
		boot_record_t record = core.journal.state;
		CHECK(BootJournal_Write(&core.journal, &record, false));
	}
	boot_result_t result = power_on(BL_RESET_POR);

	CHECK(result.booted && (result.run == BOOT_RUN_PENDING));
	CHECK(BootJournal_Free(&core.journal) > BOOT_JOURNAL_RESERVE);
	CHECK(app_confirm());
	CHECK(power_on(BL_RESET_POR).run == BOOT_RUN_CONFIRMED);
	CHECK(overwrites == 0);
}

/* Corte de energia em cada escrita da flash durante um update completo
 * (BEGIN com backup, DATA, END, arranque em teste, confirmacao). Depois do
 * corte o arranque seguinte (que pode ele proprio ser cortado uma vez) tem de
 * arrancar o firmware antigo ou o novo, nunca ficar sem nada */
static uint8_t snapshot[BOOT_FLASH_SIZE];

typedef enum { STEP_UPLOAD, STEP_BOOT, STEP_CONFIRM, STEP_DONE } step_t;

static int update_until_cut(long cut) {

	volatile step_t step = STEP_UPLOAD;

	cut_at = cut;
	flash_ops = 0;
	if (setjmp(power_cut) != 0) {
		return (int) step;
	}
	enter_bootloader();
	if (upload(small_image_v3, small_length_v3, ~0u) != BL_OK) {
		return -1;
	}
	step = STEP_BOOT;
	if (!power_on(BL_RESET_SOFT).booted) {
		return -2;
	}
	step = STEP_CONFIRM;
	if (!app_confirm()) {
		return -3;
	}
	step = STEP_DONE;
	return (int) step;
}

static boot_result_t boot_after_cut(long second_cut) {

	volatile boot_result_t result = { 0 };

	cut_at = second_cut;
	flash_ops = 0;
	if (setjmp(power_cut) != 0) {
		// cortou outra vez a meio da recuperacao: o proximo arranque tem de chegar la
		cut_at = -1;
		boot_result_t again = power_on(BL_RESET_POR);
		return again;
	}
	boot_result_t first = power_on(BL_RESET_POR);

	cut_at = -1;
	result = first;
	return result;
}

static void test_power_cut_everywhere(void) {

	// v2 confirmado no EXEC, v1 no BACKUP: o update para v3 e cortado em todo o lado
	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());
	memcpy(snapshot, boot_test_flash, sizeof(snapshot));

	// quantas escritas tem o update inteiro
	CHECK(update_until_cut(-1) == STEP_DONE);
	long total = flash_ops;

	CHECK(total > 1000);

	int bad = 0;
	int new_boots = 0;
	int old_boots = 0;

	for (long cut = 0; cut < total; cut++) {

		memcpy(boot_test_flash, snapshot, sizeof(snapshot));
		int step = update_until_cut(cut);

		// cortes mais raros tambem durante a recuperacao (o rollback ou a escrita da tentativa)
		long second = ((cut % 97) == 0) ? (cut % 13) * 7 : -1;
		boot_result_t result = boot_after_cut(second);

		if (!result.booted || ((result.major != 2) && (result.major != 3))) {
			if (bad++ < 5) {
				printf("  corte %ld (passo %d, segundo %ld): booted=%d v%u reason %s\n", cut, step, second, result.booted, result.major, BootNames_Error(result.reason));
			}
			continue;
		}
		if (result.major == 3) {
			new_boots++;
		} else {
			old_boots++;
		}

		// o v3 so arranca se o upload chegou ao fim (em teste ou ja confirmado)
		if ((result.major == 3) && (step == STEP_UPLOAD)) {
			bad++;
		}
		// e tem de voltar a arrancar igual sem cortes
		boot_result_t next = power_on(BL_RESET_POR);
		if (!next.booted) {
			bad++;
		}
	}
	printf("  %ld cortes: %d arrancaram o v3, %d o v2\n", total, new_boots, old_boots);
	CHECK(bad == 0);
	CHECK((new_boots > 0) && (old_boots > 0));
}

/* ================== ERROS DA FLASH ================== */

static uint8_t update_with_errors(long fail, long count) {

	fail_at = fail;
	fail_count = count;
	flash_ops = 0;
	enter_bootloader();

	uint8_t status = upload(small_image_v3, small_length_v3, ~0u);

	fail_at = -1;
	return status;
}

/* A flash da erro numa operacao do update (ou em 3 seguidas: o journal tenta
 * duas vezes e depois muda de setor). O update pode falhar, mas o arranque
 * seguinte arranca sempre o v2 ou o v3, e o v3 so se o upload acabou bem */
static void test_flash_errors_everywhere(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());
	memcpy(snapshot, boot_test_flash, sizeof(snapshot));

	CHECK(update_with_errors(-1, 1) == BL_OK);
	long total = flash_ops;
	int bad = 0;
	int failed = 0;
	int new_boots = 0;

	for (long count = 1; count <= 3; count += 2) {
		for (long fail = 0; fail < total; fail++) {

			memcpy(boot_test_flash, snapshot, sizeof(snapshot));
			overwrites = 0;

			uint8_t status = update_with_errors(fail, count);
			boot_result_t result = power_on(BL_RESET_POR);
			bool ok = result.booted && ((result.major == 2) || ((result.major == 3) && (status == BL_OK)));

			if (ok) {
				ok = power_on(BL_RESET_POR).booted;
			}
			if (!ok || (overwrites != 0)) {
				if (bad++ < 5) {
					printf("  erro na operacao %ld (x%ld): status %u, booted=%d v%u reason %s\n", fail, count, status, result.booted, result.major,
							BootNames_Error(result.reason));
				}
			}
			failed += (status != BL_OK) ? 1 : 0;
			new_boots += (result.major == 3) ? 1 : 0;
		}
	}
	printf("  %ld operacoes com erro (x1 e x3): %d updates falharam, %d arrancaram o v3\n", total, failed, new_boots);
	CHECK(bad == 0);
	CHECK((failed > 0) && (new_boots > 0));
}

// o rollback de um upload interrompido com erros na flash: fica no bootloader e o arranque seguinte consegue
static void test_rollback_with_flash_errors(void) {

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());
	enter_bootloader();
	CHECK(upload(small_image_v3, small_length_v3, 1000u) == 0xFD);   // o uploader foi-se embora a meio
	memcpy(snapshot, boot_test_flash, sizeof(snapshot));

	flash_ops = 0;
	boot_result_t clean = power_on(BL_RESET_POR);
	long total = flash_ops;

	CHECK(clean.booted && (clean.major == 2) && (clean.event == BOOT_EVENT_ROLLBACK));

	int bad = 0;
	int stayed = 0;

	for (long count = 1; count <= 3; count += 2) {
		for (long fail = 0; fail < total; fail++) {

			memcpy(boot_test_flash, snapshot, sizeof(snapshot));
			fail_at = fail;
			fail_count = count;
			flash_ops = 0;

			boot_result_t first = power_on(BL_RESET_POR);

			fail_at = -1;
			boot_result_t second = power_on(BL_RESET_POR);

			stayed += first.booted ? 0 : 1;
			if (!second.booted || (second.major != 2) || (first.booted && (first.major != 2))) {
				if (bad++ < 5) {
					printf("  rollback com erro em %ld (x%ld): %d/v%u e %d/v%u\n", fail, count, first.booted, first.major, second.booted, second.major);
				}
			}
		}
	}
	CHECK(bad == 0);
	CHECK(stayed > 0);
}

// a flash muda entre a verificacao do arranque e o uso (bit que se estraga, debugger por cima)
static void test_flash_changes_after_checks(void) {

	uint8_t reply[8];

	// v2 instalado e confirmado, v1 no BACKUP
	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	CHECK(power_on(BL_RESET_POR).booted);
	CHECK(app_confirm());
	memcpy(snapshot, boot_test_flash, sizeof(snapshot));

	// o EXEC estraga-se depois do arranque do bootloader: a copia para o BACKUP nao confere
	enter_bootloader();
	*flash_at(BOOT_APP_ADDRESS + 1000u) ^= 0x10u;
	CHECK(send(0, BL_CMD_BEGIN, small_length_v3, NULL, reply) == BL_FLASH_ERROR);
	CHECK(bl_diag.error_code == BOOT_ERR_HASH);
	CHECK(strstr(log_text, "a copia no BACKUP nao confere") != NULL);

	// o EXEC trocado por outra imagem valida do mesmo tamanho: a copia e valida mas nao e a que foi verificada
	static uint8_t twin_app[sizeof(small_v2)];
	static uint8_t twin_image[sizeof(small_image_v2)];

	make_app(twin_app, sizeof(twin_app), 2, 1, 55);
	CHECK(package(twin_image, twin_app, sizeof(twin_app), 2, 1, 0, 0) == small_length_v2);
	memcpy(boot_test_flash, snapshot, sizeof(snapshot));
	enter_bootloader();
	memcpy(flash_at(BOOT_EXEC_ADDRESS), twin_image, small_length_v2);
	CHECK(send(0, BL_CMD_BEGIN, small_length_v3, NULL, reply) == BL_FLASH_ERROR);
	CHECK(bl_diag.error_code == BOOT_ERR_HASH);

	// o BACKUP estraga-se entre a decisao e o rollback: nao arranca, fica no bootloader
	memcpy(boot_test_flash, snapshot, sizeof(snapshot));
	enter_bootloader();
	CHECK(upload(small_image_v3, small_length_v3, 400u) == 0xFD);
	memset(&bl_diag, 0, sizeof(bl_diag));
	BlCore_Start(&core, BL_RESET_POR, false, 500, 500);
	CHECK(core.action == BL_ACTION_RESTORE);
	*flash_at(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE + 500u) ^= 0x01u;
	CHECK(!BlCore_PrepareBoot(&core));
	CHECK(((core.entry_flags & BL_ENTRY_RECOVERY) != 0u) && (bl_diag.error_code == BOOT_ERR_HASH));
	CHECK(strstr(log_text, "a copia nao confere") != NULL);
	CHECK(!BlCore_PrepareBoot(&core));   // e depois disso nao ha nada para arrancar

	// o journal diz que o BACKUP e o v1 mas la esta outra imagem valida do mesmo tamanho: nao serve para rollback
	static uint8_t other_app[sizeof(app_v1)];
	static uint8_t other_image[sizeof(app_v1) + 1024u];
	uint32_t other_length;

	make_app(other_app, sizeof(other_app), 1, 9, 77);
	other_length = package(other_image, other_app, sizeof(other_app), 1, 9, 0, 0);
	memcpy(boot_test_flash, snapshot, sizeof(snapshot));
	memset(flash_at(BOOT_BACKUP_ADDRESS), 0xFF, BOOT_BACKUP_SIZE);
	memcpy(flash_at(BOOT_BACKUP_ADDRESS), other_image, other_length);
	memset(&bl_diag, 0, sizeof(bl_diag));
	log_length = 0;
	BlCore_Start(&core, BL_RESET_POR, false, 500, 500);
	CHECK(!core.backup_ok && core.exec_ok);
	CHECK(strstr(log_text, "BACKUP: invalido (HASH") != NULL);
}

// END com uma imagem cujo SHA-256 nao bate (o CRC do transporte bate): recusada, e volta ao v1
static void test_end_rejects_bad_image(void) {

	static uint8_t bad_image[8192];

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	memcpy(bad_image, small_image_v2, small_length_v2);
	bad_image[BOOT_HEADER_SIZE + 100u] ^= 0xFFu;
	CHECK(upload(bad_image, small_length_v2, ~0u) == BL_BAD_IMAGE);
	CHECK(bl_diag.error_code == BOOT_ERR_HASH);
	CHECK(strstr(log_text, "END: imagem rejeitada") != NULL);

	boot_result_t result = power_on(BL_RESET_POR);

	CHECK(result.booted && (result.major == 1) && (result.event == BOOT_EVENT_ROLLBACK));
}

// uma RAW do debugger tao grande que com cabecalho ja nao cabe: o update segue sem BACKUP
static uint8_t big_app[BOOT_EXEC_SIZE - BOOT_HEADER_SIZE - 4u];

static void test_raw_too_big_for_backup(void) {

	make_app(big_app, sizeof(big_app), 1, 5, 9);
	blank_device();
	debugger_flash(big_app, sizeof(big_app));
	enter_bootloader();
	CHECK(core.exec_ok && (core.exec.kind == BOOT_IMAGE_RAW));
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	CHECK(strstr(log_text, "nao cabe no EXEC com cabecalho") != NULL);
	CHECK(*flash_at(BOOT_BACKUP_ADDRESS) == 0xFFu);

	boot_result_t result = power_on(BL_RESET_POR);

	CHECK(result.booted && (result.major == 2));
}

// o debugger volta a gravar a RAW que ja esta no BACKUP: o BEGIN nao a copia outra vez
static void test_backup_already_has_raw(void) {

	uint8_t reply[8];

	blank_device();
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(upload(small_image_v2, small_length_v2, ~0u) == BL_OK);
	debugger_flash(app_v1, sizeof(app_v1));
	enter_bootloader();
	CHECK(core.backup_ok && core.exec_ok && (core.exec.kind == BOOT_IMAGE_RAW));
	memcpy(snapshot, flash_at(BOOT_BACKUP_ADDRESS), BOOT_BACKUP_SIZE);
	CHECK(send(0, BL_CMD_BEGIN, small_length_v3, NULL, reply) == BL_OK);
	CHECK(strstr(log_text, "o BACKUP ja tem este firmware") != NULL);
	CHECK(memcmp(snapshot, flash_at(BOOT_BACKUP_ADDRESS), BOOT_BACKUP_SIZE) == 0);
}

// journal: o registo nao pega duas vezes e o apagar do outro setor tambem falha
static void test_journal_write_failures(void) {

	boot_journal_t journal;
	boot_record_t record;

	blank_device();
	BootJournal_Load(&journal);
	record = journal.state;
	record.updates = 7;
	CHECK(BootJournal_Write(&journal, &record, true));

	// uma falha: tenta o registo seguinte
	fail_at = flash_ops;
	fail_count = 1;
	record.updates = 8;
	CHECK(BootJournal_Write(&journal, &record, true));

	// tres (as duas tentativas e o apagar): falha e o que la estava fica
	fail_at = flash_ops;
	fail_count = 3;
	record.updates = 9;
	CHECK(!BootJournal_Write(&journal, &record, true));
	fail_at = flash_ops;
	fail_count = 2;
	record.updates = 10;
	CHECK(!BootJournal_Write(&journal, &record, false));   // sem poder apagar
	fail_at = flash_ops;
	fail_count = 1;
	CHECK(!BootJournal_Compact(&journal));
	fail_at = -1;

	boot_journal_t reloaded;

	BootJournal_Load(&reloaded);
	CHECK(reloaded.found && (reloaded.state.updates == 8u));

	// e o PrepareBoot de um bootloader que decidiu ficar diz logo que nao
	BlCore_Start(&core, BL_RESET_POR, false, 500, 500);
	CHECK(!BlCore_CanBoot(&core) && !BlCore_PrepareBoot(&core));
}

// verificacao das imagens: descritor que nao cabe e TLV estragados
static void test_image_edge_cases(void) {

	boot_image_t image;
	uint8_t tiny[0x100];

	// RAW pequena demais para ter o descritor em 0x200
	blank_device();
	memset(tiny, 0, sizeof(tiny));
	put32(&tiny[0], BOOT_STACK_TOP);
	put32(&tiny[4], BOOT_APP_ADDRESS + 0x41u);
	put32(&tiny[sizeof(tiny) - 4u], 0x0BADF00Du);
	memcpy(flash_at(BOOT_APP_ADDRESS), tiny, sizeof(tiny));
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, true, &image) == BOOT_ERR_BOARD);
	CHECK(image.kind == BOOT_IMAGE_NONE);

	uint32_t tlv = BOOT_EXEC_ADDRESS + BOOT_HEADER_SIZE + sizeof(small_v2);

	// TLV com o total a acabar a meio da entrada
	blank_device();
	memcpy(flash_at(BOOT_EXEC_ADDRESS), small_image_v2, small_length_v2);
	put32(flash_at(tlv), BOOT_TLV_INFO_MAGIC | (7u << 16));
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, false, &image) == BOOT_ERR_TLV);

	// SHA-256 com 16 bytes
	memcpy(flash_at(BOOT_EXEC_ADDRESS), small_image_v2, small_length_v2);
	put32(flash_at(tlv), BOOT_TLV_INFO_MAGIC | (24u << 16));
	put32(flash_at(tlv + 4u), BOOT_TLV_SHA256 | (16u << 16));
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, false, &image) == BOOT_ERR_TLV);

	// nenhuma entrada SHA-256
	memcpy(flash_at(BOOT_EXEC_ADDRESS), small_image_v2, small_length_v2);
	put32(flash_at(tlv + 4u), 0x0020u | (32u << 16));
	CHECK(BootImage_Check(BOOT_EXEC_ADDRESS, BOOT_EXEC_SIZE, 0, false, &image) == BOOT_ERR_TLV);

	// o SHA-256 conta o comprimento em 64 bits (o de cima so mexe depois de 4 GB)
	boot_sha256_t sha;
	uint8_t block[128];

	memset(block, 0xA5, sizeof(block));
	BootSha256_Init(&sha);
	sha.length_low = 0xFFFFFFC0u;
	BootSha256_Update(&sha, block, sizeof(block));
	CHECK((sha.length_high == 1u) && (sha.length_low == 0x40u));
}

// o formatador dos logs (o printf do bootloader)
static void test_log_format(void) {

	log_length = 0;
	BlLog("%c|%d|%d|%5d|%05d|%x|%08x|%s|%s|%u|%%|%q|%", 'A', -42, (int32_t) INT32_MIN, -3, -3, 0xBEEFu, 0x12u, "texto", (const char*) NULL, 4000000000u);
	CHECK(strcmp(log_text, "[boot] A|-42|-2147483648|   -3|-0003|beef|00000012|texto||4000000000|%|q|\r\n") == 0);

	// linha comprida: corta e fica o \r\n
	char longer[300];

	memset(longer, 'x', sizeof(longer) - 1u);
	longer[sizeof(longer) - 1u] = 0;
	log_length = 0;
	BlLog("%s", longer);
	CHECK((log_length == 120u) && (log_text[118] == '\r') && (log_text[119] == '\n'));
}

// diagnosticos: a trama da flash e bits pendentes que nao existem
static void test_diag_frames(void) {

	uint32_t id;
	uint8_t data[8];
	int kind;
	bool seen = false;

	memset(&bl_diag, 0, sizeof(bl_diag));
	BlDiag_FlashError(BOOT_ERR_FLASH_WRITE, 0x1234u, 0x80u);
	memset(bl_diag.pending, 0, sizeof(bl_diag.pending));
	BlDiag_Snapshot();   // volta a marcar a da flash (o erro veio da flash)
	while ((kind = BlDiag_Next(0, &id, data)) >= 0) {
		if (kind == BL_DIAG_FLASH) {
			seen = (id == (BL_CAN_DIAG_BASE_ID + BL_DIAG_FLASH)) && (data[0] == 0x34u) && (data[1] == 0x12u) && (data[4] == 0x80u);
		}
		BlDiag_Sent(0, kind);
	}
	CHECK(seen);

	bl_diag.pending[1] = 0x8000u;
	CHECK(BlDiag_Next(1, &id, data) < 0);
	CHECK(bl_diag.pending[1] == 0u);
}

int main(int argc, char **argv) {

	verbose = (argc > 1) && (strcmp(argv[1], "-v") == 0);
	build_images();

	test_hashes();
	test_layout();
	test_journal();
	test_image_checks();
	test_fresh_device_boots_debugger_image();
	test_protocol_rules();
	test_update_confirm_and_crash_rollback();
	test_power_cycles_and_unconfirmed();
	test_interrupted_upload_restores();
	test_manual_rollback_and_corruption();
	test_no_backup_accepts_image();
	test_journal_reserve_for_app();
	test_power_cut_everywhere();
	test_flash_errors_everywhere();
	test_rollback_with_flash_errors();
	test_flash_changes_after_checks();
	test_end_rejects_bad_image();
	test_raw_too_big_for_backup();
	test_backup_already_has_raw();
	test_journal_write_failures();
	test_image_edge_cases();
	test_log_format();
	test_diag_frames();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
