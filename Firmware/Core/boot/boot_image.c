/*
 * boot_image.c
 *
 * Ver boot_image.h. As regras sao as do validate_image() da ACU
 * (mcu_boot/Core/Src/boot_update.c), com o descritor da placa a mais e o
 * SHA-256 calculado aqui (sem a biblioteca do MCUboot).
 */

#include "boot_image.h"
#include "boot_hash.h"

static uint8_t fail(boot_image_t *image, uint8_t error, uint32_t detail) {

	image->error = error;
	image->detail = detail;
	return error;
}

bool BootImage_VectorsValid(uint32_t sp, uint32_t pc, uint32_t image_size) {

	uint32_t entry = pc & ~1u;
	bool ram = (sp > BOOT_RAM_ADDRESS) && (sp <= BOOT_STACK_TOP);

	return ram && ((sp & 7u) == 0u) && ((pc & 1u) != 0u) && (entry >= BOOT_APP_ADDRESS) && ((entry - BOOT_APP_ADDRESS) < image_size);
}

bool BootImage_HeaderErased(uint32_t slot) {

	for (uint32_t offset = 0; offset < BOOT_HEADER_SIZE; offset += 4u) {
		if (BootFlash_ReadWord(slot + offset) != 0xFFFFFFFFu) {
			return false;
		}
	}
	return true;
}

// vetores e descritor, comuns as duas formas
static uint8_t check_application(uint32_t slot, uint32_t image_size, boot_image_t *image) {

	boot_app_info_t info;

	image->sp = BootFlash_ReadWord(slot + BOOT_HEADER_SIZE);
	image->pc = BootFlash_ReadWord(slot + BOOT_HEADER_SIZE + 4u);
	if (!BootImage_VectorsValid(image->sp, image->pc, image_size)) {
		return fail(image, BOOT_ERR_VECTORS, image->pc);
	}

	if ((BOOT_APP_INFO_OFFSET + sizeof(info)) > image_size) {
		return fail(image, BOOT_ERR_BOARD, slot + BOOT_HEADER_SIZE + BOOT_APP_INFO_OFFSET - BOOT_FLASH_BASE);
	}
	memcpy(&info, BOOT_FLASH_PTR(slot + BOOT_HEADER_SIZE + BOOT_APP_INFO_OFFSET), sizeof(info));
	if ((info.magic != BOOT_APP_INFO_MAGIC) || (info.board != BOOT_BOARD_ID)) {
		return fail(image, BOOT_ERR_BOARD, slot + BOOT_HEADER_SIZE + BOOT_APP_INFO_OFFSET - BOOT_FLASH_BASE);
	}
	image->features = info.features;
	image->version_major = info.version_major;
	image->version_minor = info.version_minor;
	image->version_revision = info.version_revision;
	image->version_build = info.version_build;

	return BOOT_ERR_NONE;
}

static uint8_t check_raw(uint32_t slot, uint32_t slot_size, boot_image_t *image) {

	// ate a ultima palavra gravada: o resto da slot esta apagado e copia-se igual
	uint32_t end = slot_size;

	while ((end > BOOT_HEADER_SIZE) && (BootFlash_ReadWord(slot + end - 4u) == 0xFFFFFFFFu)) {
		end -= 4u;
	}
	if (end <= (BOOT_HEADER_SIZE + 8u)) {
		return fail(image, BOOT_ERR_HEADER, slot - BOOT_FLASH_BASE);
	}

	image->kind = BOOT_IMAGE_RAW;
	image->length = end;
	image->image_size = end - BOOT_HEADER_SIZE;
	image->id = 0;

	uint8_t error = check_application(slot, image->image_size, image);

	if (error != BOOT_ERR_NONE) {
		image->kind = BOOT_IMAGE_NONE;
	}
	return error;
}

static uint8_t check_packaged(uint32_t slot, uint32_t slot_size, uint32_t expected_length, boot_image_t *image) {

	boot_image_header_t header;
	uint32_t slot_offset = slot - BOOT_FLASH_BASE;

	memcpy(&header, BOOT_FLASH_PTR(slot), sizeof(header));
	if ((header.magic != BOOT_IMAGE_MAGIC) || (header.header_size != BOOT_HEADER_SIZE) || (header.flags != 0u) || (header.protect_tlv_size != 0u)
			|| (header.image_size < 8u) || (header.image_size > (slot_size - BOOT_HEADER_SIZE - BOOT_TLV_TOTAL))) {
		return fail(image, BOOT_ERR_HEADER, slot_offset);
	}

	// TLV: info + exatamente um SHA-256
	uint32_t end = BOOT_HEADER_SIZE + header.image_size;
	uint32_t info = BootFlash_ReadWord(slot + end);
	uint32_t total = info >> 16;

	if (((info & 0xFFFFu) != BOOT_TLV_INFO_MAGIC) || (total < 4u) || (total > (slot_size - end)) || ((expected_length != 0u) && ((end + total) != expected_length))) {
		return fail(image, BOOT_ERR_TLV, slot_offset + end);
	}

	uint32_t position = end + 4u;
	uint32_t hash_position = 0;
	uint8_t hashes = 0;

	while (position < (end + total)) {
		if ((position + 4u) > (end + total)) {
			return fail(image, BOOT_ERR_TLV, slot_offset + position);
		}
		uint32_t entry = BootFlash_ReadWord(slot + position);
		uint32_t type = entry & 0xFFFFu;
		uint32_t length = entry >> 16;

		position += 4u;
		if ((position + length) > (end + total)) {
			return fail(image, BOOT_ERR_TLV, slot_offset + position);
		}
		if (type == BOOT_TLV_SHA256) {
			if (length != BOOT_SHA256_SIZE) {
				return fail(image, BOOT_ERR_TLV, slot_offset + position);
			}
			hash_position = position;
			hashes++;
		}
		position += length;
	}
	if (hashes != 1u) {
		return fail(image, BOOT_ERR_TLV, slot_offset + end);
	}

	image->kind = BOOT_IMAGE_PACKAGED;
	image->length = end + total;
	image->image_size = header.image_size;

	uint8_t error = check_application(slot, header.image_size, image);

	if (error != BOOT_ERR_NONE) {
		image->kind = BOOT_IMAGE_NONE;
		return error;
	}
	// a versao que conta e a do cabecalho (e a que o 0x583 mostra)
	image->version_major = header.version_major;
	image->version_minor = header.version_minor;
	image->version_revision = header.version_revision;
	image->version_build = header.version_build;

	boot_sha256_t sha;
	uint8_t digest[BOOT_SHA256_SIZE];

	BootSha256_Init(&sha);
	BootSha256_Update(&sha, BOOT_FLASH_PTR(slot), end);
	BootSha256_Final(&sha, digest);
	if (memcmp(digest, BOOT_FLASH_PTR(slot + hash_position), BOOT_SHA256_SIZE) != 0) {
		image->kind = BOOT_IMAGE_NONE;
		return fail(image, BOOT_ERR_HASH, slot_offset);
	}
	image->id = ((uint32_t) digest[0] << 24) | ((uint32_t) digest[1] << 16) | ((uint32_t) digest[2] << 8) | digest[3];

	return BOOT_ERR_NONE;
}

uint8_t BootImage_Check(uint32_t slot, uint32_t slot_size, uint32_t expected_length, bool allow_raw, boot_image_t *image) {

	memset(image, 0, sizeof(*image));

	if (BootFlash_ReadWord(slot) == BOOT_IMAGE_MAGIC) {
		return check_packaged(slot, slot_size, expected_length, image);
	}
	if (allow_raw && BootImage_HeaderErased(slot)) {
		return check_raw(slot, slot_size, image);
	}
	return fail(image, BOOT_ERR_HEADER, slot - BOOT_FLASH_BASE);
}

void BootImage_MakeHeader(uint8_t header[BOOT_HEADER_SIZE], uint32_t image_size, const boot_image_t *source) {

	boot_image_header_t fields = {
		.magic = BOOT_IMAGE_MAGIC,
		.header_size = BOOT_HEADER_SIZE,
		.image_size = image_size,
		.version_major = source->version_major,
		.version_minor = source->version_minor,
		.version_revision = source->version_revision,
		.version_build = source->version_build
	};

	memset(header, 0, BOOT_HEADER_SIZE);
	memcpy(header, &fields, sizeof(fields));
}

void BootImage_MakeTlv(uint8_t tlv[BOOT_TLV_TOTAL], const uint8_t digest[32]) {

	tlv[0] = (uint8_t) (BOOT_TLV_INFO_MAGIC & 0xFFu);
	tlv[1] = (uint8_t) (BOOT_TLV_INFO_MAGIC >> 8);
	tlv[2] = (uint8_t) BOOT_TLV_TOTAL;
	tlv[3] = 0;
	tlv[4] = (uint8_t) BOOT_TLV_SHA256;
	tlv[5] = 0;
	tlv[6] = (uint8_t) BOOT_SHA256_SIZE;
	tlv[7] = 0;
	memcpy(&tlv[8], digest, BOOT_SHA256_SIZE);
}

uint32_t BootImage_PeekVersion(uint32_t slot) {

	boot_image_header_t header;

	memcpy(&header, BOOT_FLASH_PTR(slot), sizeof(header));
	if (header.magic != BOOT_IMAGE_MAGIC) {
		return 0;
	}
	return ((uint32_t) header.version_major << 24) | ((uint32_t) header.version_minor << 16) | header.version_revision;
}
