/*
 * stm32_model.c - ver stm32_model.h.
 *
 * Como se apanha um acesso: a pagina do registo esta sem permissao de
 * escrita (PROT_READ) ou sem nenhuma (o TIM2: as leituras do CNT fazem andar
 * o tempo). O SIGSEGV diz o endereco e se era escrita (REG_ERR); o handler
 * guarda o valor antigo, abre a pagina e liga a flag TF. A instrucao corre,
 * o CPU para logo a seguir (SIGTRAP), o handler fecha a pagina e aplica a
 * semantica do registo ao valor que o codigo escreveu. As leituras dos
 * outros registos nao custam nada: o modelo mantem-nos sempre atualizados
 * (cada mudanca de estado vem de uma escrita ou de um passo do tempo).
 */

#define _GNU_SOURCE
#include "stm32_model.h"
#include "stm32f4xx.h"
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__x86_64__) && defined(__linux__)
#include <ucontext.h>
#endif

#define PERIPH_START   0x40000000u
#define PERIPH_SIZE    0x00024000u     // TIM2 .. interface da flash
#define FLASH_START    0x08000000u
#define FLASH_SIZE     0x00080000u
#define RAM_PAGE       0x2003F000u
#define DBG_PAGE       0xE0042000u
#define PAGE           0x1000u
#define TIM2_PAGE      0x40000000u

#define HSI_HZ         16000000u
#define HSE_HZ         8000000u
#define LSI_HZ         32000u
#define FLASH_KEY1     0x45670123u
#define FLASH_KEY2     0xCDEF89ABu
#define FLASH_SR_W1C   (FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR)
#define CAN_CONFIG_BITS (CAN_MCR_TTCM | CAN_MCR_ABOM | CAN_MCR_AWUM | CAN_MCR_NART | CAN_MCR_RFLM | CAN_MCR_TXFP)

stm32_t *stm32;
SCB_Type stm32_scb;
SysTick_Type stm32_systick;

static uint8_t *periph;        // os registos pelo lado do modelo (outro mapeamento da mesma memoria)
static uint8_t *flash;
static sigjmp_buf exit_jump;
static volatile bool running;  // no filho, dentro de stm32_boot

static struct {
	bool      active;
	uintptr_t page;
	int       prot;
	uint32_t  address;
	uint32_t  old;
	bool      write;
	bool      is_flash;
} step;

/* ================== utilitarios ================== */

volatile uint32_t* stm32_reg(uint32_t address) {

	return (volatile uint32_t*) (periph + (address - PERIPH_START));
}

uint8_t* stm32_flash(uint32_t address) {

	return &flash[address - FLASH_START];
}

#define R(base, type, field)   (*stm32_reg((uint32_t) (base) + (uint32_t) offsetof(type, field)))
#define RCCR(field)            R(RCC_BASE, RCC_TypeDef, field)
#define FLASHR(field)          R(FLASH_R_BASE, FLASH_TypeDef, field)
#define TIM2R(field)           R(TIM2_BASE, TIM_TypeDef, field)
#define CANR(bus, field)       R(can_base(bus), CAN_TypeDef, field)

static uint32_t can_base(uint8_t bus) {

	return (bus == 0u) ? CAN1_BASE : CAN2_BASE;
}

static volatile uint32_t* tx_box(uint8_t bus, uint32_t box, size_t field) {

	return stm32_reg(can_base(bus) + (uint32_t) offsetof(CAN_TypeDef, sTxMailBox) + (box * (uint32_t) sizeof(CAN_TxMailBox_TypeDef)) + (uint32_t) field);
}

static volatile uint32_t* filter(uint32_t bank, bool second) {

	return stm32_reg(CAN1_BASE + (uint32_t) offsetof(CAN_TypeDef, sFilterRegister) + (bank * (uint32_t) sizeof(CAN_FilterRegister_TypeDef)) + (second ? 4u : 0u));
}

static void violation(const char *format, ...) {

	if (stm32->violations < STM32_VIOLATION_MAX) {
		va_list args;

		va_start(args, format);
		vsnprintf(stm32->violation[stm32->violations], sizeof(stm32->violation[0]), format, args);
		va_end(args);
	}
	stm32->violations++;
}

static void finish(int reason) {

	siglongjmp(exit_jump, reason);
}

static __attribute__((noreturn)) void crash(const char *what, uintptr_t address, uintptr_t pc) {

	char text[160];
	int length = snprintf(text, sizeof(text), "stm32_model: %s em 0x%lx (pc 0x%lx)\n", what, (unsigned long) address, (unsigned long) pc);

	if (length > 0) {
		(void) !write(2, text, (size_t) length);
	}
	_exit(99);
}

const char* stm32_exit_name(int reason) {

	static const char *const names[] = { "nada", "reset", "salto", "IWDG", "limite", "voltou", "crash" };

	return ((reason >= 0) && (reason <= STM32_EXIT_CRASH)) ? names[reason] : "?";
}

int32_t stm32_find_write(uint32_t address, uint32_t bits, uint32_t from) {

	uint32_t count = (stm32->log_count < STM32_LOG_MAX) ? stm32->log_count : STM32_LOG_MAX;

	for (uint32_t index = from; index < count; index++) {
		if ((stm32->log[index].address == address) && ((stm32->log[index].value & bits) == bits)) {
			return (int32_t) index;
		}
	}
	return -1;
}

/* ================== relogios ================== */

static uint32_t pll_hz(void) {

	uint32_t config = RCCR(PLLCFGR);
	uint32_t m = config & RCC_PLLCFGR_PLLM_Msk;
	uint32_t n = (config & RCC_PLLCFGR_PLLN_Msk) >> RCC_PLLCFGR_PLLN_Pos;
	uint32_t p = (((config & RCC_PLLCFGR_PLLP_Msk) >> RCC_PLLCFGR_PLLP_Pos) + 1u) * 2u;
	uint64_t input = ((config & RCC_PLLCFGR_PLLSRC) != 0u) ? HSE_HZ : HSI_HZ;

	return (m == 0u) ? 0u : (uint32_t) (((input / m) * n) / p);
}

static uint32_t ahb_divider(uint32_t cfgr) {

	uint32_t hpre = (cfgr & RCC_CFGR_HPRE_Msk) >> RCC_CFGR_HPRE_Pos;
	static const uint32_t dividers[8] = { 2, 4, 8, 16, 64, 128, 256, 512 };

	return (hpre < 8u) ? 1u : dividers[hpre - 8u];
}

static uint32_t apb_divider(uint32_t ppre) {

	return (ppre < 4u) ? 1u : (1u << (ppre - 3u));
}

static uint32_t sysclk_for(uint32_t sws) {

	return (sws == 0u) ? HSI_HZ : ((sws == 1u) ? HSE_HZ : pll_hz());
}

static void update_clocks(void) {

	uint32_t cfgr = RCCR(CFGR);
	uint32_t apb1 = apb_divider((cfgr & RCC_CFGR_PPRE1_Msk) >> RCC_CFGR_PPRE1_Pos);
	uint32_t apb2 = apb_divider((cfgr & RCC_CFGR_PPRE2_Msk) >> RCC_CFGR_PPRE2_Pos);

	stm32->sysclk_hz = sysclk_for((cfgr & RCC_CFGR_SWS_Msk) >> RCC_CFGR_SWS_Pos);
	stm32->hclk_hz = stm32->sysclk_hz / ahb_divider(cfgr);
	stm32->pclk1_hz = stm32->hclk_hz / apb1;
	stm32->pclk2_hz = stm32->hclk_hz / apb2;
	stm32->timclk1_hz = (apb1 == 1u) ? stm32->pclk1_hz : (2u * stm32->pclk1_hz);
}

// RM0402 tabela 6 (2,7..3,6 V)
static uint32_t wait_states_for(uint32_t hclk) {

	return (hclk <= 30000000u) ? 0u : ((hclk <= 64000000u) ? 1u : ((hclk <= 90000000u) ? 2u : 3u));
}

static bool clock_on(uint32_t block) {

	uint32_t ahb1 = RCCR(AHB1ENR);
	uint32_t apb1 = RCCR(APB1ENR);
	uint32_t apb2 = RCCR(APB2ENR);

	switch (block) {
	case GPIOA_BASE:
		return (ahb1 & RCC_AHB1ENR_GPIOAEN) != 0u;
	case GPIOB_BASE:
		return (ahb1 & RCC_AHB1ENR_GPIOBEN) != 0u;
	case GPIOC_BASE:
		return (ahb1 & RCC_AHB1ENR_GPIOCEN) != 0u;
	case TIM2_BASE:
		return (apb1 & RCC_APB1ENR_TIM2EN) != 0u;
	case CAN1_BASE:
		return (apb1 & RCC_APB1ENR_CAN1EN) != 0u;
	case CAN2_BASE:
		return (apb1 & RCC_APB1ENR_CAN2EN) != 0u;
	case PWR_BASE:
		return (apb1 & RCC_APB1ENR_PWREN) != 0u;
	case USART1_BASE:
		return (apb2 & RCC_APB2ENR_USART1EN) != 0u;
	default:
		return true;   // RCC, FLASH, IWDG
	}
}

/* ================== TIM2 ================== */

static uint64_t tim2_ticks(void) {

	if (!stm32->tim2.running || !clock_on(TIM2_BASE)) {
		return stm32->tim2.base_count;
	}

	// em us e kHz para caber em 64 bits (horas simuladas a 100 MHz)
	uint64_t elapsed_us = (stm32->now_ns - stm32->tim2.base_ns) / 1000u;
	uint64_t cycles = (elapsed_us * (stm32->timclk1_hz / 1000u)) / 1000u;

	return stm32->tim2.base_count + (cycles / (stm32->tim2.psc + 1u));
}

static void tim2_rebase(void) {

	stm32->tim2.base_count = tim2_ticks();
	stm32->tim2.base_ns = stm32->now_ns;
}

static void tim2_show(void) {

	uint64_t arr = TIM2R(ARR);

	TIM2R(CNT) = (uint32_t) (tim2_ticks() % (arr + 1u));
}

static void tim2_write(uint32_t offset, uint32_t old, uint32_t value) {

	if (offset == offsetof(TIM_TypeDef, CR1)) {
		if (((value & TIM_CR1_CEN) != 0u) && !stm32->tim2.running) {
			stm32->tim2.running = true;
			stm32->tim2.base_ns = stm32->now_ns;
		} else if (((value & TIM_CR1_CEN) == 0u) && stm32->tim2.running) {
			tim2_rebase();
			stm32->tim2.running = false;
		}
	} else if (offset == offsetof(TIM_TypeDef, EGR)) {
		if ((value & TIM_EGR_UG) != 0u) {
			stm32->tim2.base_count = 0;
			stm32->tim2.base_ns = stm32->now_ns;
			stm32->tim2.psc = TIM2R(PSC) & 0xFFFFu;   // o PSC so entra num update
		}
		TIM2R(EGR) = 0u;
	} else if (offset == offsetof(TIM_TypeDef, CNT)) {
		stm32->tim2.base_count = value;
		stm32->tim2.base_ns = stm32->now_ns;
	}
	(void) old;
}

/* ================== tempo ================== */

static void can_service(uint8_t bus);
static void client_step(void);

static void advance(uint64_t ns) {

	stm32->now_ns += ns;
	if ((stm32->warp_by_ns != 0u) && (stm32->now_ns >= stm32->warp_at_ns)) {
		stm32->now_ns += stm32->warp_by_ns;
		stm32->warp_by_ns = 0;
	}

	if (stm32->iwdg_started) {
		uint64_t timeout = ((uint64_t) (stm32->iwdg.rlr + 1u) * (4u << stm32->iwdg.pr) * 1000000000u) / LSI_HZ;

		if ((stm32->now_ns - stm32->iwdg.reload_ns) > timeout) {
			RCCR(CSR) |= RCC_CSR_IWDGRSTF;
			finish(STM32_EXIT_IWDG);
		}
	}
	if ((stm32->limit_ns != 0u) && (stm32->now_ns >= stm32->limit_ns)) {
		finish(STM32_EXIT_LIMIT);
	}

	can_service(0);
	can_service(1);
	if (stm32->hook != NULL) {
		stm32->hook();
	}
	client_step();
}

/* ================== GPIO ================== */

static void gpio_write(uint32_t block, uint32_t offset, uint32_t old, uint32_t value) {

	if (offset != offsetof(GPIO_TypeDef, BSRR)) {
		return;
	}

	volatile uint32_t *odr = stm32_reg(block + (uint32_t) offsetof(GPIO_TypeDef, ODR));
	uint32_t before = *odr;
	uint32_t after = (before & ~(value >> 16)) | (value & 0xFFFFu);   // o set ganha ao reset

	*odr = after;
	*stm32_reg(block + (uint32_t) offsetof(GPIO_TypeDef, BSRR)) = 0u;   // le-se 0

	if ((block == GPIOB_BASE) && (((before ^ after) & (1u << 10)) != 0u)) {
		if (stm32->led_toggles > 0u) {
			stm32->led_period_ns = stm32->now_ns - stm32->led_last_ns;
		}
		stm32->led_toggles++;
		stm32->led_last_ns = stm32->now_ns;
	}
	(void) old;
}

/* ================== USART1 ================== */

static void usart_write(uint32_t offset, uint32_t old, uint32_t value) {

	if (offset != offsetof(USART_TypeDef, DR)) {
		return;
	}

	uint32_t cr1 = R(USART1_BASE, USART_TypeDef, CR1);
	uint32_t brr = R(USART1_BASE, USART_TypeDef, BRR);

	if (((cr1 & USART_CR1_UE) == 0u) || ((cr1 & USART_CR1_TE) == 0u) || (brr == 0u)) {
		stm32->uart_violations++;
		violation("USART1->DR com o transmissor desligado");
		return;
	}
	if (stm32->uart_length < (STM32_UART_MAX - 1u)) {
		stm32->uart[stm32->uart_length++] = (char) value;
		stm32->uart[stm32->uart_length] = 0;
	}
	// 10 bits ao baud configurado (OVER8 = 0: baud = PCLK2 / BRR)
	advance(((uint64_t) brr * 10u * 1000000000u) / stm32->pclk2_hz);
	(void) old;
}

/* ================== RCC ================== */

static void reset_can(uint8_t bus);
static void reset_usart1(void);
static void reset_tim2(void);

static bool pll_config_ok(uint32_t config) {

	uint32_t m = config & RCC_PLLCFGR_PLLM_Msk;
	uint32_t n = (config & RCC_PLLCFGR_PLLN_Msk) >> RCC_PLLCFGR_PLLN_Pos;
	uint32_t q = (config & RCC_PLLCFGR_PLLQ_Msk) >> RCC_PLLCFGR_PLLQ_Pos;
	uint32_t r = (config & RCC_PLLCFGR_PLLR_Msk) >> RCC_PLLCFGR_PLLR_Pos;
	uint64_t input = ((config & RCC_PLLCFGR_PLLSRC) != 0u) ? HSE_HZ : HSI_HZ;

	if ((m < 2u) || (n < 50u) || (n > 432u) || (q < 2u) || (r < 2u)) {
		return false;
	}

	uint64_t vco_in = input / m;
	uint64_t vco = vco_in * n;
	uint64_t p_out = vco / ((((config & RCC_PLLCFGR_PLLP_Msk) >> RCC_PLLCFGR_PLLP_Pos) + 1u) * 2u);

	return (vco_in >= 1000000u) && (vco_in <= 2000000u) && (vco >= 100000000u) && (vco <= 432000000u) && (p_out <= 100000000u);
}

static void rcc_write(uint32_t offset, uint32_t old, uint32_t value) {

	uint32_t sws = (RCCR(CFGR) & RCC_CFGR_SWS_Msk) >> RCC_CFGR_SWS_Pos;

	if (offset == offsetof(RCC_TypeDef, CR)) {

		uint32_t cr = value;
		bool pll_from_hse = (RCCR(PLLCFGR) & RCC_PLLCFGR_PLLSRC) != 0u;

		if (((old & RCC_CR_PLLON) != 0u) && ((cr & RCC_CR_PLLON) == 0u) && (sws == 2u)) {
			violation("PLL desligado sendo o SYSCLK");
			cr |= RCC_CR_PLLON;
		}
		if (((old & RCC_CR_HSEON) != 0u) && ((cr & RCC_CR_HSEON) == 0u) && ((sws == 1u) || (((cr & RCC_CR_PLLON) != 0u) && pll_from_hse))) {
			violation("HSE desligado em uso");
			cr |= RCC_CR_HSEON;
		}
		if (((cr & RCC_CR_HSION) == 0u) && ((sws == 0u) || (((cr & RCC_CR_PLLON) != 0u) && !pll_from_hse))) {
			violation("HSI desligado em uso");
			cr |= RCC_CR_HSION;
		}
		cr &= ~(RCC_CR_HSIRDY | RCC_CR_HSERDY | RCC_CR_PLLRDY);
		if ((cr & RCC_CR_HSION) != 0u) {
			cr |= RCC_CR_HSIRDY;
		}
		if (((cr & RCC_CR_HSEON) != 0u) && stm32->hse_present) {
			cr |= RCC_CR_HSERDY;
		}
		bool source = pll_from_hse ? ((cr & RCC_CR_HSERDY) != 0u) : ((cr & RCC_CR_HSIRDY) != 0u);

		if (((cr & RCC_CR_PLLON) != 0u) && ((old & RCC_CR_PLLRDY) != 0u)) {
			cr |= RCC_CR_PLLRDY;   // ja estava a trabalhar
		} else if (((cr & RCC_CR_PLLON) != 0u) && stm32->pll_locks && source && pll_config_ok(RCCR(PLLCFGR))) {
			cr |= RCC_CR_PLLRDY;
		}
		RCCR(CR) = cr;

	} else if (offset == offsetof(RCC_TypeDef, PLLCFGR)) {

		if ((RCCR(CR) & RCC_CR_PLLON) != 0u) {
			violation("PLLCFGR escrito com o PLL ligado");
		}
		if (!pll_config_ok(value)) {
			violation("PLL fora dos limites do RM0402 (0x%08x)", value);
		}

	} else if (offset == offsetof(RCC_TypeDef, CFGR)) {

		uint32_t cr = RCCR(CR);
		uint32_t sw = value & RCC_CFGR_SW_Msk;
		bool ready = ((sw == 0u) && ((cr & RCC_CR_HSIRDY) != 0u)) || ((sw == 1u) && ((cr & RCC_CR_HSERDY) != 0u))
				|| ((sw == 2u) && ((cr & RCC_CR_PLLRDY) != 0u));

		if (!ready) {
			violation("SW para um relogio que nao esta pronto (%u)", sw);
		}

		uint32_t new_sws = ready ? sw : sws;
		uint32_t cfgr = (value & ~RCC_CFGR_SWS_Msk) | (new_sws << RCC_CFGR_SWS_Pos);
		uint32_t hclk = sysclk_for(new_sws) / ahb_divider(cfgr);
		uint32_t latency = FLASHR(ACR) & FLASH_ACR_LATENCY_Msk;

		if (latency < wait_states_for(hclk)) {
			violation("latencia da flash %u WS curta para HCLK %u Hz", latency, hclk);
		}
		tim2_rebase();
		RCCR(CFGR) = cfgr;
		update_clocks();
		if (stm32->pclk1_hz > 50000000u) {
			violation("APB1 a %u Hz (max 50 MHz)", stm32->pclk1_hz);
		}

	} else if ((offset == offsetof(RCC_TypeDef, APB1RSTR)) || (offset == offsetof(RCC_TypeDef, APB2RSTR))) {

		uint32_t raised = value & ~old;

		if (raised != 0u) {
			stm32->peripheral_resets++;
		}
		if (offset == offsetof(RCC_TypeDef, APB1RSTR)) {
			if ((raised & RCC_APB1RSTR_TIM2RST) != 0u) {
				reset_tim2();
			}
			if ((raised & RCC_APB1RSTR_CAN1RST) != 0u) {
				reset_can(0);
			}
			if ((raised & RCC_APB1RSTR_CAN2RST) != 0u) {
				reset_can(1);
			}
		} else if ((raised & RCC_APB2RSTR_USART1RST) != 0u) {
			reset_usart1();
		}

	} else if (offset == offsetof(RCC_TypeDef, CSR)) {

		uint32_t flags = old & 0xFE000000u;

		if ((value & RCC_CSR_RMVF) != 0u) {
			flags = 0u;
		}
		RCCR(CSR) = flags | (value & RCC_CSR_LSION) | (((value & RCC_CSR_LSION) != 0u) ? RCC_CSR_LSIRDY : 0u)
				| (old & (stm32->iwdg_started ? (RCC_CSR_LSION | RCC_CSR_LSIRDY) : 0u));

	} else if (offset == offsetof(RCC_TypeDef, CIR)) {
		RCCR(CIR) = value & 0x00003F00u;   // so os enables; as flags limpam-se
	}
}

/* ================== FLASH ================== */

static const uint32_t sector_offsets[9] = { 0x00000, 0x04000, 0x08000, 0x0C000, 0x10000, 0x20000, 0x40000, 0x60000, 0x80000 };
static const uint32_t erase_ms[8] = { 250, 250, 250, 250, 550, 1000, 1000, 1000 };   // tipicos do datasheet (x32)

static void erase_sector(uint32_t sector) {

	if (sector > 7u) {
		violation("SNB %u nao existe", sector);
		FLASHR(SR) |= FLASH_SR_WRPERR;
		return;
	}
	stm32->erases[stm32->erase_count % 64u] = (uint8_t) sector;
	stm32->erase_count++;
	if (sector < 2u) {
		violation("apagou o setor %u (bootloader)", sector);
	}
	if (stm32->flash_erase_error != 0u) {
		FLASHR(SR) |= stm32->flash_erase_error;
		stm32->flash_erase_error = 0;
		return;
	}

	uint32_t start = sector_offsets[sector];
	uint32_t end = sector_offsets[sector + 1u];

	memset(&flash[start], 0xFF, end - start);
	if ((stm32->flash_stuck_offset >= (int32_t) start) && (stm32->flash_stuck_offset < (int32_t) end)) {
		uint32_t word;

		memcpy(&word, &flash[stm32->flash_stuck_offset], 4);
		word &= ~stm32->flash_stuck_zero;
		memcpy(&flash[stm32->flash_stuck_offset], &word, 4);
	}
	advance((uint64_t) erase_ms[sector] * 1000000u);
}

static void flash_reg_write(uint32_t offset, uint32_t old, uint32_t value) {

	if (offset == offsetof(FLASH_TypeDef, ACR)) {

		if (((value & FLASH_ACR_ICRST) != 0u) && ((value & FLASH_ACR_ICEN) != 0u)) {
			violation("ICRST com a cache de instrucoes ligada");
		}
		if (((value & FLASH_ACR_DCRST) != 0u) && ((value & FLASH_ACR_DCEN) != 0u)) {
			violation("DCRST com a cache de dados ligada");
		}
		if (((value & FLASH_ACR_ICRST) != 0u) && ((old & FLASH_ACR_ICRST) == 0u)) {
			stm32->cache_flushes++;
		}
		if ((value & FLASH_ACR_LATENCY_Msk) < wait_states_for(stm32->hclk_hz)) {
			violation("latencia da flash baixada para HCLK %u Hz", stm32->hclk_hz);
		}

	} else if (offset == offsetof(FLASH_TypeDef, KEYR)) {

		FLASHR(KEYR) = 0u;
		if ((FLASHR(CR) & FLASH_CR_LOCK) == 0u) {
			violation("FLASH_KEYR com o CR desbloqueado");
		} else if ((stm32->flash.key_stage == 0u) && (value == FLASH_KEY1)) {
			stm32->flash.key_stage = 1;
		} else if ((stm32->flash.key_stage == 1u) && (value == FLASH_KEY2)) {
			stm32->flash.key_stage = 0;
			if (!stm32->flash_lock_stuck && !stm32->flash.locked_out) {
				FLASHR(CR) &= ~FLASH_CR_LOCK;
			}
		} else {
			stm32->flash.key_stage = 0;
			stm32->flash.locked_out = true;
			violation("sequencia de chaves errada: FLASH_CR bloqueado ate ao reset");
		}

	} else if ((offset == offsetof(FLASH_TypeDef, OPTKEYR)) || (offset == offsetof(FLASH_TypeDef, OPTCR))) {

		violation("mexeu nos option bytes");
		*stm32_reg(FLASH_R_BASE + offset) = old;

	} else if (offset == offsetof(FLASH_TypeDef, SR)) {

		FLASHR(SR) = old & ~(value & FLASH_SR_W1C);

	} else if (offset == offsetof(FLASH_TypeDef, CR)) {

		if ((old & FLASH_CR_LOCK) != 0u) {
			violation("FLASH_CR escrito bloqueado");
			FLASHR(CR) = old;
			return;
		}

		uint32_t cr = value;

		if ((cr & FLASH_CR_STRT) != 0u) {
			cr &= ~FLASH_CR_STRT;
			FLASHR(CR) = cr;
			if ((cr & FLASH_CR_MER) != 0u) {
				violation("apagar a flash toda");
			} else if ((cr & FLASH_CR_SER) != 0u) {
				erase_sector((cr & FLASH_CR_SNB_Msk) >> FLASH_CR_SNB_Pos);
			} else {
				violation("STRT sem SER");
			}
		}
		FLASHR(CR) = cr;
	}
}

static void flash_program(uint32_t address, uint32_t old, uint32_t value) {

	uint32_t offset = address - FLASH_START;
	uint32_t cr = FLASHR(CR);
	uint32_t result;

	memcpy(&flash[offset], &old, 4);   // a escrita "verdadeira" e a de baixo
	if (((cr & FLASH_CR_LOCK) != 0u) || ((cr & FLASH_CR_PG) == 0u)) {
		FLASHR(SR) |= FLASH_SR_PGSERR;
		violation("escrita na flash sem PG (0x%08x)", address);
		return;
	}
	if (((cr & FLASH_CR_PSIZE_Msk) >> FLASH_CR_PSIZE_Pos) != 2u) {
		FLASHR(SR) |= FLASH_SR_PGPERR;
		violation("PSIZE nao e x32 numa escrita de 32 bits");
		return;
	}
	if (offset < 0x8000u) {
		violation("programou o bootloader (0x%08x)", address);
	}
	if ((stm32->flash_program_error_at >= 0) && ((uint32_t) stm32->flash_program_error_at == offset)) {
		FLASHR(SR) |= stm32->flash_program_error;
		return;
	}

	result = old & value;
	if ((stm32->flash_stuck_offset >= 0) && ((uint32_t) stm32->flash_stuck_offset == offset)) {
		result |= stm32->flash_stuck_one;
	}
	if (((old & value) != value)) {
		stm32->overwrites++;
	}
	memcpy(&flash[offset], &result, 4);
	stm32->program_words++;
	advance(16000u);
}

/* ================== IWDG ================== */

static void iwdg_write(uint32_t offset, uint32_t old, uint32_t value) {

	volatile uint32_t *reg = stm32_reg(IWDG_BASE + offset);

	if (offset == offsetof(IWDG_TypeDef, KR)) {
		*reg = 0u;
		switch (value & 0xFFFFu) {
		case 0xCCCCu:
			if (!stm32->iwdg_started) {
				stm32->iwdg_started = true;
				stm32->iwdg.reload_ns = stm32->now_ns;
				RCCR(CSR) |= RCC_CSR_LSION | RCC_CSR_LSIRDY;
			}
			break;
		case 0x5555u:
			stm32->iwdg.access = true;
			break;
		case 0xAAAAu:
			stm32->iwdg.access = false;
			stm32->iwdg.pr = R(IWDG_BASE, IWDG_TypeDef, PR) & 7u;
			stm32->iwdg.rlr = R(IWDG_BASE, IWDG_TypeDef, RLR) & 0xFFFu;
			stm32->iwdg.reload_ns = stm32->now_ns;
			stm32->iwdg_reloads++;
			stm32->iwdg_timeout_ms = (uint32_t) (((uint64_t) (stm32->iwdg.rlr + 1u) * (4u << stm32->iwdg.pr) * 1000u) / LSI_HZ);
			break;
		default:
			stm32->iwdg.access = false;
			break;
		}
	} else if ((offset == offsetof(IWDG_TypeDef, PR)) || (offset == offsetof(IWDG_TypeDef, RLR))) {
		if (!stm32->iwdg.access) {
			violation("IWDG_PR/RLR sem a chave 0x5555");
			*reg = old;
		}
	} else {
		*reg = old;   // SR so de leitura
	}
}

/* ================== bxCAN ================== */

static void can_reset_registers(uint8_t bus) {

	memset((void*) stm32_reg(can_base(bus)), 0, 0x400);
	CANR(bus, MCR) = 0x00010002u;
	CANR(bus, MSR) = 0x00000C02u;
	CANR(bus, TSR) = 0x1C000000u;
	CANR(bus, BTR) = 0x01230000u;
	if (bus == 0u) {
		CANR(0, FMR) = 0x2A1C0E01u;
	}
	memset(&stm32->fifo[bus], 0, sizeof(stm32->fifo[bus]));
}

static void reset_can(uint8_t bus) {

	can_reset_registers(bus);
}

static bool can_normal(uint8_t bus) {

	uint32_t msr = CANR(bus, MSR);

	return ((msr & (CAN_MSR_INAK | CAN_MSR_SLAK)) == 0u) && clock_on(can_base(bus));
}

// a cabeca da FIFO 0 nos registos
static void can_show_fifo(uint8_t bus) {

	uint32_t rf0r = stm32->fifo[bus].count;

	if (stm32->fifo[bus].count == 3u) {
		rf0r |= CAN_RF0R_FULL0;
	}
	if (stm32->fifo[bus].overrun) {
		rf0r |= CAN_RF0R_FOVR0;
	}
	CANR(bus, RF0R) = rf0r;

	if (stm32->fifo[bus].count > 0u) {

		const stm32_frame_t *frame = &stm32->fifo[bus].frames[0];
		uint32_t low = 0;
		uint32_t high = 0;

		for (uint32_t index = 0; index < 4u; index++) {
			low |= (uint32_t) frame->data[index] << (8u * index);
			high |= (uint32_t) frame->data[index + 4u] << (8u * index);
		}
		CANR(bus, sFIFOMailBox[0].RIR) = frame->extended ? ((frame->id << 3) | CAN_RI0R_IDE | (frame->rtr ? CAN_RI0R_RTR : 0u))
				: ((frame->id << 21) | (frame->rtr ? CAN_RI0R_RTR : 0u));
		CANR(bus, sFIFOMailBox[0].RDTR) = frame->dlc | (stm32->fifo[bus].fmi[0] << 8);
		CANR(bus, sFIFOMailBox[0].RDLR) = low;
		CANR(bus, sFIFOMailBox[0].RDHR) = high;
	}
}

static uint32_t can_code(uint8_t bus) {

	uint32_t tsr = CANR(bus, TSR);

	for (uint32_t box = 0; box < 3u; box++) {
		if ((tsr & (CAN_TSR_TME0 << box)) != 0u) {
			return box;
		}
	}
	return 0u;
}

static void can_set_code(uint8_t bus) {

	CANR(bus, TSR) = (CANR(bus, TSR) & ~CAN_TSR_CODE_Msk) | (can_code(bus) << CAN_TSR_CODE_Pos);
}

// as caixas pendentes saem por ordem de ID (TXFP = 0), se houver quem de ACK
static void can_transmit(uint8_t bus) {

	if (!can_normal(bus) || !stm32->can_ack[bus]) {
		return;
	}

	for (;;) {

		int best = -1;
		uint32_t best_id = 0;

		for (uint32_t box = 0; box < 3u; box++) {
			uint32_t tir = *tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TIR));

			if (((CANR(bus, TSR) & (CAN_TSR_TME0 << box)) == 0u) && ((best < 0) || ((tir >> 3) < best_id))) {
				best = (int) box;
				best_id = tir >> 3;
			}
		}
		if (best < 0) {
			break;
		}

		uint32_t box = (uint32_t) best;
		uint32_t tir = *tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TIR));
		uint32_t low = *tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TDLR));
		uint32_t high = *tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TDHR));
		stm32_frame_t frame;

		memset(&frame, 0, sizeof(frame));
		frame.extended = (tir & CAN_TI0R_IDE) != 0u;
		frame.rtr = (tir & CAN_TI0R_RTR) != 0u;
		frame.id = frame.extended ? (tir >> 3) : (tir >> 21);
		frame.dlc = (uint8_t) (*tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TDTR)) & 0xFu);
		frame.time_ns = stm32->now_ns;
		for (uint32_t index = 0; index < 4u; index++) {
			frame.data[index] = (uint8_t) (low >> (8u * index));
			frame.data[index + 4u] = (uint8_t) (high >> (8u * index));
		}
		if (stm32->tx_count[bus] < STM32_TX_MAX) {
			stm32->tx[bus][stm32->tx_count[bus]] = frame;
		}
		stm32->tx_count[bus]++;

		*tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TIR)) = tir & ~CAN_TI0R_TXRQ;
		CANR(bus, TSR) |= (CAN_TSR_TME0 << box) | ((CAN_TSR_RQCP0 | CAN_TSR_TXOK0) << (8u * box));
	}
	can_set_code(bus);
}

// os modos (RM0402 32.4): sleep, init, normal
static void can_mode(uint8_t bus) {

	uint32_t mcr = CANR(bus, MCR);
	uint32_t msr = CANR(bus, MSR);

	if (!stm32->can_init_ack[bus]) {
		return;   // periferico que nao responde: fica como estava
	}
	if ((mcr & CAN_MCR_INRQ) != 0u) {
		msr = (msr | CAN_MSR_INAK) & ~CAN_MSR_SLAK;
	} else if ((mcr & CAN_MCR_SLEEP) != 0u) {
		msr = (msr | CAN_MSR_SLAK) & ~CAN_MSR_INAK;
	} else if (stm32->can_bus_idle[bus]) {
		msr &= ~(CAN_MSR_SLAK | CAN_MSR_INAK);   // viu 11 bits recessivos: modo normal
	}
	// sem bus fica onde estava (init ou sleep) ate o ver
	CANR(bus, MSR) = msr;
}

static void can_service(uint8_t bus) {

	can_mode(bus);
	can_transmit(bus);
}

static bool filter_match(uint8_t bus, const stm32_frame_t *frame, uint32_t *fifo, uint32_t *fmi) {

	uint32_t fmr = CANR(0, FMR);
	uint32_t split = (fmr & CAN_FMR_CAN2SB_Msk) >> CAN_FMR_CAN2SB_Pos;
	uint32_t first = (bus == 0u) ? 0u : split;
	uint32_t last = (bus == 0u) ? split : 28u;
	uint32_t value = frame->extended ? ((frame->id << 3) | CAN_RI0R_IDE) : (frame->id << 21);
	uint32_t number = 0;

	if (frame->rtr) {
		value |= CAN_RI0R_RTR;
	}
	if (((fmr & CAN_FMR_FINIT) != 0u) || !clock_on(CAN1_BASE)) {
		return false;   // filtros em init ou sem relogio: nao recebe nada
	}

	for (uint32_t bank = first; bank < last; bank++) {

		uint32_t bit = 1u << bank;
		bool list = (CANR(0, FM1R) & bit) != 0u;
		bool wide = (CANR(0, FS1R) & bit) != 0u;
		uint32_t fr1 = *filter(bank, false) & ~1u;
		uint32_t fr2 = *filter(bank, true) & ~1u;

		if ((CANR(0, FA1R) & bit) == 0u) {
			continue;
		}
		if (!wide) {
			violation("filtro de 16 bits no banco %u (nao modelado)", bank);
			continue;
		}
		if (list ? ((value == fr1) || (value == fr2)) : (((value ^ fr1) & fr2) == 0u)) {
			*fifo = ((CANR(0, FFA1R) & bit) != 0u) ? 1u : 0u;
			*fmi = number;
			return true;
		}
		number += list ? 2u : 1u;
	}
	return false;
}

void stm32_can_send(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t dlc, bool extended, bool rtr) {

	stm32_frame_t frame;
	uint32_t fifo;
	uint32_t fmi;

	memset(&frame, 0, sizeof(frame));
	frame.id = id;
	frame.dlc = dlc;
	frame.extended = extended;
	frame.rtr = rtr;
	frame.time_ns = stm32->now_ns;
	if (data != NULL) {
		memcpy(frame.data, data, (dlc > 8u) ? 8u : dlc);
	}

	if (!can_normal(bus) || !filter_match(bus, &frame, &fifo, &fmi) || (fifo != 0u)) {
		stm32->rx_dropped[bus]++;
		return;
	}
	if (stm32->fifo[bus].count == 3u) {
		stm32->fifo[bus].frames[2] = frame;   // RFLM = 0: a ultima e substituida
		stm32->fifo[bus].fmi[2] = fmi;
		stm32->fifo[bus].overrun = true;
		stm32->rx_overruns[bus]++;
	} else {
		stm32->fifo[bus].frames[stm32->fifo[bus].count] = frame;
		stm32->fifo[bus].fmi[stm32->fifo[bus].count] = fmi;
		stm32->fifo[bus].count++;
	}
	can_show_fifo(bus);
}

static void can_write(uint8_t bus, uint32_t offset, uint32_t old, uint32_t value) {

	volatile uint32_t *reg = stm32_reg(can_base(bus) + offset);
	bool init = (CANR(bus, MSR) & CAN_MSR_INAK) != 0u;

	if (offset == offsetof(CAN_TypeDef, MCR)) {

		if (!init && (((old ^ value) & CAN_CONFIG_BITS) != 0u)) {
			violation("CAN%u_MCR: bits de configuracao fora do modo de init", bus + 1u);
		}
		can_mode(bus);
		can_transmit(bus);

	} else if (offset == offsetof(CAN_TypeDef, MSR)) {

		*reg = old;   // as flags de wakeup/erro nao se usam

	} else if (offset == offsetof(CAN_TypeDef, BTR)) {

		if (!init) {
			violation("CAN%u_BTR escrito fora do modo de init", bus + 1u);
			*reg = old;
		}

	} else if (offset == offsetof(CAN_TypeDef, TSR)) {

		uint32_t tsr = old;

		for (uint32_t box = 0; box < 3u; box++) {

			uint32_t shift = 8u * box;

			if (((value & (CAN_TSR_ABRQ0 << shift)) != 0u) && ((old & (CAN_TSR_TME0 << box)) == 0u)) {
				tsr |= (CAN_TSR_TME0 << box) | (CAN_TSR_RQCP0 << shift);
				tsr &= ~(CAN_TSR_TXOK0 << shift);
				*tx_box(bus, box, offsetof(CAN_TxMailBox_TypeDef, TIR)) &= ~CAN_TI0R_TXRQ;
				stm32->aborts[bus]++;
			}
			if ((value & (CAN_TSR_RQCP0 << shift)) != 0u) {
				tsr &= ~((CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0) << shift);
			}
		}
		*reg = tsr;
		can_set_code(bus);

	} else if (offset == offsetof(CAN_TypeDef, RF0R)) {

		if (((value & CAN_RF0R_RFOM0) != 0u) && (stm32->fifo[bus].count > 0u)) {
			memmove(&stm32->fifo[bus].frames[0], &stm32->fifo[bus].frames[1], 2u * sizeof(stm32_frame_t));
			memmove(&stm32->fifo[bus].fmi[0], &stm32->fifo[bus].fmi[1], 2u * sizeof(uint32_t));
			stm32->fifo[bus].count--;
		}
		if ((value & CAN_RF0R_FOVR0) != 0u) {
			stm32->fifo[bus].overrun = false;
		}
		can_show_fifo(bus);

	} else if ((offset >= offsetof(CAN_TypeDef, sTxMailBox)) && (offset < offsetof(CAN_TypeDef, sFIFOMailBox))) {

		uint32_t box = (offset - (uint32_t) offsetof(CAN_TypeDef, sTxMailBox)) / (uint32_t) sizeof(CAN_TxMailBox_TypeDef);
		uint32_t field = (offset - (uint32_t) offsetof(CAN_TypeDef, sTxMailBox)) % (uint32_t) sizeof(CAN_TxMailBox_TypeDef);
		bool empty = (CANR(bus, TSR) & (CAN_TSR_TME0 << box)) != 0u;

		if (!empty) {
			violation("CAN%u: caixa %u escrita com um envio pendente", bus + 1u, box);
			*reg = old;
		} else if ((field == offsetof(CAN_TxMailBox_TypeDef, TIR)) && ((value & CAN_TI0R_TXRQ) != 0u)) {
			CANR(bus, TSR) &= ~(CAN_TSR_TME0 << box);
			can_set_code(bus);
			can_transmit(bus);
		}

	} else if (offset >= offsetof(CAN_TypeDef, FMR)) {

		if (bus != 0u) {
			violation("filtros escritos no CAN2 (estao no CAN1)");
			*reg = old;
			return;
		}

		bool finit = (CANR(0, FMR) & CAN_FMR_FINIT) != 0u;

		if ((offset == offsetof(CAN_TypeDef, FM1R)) || (offset == offsetof(CAN_TypeDef, FS1R)) || (offset == offsetof(CAN_TypeDef, FFA1R))) {
			if (!finit) {
				violation("CAN_FxxR com FINIT = 0");
				*reg = old;
			}
		} else if (offset >= offsetof(CAN_TypeDef, sFilterRegister)) {
			uint32_t bank = (offset - (uint32_t) offsetof(CAN_TypeDef, sFilterRegister)) / 8u;

			if (!finit && ((CANR(0, FA1R) & (1u << bank)) != 0u)) {
				violation("banco de filtro %u escrito ativo e fora do FINIT", bank);
				*reg = old;
			}
		} else if ((offset == offsetof(CAN_TypeDef, FMR)) && (((old ^ value) & CAN_FMR_CAN2SB_Msk) != 0u) && ((old & CAN_FMR_FINIT) == 0u)
				&& ((value & CAN_FMR_FINIT) == 0u)) {
			violation("CAN2SB mudado fora do FINIT");
		}
	}
}

/* ================== perifericos: reset e despacho ================== */

static void reset_usart1(void) {

	memset((void*) stm32_reg(USART1_BASE), 0, 0x400);
	R(USART1_BASE, USART_TypeDef, SR) = USART_SR_TXE | USART_SR_TC;
}

static void reset_tim2(void) {

	memset((void*) stm32_reg(TIM2_BASE), 0, 0x400);
	TIM2R(ARR) = 0xFFFFFFFFu;
	memset(&stm32->tim2, 0, sizeof(stm32->tim2));
}

static void log_write(uint32_t address, uint32_t old, uint32_t value) {

	if (stm32->log_count < STM32_LOG_MAX) {
		stm32_write_t *entry = &stm32->log[stm32->log_count];

		entry->seq = stm32->seq;
		entry->time_ns = stm32->now_ns;
		entry->address = address;
		entry->old_value = old;
		entry->value = value;
	}
	stm32->log_count++;
	stm32->seq++;
}

static void peripheral_write(uint32_t address, uint32_t old, uint32_t value) {

	uint32_t block = address & ~0x3FFu;
	uint32_t offset = address & 0x3FFu;

	log_write(address, old, value);
	if (!clock_on(block)) {
		violation("escrita em 0x%08x com o relogio do periferico desligado", address);
		*stm32_reg(address) = old;
		return;
	}

	switch (block) {
	case TIM2_BASE:
		tim2_write(offset, old, value);
		break;
	case IWDG_BASE:
		iwdg_write(offset, old, value);
		break;
	case CAN1_BASE:
		can_write(0, offset, old, value);
		break;
	case CAN2_BASE:
		can_write(1, offset, old, value);
		break;
	case PWR_BASE:
		break;
	case USART1_BASE:
		usart_write(offset, old, value);
		break;
	case GPIOA_BASE:
	case GPIOB_BASE:
	case GPIOC_BASE:
		gpio_write(block, offset, old, value);
		break;
	case RCC_BASE:
		rcc_write(offset, old, value);
		break;
	case FLASH_R_BASE:
		flash_reg_write(offset, old, value);
		break;
	default:
		violation("escrita num periferico fora do modelo: 0x%08x", address);
		break;
	}
}

/* ================== o uploader ================== */

void stm32_request(uint8_t bus, const uint8_t data[8], uint64_t not_before_ns, bool wait_reply) {

	if (stm32->request_count < STM32_REQUEST_MAX) {
		stm32_request_t *request = &stm32->requests[stm32->request_count++];

		request->bus = bus;
		memcpy(request->data, data, 8);
		request->not_before_ns = not_before_ns;
		request->wait_reply = wait_reply;
	}
}

static void client_step(void) {

	if (stm32->awaiting_reply) {

		uint8_t bus = stm32->requests[stm32->next_request - 1u].bus;
		uint8_t command = stm32->requests[stm32->next_request - 1u].data[0];

		while (stm32->awaiting_reply && (stm32->seen_tx[bus] < stm32->tx_count[bus]) && (stm32->seen_tx[bus] < STM32_TX_MAX)) {

			const stm32_frame_t *frame = &stm32->tx[bus][stm32->seen_tx[bus]++];

			// so a resposta a este comando (as das repeticoes de um pedido lento ficam de fora)
			if ((frame->id == 0x580u) && (frame->data[0] == (command | 0x80u))) {
				if (stm32->reply_count < STM32_REQUEST_MAX) {
					memcpy(stm32->replies[stm32->reply_count], frame->data, 8);
				}
				stm32->reply_count++;
				stm32->awaiting_reply = false;
			}
		}
		if (stm32->awaiting_reply) {
			// sem resposta (perdeu-se, o CAN ainda nao estava pronto): repete, como o can_upload.py
			if ((stm32->retry_ns != 0u) && ((stm32->now_ns - stm32->sent_ns) >= stm32->retry_ns)) {
				const stm32_request_t *request = &stm32->requests[stm32->next_request - 1u];

				stm32->sent_ns = stm32->now_ns;
				stm32->retries++;
				stm32_can_send(request->bus, 0x602u, request->data, 8, false, false);
			}
			return;
		}
	}

	if ((stm32->next_request < stm32->request_count) && (stm32->now_ns >= stm32->requests[stm32->next_request].not_before_ns)) {

		const stm32_request_t *request = &stm32->requests[stm32->next_request++];

		stm32->seen_tx[request->bus] = stm32->tx_count[request->bus];
		stm32->awaiting_reply = request->wait_reply;
		stm32->sent_ns = stm32->now_ns;
		stm32_can_send(request->bus, 0x602u, request->data, 8, false, false);
	}
}

/* ================== o que o bootloader chama diretamente ================== */

void stm32_irq_enable(int enable) {

	stm32->exit_irq_disabled = (enable == 0);
}

void stm32_barrier(void) {

	stm32->barriers++;
}

void stm32_system_reset(void) {

	finish(STM32_EXIT_RESET);
	__builtin_unreachable();
}

void start_application(uint32_t sp, uint32_t pc) __attribute__((noreturn));

void start_application(uint32_t sp, uint32_t pc) {

	stm32->exit_sp = sp;
	stm32->exit_pc = pc;
	stm32->exit_vtor = stm32_scb.VTOR;
	stm32->exit_systick_ctrl = stm32_systick.CTRL;
	finish(STM32_EXIT_JUMP);
	__builtin_unreachable();
}

/* ================== page fault + passo ================== */

#if defined(__x86_64__) && defined(__linux__)

static void on_segv(int signal_number, siginfo_t *info, void *context) {

	ucontext_t *uc = (ucontext_t*) context;
	uintptr_t address = (uintptr_t) info->si_addr;
	uintptr_t pc = (uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
	bool write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
	bool in_periph = (address >= PERIPH_START) && (address < (PERIPH_START + PERIPH_SIZE));
	bool in_flash = (address >= FLASH_START) && (address < (FLASH_START + FLASH_SIZE));

	(void) signal_number;
	if (!running || step.active || !(in_periph || (in_flash && write))) {
		crash(in_flash ? "escrita na flash" : "acesso fora do modelo", address, pc);
	}

	uint32_t aligned = (uint32_t) address & ~3u;
	uintptr_t page = address & ~(uintptr_t) (PAGE - 1u);

	if (page == TIM2_PAGE) {
		if (!write && (aligned == (TIM2_BASE + (uint32_t) offsetof(TIM_TypeDef, CNT)))) {
			advance((uint64_t) stm32->us_per_timer_read * 1000u);   // pode sair daqui (siglongjmp)
		}
		tim2_show();
	}

	step.active = true;
	step.page = page;
	step.prot = (page == TIM2_PAGE) ? PROT_NONE : PROT_READ;
	step.address = aligned;
	step.write = write;
	step.is_flash = in_flash;
	if (in_flash) {
		memcpy(&step.old, &flash[aligned - FLASH_START], 4);
	} else {
		step.old = *stm32_reg(aligned);
	}
	if (mprotect((void*) page, PAGE, PROT_READ | PROT_WRITE) != 0) {
		crash("mprotect", address, pc);
	}
	uc->uc_mcontext.gregs[REG_EFL] |= 0x100;   // TF: para depois desta instrucao
}

static void on_trap(int signal_number, siginfo_t *info, void *context) {

	ucontext_t *uc = (ucontext_t*) context;
	uint32_t value;

	(void) signal_number;
	(void) info;
	if (!step.active) {
		crash("SIGTRAP sem acesso pendente", 0, (uintptr_t) uc->uc_mcontext.gregs[REG_RIP]);
	}
	uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
	(void) mprotect((void*) step.page, PAGE, step.prot);
	step.active = false;

	if (step.is_flash) {
		memcpy(&value, &flash[step.address - FLASH_START], 4);
	} else {
		value = *stm32_reg(step.address);
	}
	if (step.write || (value != step.old)) {
		stm32->writes++;
		if (step.is_flash) {
			flash_program(step.address, step.old, value);
		} else {
			peripheral_write(step.address, step.old, value);
		}
	}
}

static void* map_shared(uintptr_t address, size_t size, int prot, uint8_t **alias) {

	int fd = memfd_create("stm32", 0);

	if ((fd < 0) || (ftruncate(fd, (off_t) size) != 0)) {
		return NULL;
	}

	void *mapped = mmap((void*) address, size, prot, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);

	if ((mapped == MAP_FAILED) || (mapped != (void*) address)) {
		close(fd);
		return NULL;
	}
	if (alias != NULL) {
		void *other = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

		if (other == MAP_FAILED) {
			close(fd);
			return NULL;
		}
		*alias = (uint8_t*) other;
	}
	close(fd);
	return mapped;
}

bool stm32_init(void) {

	void *shared = mmap(NULL, sizeof(stm32_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (shared == MAP_FAILED) {
		return false;
	}
	stm32 = (stm32_t*) shared;

	if ((map_shared(FLASH_START, FLASH_SIZE, PROT_READ, &flash) == NULL) || (map_shared(PERIPH_START, PERIPH_SIZE, PROT_READ, &periph) == NULL)
			|| (map_shared(RAM_PAGE, PAGE, PROT_READ | PROT_WRITE, NULL) == NULL) || (map_shared(DBG_PAGE, PAGE, PROT_READ | PROT_WRITE, NULL) == NULL)) {
		return false;
	}
	if (mprotect((void*) TIM2_PAGE, PAGE, PROT_NONE) != 0) {
		return false;
	}
	memset(flash, 0xFF, FLASH_SIZE);

	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = on_segv;
	if (sigaction(SIGSEGV, &action, NULL) != 0) {
		return false;
	}
	action.sa_sigaction = on_trap;
	return sigaction(SIGTRAP, &action, NULL) == 0;
}

#else

bool stm32_init(void) {

	return false;
}

#endif

/* ================== arranques ================== */

void stm32_power_on(bool keep_ram) {

	memset(periph, 0, PERIPH_SIZE);

	RCCR(CR) = 0x00000083u;
	RCCR(PLLCFGR) = 0x24003010u;
	RCCR(CSR) = stm32->reset_flags & 0xFE000000u;
	FLASHR(CR) = FLASH_CR_LOCK;
	FLASHR(OPTCR) = 0x0FFFAAEDu;
	R(PWR_BASE, PWR_TypeDef, CR) = 0x0000C000u;
	R(GPIOA_BASE, GPIO_TypeDef, MODER) = 0xA8000000u;
	R(GPIOA_BASE, GPIO_TypeDef, PUPDR) = 0x64000000u;
	R(GPIOB_BASE, GPIO_TypeDef, MODER) = 0x00000280u;
	R(GPIOB_BASE, GPIO_TypeDef, PUPDR) = 0x00000100u;
	R(GPIOB_BASE, GPIO_TypeDef, OSPEEDR) = 0x000000C0u;
	R(IWDG_BASE, IWDG_TypeDef, RLR) = 0x00000FFFu;
	reset_usart1();
	reset_tim2();
	can_reset_registers(0);
	can_reset_registers(1);
	*(volatile uint32_t*) (uintptr_t) (DBGMCU_BASE + offsetof(DBGMCU_TypeDef, APB1FZ)) = 0u;

	if (!keep_ram) {
		uint32_t state = 0x1234567u + (uint32_t) stm32->now_ns;

		for (uint32_t offset = 0; offset < PAGE; offset += 4u) {
			state = (state * 1103515245u) + 12345u;
			*(volatile uint32_t*) (uintptr_t) (RAM_PAGE + offset) = state;   // a RAM liga com lixo
		}
	}

	// o que aconteceu no arranque anterior sai; a configuracao fica
	stm32->now_ns = 0;
	stm32->exit_reason = STM32_EXIT_NONE;
	stm32->exit_signal = 0;
	stm32->exit_sp = stm32->exit_pc = stm32->exit_vtor = stm32->exit_systick_ctrl = 0;
	stm32->exit_irq_disabled = false;
	stm32->writes = 0;
	stm32->log_count = 0;
	stm32->violations = 0;
	memset(stm32->violation, 0, sizeof(stm32->violation));
	stm32->uart_length = 0;
	stm32->uart[0] = 0;
	stm32->uart_violations = 0;
	memset(stm32->tx_count, 0, sizeof(stm32->tx_count));
	memset(stm32->rx_dropped, 0, sizeof(stm32->rx_dropped));
	memset(stm32->rx_overruns, 0, sizeof(stm32->rx_overruns));
	memset(stm32->aborts, 0, sizeof(stm32->aborts));
	stm32->erase_count = 0;
	stm32->program_words = 0;
	stm32->overwrites = 0;
	stm32->cache_flushes = 0;
	stm32->peripheral_resets = 0;
	stm32->iwdg_started = false;
	stm32->iwdg_reloads = 0;
	stm32->iwdg_timeout_ms = 0;
	stm32->led_toggles = 0;
	stm32->led_last_ns = 0;
	stm32->led_period_ns = 0;
	stm32->barriers = 0;
	stm32->request_count = 0;
	stm32->next_request = 0;
	stm32->awaiting_reply = false;
	stm32->sent_ns = 0;
	stm32->retries = 0;
	memset(stm32->seen_tx, 0, sizeof(stm32->seen_tx));
	stm32->reply_count = 0;
	memset(stm32->user, 0, sizeof(stm32->user));
	memset(&stm32->flash, 0, sizeof(stm32->flash));
	memset(&stm32->iwdg, 0, sizeof(stm32->iwdg));
	stm32->seq = 0;
	update_clocks();
}

int stm32_boot(void (*body)(void)) {

	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();

	if (pid < 0) {
		return STM32_EXIT_CRASH;
	}
	if (pid == 0) {

		int reason;

		memset(&stm32_scb, 0, sizeof(stm32_scb));
		memset(&stm32_systick, 0, sizeof(stm32_systick));
		memset(&step, 0, sizeof(step));
		alarm(30);    // pendurado num ciclo sem acessos (nenhum cenario passa de 2 s): morre
		running = true;
		reason = sigsetjmp(exit_jump, 1);
		if (reason == 0) {
			body();
			reason = STM32_EXIT_RETURNED;
		}
		running = false;
		stm32->exit_reason = reason;
		exit(0);   // exit e nao _exit: o gcov escreve os contadores do filho
	}

	int status = 0;

	(void) waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		stm32->exit_reason = STM32_EXIT_CRASH;
		stm32->exit_signal = WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status);
	}
	return stm32->exit_reason;
}
