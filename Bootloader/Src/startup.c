/*
 * startup.c
 *
 * Tabela de vetores (so as 16 excecoes do Cortex-M4: o bootloader nao usa
 * interrupcoes) e o Reset_Handler: copia o .data, limpa o .bss e chama o
 * main. Uma falha (HardFault e companhia) poe as saidas seguras e reinicia.
 */

#include "stm32f4xx.h"
#include "bl_hw.h"

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

int main(void);

void Reset_Handler(void) __attribute__((noreturn));
void Fault_Handler(void) __attribute__((noreturn));

void Reset_Handler(void) {

	uint32_t *source = &_sidata;

	for (uint32_t *destination = &_sdata; destination < &_edata; destination++) {
		*destination = *source++;
	}
	for (uint32_t *destination = &_sbss; destination < &_ebss; destination++) {
		*destination = 0u;
	}

	(void) main();

	for (;;) {
	}
}

void Fault_Handler(void) {

	BlHw_SafeOutputs();
	NVIC_SystemReset();
	for (;;) {
	}
}

__attribute__((section(".isr_vector"), used)) static void (*const vectors[16])(void) = {
	(void (*)(void)) &_estack,
	Reset_Handler,
	Fault_Handler,   // NMI
	Fault_Handler,   // HardFault
	Fault_Handler,   // MemManage
	Fault_Handler,   // BusFault
	Fault_Handler,   // UsageFault
	0, 0, 0, 0,
	Fault_Handler,   // SVC
	Fault_Handler,   // DebugMon
	0,
	Fault_Handler,   // PendSV
	Fault_Handler    // SysTick
};
