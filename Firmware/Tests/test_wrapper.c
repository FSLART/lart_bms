/*
 * test_wrapper.c - testes do mcuWrapper.c no PC: timer de us, pulsos de CS
 * para acordar o isoSPI e contagem de erros do SPI.
 *
 * O TIM5 falso anda 1 us por cada leitura do contador, por isso as esperas
 * ativas do wrapper acabam. Correr com Tests/run_tests.sh.
 */

#include "mcuWrapper.h"
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* ================== HAL FALSO ================== */

TIM_TypeDef fake_tim5;
RCC_TypeDef fake_rcc;
GPIO_TypeDef fake_gpioa;
TIM_HandleTypeDef htim5 = { .Instance = &fake_tim5 };
SPI_HandleTypeDef hspi1;

static uint32_t pclk1_hz = 32000000u;
static HAL_StatusTypeDef spi_result = HAL_OK;

typedef struct {
	GPIO_PinState state;
	uint32_t time_us;
} edge_t;

static edge_t edges[64];
static uint32_t edge_count;

uint32_t fake_tim_counter(TIM_HandleTypeDef *htim) {

	return ++htim->Instance->CNT;
}

uint32_t HAL_RCC_GetPCLK1Freq(void) {

	return pclk1_hz;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *htim) {

	htim->Instance->CR1 |= TIM_CR1_CEN;
	return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {

	if ((port == BMS_CS_GPIO_Port) && (pin == BMS_CS_Pin) && (edge_count < 64u)) {
		edges[edge_count].state = state;
		edges[edge_count].time_us = fake_tim5.CNT;
		edge_count++;
	}
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size, uint32_t timeout) {

	(void) hspi;
	(void) data;
	(void) size;
	CHECK(timeout < 100u);   // nunca HAL_MAX_DELAY: um SPI encravado nao pode prender o loop
	return spi_result;
}

HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *data, uint16_t size, uint32_t timeout) {

	(void) hspi;
	(void) data;
	(void) size;
	CHECK(timeout < 100u);
	return spi_result;
}

/* ================== TESTES ================== */

static void test_timer_clock(void) {

	fake_rcc.CFGR = RCC_CFGR_PPRE1_DIV2;
	CHECK(adBmsTimerClockHz() == 64000000u);   // APB1 dividido: timers ao dobro do PCLK1

	fake_rcc.CFGR = RCC_CFGR_PPRE1_DIV1;
	CHECK(adBmsTimerClockHz() == 32000000u);

	fake_rcc.CFGR = RCC_CFGR_PPRE1_DIV2;
}

static void test_timer_init_1mhz(void) {

	fake_tim5.CR1 = 0;
	fake_tim5.PSC = 0;
	adBmsTimerInit();

	CHECK(fake_tim5.PSC == 63u);               // 64 MHz / 64 = 1 MHz
	CHECK((fake_tim5.CR1 & TIM_CR1_CEN) != 0u);
}

static void test_micros_restarts_stopped_timer(void) {

	fake_tim5.CR1 = 0;
	(void) adBmsMicros();

	CHECK((fake_tim5.CR1 & TIM_CR1_CEN) != 0u); // contador parado prendia os waits
}

static void test_wake_skipped_when_ready(void) {

	adBmsCsLow();
	adBmsCsHigh();                             // transacao acabou agora
	edge_count = 0;

	adBmsWakeupIc(12);
	CHECK(edge_count == 0u);                   // a porta ainda esta READY (< tIDLE)
}

static void test_wake_pulses_when_idle(void) {

	adBmsCsHigh();
	fake_tim5.CNT += 4000u;                    // 4 ms sem atividade: a porta pode estar em IDLE
	edge_count = 0;

	adBmsWakeupIc(12);
	CHECK(edge_count == 24u);                  // um par -1/+1 por slave

	bool alternates = true;
	bool spaced = true;
	for (uint32_t edge_index = 0; edge_index < edge_count; edge_index++) {
		GPIO_PinState expected_state = ((edge_index % 2u) == 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET;

		alternates = alternates && (edges[edge_index].state == expected_state);
		if (edge_index > 0u) {
			spaced = spaced && ((edges[edge_index].time_us - edges[edge_index - 1u].time_us) >= 20u);   // > tREADY (10 us)
		}
	}
	CHECK(alternates);
	CHECK(spaced);

	edge_count = 0;
	adBmsWakeupIc(12);
	CHECK(edge_count == 0u);                   // logo a seguir ja nao e preciso
}

static void test_spi_errors_counted(void) {

	uint8_t spi_buffer[8] = { 0 };
	uint32_t errors_before = adBmsSpiErrors();

	spi_result = HAL_OK;
	spiWriteBytes(4, spi_buffer);
	spiReadBytes(4, spi_buffer);
	CHECK(adBmsSpiErrors() == errors_before);

	spi_result = HAL_TIMEOUT;
	spiWriteBytes(4, spi_buffer);
	spiWriteReadBytes(spi_buffer, spi_buffer, 4);   // transmit e receive falham os dois
	CHECK(adBmsSpiErrors() == errors_before + 3u);

	spi_result = HAL_OK;
}

static void test_poll_timer(void) {

	startTimer();
	fake_tim5.CNT += 500u;
	uint32_t measured_us = getTimCount();
	stopTimer();

	CHECK((measured_us >= 500u) && (measured_us <= 505u));
	CHECK(fake_tim5.CNT > 500u);               // o TIM5 nunca e reposto a 0
}

typedef struct {
	const char *name;
	void (*run)(void);
} test_t;

#define TEST(test_function) { #test_function, test_function }

int main(void) {

	const test_t tests[] = {
		TEST(test_timer_clock),
		TEST(test_timer_init_1mhz),
		TEST(test_micros_restarts_stopped_timer),
		TEST(test_wake_skipped_when_ready),
		TEST(test_wake_pulses_when_idle),
		TEST(test_spi_errors_counted),
		TEST(test_poll_timer),
	};

	fake_rcc.CFGR = RCC_CFGR_PPRE1_DIV2;

	for (size_t test_index = 0; test_index < sizeof(tests) / sizeof(tests[0]); test_index++) {
		int failures_before = failures;
		tests[test_index].run();
		printf("%s %s\n", (failures == failures_before) ? "ok    " : "FALHOU", tests[test_index].name);
	}

	printf("\n%d verificacoes, %d falhadas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
