/*
 * core_cm4.h (falso) - o nucleo Cortex-M4 para compilar o bootloader no PC
 * (test_boot_hw.c). O stm32f412rx.h verdadeiro inclui "core_cm4.h"; com
 * Tests/stm32 antes no include path apanha este, que tem so o que o
 * bootloader usa e nenhum assembly ARM:
 *
 *   - __IO e companhia para as structs dos perifericos (essas sao as do ST,
 *     nos enderecos verdadeiros, simulados pelo stm32_model.c);
 *   - SCB e SysTick como structs normais (o teste le o VTOR no salto);
 *   - __disable_irq, barreiras e NVIC_SystemReset vao ao modelo.
 */

#ifndef CORE_CM4_FAKE_H
#define CORE_CM4_FAKE_H

#include <stdint.h>

#define __I     volatile const
#define __O     volatile
#define __IO    volatile
#define __IM    volatile const
#define __OM    volatile
#define __IOM   volatile

typedef struct {
	__IOM uint32_t CTRL;
	__IOM uint32_t LOAD;
	__IOM uint32_t VAL;
	__IM  uint32_t CALIB;
} SysTick_Type;

typedef struct {
	__IM  uint32_t CPUID;
	__IOM uint32_t ICSR;
	__IOM uint32_t VTOR;
	__IOM uint32_t AIRCR;
	__IOM uint32_t SCR;
	__IOM uint32_t CCR;
} SCB_Type;

extern SCB_Type stm32_scb;
extern SysTick_Type stm32_systick;

#define SCB      (&stm32_scb)
#define SysTick  (&stm32_systick)

void stm32_irq_enable(int enable);
void stm32_barrier(void);
void stm32_system_reset(void) __attribute__((noreturn));

#define __disable_irq()     stm32_irq_enable(0)
#define __enable_irq()      stm32_irq_enable(1)
#define __DSB()             stm32_barrier()
#define __ISB()             stm32_barrier()
#define __DMB()             stm32_barrier()
#define NVIC_SystemReset()  stm32_system_reset()

#endif
