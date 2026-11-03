/*
 * bl_hw.c
 *
 * Ver bl_hw.h. Registos do RM0402 (STM32F412).
 */

#include "stm32f4xx.h"
#include "bl_hw.h"
#include "boot_layout.h"

#define HSE_TIMEOUT    200000u   // ~50 ms no HSI
#define PLL_TIMEOUT    200000u
#define UART_BAUD      230400u

static uint32_t hclk_hz = 16000000u;   // HSI ate o PLL arrancar
static uint32_t pclk_hz = 16000000u;   // APB1 e APB2 (iguais nos dois casos)

/* ================== GPIO ================== */

static void gpio_output(GPIO_TypeDef *port, uint32_t pin, bool high, bool pull_up) {

	port->BSRR = high ? (1u << pin) : (1u << (pin + 16u));   // o nivel antes de virar saida
	port->OTYPER &= ~(1u << pin);
	port->OSPEEDR &= ~(3u << (pin * 2u));
	port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | ((pull_up ? 1u : 0u) << (pin * 2u));
	port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
}

static void gpio_alternate(GPIO_TypeDef *port, uint32_t pin, uint32_t function) {

	uint32_t shift = (pin & 7u) * 4u;

	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << shift)) | (function << shift);
	port->OTYPER &= ~(1u << pin);
	port->OSPEEDR |= (3u << (pin * 2u));
	port->PUPDR &= ~(3u << (pin * 2u));
	port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
}

void BlHw_SafeOutputs(void) {

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
	(void) RCC->AHB1ENR;

	gpio_output(GPIOC, 8u, true, false);    // AMS_ERROR em erro
	gpio_output(GPIOB, 0u, true, true);     // AIR+ aberto
	gpio_output(GPIOB, 1u, true, true);     // AIR- aberto
	gpio_output(GPIOC, 5u, true, true);     // pre-carga aberta
	gpio_output(GPIOB, 10u, false, false);  // LED
}

void BlHw_Led(bool on) {

	GPIOB->BSRR = on ? (1u << 10) : (1u << (10u + 16u));
}

/* ================== RELOGIO ================== */

bool BlHw_ClockInit(void) {

	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	(void) RCC->APB1ENR;
	PWR->CR |= PWR_CR_VOS;   // escala 1

	RCC->CR |= RCC_CR_HSEON;

	uint32_t timeout = HSE_TIMEOUT;

	while (((RCC->CR & RCC_CR_HSERDY) == 0u) && (--timeout > 0u)) {
	}
	bool hse = (RCC->CR & RCC_CR_HSERDY) != 0u;

	if (!hse) {
		RCC->CR &= ~RCC_CR_HSEON;
	}

	// 2 MHz na entrada do PLL: HSE 8 / 4 ou HSI 16 / 8; VCO 128, SYSCLK 64 (PLLP 2)
	uint32_t pllm = hse ? 4u : 8u;

	RCC->PLLCFGR = (2u << RCC_PLLCFGR_PLLR_Pos) | (2u << RCC_PLLCFGR_PLLQ_Pos) | (hse ? RCC_PLLCFGR_PLLSRC_HSE : 0u) | (0u << RCC_PLLCFGR_PLLP_Pos)
			| (64u << RCC_PLLCFGR_PLLN_Pos) | (pllm << RCC_PLLCFGR_PLLM_Pos);
	RCC->CR |= RCC_CR_PLLON;

	timeout = PLL_TIMEOUT;
	while (((RCC->CR & RCC_CR_PLLRDY) == 0u) && (--timeout > 0u)) {
	}
	if ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
		return false;   // fica no HSI a 16 MHz
	}

	FLASH->ACR = FLASH_ACR_LATENCY_1WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;   // 1 WS ate 64 MHz, como a aplicacao
	RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
	}
	hclk_hz = 64000000u;
	pclk_hz = 32000000u;
	return hse;
}

uint32_t BlHw_PclkHz(void) {

	return pclk_hz;
}

/* ================== TEMPO ================== */

void BlHw_TimerInit(void) {

	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	(void) RCC->APB1ENR;

	// APB1 a HCLK/2 (ou HCLK/1 sem PLL): os timers contam sempre a HCLK
	TIM2->PSC = (hclk_hz / 1000u) - 1u;
	TIM2->ARR = 0xFFFFFFFFu;
	TIM2->EGR = TIM_EGR_UG;
	TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t BlHw_Millis(void) {

	return TIM2->CNT;
}

/* ================== UART1 ================== */

void BlHw_UartInit(void) {

	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
	(void) RCC->APB2ENR;

	gpio_alternate(GPIOA, 9u, 7u);   // TX
	USART1->BRR = (pclk_hz + (UART_BAUD / 2u)) / UART_BAUD;
	USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void BlHw_UartWrite(const char *text, uint32_t length) {

	for (uint32_t index = 0; index < length; index++) {
		while ((USART1->SR & USART_SR_TXE) == 0u) {
		}
		USART1->DR = (uint8_t) text[index];
	}
}

static void uart_drain(void) {

	uint32_t timeout = 1000000u;

	while (((USART1->SR & USART_SR_TC) == 0u) && (--timeout > 0u)) {
	}
}

/* ================== RESET, WATCHDOG, SALTO ================== */

uint32_t BlHw_TakeResetFlags(void) {

	uint32_t flags = RCC->CSR;

	RCC->CSR |= RCC_CSR_RMVF;
	return flags;
}

void BlHw_ArmIwdg(void) {

	DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;   // parado com o debugger em pausa

	// LSI ~32 kHz / 64 = 500 Hz
	IWDG->KR = 0xCCCCu;
	IWDG->KR = 0x5555u;
	IWDG->PR = 4u;
	IWDG->RLR = (BOOT_IWDG_TIMEOUT_MS * 500u) / 1000u;

	uint32_t timeout = 1000000u;

	while ((IWDG->SR != 0u) && (--timeout > 0u)) {
	}
	IWDG->KR = 0xAAAAu;
}

void BlHw_Reset(void) {

	uart_drain();
	NVIC_SystemReset();
	for (;;) {
	}
}

#if defined(__arm__)
__attribute__((noreturn, naked)) static void start_application(uint32_t sp __attribute__((unused)), uint32_t pc __attribute__((unused))) {

	__asm volatile(
			"msr msp, r0\n"
			"bx r1\n");
}
#else
// no PC (Firmware/Tests/test_boot_hw.c) o modelo do F412 apanha o salto
void start_application(uint32_t sp, uint32_t pc) __attribute__((noreturn));
#endif

void BlHw_Jump(uint32_t vectors) {

	uint32_t sp = *(volatile const uint32_t*) (uintptr_t) vectors;
	uint32_t pc = *(volatile const uint32_t*) (uintptr_t) (vectors + 4u);

	uart_drain();

	// perifericos do bootloader como depois de um reset (os pinos seguros ficam como estao)
	RCC->APB1RSTR |= RCC_APB1RSTR_CAN1RST | RCC_APB1RSTR_CAN2RST | RCC_APB1RSTR_TIM2RST;
	RCC->APB1RSTR &= ~(RCC_APB1RSTR_CAN1RST | RCC_APB1RSTR_CAN2RST | RCC_APB1RSTR_TIM2RST);
	RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
	RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
	RCC->APB1ENR &= ~(RCC_APB1ENR_CAN1EN | RCC_APB1ENR_CAN2EN | RCC_APB1ENR_TIM2EN);
	RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN;

	// relogio do reset (HSI): o SystemClock_Config da aplicacao nao reconfigura um PLL em uso
	RCC->CR |= RCC_CR_HSION;
	while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {
	}
	RCC->CFGR = 0u;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
	}
	RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON);
	while ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
	}
	RCC->PLLCFGR = 0x24003010u;
	RCC->CIR = 0u;

	__disable_irq();
	SysTick->CTRL = 0u;
	SCB->VTOR = vectors;
	__DSB();
	__ISB();
	__enable_irq();

	start_application(sp, pc);
}
