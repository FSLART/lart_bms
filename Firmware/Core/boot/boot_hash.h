/*
 * boot_hash.h
 *
 * SHA-256 (FIPS 180-4), o hash do TLV 0x10 das imagens MCUboot, e o CRC-32
 * do zlib (polinomio refletido 0xEDB88320), o do END do protocolo CAN.
 * BootCrc32_Update(0, ...) da o mesmo que zlib.crc32() e encadeia como ele:
 * crc = BootCrc32_Update(crc, mais_dados, n).
 */

#ifndef BOOT_HASH_H_
#define BOOT_HASH_H_

#include <stdint.h>

#define BOOT_SHA256_SIZE 32u

typedef struct {
	uint32_t state[8];
	uint32_t length_low;      // bytes processados (o F412 nao precisa de mais de 32 bits, mas o padding usa 64)
	uint32_t length_high;
	uint8_t  block[64];
	uint8_t  used;
} boot_sha256_t;

void BootSha256_Init(boot_sha256_t *sha);
void BootSha256_Update(boot_sha256_t *sha, const void *data, uint32_t length);
void BootSha256_Final(boot_sha256_t *sha, uint8_t digest[BOOT_SHA256_SIZE]);

uint32_t BootCrc32_Update(uint32_t crc, const void *data, uint32_t length);

#endif /* BOOT_HASH_H_ */
