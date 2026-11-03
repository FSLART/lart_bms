/*
 * fake_spi_chain.c - ver fake_spi_chain.h. Faz de mcuWrapper (SPI, CS,
 * acordar, timer) para o driver da ADI.
 */

#include "fake_spi_chain.h"
#include <string.h>

spi_chain_t spi_chain;

// estado da transacao em curso (entre CS em baixo e CS em cima)
static uint8_t frame[4u + (SPI_CHAIN_MAX * SPI_REG_MAX)];
static uint16_t frame_length;

/* ================== PEC do datasheet ================== */

// PEC15: x^15 + x^14 + x^10 + x^8 + x^7 + x^4 + x^3 + 1, semente 16, o resultado com um 0 no fim
uint16_t ref_pec15(const uint8_t *data, uint8_t length) {

	uint16_t pec = 16u;

	for (uint8_t index = 0; index < length; index++) {
		for (int bit = 7; bit >= 0; bit--) {
			uint16_t in = (uint16_t) (((data[index] >> bit) & 1u) ^ ((pec >> 14) & 1u));
			pec = (uint16_t) ((pec << 1) & 0x7FFFu);
			if (in != 0u) {
				pec ^= 0x4599u;
			}
		}
	}
	return (uint16_t) (pec << 1);
}

// PEC10: x^10 + x^7 + x^3 + x^2 + x + 1, semente 16, sobre os dados e depois os 6 bits do contador (0 nas escritas)
uint16_t ref_pec10(const uint8_t *data, uint8_t length, uint8_t counter6) {

	uint16_t pec = 16u;

	for (uint16_t bit_index = 0; bit_index < ((uint16_t) length * 8u) + 6u; bit_index++) {

		uint8_t bit = (bit_index < ((uint16_t) length * 8u)) ? (uint8_t) ((data[bit_index / 8u] >> (7u - (bit_index % 8u))) & 1u)
				: (uint8_t) ((counter6 >> (5u - (bit_index - ((uint16_t) length * 8u)))) & 1u);
		uint16_t in = (uint16_t) (bit ^ ((pec >> 9) & 1u));

		pec = (uint16_t) ((pec << 1) & 0x3FFu);
		if (in != 0u) {
			pec ^= 0x08Fu;
		}
	}
	return pec;
}

/* ================== registos ================== */

uint8_t spi_chain_read_size(uint16_t command) {

	switch (command) {
	case 0x00C:   // RDCVALL
	case 0x010:   // RDSALL
	case 0x04C:   // RDACALL
	case 0x018:   // RDFCALL
		return 34u;
	case 0x011:   // RDCSALL
	case 0x051:   // RDACSALL
		return 66u;
	case 0x035:   // RDASALL
		return 70u;
	default:
		return 8u;
	}
}

// a escrita de um comando WR* fica no registo que o RD* correspondente le
static uint16_t read_slot_of_write(uint16_t command) {

	switch (command) {
	case 0x001:   // WRCFGA -> RDCFGA
		return 0x002;
	case 0x024:   // WRCFGB -> RDCFGB
		return 0x026;
	case 0x721:   // WRCOMM -> RDCOMM
		return 0x722;
	case 0x020:   // WRPWMA -> RDPWMA
		return 0x022;
	case 0x021:   // WRPWMB -> RDPWMB
		return 0x023;
	default:
		return command;   // CLRFLAG e afins: fica no proprio codigo para o teste ver
	}
}

void spi_chain_reset(uint8_t count) {

	memset(&spi_chain, 0, sizeof(spi_chain));
	spi_chain.count = count;
	frame_length = 0;
}

/* ================== mcuWrapper ================== */

void adBmsCsLow(void) {

	spi_chain.cs_low = true;
	spi_chain.transactions++;
	frame_length = 0;
}

static bool command_ok(uint16_t *command) {

	if (frame_length < 4u) {
		return false;
	}
	if (ref_pec15(frame, 2) != (uint16_t) ((frame[2] << 8) | frame[3])) {
		spi_chain.bad_command_pec++;
		return false;
	}
	*command = (uint16_t) (((frame[0] & 0x07u) << 8) | frame[1]);
	return true;
}

void adBmsCsHigh(void) {

	uint16_t command;

	spi_chain.cs_low = false;
	if (!command_ok(&command)) {
		return;
	}
	spi_chain.last_command = command;
	spi_chain.commands++;

	// escrita: blocos de 8 bytes, o primeiro para o ultimo slave
	uint16_t data_bytes = (uint16_t) (frame_length - 4u);

	if ((data_bytes > 0u) && ((data_bytes % 8u) == 0u)) {
		uint8_t blocks = (uint8_t) (data_bytes / 8u);

		for (uint8_t block = 0; (block < blocks) && (block < spi_chain.count); block++) {

			const uint8_t *chunk = &frame[4u + (block * 8u)];
			spi_device_t *device = &spi_chain.devices[spi_chain.count - 1u - block];
			uint16_t pec = (uint16_t) (((chunk[6] & 0x03u) << 8) | chunk[7]);

			if (((chunk[6] >> 2) != 0u) || (ref_pec10(chunk, 6, 0) != pec)) {
				spi_chain.bad_data_pec++;
				continue;
			}
			memcpy(device->regs[read_slot_of_write(command)], chunk, 6);
		}
	}

	// cada comando aceite conta no contador de cada slave (as leituras nao contam)
	for (uint8_t index = 0; index < spi_chain.count; index++) {
		spi_chain.devices[index].cc = (uint8_t) ((spi_chain.devices[index].cc + 1u) & 0x3Fu);
	}
}

void spiWriteBytes(uint16_t size, uint8_t *tx_data) {

	uint16_t room = (uint16_t) (sizeof(frame) - frame_length);
	uint16_t count = (size < room) ? size : room;

	memcpy(&frame[frame_length], tx_data, count);
	frame_length = (uint16_t) (frame_length + count);
}

// comando de 4 bytes e a resposta de toda a cadeia (o slave mais perto primeiro)
void spiWriteReadBytes(uint8_t *tx_data, uint8_t *rx_data, uint16_t size) {

	uint16_t command;

	memcpy(frame, tx_data, 4);
	frame_length = 4;
	if (spi_chain.rx_fail) {
		frame_length = 0;
		return;
	}
	memset(rx_data, 0xFF, size);   // ninguem a responder = 0xFF no SDO
	if (!command_ok(&command)) {
		frame_length = 0;
		return;
	}

	uint8_t per_device = spi_chain_read_size(command);
	uint8_t data_length = (uint8_t) (per_device - 2u);

	for (uint8_t index = 0; (index < spi_chain.count) && ((uint16_t) ((index + 1u) * per_device) <= size); index++) {

		spi_device_t *device = &spi_chain.devices[index];
		uint8_t *out = &rx_data[index * per_device];
		uint16_t pec = ref_pec10(device->regs[command], data_length, device->cc);

		memcpy(out, device->regs[command], data_length);
		out[data_length] = (uint8_t) ((device->cc << 2) | ((pec >> 8) & 0x03u));
		out[data_length + 1u] = (uint8_t) pec;
		if (device->corrupt_next_read) {
			out[1] ^= 0x10u;
			device->corrupt_next_read = false;
		}
	}
	spi_chain.last_command = command;
	frame_length = 0;   // as leituras nao passam pelo commit do CS em cima
}

void spiReadBytes(uint16_t size, uint8_t *rx_data) {

	memset(rx_data, 0xFF, size);   // o PLADC acaba quando o SDO vai a 1
}

void adBmsWakeupIc(uint8_t total_ic) {

	(void) total_ic;
	spi_chain.wakeups++;
}

void startTimer(void) {
}

void stopTimer(void) {
}

uint32_t getTimCount(void) {

	return spi_chain.timer_count;
}
