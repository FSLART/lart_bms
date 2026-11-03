/*
 * fake_hal.h - o controlo do HAL falso (Tests/hal/stm32f4xx_hal.h) nos
 * testes dos drivers: o que os drivers chamaram e o que o "hardware" faz.
 */

#ifndef FAKE_HAL_H
#define FAKE_HAL_H

#include "stm32f4xx_hal.h"

/* ================== tempo e sistema ================== */

extern uint32_t fake_hal_tick;
extern uint32_t fake_hal_pclk1_hz;
extern uint32_t fake_hal_resets;          // NVIC_SystemReset
extern uint32_t fake_hal_irq_disables;
extern int fake_hal_primask;              // 1 = interrupcoes desligadas

/* ================== GPIO: registo de escritas ================== */

#define FAKE_GPIO_LOG 64u

typedef struct {
	char port;
	uint16_t pin;
	GPIO_PinState state;
	uint32_t sequence;                   // ordem global (para comparar com o reset)
} fake_gpio_write_t;

extern fake_gpio_write_t fake_gpio_log[FAKE_GPIO_LOG];
extern uint32_t fake_gpio_log_count;
extern uint32_t fake_sequence;           // conta tudo o que fica registado
extern uint32_t fake_reset_sequence;     // o fake_sequence no NVIC_SystemReset

/* ================== CAN ================== */

#define FAKE_CAN_FILTERS 32u
#define FAKE_CAN_RX      128u
#define FAKE_CAN_TX_LOG  256u

typedef struct {
	CAN_TxHeaderTypeDef header;
	uint8_t data[8];
	uint32_t mailbox;
} fake_can_tx_t;

typedef struct {
	CAN_RxHeaderTypeDef header;
	uint8_t data[8];
} fake_can_rx_t;

typedef struct {
	uint32_t init_calls;
	CAN_InitTypeDef init;                 // o ultimo HAL_CAN_Init
	HAL_StatusTypeDef start_result;
	uint32_t start_calls;
	uint32_t notifications;               // mascara do ActivateNotification
	CAN_FilterTypeDef filters[FAKE_CAN_FILTERS];
	uint32_t filter_count;
	// TX: 3 mailboxes; ocupadas ate o teste as libertar (ou auto_complete)
	bool mailbox_busy[3];
	bool auto_complete;                   // as tramas saem logo (bus saudavel)
	HAL_StatusTypeDef add_result;
	fake_can_tx_t tx_log[FAKE_CAN_TX_LOG];
	uint32_t tx_count;
	uint32_t abort_calls;
	uint32_t abort_mask;
	// RX: FIFO 0
	fake_can_rx_t rx[FAKE_CAN_RX];
	uint32_t rx_head;
	uint32_t rx_tail;
	HAL_StatusTypeDef get_result;
} fake_can_t;

extern fake_can_t fake_can[2];           // [0] = hcan1, [1] = hcan2

void fake_can_rx_push(uint8_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t dlc);
void fake_can_complete(uint8_t bus);     // as mailboxes ocupadas acabam de sair

/* ================== UART ================== */

typedef struct {
	HAL_StatusTypeDef tx_result;
	const uint8_t *tx_data;               // o bloco do DMA em curso
	uint16_t tx_size;
	uint32_t tx_calls;
	uint8_t sent[16384];                  // o que ja saiu (blocos completos)
	uint32_t sent_length;
	uint32_t abort_calls;
	uint8_t *rx_buffer;                   // o ReceiveToIdle armado
	uint16_t rx_size;
	uint32_t rx_arms;
} fake_uart_t;

extern fake_uart_t fake_uart[2];         // [0] = huart1, [1] = huart2

// o DMA acaba o bloco em curso (copia-o para sent[] e chama o TxCplt)
void fake_uart_dma_done(uint8_t index);
// chegam bytes: copia para o buffer armado e chama o RxEvent
void fake_uart_receive(uint8_t index, const char *text, uint16_t length);

/* ================== I2C: a 24FC08 ================== */

typedef struct {
	uint8_t memory[1024];
	bool present;
	bool fail_reads;
	bool fail_writes;
	uint32_t reads;
	uint32_t writes;
	uint32_t ready_calls;
	uint32_t init_calls;
	uint32_t deinit_calls;
	uint16_t last_device;
	uint16_t last_address;
	uint16_t last_size;
} fake_eeprom_t;

extern fake_eeprom_t fake_eeprom;

/* ================== ADC, TIM ================== */

extern uint16_t *fake_adc_dma_buffer;
extern uint32_t fake_adc_dma_length;
extern uint32_t fake_tim_base_starts;
extern uint32_t fake_pwm_starts;
extern uint32_t fake_pwm_stops;

/* ================== FLASH ================== */

typedef struct {
	bool locked;
	bool fail_unlock;
	int32_t fail_program_at;              // falha no n-esimo HAL_FLASH_Program (-1 = nunca)
	uint32_t stuck_mask;                  // bits que nunca passam a 0 (flash estragada)
	uint32_t programs;
	uint32_t unlocks;
	uint32_t locks;
	uint32_t cleared_flags;
	char cache_events[8];                 // a ordem das operacoes na cache
	uint32_t cache_event_count;
} fake_flash_t;

extern fake_flash_t fake_flash;

/* memoria nos enderecos verdadeiros (Linux): a flash em 0x08000000 e as
 * calibracoes de fabrica em 0x1FFF7A2A. Devolve false se nao der */
bool fake_hal_map_memory(void);

void fake_hal_reset(void);

#endif /* FAKE_HAL_H */
