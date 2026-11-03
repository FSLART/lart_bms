/*
 * fake_board.c - ver fake_board.h.
 */

#include "fake_board.h"
#include "boot_shared.h"
#include <string.h>
#include <stdio.h>

fake_board_t board;

/* ================== BSP ================== */

void fake_board_reset(void) {

	memset(&board, 0, sizeof(board));
	board.sdc_closed = true;
	board.follow_contactors = true;
	board.eeprom_present = true;
	memset(board.eeprom, 0xFF, sizeof(board.eeprom));
	strcpy(board.rn_name, "RN4871-1234");
	board.isa_pack_present = true;
	board.isa_pack_bus_mV = -1;
	board.isa_handcart_mV = -1;
	fake_can_clear();
	fake_uart_clear(0);
	fake_uart_clear(1);
}

uint8_t Bsp_ReadInputs(void) {

	if (!board.follow_contactors) {
		return board.forced_inputs;
	}

	uint8_t inputs = 0;
	// sem SDC as bobinas nao tem energia: tudo cai
	bool powered = board.sdc_closed;

	if ((powered && board.air_pos) || board.air_pos_welded) {
		inputs |= BSP_IN_AIR_POS;
	}
	if (powered && board.air_neg && !board.air_neg_dead) {
		inputs |= BSP_IN_AIR_NEG;
	}
	if (powered && board.precharge) {
		inputs |= BSP_IN_PRE;
	}
	if (powered && (board.air_neg || board.air_pos)) {
		inputs |= BSP_IN_DISCH;
	}
	if (board.sdc_closed) {
		inputs |= BSP_IN_SDC;
	}
	return inputs;
}

uint8_t Bsp_InputFromPin(uint16_t gpio_pin) {

	(void) gpio_pin;
	return 0;
}

void Bsp_SetContactors(bool air_pos, bool air_neg, bool precharge) {

	board.air_pos = air_pos;
	board.air_neg = air_neg;
	board.precharge = precharge;
}

void Bsp_SetAmsError(bool error) {

	board.ams_error = error;
	board.ams_error_writes++;
}

void Bsp_ToggleHeartbeat(void) {
}

void Bsp_FanInit(uint16_t pwm_hz) {

	(void) pwm_hz;
}

void Bsp_SetFanPwm(uint8_t pwm) {

	board.fan_pwm = pwm;
}

void Bsp_AdcStart(void) {
}

bool Bsp_AdcAverage(uint16_t *temp_raw, uint16_t *vref_raw, uint16_t *current_raw) {

	*temp_raw = 1000;
	*vref_raw = 1500;     // com o VREFINT_CAL a 1500: VDDA = 3,3 V
	*current_raw = 2172;  // ~100 mA no MCS1802 (264 mV/A, zero a VDDA/2)
	return true;
}

uint32_t Bsp_AdcConversions(void) {

	return HAL_GetTick() / 33u;   // um buffer a cada ~33 ms
}

uint16_t Bsp_VrefintCal(void) {

	return 1500;
}

uint16_t Bsp_TsCal1(void) {

	return 940;
}

uint16_t Bsp_TsCal2(void) {

	return 1200;
}

bool Bsp_EepromPresent(void) {

	return board.eeprom_present;
}

bool Bsp_EepromRead(uint16_t address, uint8_t *data, uint16_t length) {

	if (!board.eeprom_present || ((uint32_t) address + length > BSP_EEPROM_SIZE) || ((int32_t) (HAL_GetTick() - board.eeprom_busy_until_ms) < 0)) {
		return false;
	}
	memcpy(data, &board.eeprom[address], length);
	return true;
}

bool Bsp_EepromWritePage(uint16_t address, const uint8_t *data, uint8_t length) {

	// como o 24FC08: sem ACK enquanto a escrita anterior decorre, nada atravessa uma pagina
	if (!board.eeprom_present || board.eeprom_fail_writes || ((int32_t) (HAL_GetTick() - board.eeprom_busy_until_ms) < 0)
			|| (((address % BSP_EEPROM_PAGE) + length) > BSP_EEPROM_PAGE)) {
		return false;
	}
	memcpy(&board.eeprom[address], data, length);
	board.eeprom_busy_until_ms = HAL_GetTick() + 5u;
	board.eeprom_page_writes++;
	if (length > board.eeprom_max_page_bytes) {
		board.eeprom_max_page_bytes = length;
	}
	return true;
}

bool Bsp_EepromReady(void) {

	return board.eeprom_present && ((int32_t) (HAL_GetTick() - board.eeprom_busy_until_ms) >= 0);
}

void Bsp_EepromRecover(void) {
}

void Bsp_SystemReset(void) {

	// como o bsp.c: saidas seguras antes do reset
	Bsp_SetContactors(false, false, false);
	Bsp_SetAmsError(true);
	board.safe_at_reset = board.ams_error && !board.air_pos && !board.air_neg && !board.precharge;
	board.reset_requested = true;
	board.bootloader_requested = (boot_test_shared.request == BOOT_REQUEST_MAGIC);
}

void Bsp_IwdgRefresh(void) {

	board.iwdg_refreshes++;
}

/* ================== FLASH E RAM DO BOOTLOADER ================== */

uint8_t boot_test_flash[BOOT_FLASH_SIZE];
boot_shared_t boot_test_shared;

void fake_flash_erase_all(void) {

	memset(boot_test_flash, 0xFF, sizeof(boot_test_flash));
	memset(&boot_test_shared, 0, sizeof(boot_test_shared));
}

bool BootFlash_EraseSector(uint32_t sector_address) {

	(void) sector_address;
	return false;   // como o bsp.c: a aplicacao nunca apaga
}

bool BootFlash_Program(uint32_t address, const void *data, uint32_t length) {

	const uint8_t *bytes = (const uint8_t*) data;
	uint8_t *cell = &boot_test_flash[address - BOOT_FLASH_BASE];

	if (((address & 3u) != 0u) || ((length & 3u) != 0u) || (address < BOOT_JOURNAL_A_ADDRESS) || ((address + length) > (BOOT_JOURNAL_B_ADDRESS + BOOT_JOURNAL_SIZE))) {
		return false;
	}
	for (uint32_t index = 0; index < length; index++) {
		cell[index] &= bytes[index];   // NOR: so passa bits a 0
	}
	board.flash_programs++;
	return memcmp(cell, data, length) == 0;
}

/* ================== CAN ================== */

#define CAN_LOG 256u
#define CAN_RX  256u

typedef struct {
	uint8_t bus;
	can_frame_t frame;
	uint32_t count;
} sent_entry_t;

static sent_entry_t sent_log[CAN_LOG];
static uint16_t sent_entries;
static struct {
	uint8_t bus;
	can_frame_t frame;
} rx_queue[CAN_RX];
static uint16_t rx_head;
static uint16_t rx_tail;
static uint32_t isa_last_ms[CAN_BUS_COUNT];
static uint8_t isa_phase[CAN_BUS_COUNT];

void fake_can_clear(void) {

	memset(sent_log, 0, sizeof(sent_log));
	sent_entries = 0;
	rx_head = 0;
	rx_tail = 0;
	memset(isa_last_ms, 0, sizeof(isa_last_ms));
	memset(isa_phase, 0, sizeof(isa_phase));
}

void fake_can_inject(uint8_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t dlc) {

	rx_queue[rx_head].bus = bus;
	rx_queue[rx_head].frame.id = id;
	rx_queue[rx_head].frame.extended = extended;
	rx_queue[rx_head].frame.dlc = dlc;
	memset(rx_queue[rx_head].frame.data, 0, 8);
	memcpy(rx_queue[rx_head].frame.data, data, dlc);
	rx_head = (uint16_t) ((rx_head + 1u) % CAN_RX);
}

uint32_t fake_can_sent(uint8_t bus, uint32_t id) {

	for (uint16_t index = 0; index < sent_entries; index++) {
		if ((sent_log[index].bus == bus) && (sent_log[index].frame.id == id)) {
			return sent_log[index].count;
		}
	}
	return 0;
}

bool fake_can_last(uint8_t bus, uint32_t id, can_frame_t *out) {

	for (uint16_t index = 0; index < sent_entries; index++) {
		if ((sent_log[index].bus == bus) && (sent_log[index].frame.id == id)) {
			*out = sent_log[index].frame;
			return true;
		}
	}
	return false;
}

void CanBus_Init(void) {

	bms.can1.started = true;
	bms.can2.started = true;
	bms.can1.kbps = bms.config.can1_kbps;
	bms.can2.kbps = bms.config.can2_kbps;
}

bool CanBus_Send(uint8_t bus, const can_frame_t *frame, bool high_priority) {

	(void) high_priority;

	for (uint16_t index = 0; index < sent_entries; index++) {
		if ((sent_log[index].bus == bus) && (sent_log[index].frame.id == frame->id)) {
			sent_log[index].frame = *frame;
			sent_log[index].count++;
			return true;
		}
	}
	if (sent_entries < CAN_LOG) {
		sent_log[sent_entries].bus = bus;
		sent_log[sent_entries].frame = *frame;
		sent_log[sent_entries].count = 1;
		sent_entries++;
	}
	return true;
}

static void isa_frame(can_frame_t *frame, uint32_t id, int32_t value) {

	memset(frame, 0, sizeof(*frame));
	frame->id = id;
	frame->dlc = 6;
	frame->data[0] = (uint8_t) (id - 0x521u);
	frame->data[2] = (uint8_t) ((uint32_t) value >> 24);
	frame->data[3] = (uint8_t) ((uint32_t) value >> 16);
	frame->data[4] = (uint8_t) ((uint32_t) value >> 8);
	frame->data[5] = (uint8_t) value;
}

// a ISA: corrente e U1 alternados, uma trama a cada 25 ms
static bool isa_receive(uint8_t bus, can_frame_t *frame) {

	bool present = (bus == CAN_BUS_1) ? board.isa_pack_present : board.isa_handcart_present;
	uint32_t now_ms = HAL_GetTick();

	if (!present || ((now_ms - isa_last_ms[bus]) < 25u)) {
		return false;
	}
	isa_last_ms[bus] = now_ms;
	isa_phase[bus] ^= 1u;

	if (isa_phase[bus] != 0u) {
		isa_frame(frame, 0x521u, (bus == CAN_BUS_1) ? board.isa_pack_current_mA : board.isa_handcart_current_mA);
		return true;
	}

	int32_t voltage_mV;
	if (bus == CAN_BUS_1) {
		bool bus_charged = board.sdc_closed && board.air_neg && (board.precharge || board.air_pos);
		voltage_mV = (board.isa_pack_bus_mV >= 0) ? board.isa_pack_bus_mV : (bus_charged ? (int32_t) bms.accu.voltage_sum_mV : 0);
	} else {
		voltage_mV = (board.isa_handcart_mV >= 0) ? board.isa_handcart_mV : (int32_t) bms.accu.voltage_sum_mV;
	}
	isa_frame(frame, 0x522u, voltage_mV);
	return true;
}

bool CanBus_Receive(uint8_t bus, can_frame_t *frame) {

	// a ordem de chegada e global: so entrega a da cabeca se for deste barramento
	if ((rx_tail == rx_head) || (rx_queue[rx_tail].bus != bus)) {
		return isa_receive(bus, frame);
	}
	*frame = rx_queue[rx_tail].frame;
	rx_tail = (uint16_t) ((rx_tail + 1u) % CAN_RX);
	return true;
}

void CanBus_Service(uint32_t now_ms, bool can2_required) {

	(void) now_ms;
	(void) can2_required;
}

uint16_t CanBus_FreeSlots(uint8_t bus, bool high_priority) {

	(void) bus;
	(void) high_priority;
	return 100;
}

/* ================== UART + RN4871 ================== */

#define UART_OUT 65536u
#define UART_IN  4096u

static char output[UART_LINK_COUNT][UART_OUT];
static uint32_t output_length[UART_LINK_COUNT];
static char input[UART_LINK_COUNT][UART_IN];
static uint32_t input_head[UART_LINK_COUNT];
static uint32_t input_tail[UART_LINK_COUNT];
static bool rn_command_mode;
static char rn_line[64];
static uint8_t rn_line_length;

void fake_uart_clear(uint8_t link) {

	output[link][0] = '\0';
	output_length[link] = 0;
	if (link == UART_LINK_2) {
		rn_command_mode = false;
		rn_line_length = 0;
	}
}

void fake_uart_inject(uint8_t link, const char *text) {

	for (; *text != '\0'; text++) {
		input[link][input_head[link] % UART_IN] = *text;
		input_head[link]++;
	}
}

const char* fake_uart_output(uint8_t link) {

	return output[link];
}

static void append_output(uint8_t link, const char *data, uint16_t length) {

	if ((output_length[link] + length + 1u) >= UART_OUT) {
		output_length[link] = 0;   // cheio: recomeca (os testes limpam antes de olhar)
	}
	memcpy(&output[link][output_length[link]], data, length);
	output_length[link] += length;
	output[link][output_length[link]] = '\0';
}

// o RN4871: em modo de comando responde aos comandos, em modo de dados passa tudo
static void rn4871_receive(const char *data, uint16_t length) {

	if (board.rn_dead) {
		return;
	}

	if (!rn_command_mode) {
		if ((length == 3u) && (memcmp(data, "$$$", 3) == 0)) {
			rn_command_mode = true;
			rn_line_length = 0;
			fake_uart_inject(UART_LINK_2, "CMD> ");
			return;
		}
		append_output(UART_LINK_2, data, length);
		return;
	}

	for (uint16_t index = 0; index < length; index++) {

		char character = data[index];

		if (character != '\r') {
			if (rn_line_length < (sizeof(rn_line) - 1u)) {
				rn_line[rn_line_length++] = character;
			}
			rn_line[rn_line_length] = '\0';
			if (strcmp(rn_line, "$$$") == 0) {
				rn_line_length = 0;
				fake_uart_inject(UART_LINK_2, "Err\r\nCMD> ");
			}
			continue;
		}

		rn_line[rn_line_length] = '\0';
		rn_line_length = 0;

		if (strcmp(rn_line, "GN") == 0) {
			char reply[sizeof(board.rn_name) + 16u];
			snprintf(reply, sizeof(reply), "%s\r\nCMD> ", board.rn_name);
			fake_uart_inject(UART_LINK_2, reply);
		} else if (strncmp(rn_line, "SN,", 3) == 0) {
			snprintf(board.rn_name, sizeof(board.rn_name), "%.31s", &rn_line[3]);
			fake_uart_inject(UART_LINK_2, "AOK\r\nCMD> ");
		} else if (strcmp(rn_line, "---") == 0) {
			rn_command_mode = false;
			fake_uart_inject(UART_LINK_2, "END\r\n");
		} else if (strcmp(rn_line, "R,1") == 0) {
			rn_command_mode = false;
			fake_uart_inject(UART_LINK_2, "Rebooting\r\n%REBOOT%");
		} else {
			fake_uart_inject(UART_LINK_2, "Err\r\nCMD> ");
		}
	}
}

void UartLink_Init(void) {

	for (uint8_t link = 0; link < UART_LINK_COUNT; link++) {
		input_head[link] = 0;
		input_tail[link] = 0;
	}
	rn_command_mode = false;
	memset(&bms.uart1, 0, sizeof(bms.uart1));
	memset(&bms.uart2, 0, sizeof(bms.uart2));
}

bool UartLink_Write(uint8_t link, const void *data, uint16_t length) {

	if (link == UART_LINK_2) {
		rn4871_receive((const char*) data, length);
	} else {
		append_output(link, (const char*) data, length);
	}
	((link == UART_LINK_2) ? &bms.uart2 : &bms.uart1)->frames_sent++;
	return true;
}

uint16_t UartLink_Free(uint8_t link) {

	(void) link;
	return 8191;
}

bool UartLink_TxIdle(uint8_t link) {

	(void) link;
	return true;
}

uint16_t UartLink_Read(uint8_t link, uint8_t *out, uint16_t max_length) {

	uint16_t count = 0;

	while ((count < max_length) && (input_tail[link] != input_head[link])) {
		out[count++] = (uint8_t) input[link][input_tail[link] % UART_IN];
		input_tail[link]++;
	}
	return count;
}

void UartLink_Service(uint32_t now_ms) {

	(void) now_ms;
}
