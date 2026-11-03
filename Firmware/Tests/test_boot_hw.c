/*
 * test_boot_hw.c - o bootloader inteiro (Bootloader/Src: startup, main,
 * bl_hw, bl_can, bl_flash e a logica) a correr num STM32F412 simulado
 * (stm32_model.c), com os registos nos enderecos verdadeiros. O test_boot.c
 * testa a logica com a flash num array; este testa o que so corria no
 * hardware: a ordem das saidas seguras, os relogios (HSE, PLL, latencia da
 * flash), o TIM2 a contar ms, o bit timing e os filtros do bxCAN, o driver
 * da flash (chaves, apagar, programar, caches), o IWDG e o salto para a
 * aplicacao (VTOR, MSP, perifericos e relogio repostos). Um upload passa
 * pelo bus CAN simulado trama a trama, como o can_upload.py.
 *
 * So corre em x86-64 Linux (page faults + passo a passo); nos outros diz
 * que salta. Correr com Tests/run_tests.sh.
 */

#undef main   // o -Dmain=bootloader_main e para o main.c do bootloader

#include "stm32_model.h"
#include "stm32f4xx.h"
#include "bl_can.h"
#include "bl_core.h"
#include "bl_protocol.h"
#include "boot_hash.h"
#include "boot_image.h"
#include "boot_journal.h"
#include "boot_shared.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// o que o startup.c copia e limpa (no ARM vem do linker). O _edata do PC e
// do linker (fim do .data do executavel todo): o startup.c compila-se com
// -D_edata=stm32_edata para nao copiar lixo por cima do .data inteiro
__asm__(
		".pushsection .rodata\n"
		".balign 4\n"
		".globl _sidata\n"
		"_sidata: .long 0x11111111, 0x22222222, 0x33333333\n"
		".popsection\n"
		".pushsection .data\n"
		".balign 4\n"
		".globl _sdata, stm32_edata, _sbss, _ebss, _estack\n"
		"_sdata: .long 0, 0, 0\n"
		"stm32_edata:\n"
		"_sbss: .long 0xDEADBEEF, 0xDEADBEEF\n"
		"_ebss:\n"
		"_estack: .long 0\n"
		".popsection\n");

extern uint32_t _sdata[];
extern uint32_t _sbss[];

void Reset_Handler(void);
void Fault_Handler(void);
bool BootFlash_EraseSector(uint32_t sector_address);
bool BootFlash_Program(uint32_t address, const void *data, uint32_t length);

#define MS            1000000ull
#define GPIO_REG(port, field)  (*stm32_reg((uint32_t) (port) + (uint32_t) offsetof(GPIO_TypeDef, field)))
#define RCC_REG(field)         (*stm32_reg(RCC_BASE + (uint32_t) offsetof(RCC_TypeDef, field)))
#define CAN_REG(base, field)   (*stm32_reg((uint32_t) (base) + (uint32_t) offsetof(CAN_TypeDef, field)))
#define FLASH_REG(field)       (*stm32_reg(FLASH_R_BASE + (uint32_t) offsetof(FLASH_TypeDef, field)))
#define ADDR(base, type, field) ((uint32_t) (base) + (uint32_t) offsetof(type, field))

/* ================== imagens (como no test_boot.c) ================== */

static void put32(uint8_t *out, uint32_t value) {

	memcpy(out, &value, 4);
}

static uint32_t get32(const uint8_t *in) {

	uint32_t value;

	memcpy(&value, in, 4);
	return value;
}

static void make_app(uint8_t *out, uint32_t size, uint8_t major, uint8_t minor, uint32_t seed) {

	boot_app_info_t info = { BOOT_APP_INFO_MAGIC, BOOT_BOARD_ID, major, minor, 0, 0, BOOT_APP_FEATURE_IWDG | BOOT_APP_FEATURE_CONFIRM, { 0, 0, 0 } };
	uint32_t state = (seed * 2654435761u) + 1u;

	memset(out, 0, size);
	put32(&out[0], BOOT_STACK_TOP);
	put32(&out[4], BOOT_APP_ADDRESS + 0x501u + (seed * 4u));
	for (uint32_t offset = 8; offset < 0x1C4u; offset += 4u) {
		put32(&out[offset], BOOT_APP_ADDRESS + 0x601u);
	}
	memcpy(&out[BOOT_APP_INFO_OFFSET], &info, sizeof(info));
	for (uint32_t offset = BOOT_APP_INFO_OFFSET + sizeof(info); offset < size; offset++) {
		state = (state * 1103515245u) + 12345u;
		out[offset] = (uint8_t) (state >> 16);
	}
	put32(&out[size - 4u], 0x12345678u ^ seed);
}

static uint32_t package(uint8_t *out, const uint8_t *app, uint32_t size, uint8_t major, uint8_t minor) {

	boot_image_header_t header = { BOOT_IMAGE_MAGIC, 0, BOOT_HEADER_SIZE, 0, size, 0, major, minor, 0, 7, 0 };
	boot_sha256_t sha;
	uint8_t digest[32];

	memset(out, 0, BOOT_HEADER_SIZE);
	memcpy(out, &header, sizeof(header));
	memcpy(&out[BOOT_HEADER_SIZE], app, size);
	BootSha256_Init(&sha);
	BootSha256_Update(&sha, out, BOOT_HEADER_SIZE + size);
	BootSha256_Final(&sha, digest);
	BootImage_MakeTlv(&out[BOOT_HEADER_SIZE + size], digest);
	return BOOT_HEADER_SIZE + size + BOOT_TLV_TOTAL;
}

static uint8_t app_v1[4096];
static uint8_t app_v2[2048];
static uint8_t image_v2[4096];
static uint32_t image_v2_length;

/* ================== o "PC" a volta do chip ================== */

static void hardware_ok(void) {

	stm32->hse_present = true;
	stm32->pll_locks = true;
	for (uint8_t bus = 0; bus < 2u; bus++) {
		stm32->can_init_ack[bus] = true;
		stm32->can_bus_idle[bus] = true;
		stm32->can_ack[bus] = true;
	}
	stm32->reset_flags = BL_RESET_POR | BL_RESET_PIN | BL_RESET_BOR;
	stm32->us_per_timer_read = 1000;   // 1 ms por leitura do TIM2: cada acesso apanhado custa ~60 us no PC
	stm32->retry_ns = 200u * MS;
	stm32->limit_ns = 0;
	stm32->warp_at_ns = 0;
	stm32->warp_by_ns = 0;
	stm32->flash_lock_stuck = false;
	stm32->flash_erase_error = 0;
	stm32->flash_program_error_at = -1;
	stm32->flash_program_error = 0;
	stm32->flash_stuck_offset = -1;
	stm32->flash_stuck_zero = 0;
	stm32->flash_stuck_one = 0;
	stm32->hook = NULL;
}

// a flash como sai do ST-LINK: tudo apagado e a aplicacao em 0x08010200
static void debugger_flash(const uint8_t *app, uint32_t size) {

	memset(stm32_flash(BOOT_JOURNAL_A_ADDRESS), 0xFF, (BOOT_FLASH_BASE + BOOT_FLASH_SIZE) - BOOT_JOURNAL_A_ADDRESS);
	memcpy(stm32_flash(BOOT_APP_ADDRESS), app, size);
}

static void blank_flash(void) {

	memset(stm32_flash(BOOT_JOURNAL_A_ADDRESS), 0xFF, (BOOT_FLASH_BASE + BOOT_FLASH_SIZE) - BOOT_JOURNAL_A_ADDRESS);
}

// a aplicacao pede o bootloader (ENTER_BOOT / START_PROGRAMMER) e faz reset
static void app_requests_bootloader(void) {

	boot_shared_t shared;

	memset(&shared, 0, sizeof(shared));
	shared.can1_kbps = 500;
	shared.can2_kbps = 500;
	BootShared_Seal(&shared);
	shared.request = BOOT_REQUEST_MAGIC;
	BootShared_Write(&shared);
}

static void run_bootloader(void) {

	Reset_Handler();
}

static int boot(void) {

	return stm32_boot(run_bootloader);
}

static void show_violations(void) {

	for (uint32_t index = 0; (index < stm32->violations) && (index < STM32_VIOLATION_MAX); index++) {
		printf("    violacao: %s\n", stm32->violation[index]);
	}
}

#define CHECK_CLEAN() do { CHECK(stm32->violations == 0u); if (stm32->violations != 0u) { show_violations(); } } while (0)

static bool uart_has(const char *text) {

	return strstr(stm32->uart, text) != NULL;
}

static uint32_t frames_with_id(uint8_t bus, uint32_t id) {

	uint32_t count = 0;

	for (uint32_t index = 0; (index < stm32->tx_count[bus]) && (index < STM32_TX_MAX); index++) {
		if (stm32->tx[bus][index].id == id) {
			count++;
		}
	}
	return count;
}

// o ultimo valor escrito num registo (o log tem o que o codigo escreveu)
static bool last_write(uint32_t address, uint32_t *value) {

	bool found = false;
	uint32_t count = (stm32->log_count < STM32_LOG_MAX) ? stm32->log_count : STM32_LOG_MAX;

	for (uint32_t index = 0; index < count; index++) {
		if (stm32->log[index].address == address) {
			*value = stm32->log[index].value;
			found = true;
		}
	}
	return found;
}

// bitrate e ponto de amostragem do BTR com o PCLK1 que havia
static uint32_t can_kbps(uint32_t btr, uint32_t pclk1, uint32_t *sample_permille) {

	uint32_t brp = (btr & CAN_BTR_BRP_Msk) + 1u;
	uint32_t ts1 = ((btr & CAN_BTR_TS1_Msk) >> CAN_BTR_TS1_Pos) + 1u;
	uint32_t ts2 = ((btr & CAN_BTR_TS2_Msk) >> CAN_BTR_TS2_Pos) + 1u;

	*sample_permille = ((1u + ts1) * 1000u) / (1u + ts1 + ts2);
	return pclk1 / (brp * (1u + ts1 + ts2) * 1000u);
}

static void request_bytes(uint8_t out[8], uint8_t command, uint32_t value, const uint8_t *payload) {

	memset(out, 0, 8);
	out[0] = command;
	if (command == BL_CMD_DATA) {
		out[1] = (uint8_t) value;
		out[2] = (uint8_t) (value >> 8);
		out[3] = (uint8_t) (value >> 16);
		memcpy(&out[4], payload, 4);
	} else {
		put32(&out[1], value);
	}
}

static void ask(uint8_t bus, uint8_t command, uint32_t value, uint64_t at_ns) {

	uint8_t data[8];

	request_bytes(data, command, value, NULL);
	stm32_request(bus, data, at_ns, true);
}

// INFO, BEGIN, DATA..., END (e RESET) pelo bus, como o can_upload.py
static void ask_upload(uint8_t bus, const uint8_t *image, uint32_t length, bool reset) {

	uint8_t data[8];

	ask(bus, BL_CMD_INFO, 0, 0);
	ask(bus, BL_CMD_BEGIN, length, 0);
	for (uint32_t offset = 0; offset < length; offset += 4u) {

		uint8_t chunk[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

		memcpy(chunk, &image[offset], ((length - offset) < 4u) ? (length - offset) : 4u);
		request_bytes(data, BL_CMD_DATA, offset, chunk);
		stm32_request(bus, data, 0, true);
	}
	ask(bus, BL_CMD_END, BootCrc32_Update(0, image, length), 0);
	if (reset) {
		ask(bus, BL_CMD_RESET, 0, 0);
	}
}

static uint8_t reply_status(uint32_t index) {

	return stm32->replies[index][1];
}

static uint32_t reply_value(uint32_t index) {

	return get32(&stm32->replies[index][2]);
}

/* ================== TESTES ================== */

// arranque a frio com a aplicacao do debugger: saidas seguras primeiro, relogios, CAN, salto
static void test_cold_boot(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	stm32->hook = NULL;

	int reason = boot();

	CHECK(reason == STM32_EXIT_JUMP);
	CHECK_CLEAN();

	// o salto: MSP e PC dos vetores da aplicacao, VTOR, SysTick parado, interrupcoes ligadas
	CHECK(stm32->exit_sp == BOOT_STACK_TOP);
	CHECK(stm32->exit_pc == get32(&app_v1[4]));
	CHECK(stm32->exit_vtor == BOOT_APP_ADDRESS);
	CHECK(stm32->exit_systick_ctrl == 0u);
	CHECK(!stm32->exit_irq_disabled);

	// sem janela num arranque a frio (POR): arranca logo
	CHECK(stm32->now_ns < (100u * MS));

	// saidas seguras: o nivel (BSRR) antes do pino virar saida, e tudo antes de mexer no relogio
	int32_t ams_level = stm32_find_write(ADDR(GPIOC_BASE, GPIO_TypeDef, BSRR), 1u << 8, 0);
	int32_t ams_mode = stm32_find_write(ADDR(GPIOC_BASE, GPIO_TypeDef, MODER), 1u << 16, 0);
	int32_t air_plus = stm32_find_write(ADDR(GPIOB_BASE, GPIO_TypeDef, BSRR), 1u << 0, 0);
	int32_t air_minus = stm32_find_write(ADDR(GPIOB_BASE, GPIO_TypeDef, BSRR), 1u << 1, 0);
	int32_t precharge = stm32_find_write(ADDR(GPIOC_BASE, GPIO_TypeDef, BSRR), 1u << 5, 0);
	int32_t precharge_mode = stm32_find_write(ADDR(GPIOC_BASE, GPIO_TypeDef, MODER), 1u << 10, 0);
	int32_t hse_on = stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, CR), RCC_CR_HSEON, 0);

	CHECK((ams_level >= 0) && (ams_mode > ams_level));
	CHECK((precharge >= 0) && (precharge_mode > precharge));
	CHECK((air_plus >= 0) && (air_minus >= 0));
	CHECK((hse_on > ams_mode) && (hse_on > air_plus) && (hse_on > air_minus) && (hse_on > precharge_mode));
	CHECK(ams_level < 8);   // logo a seguir a ligar os relogios dos GPIO

	// e ficam assim depois do salto (o bootloader nunca larga o AMS_ERROR nem fecha contactores)
	CHECK((GPIO_REG(GPIOC_BASE, ODR) & ((1u << 8) | (1u << 5))) == ((1u << 8) | (1u << 5)));
	CHECK((GPIO_REG(GPIOB_BASE, ODR) & 3u) == 3u);
	CHECK(((GPIO_REG(GPIOC_BASE, MODER) >> 16) & 3u) == 1u);
	CHECK(((GPIO_REG(GPIOB_BASE, MODER) >> 0) & 0xFu) == 0x5u);
	CHECK(((GPIO_REG(GPIOB_BASE, PUPDR) >> 0) & 0xFu) == 0x5u);   // pull-up nos AIR (abertos sem MCU)

	// relogio: PLL do HSE (8 / 4 * 64 / 2 = 64 MHz) com 1 WS posto ANTES da troca
	uint32_t pllcfgr = 0;
	int32_t latency = stm32_find_write(ADDR(FLASH_R_BASE, FLASH_TypeDef, ACR), FLASH_ACR_LATENCY_1WS, 0);
	int32_t to_pll = stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, CFGR), RCC_CFGR_SW_PLL, 0);

	CHECK(last_write(ADDR(RCC_BASE, RCC_TypeDef, PLLCFGR), &pllcfgr) || true);
	CHECK((stm32->log[stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, PLLCFGR), RCC_PLLCFGR_PLLSRC_HSE, 0)].value & RCC_PLLCFGR_PLLM_Msk) == 4u);
	CHECK((latency >= 0) && (to_pll > latency));
	CHECK(stm32_find_write(ADDR(PWR_BASE, PWR_TypeDef, CR), PWR_CR_VOS, 0) >= 0);

	// CAN a 500 kbit/s com amostragem a 87,5 % (PCLK1 = 32 MHz) nos dois
	uint32_t btr1 = 0;
	uint32_t btr2 = 0;
	uint32_t sample = 0;

	CHECK(last_write(ADDR(CAN1_BASE, CAN_TypeDef, BTR), &btr1) && last_write(ADDR(CAN2_BASE, CAN_TypeDef, BTR), &btr2));
	CHECK(can_kbps(btr1, 32000000u, &sample) == 500u);
	CHECK(sample == 875u);
	CHECK(can_kbps(btr2, 32000000u, &sample) == 500u);

	// heartbeat nos dois bus logo a arrancar
	CHECK((frames_with_id(0, BL_CAN_HEARTBEAT_ID) >= 1u) && (frames_with_id(1, BL_CAN_HEARTBEAT_ID) >= 1u));

	// UART1 a 230400 (BRR com PCLK2 = 32 MHz) e o texto
	uint32_t brr = 0;

	CHECK(last_write(ADDR(USART1_BASE, USART_TypeDef, BRR), &brr));
	CHECK((brr > 0u) && (abs((int) (32000000u / brr) - 230400) < (230400 / 50)));
	CHECK(uart_has("LART BMS bootloader 1.0.0"));
	CHECK(uart_has("CAN1 500 kbit/s ok, CAN2 500 kbit/s ok"));
	CHECK(uart_has("a arrancar v1.0"));
	CHECK(stm32->uart_violations == 0u);

	// as flags de reset lidas e limpas (RMVF)
	CHECK((RCC_REG(CSR) & 0xFE000000u) == 0u);
	CHECK(stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, CSR), RCC_CSR_RMVF, 0) >= 0);

	// a area partilhada para a aplicacao
	boot_shared_t shared;

	BootShared_Read(&shared);
	CHECK(BootShared_Valid(&shared));
	CHECK((shared.can1_kbps == 500u) && (shared.can2_kbps == 500u));
	CHECK(shared.reset_flags == (BL_RESET_POR | BL_RESET_PIN | BL_RESET_BOR));
	CHECK(shared.boot_version == BOOT_VERSION_PACKED);
	CHECK(shared.request == 0u);

	// a aplicacao recebe o chip como depois de um reset: HSI, PLL e HSE desligados, perifericos repostos
	CHECK(stm32->sysclk_hz == 16000000u);
	CHECK((RCC_REG(CR) & (RCC_CR_PLLON | RCC_CR_HSEON | RCC_CR_CSSON)) == 0u);
	CHECK((RCC_REG(CFGR) & RCC_CFGR_SWS_Msk) == RCC_CFGR_SWS_HSI);
	CHECK(RCC_REG(PLLCFGR) == 0x24003010u);
	CHECK((RCC_REG(APB1ENR) & (RCC_APB1ENR_CAN1EN | RCC_APB1ENR_CAN2EN | RCC_APB1ENR_TIM2EN)) == 0u);
	CHECK((RCC_REG(APB2ENR) & RCC_APB2ENR_USART1EN) == 0u);
	CHECK(CAN_REG(CAN1_BASE, MCR) == 0x00010002u);   // CAN1 reposto (o impulso no APB1RSTR)
	CHECK(CAN_REG(CAN2_BASE, MCR) == 0x00010002u);
	CHECK((RCC_REG(APB1RSTR) == 0u) && (RCC_REG(APB2RSTR) == 0u));   // e largado

	// a imagem do debugger nao arma o IWDG
	CHECK(!stm32->iwdg_started);
}

// no primeiro passo do tempo: o que o Reset_Handler fez ao .data e ao .bss
static void capture_startup(void) {

	if (stm32->user[5] == 0u) {
		stm32->user[0] = _sdata[0];
		stm32->user[1] = _sdata[1];
		stm32->user[2] = _sdata[2];
		stm32->user[3] = _sbss[0];
		stm32->user[4] = _sbss[1];
		stm32->user[5] = 1u;
	}
}

static void test_cold_boot_with_startup_capture(void) {

	// o mesmo arranque, agora a ver o startup (o hook corre no filho)
	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	stm32->hook = capture_startup;
	(void) boot();
	CHECK((stm32->user[5] == 1u) && (stm32->user[0] == 0x11111111u) && (stm32->user[1] == 0x22222222u) && (stm32->user[2] == 0x33333333u));
	CHECK((stm32->user[3] == 0u) && (stm32->user[4] == 0u));
}

// sem cristal: PLL do HSI (16 / 8 * 64 / 2), o CAN continua a 500 kbit/s
static void test_without_hse(void) {

	uint32_t btr = 0;
	uint32_t sample = 0;

	hardware_ok();
	stm32->hse_present = false;
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(uart_has("SEM HSE"));

	int32_t pll = stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, PLLCFGR), 0, 0);

	CHECK(pll >= 0);
	if (pll >= 0) {
		CHECK((stm32->log[pll].value & RCC_PLLCFGR_PLLSRC) == 0u);
		CHECK((stm32->log[pll].value & RCC_PLLCFGR_PLLM_Msk) == 8u);
	}
	CHECK(stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, CFGR), RCC_CFGR_SW_PLL, 0) >= 0);
	CHECK(last_write(ADDR(CAN1_BASE, CAN_TypeDef, BTR), &btr) && (can_kbps(btr, 32000000u, &sample) == 500u));
}

// o PLL nao tranca: fica no HSI a 16 MHz e as contas dos perifericos acompanham
static void test_pll_fails(void) {

	uint32_t btr = 0;
	uint32_t psc = 0;
	uint32_t brr = 0;
	uint32_t sample = 0;

	hardware_ok();
	stm32->pll_locks = false;
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(stm32_find_write(ADDR(RCC_BASE, RCC_TypeDef, CFGR), RCC_CFGR_SW_PLL, 0) < 0);   // nunca troca para um PLL sem tranca
	CHECK(last_write(ADDR(TIM2_BASE, TIM_TypeDef, PSC), &psc) && (psc == 15999u));
	CHECK(last_write(ADDR(CAN2_BASE, CAN_TypeDef, BTR), &btr) && (can_kbps(btr, 16000000u, &sample) == 500u) && (sample == 875u));
	CHECK(last_write(ADDR(USART1_BASE, USART_TypeDef, BRR), &brr) && (abs((int) (16000000u / brr) - 230400) < (230400 / 50)));
}

// o heartbeat a cada 500 ms com o tick em ms do TIM2 (o tempo do bootloader bate com o do chip)
static void test_timer_and_heartbeat(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	stm32->limit_ns = 3000u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(uart_has("LART BMS bootloader"));

	uint32_t beats = 0;
	uint64_t previous = 0;
	int bad_period = 0;
	int bad_tick = 0;

	for (uint32_t index = 0; index < stm32->tx_count[0]; index++) {

		const stm32_frame_t *frame = &stm32->tx[0][index];

		if (frame->id != BL_CAN_HEARTBEAT_ID) {
			continue;
		}
		uint32_t tick = get32(&frame->data[1]);
		int64_t error_ms = (int64_t) tick - (int64_t) (frame->time_ns / MS);

		if ((error_ms < -2) || (error_ms > 2)) {
			bad_tick++;
		}
		if ((beats > 1u) && (((frame->time_ns - previous) < (495u * MS)) || ((frame->time_ns - previous) > (505u * MS)))) {
			bad_period++;
		}
		previous = frame->time_ns;
		beats++;
	}
	CHECK((beats >= 5u) && (beats <= 8u));
	CHECK(bad_period == 0);
	CHECK(bad_tick == 0);
	CHECK(frames_with_id(1, BL_CAN_HEARTBEAT_ID) == beats);   // os dois bus

	// a espera de um upload: pisca devagar (250 ms)
	CHECK((stm32->led_toggles >= 8u) && (stm32->led_period_ns >= (249u * MS)) && (stm32->led_period_ns <= (251u * MS)));
}

// sem imagem nenhuma: fica no bootloader, pisca depressa
static void test_recovery_without_image(void) {

	hardware_ok();
	blank_flash();
	stm32_power_on(false);
	stm32->limit_ns = 2000u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(uart_has("decisao: ficar no bootloader"));
	CHECK((stm32->led_period_ns >= (99u * MS)) && (stm32->led_period_ns <= (101u * MS)));
	CHECK(frames_with_id(0, BL_CAN_HEARTBEAT_ID) >= 3u);
}

// upload completo pelo CAN2 (trama a trama), RESET, e o arranque seguinte com o IWDG
static void test_upload_and_boot_with_iwdg(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	stm32->reset_flags = BL_RESET_SOFT | BL_RESET_PIN;
	stm32_power_on(true);
	ask_upload(1, image_v2, image_v2_length, true);
	stm32->limit_ns = 60000u * MS;

	int reason = boot();

	CHECK(reason == STM32_EXIT_RESET);
	CHECK_CLEAN();
	CHECK(stm32->next_request == stm32->request_count);
	CHECK(stm32->reply_count == stm32->request_count);

	int bad = 0;

	for (uint32_t index = 0; index < stm32->reply_count; index++) {
		if (reply_status(index) != BL_OK) {
			bad++;
		}
	}
	CHECK(bad == 0);
	CHECK(reply_value(0) == BOOT_IMAGE_MAX);   // INFO
	CHECK((stm32->reply_count >= 2u) && (stm32->replies[stm32->reply_count - 2u][7] == 2u));   // END: validado e instalado
	CHECK(frames_with_id(0, BL_CAN_RESPONSE_ID) == 0u);   // responde so no bus do pedido

	// a flash: a imagem nova no EXEC, a antiga no BACKUP, os setores do bootloader intactos
	CHECK(memcmp(stm32_flash(BOOT_EXEC_ADDRESS), image_v2, image_v2_length) == 0);
	CHECK(memcmp(stm32_flash(BOOT_BACKUP_ADDRESS + BOOT_HEADER_SIZE), app_v1, sizeof(app_v1)) == 0);
	CHECK(stm32->overwrites == 0u);
	CHECK(stm32->erase_count > 0u);

	int low_sector = 0;

	for (uint32_t index = 0; (index < stm32->erase_count) && (index < 64u); index++) {
		if (stm32->erases[index] < 2u) {
			low_sector++;
		}
	}
	CHECK(low_sector == 0);
	CHECK(stm32->cache_flushes > 0u);
	CHECK((FLASH_REG(CR) & FLASH_CR_LOCK) != 0u);   // fica trancada

	// o arranque depois do RESET (RAM mantida): a imagem nova, em teste, com o IWDG a ~4 s
	hardware_ok();
	stm32->reset_flags = BL_RESET_SOFT | BL_RESET_PIN;
	stm32_power_on(true);

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(stm32->exit_pc == get32(&app_v2[4]));
	CHECK(stm32->iwdg_started);
	CHECK((stm32->iwdg_timeout_ms >= (BOOT_IWDG_TIMEOUT_MS - 50u)) && (stm32->iwdg_timeout_ms <= (BOOT_IWDG_TIMEOUT_MS + 50u)));
	CHECK((*(volatile uint32_t*) (uintptr_t) (DBGMCU_BASE + offsetof(DBGMCU_TypeDef, APB1FZ)) & DBGMCU_APB1_FZ_DBG_IWDG_STOP) != 0u);
	CHECK(uart_has("IWDG armado"));

	boot_shared_t shared;

	BootShared_Read(&shared);
	CHECK(BootShared_Valid(&shared) && ((shared.flags & BOOT_SHARED_FLAG_IWDG) != 0u) && ((shared.flags & BOOT_SHARED_FLAG_BACKUP) != 0u));
}

// depois de um reset do watchdog: 500 ms a espera do uploader, depois arranca
static void test_window_after_watchdog(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32->reset_flags = BL_RESET_IWDG | BL_RESET_PIN;
	stm32_power_on(false);

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(uart_has("janela de 500 ms"));
	CHECK((stm32->now_ns >= (500u * MS)) && (stm32->now_ns < (700u * MS)));

	// e com o uploader a aparecer aos 100 ms: fica
	hardware_ok();
	stm32->reset_flags = BL_RESET_IWDG | BL_RESET_PIN;
	stm32_power_on(false);
	ask(0, BL_CMD_INFO, 0, 100u * MS);
	stm32->limit_ns = 1500u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(uart_has("uploader na janela"));
	CHECK((stm32->reply_count == 1u) && (reply_status(0) == BL_OK));
}

// sessao abandonada a meio: 30 s depois faz reset (o tempo salta para nao os correr)
static void test_inactivity_reset(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	ask(0, BL_CMD_INFO, 0, 0);
	ask(0, BL_CMD_BEGIN, image_v2_length, 0);
	stm32->warp_at_ns = 4000u * MS;
	stm32->warp_by_ns = 30000u * MS;

	CHECK(boot() == STM32_EXIT_RESET);
	CHECK_CLEAN();
	CHECK(uart_has("30 s sem comandos"));
	CHECK((stm32->reply_count == 2u) && (reply_status(1) == BL_OK));
	CHECK(frames_with_id(0, BL_CAN_DIAG_BASE_ID + BL_DIAG_ERROR) >= 1u);
}

// pedido sem sessao: ao fim de 5 min volta a aplicacao
static void test_idle_exit(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	ask(0, BL_CMD_INFO, 0, 0);
	stm32->warp_at_ns = 1000u * MS;
	stm32->warp_by_ns = 300000u * MS;

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(uart_has("5 min sem uploader"));
	CHECK(stm32->exit_pc == get32(&app_v1[4]));
}

/* ================== CAN: falhas e filtros ================== */

// sem ACK no CAN2: a resposta fica na caixa e e abortada aos 100 ms; o CAN1 continua normal
static void test_can_without_ack(void) {

	hardware_ok();
	stm32->can_ack[1] = false;
	stm32->retry_ns = 0;
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	ask(1, BL_CMD_INFO, 0, 50u * MS);
	stm32->limit_ns = 1000u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(stm32->tx_count[1] == 0u);
	CHECK(stm32->aborts[1] == 1u);
	CHECK(frames_with_id(0, BL_CAN_HEARTBEAT_ID) >= 2u);
	CHECK(frames_with_id(0, BL_CAN_DIAG_BASE_ID + BL_DIAG_ERROR) >= 1u);   // o timeout chega pelo CAN1

	// o heartbeat e o diagnostico deixam sempre uma caixa livre para as respostas
	uint32_t tsr = CAN_REG(CAN2_BASE, TSR);
	uint32_t free_boxes = ((tsr & CAN_TSR_TME0) ? 1u : 0u) + ((tsr & CAN_TSR_TME1) ? 1u : 0u) + ((tsr & CAN_TSR_TME2) ? 1u : 0u);

	CHECK(free_boxes == 1u);
}

// o CAN1 nao responde ao INRQ: fica de fora; o CAN2 e o arranque continuam
static void test_can_init_fails(void) {

	hardware_ok();
	stm32->can_init_ack[0] = false;
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);

	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(uart_has("CAN1 500 kbit/s FALHOU, CAN2 500 kbit/s ok"));
	CHECK(stm32->tx_count[0] == 0u);
	CHECK(frames_with_id(1, BL_CAN_HEARTBEAT_ID) >= 1u);
	CHECK(stm32_find_write(ADDR(CAN1_BASE, CAN_TypeDef, BTR), 0, 0) < 0);   // nao configura o que nao entrou em init
}

static void bus_appears(void) {

	if (stm32->now_ns >= (1000u * MS)) {
		stm32->can_bus_idle[1] = true;
	}
}

// o CAN2 sem bus ao arrancar ("sem bus"): o que ficou nas caixas sai quando o bus aparece
static void test_can_bus_appears_later(void) {

	hardware_ok();
	stm32->can_bus_idle[1] = false;
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	stm32->hook = bus_appears;
	ask(1, BL_CMD_INFO, 0, 1200u * MS);
	stm32->limit_ns = 2000u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(uart_has("CAN2 500 kbit/s sem bus"));
	CHECK((stm32->tx_count[1] > 0u) && (stm32->tx[1][0].time_ns >= (1000u * MS)));
	CHECK(stm32->tx[1][0].id == BL_CAN_HEARTBEAT_ID);   // as pendentes saem por ordem de ID
	CHECK((stm32->reply_count == 1u) && (reply_status(0) == BL_OK));
}

static void send_noise(void) {

	static bool sent;
	static const uint8_t info[8] = { BL_CMD_INFO };

	if (!sent && (stm32->now_ns >= (100u * MS))) {
		sent = true;
		stm32_can_send(0, 0x601u, info, 8, false, false);   // outro ID
		stm32_can_send(0, 0x602u, info, 8, true, false);    // estendida com o mesmo numero
		stm32_can_send(0, 0x602u, NULL, 0, false, true);    // RTR
		stm32_can_send(1, 0x603u, info, 8, false, false);
	}
}

// filtros: so 0x602 standard de dados, um banco por bus (0 e 14); responde no bus de onde veio
static void test_can_filters(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	stm32->hook = send_noise;
	ask(1, BL_CMD_INFO, 0, 200u * MS);
	ask(0, BL_CMD_INFO, 0, 300u * MS);
	stm32->limit_ns = 600u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK((stm32->rx_dropped[0] == 3u) && (stm32->rx_dropped[1] == 1u));
	CHECK(stm32->reply_count == 2u);
	CHECK((frames_with_id(0, BL_CAN_RESPONSE_ID) == 1u) && (frames_with_id(1, BL_CAN_RESPONSE_ID) == 1u));

	uint32_t fr1 = *stm32_reg(CAN1_BASE + (uint32_t) offsetof(CAN_TypeDef, sFilterRegister));
	uint32_t fr2 = *stm32_reg(CAN1_BASE + (uint32_t) offsetof(CAN_TypeDef, sFilterRegister) + 4u);

	CHECK(CAN_REG(CAN1_BASE, FA1R) == ((1u << 0) | (1u << 14)));
	CHECK(((CAN_REG(CAN1_BASE, FMR) & CAN_FMR_CAN2SB_Msk) >> CAN_FMR_CAN2SB_Pos) == 14u);
	CHECK((CAN_REG(CAN1_BASE, FMR) & CAN_FMR_FINIT) == 0u);
	CHECK((CAN_REG(CAN1_BASE, FS1R) & ((1u << 0) | (1u << 14))) == ((1u << 0) | (1u << 14)));
	CHECK((CAN_REG(CAN1_BASE, FM1R) & ((1u << 0) | (1u << 14))) == 0u);
	CHECK((fr1 == (BL_CAN_REQUEST_ID << 21)) && (fr2 == ((0x7FFu << 21) | CAN_RI0R_IDE | CAN_RI0R_RTR)));
	CHECK((CAN_REG(CAN1_BASE, MCR) & CAN_MCR_ABOM) != 0u);   // sai sozinho de bus-off
}

static void flood(void) {

	static bool sent;
	static const uint8_t info[8] = { BL_CMD_INFO };

	if (!sent && (stm32->now_ns >= (100u * MS))) {
		sent = true;
		for (int index = 0; index < 4; index++) {
			stm32_can_send(0, BL_CAN_REQUEST_ID, info, 8, false, false);
		}
	}
}

// 4 pedidos de uma vez na FIFO de 3: o bootloader limpa o FOVR e serve os 3 que ficaram
static void test_can_fifo_overrun(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	stm32->hook = flood;
	stm32->limit_ns = 500u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK_CLEAN();
	CHECK(stm32->rx_overruns[0] == 1u);
	CHECK(stm32_find_write(ADDR(CAN1_BASE, CAN_TypeDef, RF0R), CAN_RF0R_FOVR0, 0) >= 0);
	CHECK((CAN_REG(CAN1_BASE, RF0R) & CAN_RF0R_FOVR0) == 0u);
	CHECK(frames_with_id(0, BL_CAN_RESPONSE_ID) == 3u);
}

/* ================== driver da flash ================== */

static void save_diag(uint32_t first, bool ok) {

	stm32->user[first] = ok ? 1u : 0u;
	stm32->user[first + 1u] = bl_diag.error_code;
	stm32->user[first + 2u] = bl_diag.flash_offset;
	stm32->user[first + 3u] = bl_diag.flash_error;
	memset(&bl_diag, 0, sizeof(bl_diag));
}

static const uint8_t pattern[8] = { 0x01, 0x02, 0x03, 0x04, 0x55, 0xAA, 0x00, 0xF0 };

static void body_flash_ok(void) {

	save_diag(0, BootFlash_EraseSector(0x08010000u));
	save_diag(4, BootFlash_Program(0x08010000u, pattern, 8));
}

static void body_flash_refusals(void) {

	save_diag(0, BootFlash_EraseSector(0x08000000u));    // S0: o bootloader
	save_diag(4, BootFlash_EraseSector(0x08004000u));    // S1
	save_diag(8, BootFlash_EraseSector(0x08010004u));    // nao e o inicio de um setor
	save_diag(12, BootFlash_EraseSector(0x08080000u));   // fora da flash
	save_diag(16, BootFlash_Program(0x08010002u, pattern, 4));   // desalinhado
	save_diag(20, BootFlash_Program(0x08010010u, pattern, 3));   // comprimento
	save_diag(24, BootFlash_Program(0x08007FFCu, pattern, 4));   // o bootloader
	save_diag(28, BootFlash_Program(0x0807FFFCu, pattern, 8));   // passa do fim
}

static void body_erase(void) {

	save_diag(0, BootFlash_EraseSector(0x08010000u));
}

static void body_program(void) {

	save_diag(0, BootFlash_Program(0x08010000u, pattern, 8));
}

static void flash_case(void (*body)(void)) {

	hardware_ok();
	stm32_power_on(false);
}

static void test_flash_driver(void) {

	// apagar e programar: chaves, SER/SNB/PSIZE x32, PG, caches, e trancada no fim
	flash_case(NULL);
	memset(stm32_flash(0x08010000u), 0x5A, 0x10000);
	CHECK(stm32_boot(body_flash_ok) == STM32_EXIT_RETURNED);
	CHECK_CLEAN();
	CHECK((stm32->user[0] == 1u) && (stm32->user[4] == 1u));
	CHECK((stm32->erase_count == 1u) && (stm32->erases[0] == 4u));
	CHECK(stm32->now_ns >= (550u * MS));   // o CPU fica parado durante o apagar; o tempo corre
	CHECK(memcmp(stm32_flash(0x08010000u), pattern, 8) == 0);
	CHECK(stm32_flash(0x08010008u)[0] == 0xFFu);
	CHECK(stm32_flash(0x0801FFFFu)[0] == 0xFFu);
	CHECK(stm32->program_words == 2u);
	CHECK(stm32->overwrites == 0u);
	CHECK(stm32->cache_flushes == 2u);
	CHECK((FLASH_REG(CR) & FLASH_CR_LOCK) != 0u);

	int32_t start = stm32_find_write(ADDR(FLASH_R_BASE, FLASH_TypeDef, CR), FLASH_CR_STRT, 0);

	CHECK(start >= 0);
	if (start >= 0) {
		uint32_t cr = stm32->log[start].value;

		CHECK(((cr & FLASH_CR_SER) != 0u) && (((cr & FLASH_CR_SNB_Msk) >> FLASH_CR_SNB_Pos) == 4u) && (((cr & FLASH_CR_PSIZE_Msk) >> FLASH_CR_PSIZE_Pos) == 2u));
	}

	// recusas antes de tocar no hardware
	flash_case(NULL);
	CHECK(stm32_boot(body_flash_refusals) == STM32_EXIT_RETURNED);
	CHECK_CLEAN();
	CHECK((stm32->erase_count == 0u) && (stm32->program_words == 0u));

	int accepted = 0;

	for (uint32_t index = 0; index < 32u; index += 4u) {
		accepted += (int) stm32->user[index];
	}
	CHECK(accepted == 0);
	CHECK((stm32->user[1] == BOOT_ERR_FLASH_ERASE) && (stm32->user[13] == BOOT_ERR_FLASH_ERASE));
	CHECK((stm32->user[17] == BOOT_ERR_FLASH_WRITE) && (stm32->user[29] == BOOT_ERR_FLASH_WRITE));
	CHECK(stm32->user[26] == 0x7FFCu);   // offset no diagnostico

	// as chaves nao desbloqueiam: nem apaga nem programa
	flash_case(NULL);
	stm32->flash_lock_stuck = true;
	CHECK(stm32_boot(body_erase) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_UNLOCK) && (stm32->erase_count == 0u));
	flash_case(NULL);
	stm32->flash_lock_stuck = true;
	CHECK(stm32_boot(body_program) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_UNLOCK) && (stm32->program_words == 0u));

	// o apagar da erro (WRPERR): falha com o SR no diagnostico, e a flash fica trancada
	flash_case(NULL);
	stm32->flash_erase_error = FLASH_SR_WRPERR;
	CHECK(stm32_boot(body_erase) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_ERASE) && (stm32->user[3] == FLASH_SR_WRPERR));
	CHECK((FLASH_REG(CR) & FLASH_CR_LOCK) != 0u);

	// um bit que nao apaga: a verificacao apanha-o e diz onde
	flash_case(NULL);
	stm32->flash_stuck_offset = 0x10100;
	stm32->flash_stuck_zero = 0x00000100u;
	CHECK(stm32_boot(body_erase) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_ERASE) && (stm32->user[2] == 0x10100u));

	// o programar da erro na segunda palavra: para ai
	flash_case(NULL);
	memset(stm32_flash(0x08010000u), 0xFF, 16);
	stm32->flash_program_error_at = 0x10004;
	stm32->flash_program_error = FLASH_SR_PGSERR;
	CHECK(stm32_boot(body_program) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_WRITE) && (stm32->user[3] == FLASH_SR_PGSERR));
	CHECK(stm32->program_words == 1u);

	// um bit preso a 1: o hardware nao se queixa, a comparacao no fim sim
	flash_case(NULL);
	memset(stm32_flash(0x08010000u), 0xFF, 16);
	stm32->flash_stuck_offset = 0x10000;
	stm32->flash_stuck_one = 0x00000080u;
	CHECK(stm32_boot(body_program) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == 0u) && (stm32->user[1] == BOOT_ERR_FLASH_WRITE) && (stm32->user[3] == 0u));
}

/* ================== falhas, watchdog, confirmacao ================== */

static void body_fault(void) {

	Fault_Handler();
}

// HardFault e companhia: saidas seguras e reset
static void test_fault_handler(void) {

	hardware_ok();
	stm32_power_on(false);
	CHECK(stm32_boot(body_fault) == STM32_EXIT_RESET);
	CHECK_CLEAN();
	CHECK((GPIO_REG(GPIOC_BASE, ODR) & (1u << 8)) != 0u);
	CHECK(((GPIO_REG(GPIOC_BASE, MODER) >> 16) & 3u) == 1u);
	CHECK((GPIO_REG(GPIOB_BASE, ODR) & 3u) == 3u);
}

// pelo bus: debugger v1, pedido, upload do v2 pelo CAN1 e RESET
static void install_v2(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	ask_upload(0, image_v2, image_v2_length, true);
	stm32->limit_ns = 60000u * MS;
	CHECK(boot() == STM32_EXIT_RESET);
	CHECK_CLEAN();
}

// a imagem nova nunca refresca o IWDG: 3 resets do watchdog e volta ao v1
static void test_watchdog_rollback(void) {

	install_v2();
	hardware_ok();
	stm32->reset_flags = BL_RESET_SOFT | BL_RESET_PIN;
	stm32_power_on(true);
	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK((stm32->exit_pc == get32(&app_v2[4])) && stm32->iwdg_started);

	for (uint32_t attempt = 0; attempt < BOOT_MAX_FAILURES; attempt++) {
		hardware_ok();
		stm32->reset_flags = BL_RESET_IWDG;
		stm32_power_on(true);
		CHECK(boot() == STM32_EXIT_JUMP);
		CHECK_CLEAN();
	}
	CHECK(stm32->exit_pc == get32(&app_v1[4]));
	CHECK(memcmp(stm32_flash(BOOT_APP_ADDRESS), app_v1, sizeof(app_v1)) == 0);
	CHECK(frames_with_id(0, BL_CAN_DIAG_BASE_ID + BL_DIAG_ROLLBACK) >= 1u);
	CHECK(!stm32->iwdg_started);

	boot_shared_t shared;

	BootShared_Read(&shared);
	CHECK(BootShared_Valid(&shared) && (shared.event == BOOT_EVENT_ROLLBACK) && (shared.reset_flags == BL_RESET_IWDG));
}

// o que o boot_ctrl.c da aplicacao faz depois de correr bem (aqui com o driver do bootloader)
static void body_confirm(void) {

	boot_journal_t journal;
	boot_record_t record;

	BootJournal_Load(&journal);
	record = journal.state;
	stm32->user[0] = record.exec_state;
	record.exec_state = BOOT_EXEC_CONFIRMED;
	record.event = BOOT_EVENT_CONFIRMED;
	stm32->user[1] = BootJournal_Write(&journal, &record, false) ? 1u : 0u;
}

// confirmada pela aplicacao: os arranques seguintes ja nao armam o IWDG
static void test_confirmed_image(void) {

	install_v2();
	hardware_ok();
	stm32->reset_flags = BL_RESET_SOFT | BL_RESET_PIN;
	stm32_power_on(true);
	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK(stm32->iwdg_started);

	CHECK(stm32_boot(body_confirm) == STM32_EXIT_RETURNED);
	CHECK((stm32->user[0] == BOOT_EXEC_PENDING) && (stm32->user[1] == 1u));
	CHECK_CLEAN();

	hardware_ok();
	stm32_power_on(false);
	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK((stm32->exit_pc == get32(&app_v2[4])) && !stm32->iwdg_started);
}

/* ================== o rollback falha no chip ================== */

// v1 do debugger, BEGIN (v1 vai para o BACKUP, EXEC apagado) e RESET: o arranque seguinte tem de repor
static void abandon_upload(void) {

	hardware_ok();
	debugger_flash(app_v1, sizeof(app_v1));
	stm32_power_on(false);
	app_requests_bootloader();
	ask(0, BL_CMD_INFO, 0, 0);
	ask(0, BL_CMD_BEGIN, image_v2_length, 0);
	ask(0, BL_CMD_RESET, 0, 0);
	stm32->limit_ns = 30000u * MS;
	CHECK(boot() == STM32_EXIT_RESET);
	CHECK_CLEAN();
}

// o apagar do EXEC da erro durante o rollback: fica no bootloader e volta a tentar 5 min depois
static void test_rollback_erase_error(void) {

	abandon_upload();

	hardware_ok();
	stm32->reset_flags = BL_RESET_SOFT | BL_RESET_PIN;
	stm32_power_on(true);
	app_requests_bootloader();
	stm32->flash_erase_error = FLASH_SR_WRPERR;
	stm32->warp_at_ns = 1000u * MS;
	stm32->warp_by_ns = 300000u * MS;
	stm32->limit_ns = 303000u * MS;

	CHECK(boot() == STM32_EXIT_LIMIT);
	CHECK(uart_has("decisao: repor o BACKUP e arrancar"));
	CHECK(uart_has("5 min sem uploader"));
	CHECK(uart_has("rollback: erro na flash"));
	CHECK(uart_has("nada para arrancar"));
	CHECK(frames_with_id(0, BL_CAN_DIAG_BASE_ID + BL_DIAG_FLASH) >= 1u);
	CHECK((stm32->led_period_ns >= (99u * MS)) && (stm32->led_period_ns <= (101u * MS)));   // agora sem nada: pisca depressa

	// sem o erro, o arranque seguinte repoe o v1
	hardware_ok();
	stm32_power_on(false);
	CHECK(boot() == STM32_EXIT_JUMP);
	CHECK_CLEAN();
	CHECK(stm32->exit_pc == get32(&app_v1[4]));
	CHECK(uart_has("rollback: EXEC reposto"));
}

/* ================== bl_can.c fora do caminho normal ================== */

static void body_can_edges(void) {

	uint32_t id = 0;
	uint8_t data[8];
	uint8_t dlc = 0;
	static const uint8_t info[8] = { BL_CMD_INFO };

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

	// PCLK1 baixo demais para o bitrate: prescaler 1 em vez de 0
	stm32->user[0] = BlCan_Init(BL_CAN1, 1000, 8000000u, BL_CAN_REQUEST_ID) ? 1u : 0u;
	stm32->user[1] = CAN1->BTR & CAN_BTR_BRP_Msk;

	// um filtro que deixa passar tudo (como se estivesse mal configurado): o driver recusa estendidas e RTR
	CAN1->FMR |= CAN_FMR_FINIT;
	CAN1->FA1R &= ~1u;
	CAN1->sFilterRegister[0].FR2 = 0u;
	CAN1->FA1R |= 1u;
	CAN1->FMR &= ~CAN_FMR_FINIT;
	stm32_can_send(0, BL_CAN_REQUEST_ID, info, 8, true, false);
	stm32_can_send(0, BL_CAN_REQUEST_ID, NULL, 0, false, true);
	stm32_can_send(0, BL_CAN_REQUEST_ID, info, 8, false, false);
	stm32->user[2] = BlCan_Receive(BL_CAN1, &id, data, &dlc) ? 1u : 0u;
	stm32->user[3] = BlCan_Receive(BL_CAN1, &id, data, &dlc) ? 1u : 0u;
	stm32->user[4] = BlCan_Receive(BL_CAN1, &id, data, &dlc) ? 1u : 0u;
	stm32->user[5] = id;
	stm32->user[6] = BlCan_Receive(BL_CAN1, &id, data, &dlc) ? 1u : 0u;   // vazia
}

static void test_can_driver_edges(void) {

	hardware_ok();
	stm32_power_on(false);
	CHECK(stm32_boot(body_can_edges) == STM32_EXIT_RETURNED);
	CHECK_CLEAN();
	CHECK((stm32->user[0] == 1u) && (stm32->user[1] == 0u));
	CHECK((stm32->user[2] == 0u) && (stm32->user[3] == 0u));   // estendida e RTR saem da FIFO mas nao contam
	CHECK((stm32->user[4] == 1u) && (stm32->user[5] == BL_CAN_REQUEST_ID));
	CHECK(stm32->user[6] == 0u);
	CHECK((CAN_REG(CAN1_BASE, RF0R) & CAN_RF0R_FMP0) == 0u);
}

int main(void) {

	if (!stm32_init()) {
		printf("test_boot_hw: salta (precisa de x86-64 Linux e dos enderecos do F412 livres)\n");
		return 0;
	}

	make_app(app_v1, sizeof(app_v1), 1, 0, 1);
	make_app(app_v2, sizeof(app_v2), 2, 0, 2);
	image_v2_length = package(image_v2, app_v2, sizeof(app_v2), 2, 0);

	struct {
		const char *name;
		void (*run)(void);
	} tests[] = {
		{ "test_cold_boot", test_cold_boot },
		{ "test_cold_boot_with_startup_capture", test_cold_boot_with_startup_capture },
		{ "test_without_hse", test_without_hse },
		{ "test_pll_fails", test_pll_fails },
		{ "test_timer_and_heartbeat", test_timer_and_heartbeat },
		{ "test_recovery_without_image", test_recovery_without_image },
		{ "test_upload_and_boot_with_iwdg", test_upload_and_boot_with_iwdg },
		{ "test_window_after_watchdog", test_window_after_watchdog },
		{ "test_inactivity_reset", test_inactivity_reset },
		{ "test_idle_exit", test_idle_exit },
		{ "test_can_without_ack", test_can_without_ack },
		{ "test_can_init_fails", test_can_init_fails },
		{ "test_can_bus_appears_later", test_can_bus_appears_later },
		{ "test_can_filters", test_can_filters },
		{ "test_can_fifo_overrun", test_can_fifo_overrun },
		{ "test_flash_driver", test_flash_driver },
		{ "test_fault_handler", test_fault_handler },
		{ "test_watchdog_rollback", test_watchdog_rollback },
		{ "test_confirmed_image", test_confirmed_image },
		{ "test_rollback_erase_error", test_rollback_erase_error },
		{ "test_can_driver_edges", test_can_driver_edges },
	};

	for (size_t index = 0; index < (sizeof(tests) / sizeof(tests[0])); index++) {

		int before = failures;

		tests[index].run();
		printf("%s %s\n", (failures == before) ? "ok    " : "FALHOU", tests[index].name);
		if (failures != before) {
			printf("    saida: %s, t = %llu ms, %u escritas\n    uart: %.300s\n", stm32_exit_name(stm32->exit_reason),
					(unsigned long long) (stm32->now_ns / MS), stm32->writes, stm32->uart);
		}
	}

	printf("\n%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
