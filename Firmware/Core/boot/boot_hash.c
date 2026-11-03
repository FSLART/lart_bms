/*
 * boot_hash.c
 *
 * Ver boot_hash.h. Versoes pequenas (o bootloader tem 32 KB): o SHA-256 usa
 * 16 palavras rotativas em vez de 64 e o CRC-32 uma tabela de 16 entradas.
 */

#include "boot_hash.h"
#include <string.h>

/* ================== SHA-256 ================== */

static const uint32_t round_constants[64] = {
	0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
	0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
	0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
	0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
	0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
	0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
	0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
	0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t rotate_right(uint32_t value, uint32_t bits) {

	return (value >> bits) | (value << (32u - bits));
}

static void transform(uint32_t state[8], const uint8_t block[64]) {

	uint32_t schedule[16];
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

	for (uint32_t round = 0; round < 64u; round++) {

		uint32_t word;

		if (round < 16u) {
			const uint8_t *bytes = &block[round * 4u];
			word = ((uint32_t) bytes[0] << 24) | ((uint32_t) bytes[1] << 16) | ((uint32_t) bytes[2] << 8) | bytes[3];
		} else {
			// w[i] = w[i-16] + s0(w[i-15]) + w[i-7] + s1(w[i-2]), com 16 palavras rotativas
			uint32_t w15 = schedule[(round + 1u) & 15u];
			uint32_t w2 = schedule[(round + 14u) & 15u];
			uint32_t s0 = rotate_right(w15, 7) ^ rotate_right(w15, 18) ^ (w15 >> 3);
			uint32_t s1 = rotate_right(w2, 17) ^ rotate_right(w2, 19) ^ (w2 >> 10);
			word = schedule[round & 15u] + s0 + schedule[(round + 9u) & 15u] + s1;
		}
		schedule[round & 15u] = word;

		uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
		uint32_t choose = (e & f) ^ (~e & g);
		uint32_t temp1 = h + sum1 + choose + round_constants[round] + word;
		uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temp2 = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

void BootSha256_Init(boot_sha256_t *sha) {

	static const uint32_t initial[8] = {
		0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au, 0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
	};

	memcpy(sha->state, initial, sizeof(initial));
	sha->length_low = 0;
	sha->length_high = 0;
	sha->used = 0;
}

void BootSha256_Update(boot_sha256_t *sha, const void *data, uint32_t length) {

	const uint8_t *bytes = (const uint8_t*) data;

	while (length > 0u) {

		uint32_t chunk = 64u - sha->used;

		if (chunk > length) {
			chunk = length;
		}
		memcpy(&sha->block[sha->used], bytes, chunk);
		sha->used = (uint8_t) (sha->used + chunk);
		bytes += chunk;
		length -= chunk;

		uint32_t before = sha->length_low;
		sha->length_low += chunk;
		if (sha->length_low < before) {
			sha->length_high++;
		}

		if (sha->used == 64u) {
			transform(sha->state, sha->block);
			sha->used = 0;
		}
	}
}

void BootSha256_Final(boot_sha256_t *sha, uint8_t digest[BOOT_SHA256_SIZE]) {

	uint32_t bits_high = (sha->length_high << 3) | (sha->length_low >> 29);
	uint32_t bits_low = sha->length_low << 3;

	sha->block[sha->used++] = 0x80u;
	if (sha->used > 56u) {
		memset(&sha->block[sha->used], 0, 64u - sha->used);
		transform(sha->state, sha->block);
		sha->used = 0;
	}
	memset(&sha->block[sha->used], 0, 56u - sha->used);

	for (uint32_t index = 0; index < 4u; index++) {
		sha->block[56u + index] = (uint8_t) (bits_high >> (24u - (8u * index)));
		sha->block[60u + index] = (uint8_t) (bits_low >> (24u - (8u * index)));
	}
	transform(sha->state, sha->block);

	for (uint32_t word = 0; word < 8u; word++) {
		for (uint32_t index = 0; index < 4u; index++) {
			digest[(word * 4u) + index] = (uint8_t) (sha->state[word] >> (24u - (8u * index)));
		}
	}
}

/* ================== CRC-32 ================== */

uint32_t BootCrc32_Update(uint32_t crc, const void *data, uint32_t length) {

	static const uint32_t nibble_table[16] = {
		0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
		0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
	};
	const uint8_t *bytes = (const uint8_t*) data;

	crc = ~crc;
	while (length-- > 0u) {
		crc ^= *bytes++;
		crc = (crc >> 4) ^ nibble_table[crc & 0x0Fu];
		crc = (crc >> 4) ^ nibble_table[crc & 0x0Fu];
	}
	return ~crc;
}
