/*
 * fake_hal.c - ver fake_hal.h. O comportamento e o do HAL e do hardware
 * naquilo que os drivers usam: 3 mailboxes de CAN e a FIFO 0, o DMA do UART
 * em blocos, a 24FC08 com os 4 blocos de 256 B, a flash NOR (so passa bits a
 * 0, e so desbloqueada).
 */

#define _GNU_SOURCE
#include "fake_hal.h"
#include <stdio.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

GPIO_TypeDef fake_gpioa = { 'A', 0, 0 }, fake_gpiob = { 'B', 0, 0 }, fake_gpioc = { 'C', 0, 0 }, fake_gpioh = { 'H', 0, 0 };
CAN_TypeDef fake_can1_regs, fake_can2_regs;
ADC_TypeDef fake_adc1_regs = { 1 }, fake_adc2_regs = { 2 };
IWDG_TypeDef fake_iwdg_regs;

uint32_t fake_hal_tick;
uint32_t fake_hal_pclk1_hz = 32000000u;
uint32_t fake_hal_resets;
uint32_t fake_hal_irq_disables;
int fake_hal_primask;

fake_gpio_write_t fake_gpio_log[FAKE_GPIO_LOG];
uint32_t fake_gpio_log_count;
uint32_t fake_sequence;
uint32_t fake_reset_sequence;

fake_can_t fake_can[2];
fake_uart_t fake_uart[2];
fake_eeprom_t fake_eeprom;
fake_flash_t fake_flash;

uint16_t *fake_adc_dma_buffer;
uint32_t fake_adc_dma_length;
uint32_t fake_tim_base_starts;
uint32_t fake_pwm_starts;
uint32_t fake_pwm_stops;

// os handles do CubeMX (main.c)
CAN_HandleTypeDef hcan1 = { .Instance = CAN1 };
CAN_HandleTypeDef hcan2 = { .Instance = CAN2 };
UART_HandleTypeDef huart1 = { .id = 1, .gState = HAL_UART_STATE_READY, .RxState = HAL_UART_STATE_READY };
UART_HandleTypeDef huart2 = { .id = 2, .gState = HAL_UART_STATE_READY, .RxState = HAL_UART_STATE_READY };
I2C_HandleTypeDef hi2c1 = { 1 };
ADC_HandleTypeDef hadc1 = { ADC1 };
static TIM_TypeDef tim3_regs, tim12_regs;
TIM_HandleTypeDef htim3 = { .Instance = &tim3_regs };
TIM_HandleTypeDef htim12 = { .Instance = &tim12_regs };

void fake_hal_reset(void) {

	fake_hal_tick = 0;
	fake_hal_pclk1_hz = 32000000u;
	fake_hal_resets = 0;
	fake_hal_irq_disables = 0;
	fake_hal_primask = 0;
	fake_gpio_log_count = 0;
	fake_sequence = 0;
	fake_reset_sequence = 0;
	fake_gpioa.IDR = fake_gpioa.ODR = 0;
	fake_gpiob.IDR = fake_gpiob.ODR = 0;
	fake_gpioc.IDR = fake_gpioc.ODR = 0;
	fake_gpioh.IDR = fake_gpioh.ODR = 0;
	memset(fake_can, 0, sizeof(fake_can));
	for (uint8_t bus = 0; bus < 2u; bus++) {
		fake_can[bus].auto_complete = true;
	}
	fake_can1_regs.ESR = fake_can2_regs.ESR = 0;
	memset(fake_uart, 0, sizeof(fake_uart));
	huart1.gState = huart1.RxState = HAL_UART_STATE_READY;
	huart2.gState = huart2.RxState = HAL_UART_STATE_READY;
	memset(&fake_eeprom, 0, sizeof(fake_eeprom));
	memset(fake_eeprom.memory, 0xFF, sizeof(fake_eeprom.memory));
	fake_eeprom.present = true;
	memset(&fake_flash, 0, sizeof(fake_flash));
	fake_flash.locked = true;
	fake_flash.fail_program_at = -1;
	fake_adc_dma_buffer = NULL;
	fake_adc_dma_length = 0;
	fake_tim_base_starts = 0;
	fake_pwm_starts = 0;
	fake_pwm_stops = 0;
	memset(&tim3_regs, 0, sizeof(tim3_regs));
	memset(&tim12_regs, 0, sizeof(tim12_regs));
	fake_iwdg_regs.KR = 0;
}

/* ================== sistema ================== */

uint32_t HAL_GetTick(void) {

	return fake_hal_tick;
}

uint32_t HAL_RCC_GetPCLK1Freq(void) {

	return fake_hal_pclk1_hz;
}

void NVIC_SystemReset(void) {

	fake_hal_resets++;
	fake_reset_sequence = ++fake_sequence;
}

void __DMB(void) {
}

uint32_t __get_PRIMASK(void) {

	return (uint32_t) fake_hal_primask;
}

void __disable_irq(void) {

	fake_hal_irq_disables++;
	fake_hal_primask = 1;
}

void __set_PRIMASK(uint32_t primask) {

	fake_hal_primask = (int) primask;
}

/* ================== GPIO ================== */

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {

	if (state == GPIO_PIN_SET) {
		port->ODR |= pin;
	} else {
		port->ODR &= ~(uint32_t) pin;
	}
	if (fake_gpio_log_count < FAKE_GPIO_LOG) {
		fake_gpio_log[fake_gpio_log_count++] = (fake_gpio_write_t) { port->name, pin, state, ++fake_sequence };
	}
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {

	return ((port->IDR & pin) != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin) {

	port->ODR ^= pin;
}

/* ================== CAN ================== */

static fake_can_t* can_of(const CAN_HandleTypeDef *hcan) {

	return &fake_can[(hcan->Instance == CAN1) ? 0 : 1];
}

HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *hcan) {

	fake_can_t *can = can_of(hcan);

	can->init_calls++;
	can->init = hcan->Init;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, const CAN_FilterTypeDef *filter) {

	fake_can_t *can = can_of(hcan);

	if (can->filter_count < FAKE_CAN_FILTERS) {
		can->filters[can->filter_count++] = *filter;
	}
	return HAL_OK;
}

HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan) {

	fake_can_t *can = can_of(hcan);

	can->start_calls++;
	return can->start_result;
}

HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t interrupts) {

	can_of(hcan)->notifications |= interrupts;
	return HAL_OK;
}

uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *hcan) {

	const fake_can_t *can = can_of(hcan);
	uint32_t free = 0;

	for (uint8_t box = 0; box < 3u; box++) {
		free += can->mailbox_busy[box] ? 0u : 1u;
	}
	return free;
}

HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *header, const uint8_t data[], uint32_t *mailbox) {

	fake_can_t *can = can_of(hcan);

	if (can->add_result != HAL_OK) {
		return can->add_result;
	}
	for (uint8_t box = 0; box < 3u; box++) {
		if (!can->mailbox_busy[box]) {
			if (can->tx_count < FAKE_CAN_TX_LOG) {
				fake_can_tx_t *entry = &can->tx_log[can->tx_count++];
				entry->header = *header;
				memcpy(entry->data, data, (header->DLC <= 8u) ? header->DLC : 8u);
				entry->mailbox = 1u << box;
			}
			can->mailbox_busy[box] = !can->auto_complete;
			*mailbox = 1u << box;
			return HAL_OK;
		}
	}
	return HAL_ERROR;   // sem mailbox livre (o HAL real tambem)
}

HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *hcan, uint32_t mailboxes) {

	fake_can_t *can = can_of(hcan);

	can->abort_calls++;
	can->abort_mask = mailboxes;
	for (uint8_t box = 0; box < 3u; box++) {
		if ((mailboxes & (1u << box)) != 0u) {
			can->mailbox_busy[box] = false;
		}
	}
	return HAL_OK;
}

void fake_can_complete(uint8_t bus) {

	memset(fake_can[bus].mailbox_busy, 0, sizeof(fake_can[bus].mailbox_busy));
}

void fake_can_rx_push(uint8_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t dlc) {

	fake_can_t *can = &fake_can[bus];
	fake_can_rx_t *entry = &can->rx[can->rx_head % FAKE_CAN_RX];

	memset(entry, 0, sizeof(*entry));
	entry->header.IDE = extended ? CAN_ID_EXT : CAN_ID_STD;
	entry->header.StdId = extended ? 0u : id;
	entry->header.ExtId = extended ? id : 0u;
	entry->header.DLC = dlc;
	memcpy(entry->data, data, (dlc <= 8u) ? dlc : 8u);
	can->rx_head++;
}

uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t fifo) {

	const fake_can_t *can = can_of(hcan);

	(void) fifo;
	return can->rx_head - can->rx_tail;
}

HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t data[]) {

	fake_can_t *can = can_of(hcan);

	(void) fifo;
	if ((can->get_result != HAL_OK) || (can->rx_tail == can->rx_head)) {
		return HAL_ERROR;
	}

	const fake_can_rx_t *entry = &can->rx[can->rx_tail % FAKE_CAN_RX];

	*header = entry->header;
	memcpy(data, entry->data, 8);
	can->rx_tail++;
	return HAL_OK;
}

/* ================== UART ================== */

static fake_uart_t* uart_of(const UART_HandleTypeDef *huart) {

	return &fake_uart[(huart->id == 1) ? 0 : 1];
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size) {

	fake_uart_t *uart = uart_of(huart);

	uart->tx_calls++;
	if (uart->tx_result != HAL_OK) {
		return uart->tx_result;
	}
	uart->tx_data = data;
	uart->tx_size = size;
	huart->gState = HAL_UART_STATE_BUSY_TX;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *huart) {

	fake_uart_t *uart = uart_of(huart);

	uart->abort_calls++;
	uart->tx_data = NULL;
	uart->tx_size = 0;
	huart->gState = HAL_UART_STATE_READY;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size) {

	fake_uart_t *uart = uart_of(huart);

	uart->rx_buffer = data;
	uart->rx_size = size;
	uart->rx_arms++;
	huart->RxState = HAL_UART_STATE_BUSY_RX;
	return HAL_OK;
}

void fake_uart_dma_done(uint8_t index) {

	fake_uart_t *uart = &fake_uart[index];
	UART_HandleTypeDef *huart = (index == 0u) ? &huart1 : &huart2;

	if (uart->tx_data != NULL) {
		if ((uart->sent_length + uart->tx_size) <= sizeof(uart->sent)) {
			memcpy(&uart->sent[uart->sent_length], uart->tx_data, uart->tx_size);
			uart->sent_length += uart->tx_size;
		}
		uart->tx_data = NULL;
		uart->tx_size = 0;
	}
	huart->gState = HAL_UART_STATE_READY;
	HAL_UART_TxCpltCallback(huart);
}

void fake_uart_receive(uint8_t index, const char *text, uint16_t length) {

	fake_uart_t *uart = &fake_uart[index];
	UART_HandleTypeDef *huart = (index == 0u) ? &huart1 : &huart2;
	uint16_t count = (length < uart->rx_size) ? length : uart->rx_size;

	if (uart->rx_buffer != NULL) {
		memcpy(uart->rx_buffer, text, count);
	}
	huart->RxState = HAL_UART_STATE_READY;   // no modo IT a rececao acaba a cada evento
	HAL_UARTEx_RxEventCallback(huart, count);
}

/* ================== I2C: 24FC08 ================== */

static bool eeprom_address(uint16_t device, uint16_t address, uint16_t *out) {

	if ((device & 0xF0u) != 0xA0u) {
		return false;
	}
	*out = (uint16_t) ((((device >> 1) & 0x03u) << 8) | (address & 0xFFu));
	return true;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t device, uint16_t address, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout) {

	uint16_t linear;

	(void) hi2c;
	(void) address_size;
	(void) timeout;
	fake_eeprom.reads++;
	fake_eeprom.last_device = device;
	fake_eeprom.last_address = address;
	fake_eeprom.last_size = size;
	if (!fake_eeprom.present || fake_eeprom.fail_reads || !eeprom_address(device, address, &linear)) {
		return HAL_ERROR;
	}
	// a leitura sequencial do 24FC08 da a volta dentro do bloco de 256 B
	for (uint16_t index = 0; index < size; index++) {
		data[index] = fake_eeprom.memory[(linear & 0x300u) | ((linear + index) & 0xFFu)];
	}
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t device, uint16_t address, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout) {

	uint16_t linear;

	(void) hi2c;
	(void) address_size;
	(void) timeout;
	fake_eeprom.writes++;
	fake_eeprom.last_device = device;
	fake_eeprom.last_address = address;
	fake_eeprom.last_size = size;
	if (!fake_eeprom.present || fake_eeprom.fail_writes || !eeprom_address(device, address, &linear)) {
		return HAL_ERROR;
	}
	// a escrita de pagina da a volta dentro da pagina de 16 B
	for (uint16_t index = 0; index < size; index++) {
		fake_eeprom.memory[(linear & ~0x0Fu) | ((linear + index) & 0x0Fu)] = data[index];
	}
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t device, uint32_t trials, uint32_t timeout) {

	(void) hi2c;
	(void) trials;
	(void) timeout;
	fake_eeprom.ready_calls++;
	fake_eeprom.last_device = device;
	return fake_eeprom.present ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c) {

	(void) hi2c;
	fake_eeprom.init_calls++;
	fake_sequence++;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *hi2c) {

	(void) hi2c;
	fake_eeprom.deinit_calls++;
	fake_sequence++;
	return HAL_OK;
}

/* ================== ADC, TIM ================== */

HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc, uint32_t *data, uint32_t length) {

	(void) hadc;
	fake_adc_dma_buffer = (uint16_t*) data;   // o DMA do ADC escreve meias palavras
	fake_adc_dma_length = length;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim) {

	(void) htim;
	fake_tim_base_starts++;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel) {

	(void) htim;
	(void) channel;
	fake_pwm_starts++;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim, uint32_t channel) {

	(void) htim;
	(void) channel;
	fake_pwm_stops++;
	return HAL_OK;
}

/* ================== FLASH ================== */

HAL_StatusTypeDef HAL_FLASH_Unlock(void) {

	fake_flash.unlocks++;
	if (fake_flash.fail_unlock) {
		return HAL_ERROR;
	}
	fake_flash.locked = false;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void) {

	fake_flash.locks++;
	fake_flash.locked = true;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data) {

	uint32_t index = fake_flash.programs++;

	if (fake_flash.locked || (type != FLASH_TYPEPROGRAM_WORD) || ((int32_t) index == fake_flash.fail_program_at)) {
		return HAL_ERROR;
	}

	volatile uint32_t *cell = (volatile uint32_t*) (uintptr_t) address;

	*cell &= ((uint32_t) data) | fake_flash.stuck_mask;   // NOR: so passa bits a 0
	return HAL_OK;
}

void fake_flash_clear_flags(uint32_t flags) {

	fake_flash.cleared_flags |= flags;
}

void fake_flash_cache_event(char event) {

	if (fake_flash.cache_event_count < sizeof(fake_flash.cache_events)) {
		fake_flash.cache_events[fake_flash.cache_event_count++] = event;
	}
}

/* ================== memoria nos enderecos do STM32 ================== */

static bool map_at(uintptr_t address, size_t size) {

	void *wanted = (void*) address;
	void *got = mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

	return got == wanted;
}

bool fake_hal_map_memory(void) {

	static int mapped = -1;

	if (mapped < 0) {
		mapped = map_at(0x08000000u, 0x80000u) && map_at(0x1FFF7000u, 0x1000u);
		if (mapped) {
			memset((void*) (uintptr_t) 0x08000000u, 0xFF, 0x80000u);
		}
	}
	return mapped != 0;
}
