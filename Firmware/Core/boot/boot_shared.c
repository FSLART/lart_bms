/*
 * boot_shared.c
 *
 * Ver boot_shared.h.
 */

#include "boot_shared.h"
#include "boot_hash.h"

#define CHECKED_BYTES 24u

bool BootShared_Valid(const boot_shared_t *shared) {

	return (shared->magic == BOOT_SHARED_MAGIC) && (shared->check == BootCrc32_Update(0, shared, CHECKED_BYTES));
}

void BootShared_Seal(boot_shared_t *shared) {

	shared->magic = BOOT_SHARED_MAGIC;
	shared->check = BootCrc32_Update(0, shared, CHECKED_BYTES);
}

void BootShared_Read(boot_shared_t *out) {

	const volatile uint32_t *source = (const volatile uint32_t*) BOOT_SHARED;
	uint32_t *destination = (uint32_t*) out;

	for (uint32_t word = 0; word < (sizeof(boot_shared_t) / 4u); word++) {
		destination[word] = source[word];
	}
}

void BootShared_Write(const boot_shared_t *in) {

	volatile uint32_t *destination = (volatile uint32_t*) BOOT_SHARED;
	const uint32_t *source = (const uint32_t*) in;

	for (uint32_t word = 0; word < (sizeof(boot_shared_t) / 4u); word++) {
		destination[word] = source[word];
	}
}
