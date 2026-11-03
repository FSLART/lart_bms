/*
 * fake_board.h - a placa master simulada, para os testes no PC.
 *
 * Substitui o bsp.c (pinos, PWM, ADC, EEPROM 24FC08), o can_bus.c e o
 * uart_link.c. O resto (brain.c e todos os modulos) e o codigo real. O UART2
 * responde como um RN4871 ("$$$" -> "CMD> ", "GN", "SN", "---", "R,1").
 */

#ifndef FAKE_BOARD_H
#define FAKE_BOARD_H

#include "brain.h"
#include "bsp.h"
#include "can_bus.h"
#include "uart_link.h"

typedef struct {
	// entradas
	bool     sdc_closed;               // shutdown circuit fechado
	bool     follow_contactors;        // os feedbacks seguem as saidas (contactores bons)
	bool     air_pos_welded;           // AIR+ fica fechado
	bool     air_neg_dead;             // AIR- nunca fecha
	uint8_t  forced_inputs;            // sem follow_contactors: o nivel dos pinos (BSP_IN_*)
	// saidas, como ficaram nos pinos
	bool     air_pos;
	bool     air_neg;
	bool     precharge;
	bool     ams_error;
	uint8_t  fan_pwm;
	uint32_t ams_error_writes;
	// EEPROM
	uint8_t  eeprom[BSP_EEPROM_SIZE];
	bool     eeprom_present;
	bool     eeprom_fail_writes;
	bool     eeprom_fail_reads;
	bool     eeprom_stuck_busy;         // nunca acaba a escrita interna (sem ACK para sempre)
	bool     eeprom_corrupt_writes;     // grava outra coisa (a verificacao tem de apanhar)
	uint32_t eeprom_recoveries;         // Bsp_EepromRecover
	uint32_t eeprom_busy_until_ms;     // escrita interna (tWC) a decorrer
	uint32_t eeprom_page_writes;
	uint32_t eeprom_max_page_bytes;
	// sistema
	bool     reset_requested;
	bool     bootloader_requested;     // reset com o pedido do bootloader na RAM
	bool     safe_at_reset;            // contactores abertos e AMS_ERROR em erro no instante do reset
	uint32_t iwdg_refreshes;
	uint32_t flash_programs;           // escritas no journal do bootloader
	// RN4871
	bool     rn_dead;                  // nao responde a nada
	bool     rn_silent;                // entra em modo de comando mas nao responde aos comandos
	bool     rn_sn_error;              // responde Err ao SN
	char     rn_name[32];
	// ADC da placa (medias por canal) e o DMA
	uint16_t adc_temp_raw;
	uint16_t adc_vref_raw;             // 0 = sem medida (Bsp_AdcAverage = false)
	uint16_t adc_current_raw;
	bool     adc_stalled;              // o DMA parou: o contador de buffers nao anda
	// UART
	bool     uart_tx_busy[2];          // o DMA nunca acaba (UartLink_TxIdle = false)
	bool     uart_reject[2];           // o ring de TX esta cheio (UartLink_Write = false)
	// ISA IVT-S: tramas de corrente e U1 a cada 25 ms enquanto presentes
	bool     isa_pack_present;
	int32_t  isa_pack_current_mA;
	int32_t  isa_pack_bus_mV;          // < 0: segue os contactores (soma das celulas com AIR- e PRE/AIR+ fechados)
	bool     isa_handcart_present;
	int32_t  isa_handcart_current_mA;
	int32_t  isa_handcart_mV;          // < 0: a soma das celulas
} fake_board_t;

extern fake_board_t board;

void fake_board_reset(void);                 // placa saudavel, EEPROM vazia (0xFF)

// CAN
void fake_can_inject(uint8_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t dlc);
uint32_t fake_can_sent(uint8_t bus, uint32_t id);              // tramas enviadas com esse ID
bool fake_can_last(uint8_t bus, uint32_t id, can_frame_t *out);
void fake_can_clear(void);

/* Flash do bootloader (boot_test_flash, 512 KB) e a area partilhada na RAM
 * (boot_test_shared): o fake_board_reset nao lhes toca, como um reset nao
 * apaga a flash nem os ultimos 32 B da RAM */
void fake_flash_erase_all(void);             // flash toda a 0xFF e a area partilhada a zeros

// UART (link 0 = UART1, 1 = UART2/RN4871 em modo de dados)
void fake_uart_inject(uint8_t link, const char *text);
const char* fake_uart_output(uint8_t link);                     // tudo o que saiu desde o ultimo clear
void fake_uart_clear(uint8_t link);

#endif
