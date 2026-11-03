/*
 * bsp.c
 *
 * Ver bsp.h. So este ficheiro (e os drivers de CAN/UART em comms/) chamam o
 * HAL para os perifericos da placa.
 */

#include "bsp.h"
#include "main.h"
#include "mcuWrapper.h"   // adBmsTimerClockHz()
#include <string.h>

extern TIM_HandleTypeDef htim3;   // trigger do ADC (main.c)

/* calibracoes de fabrica do STM32F412 (RM0402, 6.3.22 e 6.3.24) */
#define BOARD_VREFINT_CAL_ADDR ((const uint16_t*) 0x1FFF7A2Au)
#define BOARD_TS_CAL1_ADDR     ((const uint16_t*) 0x1FFF7A2Cu)
#define BOARD_TS_CAL2_ADDR     ((const uint16_t*) 0x1FFF7A2Eu)

#define ADC_CHANNELS 3u    // TEMP, VREFINT, IN14 (ordem do .ioc)
#define ADC_SAMPLES  32u   // por canal no buffer circular (~33 ms a 977 Hz)

#define EEPROM_I2C_ADDRESS 0xA0u   // 24FC08: A9..A8 vao nos bits 2..1 do endereco
#define EEPROM_TIMEOUT_MS  20u

static uint16_t adc_buffer[ADC_SAMPLES * ADC_CHANNELS];
static volatile uint32_t adc_conversions;

/* ================== GPIO ================== */

uint8_t Bsp_ReadInputs(void) {

	uint8_t inputs = 0;

	if (HAL_GPIO_ReadPin(MCU_AIR_positivo_FB_GPIO_Port, MCU_AIR_positivo_FB_Pin) == GPIO_PIN_SET) {
		inputs |= BSP_IN_AIR_POS;
	}
	if (HAL_GPIO_ReadPin(MCU_AIR_negativo_FB_GPIO_Port, MCU_AIR_negativo_FB_Pin) == GPIO_PIN_SET) {
		inputs |= BSP_IN_AIR_NEG;
	}
	if (HAL_GPIO_ReadPin(MCU_PRE_FB_GPIO_Port, MCU_PRE_FB_Pin) == GPIO_PIN_SET) {
		inputs |= BSP_IN_PRE;
	}
	if (HAL_GPIO_ReadPin(MCU_DISCH_FB_GPIO_Port, MCU_DISCH_FB_Pin) == GPIO_PIN_SET) {
		inputs |= BSP_IN_DISCH;
	}
	if (HAL_GPIO_ReadPin(MCU_SDC_FB_GPIO_Port, MCU_SDC_FB_Pin) == GPIO_PIN_SET) {
		inputs |= BSP_IN_SDC;
	}

	return inputs;
}

uint8_t Bsp_InputFromPin(uint16_t gpio_pin) {

	switch (gpio_pin) {
	case MCU_AIR_positivo_FB_Pin:
		return BSP_IN_AIR_POS;
	case MCU_AIR_negativo_FB_Pin:
		return BSP_IN_AIR_NEG;
	case MCU_PRE_FB_Pin:
		return BSP_IN_PRE;
	case MCU_DISCH_FB_Pin:
		return BSP_IN_DISCH;
	case MCU_SDC_FB_Pin:
		return BSP_IN_SDC;
	default:
		return 0;
	}
}

void Bsp_SetContactors(bool air_pos, bool air_neg, bool precharge) {

	// abrir primeiro, fechar depois: nunca ha um instante com mais fechado que o pedido
	if (!precharge) {
		HAL_GPIO_WritePin(CONTACT_PRE_GPIO_Port, CONTACT_PRE_Pin, GPIO_PIN_SET);
	}
	if (!air_pos) {
		HAL_GPIO_WritePin(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin, GPIO_PIN_SET);
	}
	if (!air_neg) {
		HAL_GPIO_WritePin(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin, GPIO_PIN_SET);
	}

	if (air_neg) {
		HAL_GPIO_WritePin(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin, GPIO_PIN_RESET);
	}
	if (precharge) {
		HAL_GPIO_WritePin(CONTACT_PRE_GPIO_Port, CONTACT_PRE_Pin, GPIO_PIN_RESET);
	}
	if (air_pos) {
		HAL_GPIO_WritePin(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin, GPIO_PIN_RESET);
	}
}

void Bsp_SetAmsError(bool error) {

	HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, error ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Bsp_ToggleHeartbeat(void) {

	HAL_GPIO_TogglePin(MCU_HEARTBEAT_GPIO_Port, MCU_HEARTBEAT_Pin);
}

/* ================== VENTOINHA (TIM12 CH1, PB14) ================== */

void Bsp_FanInit(uint16_t pwm_hz) {

	uint32_t ticks = adBmsTimerClockHz() / ((pwm_hz > 0u) ? pwm_hz : 1000u);
	uint32_t prescaler = ticks / 65536u;               // ARR tem de caber em 16 bits
	uint32_t period = (ticks / (prescaler + 1u)) - 1u;

	HAL_TIM_PWM_Stop(&htim12, TIM_CHANNEL_1);
	__HAL_TIM_SET_PRESCALER(&htim12, prescaler);
	__HAL_TIM_SET_AUTORELOAD(&htim12, period);
	__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 0);
	htim12.Instance->EGR = TIM_EGR_UG;
	HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
}

/* A polaridade do CH1 esta invertida no CubeMX (TIM_OCPOLARITY_LOW), porque o
 * driver da placa inverte: aqui e logica positiva, 255 = ventoinha no maximo */
void Bsp_SetFanPwm(uint8_t pwm) {

	uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim12) + 1u;

	__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, (period * pwm) / 255u);
}

/* ================== ADC ================== */

void Bsp_AdcStart(void) {

	memset(adc_buffer, 0, sizeof(adc_buffer));
	HAL_TIM_Base_Start(&htim3);
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*) adc_buffer, ADC_SAMPLES * ADC_CHANNELS);
}

bool Bsp_AdcAverage(uint16_t *temp_raw, uint16_t *vref_raw, uint16_t *current_raw) {

	uint32_t sums[ADC_CHANNELS] = { 0 };

	// o DMA continua a escrever: a media mistura duas voltas do buffer, nao faz mal
	for (uint32_t sample = 0; sample < ADC_SAMPLES; sample++) {
		for (uint32_t channel = 0; channel < ADC_CHANNELS; channel++) {
			sums[channel] += adc_buffer[(sample * ADC_CHANNELS) + channel];
		}
	}

	*temp_raw = (uint16_t) (sums[0] / ADC_SAMPLES);
	*vref_raw = (uint16_t) (sums[1] / ADC_SAMPLES);
	*current_raw = (uint16_t) (sums[2] / ADC_SAMPLES);

	return (*vref_raw != 0u);
}

uint32_t Bsp_AdcConversions(void) {

	return adc_conversions;
}

uint16_t Bsp_VrefintCal(void) {

	return *BOARD_VREFINT_CAL_ADDR;
}

uint16_t Bsp_TsCal1(void) {

	return *BOARD_TS_CAL1_ADDR;
}

uint16_t Bsp_TsCal2(void) {

	return *BOARD_TS_CAL2_ADDR;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {

	if (hadc->Instance == ADC1) {
		adc_conversions++;
	}
}

/* ================== EEPROM 24FC08 ================== */

static uint16_t eeprom_device(uint16_t address) {

	return (uint16_t) (EEPROM_I2C_ADDRESS | ((address >> 7) & 0x06u));
}

bool Bsp_EepromPresent(void) {

	return HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDRESS, 2, 2) == HAL_OK;
}

/* cada bloco de 256 B tem o seu endereco I2C: as leituras nao atravessam blocos */
bool Bsp_EepromRead(uint16_t address, uint8_t *data, uint16_t length) {

	while (length > 0u) {

		uint16_t chunk = (uint16_t) (256u - (address & 0xFFu));

		if (chunk > length) {
			chunk = length;
		}
		if (HAL_I2C_Mem_Read(&hi2c1, eeprom_device(address), address & 0xFFu, I2C_MEMADD_SIZE_8BIT, data, chunk, EEPROM_TIMEOUT_MS) != HAL_OK) {
			return false;
		}
		address += chunk;
		data += chunk;
		length -= chunk;
	}

	return true;
}

bool Bsp_EepromWritePage(uint16_t address, const uint8_t *data, uint8_t length) {

	if ((length == 0u) || (((address % BSP_EEPROM_PAGE) + length) > BSP_EEPROM_PAGE)) {
		return false;
	}

	return HAL_I2C_Mem_Write(&hi2c1, eeprom_device(address), address & 0xFFu, I2C_MEMADD_SIZE_8BIT, (uint8_t*) data, length, 5u) == HAL_OK;
}

bool Bsp_EepromReady(void) {

	return HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDRESS, 1, 1) == HAL_OK;
}

void Bsp_EepromRecover(void) {

	HAL_I2C_DeInit(&hi2c1);
	HAL_I2C_Init(&hi2c1);
}

/* ================== SISTEMA ================== */

void Bsp_SystemReset(void) {

	NVIC_SystemReset();
}

/* Salto para o bootloader de sistema (AN2606), sequencia do firmware antigo,
 * baseada em FSLART/can_stm32_programmer (gonzabrusco_jump_to_bootloader.md):
 * os deinit, o remap e as barreiras sao todos precisos, nao e code smell */
#define BOOTLOADER_BASE_ADDR 0x1FFF0000u

void Bsp_JumpToBootloader(void) {

	typedef void (*jump_t)(void);

	uint32_t bootloader_stack = *(__IO uint32_t*) BOOTLOADER_BASE_ADDR;
	jump_t bootloader_entry = (jump_t) *(__IO uint32_t*) (BOOTLOADER_BASE_ADDR + 4u);

	// contactores abertos e linha em erro antes de largar tudo
	Bsp_SetContactors(false, false, false);
	Bsp_SetAmsError(true);

	// 1. DeInit com o SysTick ainda vivo
	HAL_CAN_Stop(&hcan1);
	HAL_CAN_Stop(&hcan2);
	HAL_CAN_DeInit(&hcan1);
	HAL_CAN_DeInit(&hcan2);
	HAL_DeInit();
	HAL_RCC_DeInit();

	// 2. SysTick e interrupcoes
	__disable_irq();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	for (uint32_t register_index = 0; register_index < 8u; register_index++) {
		NVIC->ICER[register_index] = 0xFFFFFFFFu;
		NVIC->ICPR[register_index] = 0xFFFFFFFFu;
	}

	// 3. memoria de sistema em 0x00000000 e tabela de vetores do bootloader
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	__HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
	SCB->VTOR = 0x00000000u;

	// 4. Thread mode com o MSP do bootloader
	__set_CONTROL(0x00u);
	__ISB();
	__set_MSP(bootloader_stack);
	__DSB();
	__ISB();

	bootloader_entry();

	while (1) {
	}
}
