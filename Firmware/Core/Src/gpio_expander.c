/*
 * gpio_expander.c
 *
 *  Created on: May 10, 2026
 *      Author: jpser
 */

#include "gpio_expander.h"
#include "fault_manager.h"

// MCP23017 I2C address
// STM32 HAL wants the address shifted left by 1.
#define MCP23017_ADDRESS        (0x21 << 1)

// MCP23017 registers - SET OUTPUT OR INPUT
#define MCP23017_IODIRA         0x00
#define MCP23017_IODIRB         0x01

// MCP23017 registers - Pull-up registers
#define MCP23017_GPPUA          0x0C
#define MCP23017_GPPUB          0x0D

// MCP23017 registers - GPIO read registers
#define MCP23017_GPIOA          0x12
#define MCP23017_GPIOB          0x13

// MCP23017 registers - Output latch register
#define MCP23017_OLATA          0x14
#define MCP23017_OLATB          0x15

// bitmastdos gpios
uint8_t mcp23017_gpioa_state = 0x00;
uint8_t mcp23017_gpiob_state = 0x00;

// helper write MCP23017 register.
void MCP23017_Write_Register(uint8_t reg, uint8_t value) {
	HAL_StatusTypeDef result;

	result = HAL_I2C_Mem_Write(&hi2c1, MCP23017_ADDRESS, reg,
	I2C_MEMADD_SIZE_8BIT, &value, 1, 1);

	if (result != HAL_OK) {
		printfDebug("MCP23017 write fail reg=0x%02X err=0x%08lX\r\n", reg,
				hi2c1.ErrorCode);
		//RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
	}
}

uint8_t MCP23017_Read_Register(uint8_t reg) {
	HAL_StatusTypeDef result;
	uint8_t value = 0;

	result = HAL_I2C_Mem_Read(&hi2c1, MCP23017_ADDRESS, reg,
	I2C_MEMADD_SIZE_8BIT, &value, 1, 1);

	if (result != HAL_OK) {
		printfDebug("error gpio expander read GPIO\r\n");
		RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
		return 0;
	}

	return value;
}

// Initialize MCP23017.
void MCP23017_Init(void) {

	//bitmask
	mcp23017_gpioa_state = 0x00;
	mcp23017_gpiob_state = 0x00;

	/*
	 * IODIR register:
	 * 1 = input
	 * 0 = output
	 */

	// inputs
	MCP23017_Write_Register(MCP23017_IODIRA, 0xFF);

	// outputs - 00 all outpus
	MCP23017_Write_Register(MCP23017_IODIRB, 0x00);

	/*
	 * isto pq n tenho pullups na pcb :)
	 *
	 * Enable pull-ups on PORTA only if your DIP switch connects the pin to GND.
	 * DIP OFF = 1
	 * DIP ON  = 0
	 */
	MCP23017_Write_Register(MCP23017_GPPUA, 0xFF);

	/* No pull-ups needed on LED outputs */
	MCP23017_Write_Register(MCP23017_GPPUB, 0x00);

	/* Start with all LEDs OFF */
	MCP23017_Write_Register(MCP23017_OLATB, mcp23017_gpiob_state);

	MCP23017_All_LEDs_Off();
}

// Control one MCP23017 output pin.
void MCP23017_LED(mcp23017_led_t led, mcp23017_led_state_t state) {
	//uint8_t bit_position;
	uint8_t bit_mask;

	if (led > MCP23017_GPB7 || led < MCP23017_GPB0) {
		RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
		return;
	}

	//bits for leds
	bit_mask = 1 << led;

	if (state == ON) {

		mcp23017_gpiob_state |= bit_mask;

	} else if (state == OFF) {

		mcp23017_gpiob_state &= ~bit_mask;

	} else if (state == TOGGLE) {

		mcp23017_gpiob_state ^= bit_mask;

	} else {

		RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
		return;
	}

	MCP23017_Write_Register(MCP23017_OLATB, mcp23017_gpiob_state);
}

// read dip switch
uint8_t MCP23017_Read_DIP(mcp23017_dip_t dip) {
	uint8_t porta_value;
	uint8_t bit_mask;

	if (dip > MCP23017_GPA7) {
		RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR);
		return 0;
	}

	porta_value = MCP23017_Read_Register(MCP23017_GPIOA);
	bit_mask = 1 << dip;

	if ((porta_value & bit_mask) != 0) {
		return 1;
	}

	return 0;
}

void MCP23017_All_LEDs_Off(void) {
	MCP23017_LED(MCP23017_GPB0, OFF);
	MCP23017_LED(MCP23017_GPB1, OFF);
	MCP23017_LED(MCP23017_GPB2, OFF);
	MCP23017_LED(MCP23017_GPB3, OFF);
	MCP23017_LED(MCP23017_GPB4, OFF);
	MCP23017_LED(MCP23017_GPB5, OFF);
	MCP23017_LED(MCP23017_GPB6, OFF);
	MCP23017_LED(MCP23017_GPB7, OFF);
}

void MCP23017_StartupAnimation_Update(void) {

	typedef enum {
		LED_PISCA_RUNNING = 0, LED_PISCA_MATA_LEDS, LED_PISCA_NAO
	} led_anim_phase_t;

	  static led_anim_phase_t phase_pisca = LED_PISCA_RUNNING;

	//static pra n voltare m a 0 cada vez que e chamada
	  static uint32_t animation_start_ms = 0;
	static uint32_t last_update_ms = 0;
	static uint8_t led_position = 0;

	uint32_t now = HAL_GetTick();

	if (animation_start_ms == 0) {
		animation_start_ms = now;
	}

	switch (phase_pisca) {

	case LED_PISCA_RUNNING:

		if ((now - animation_start_ms) >= 3000) {
			phase_pisca = LED_PISCA_MATA_LEDS;
			return;
		}

		// pisca pisca
		if ((now - last_update_ms) < 40) {
			return;
		}

		last_update_ms = now;

		// calcular os 3 LEDs com com cauda
		uint8_t led_front = led_position;
		uint8_t led_back_1 = 0;
		uint8_t led_back_2 = 0;

		if (led_position == 0) {
			led_back_1 = 7;
			led_back_2 = 6;
		} else if (led_position == 1) {
			led_back_1 = 0;
			led_back_2 = 7;
		} else {
			led_back_1 = led_position - 1;
			led_back_2 = led_position - 2;
		}

		//  turn everything off primeirio
		MCP23017_All_LEDs_Off();

		// acender LED da frente e cauda
		MCP23017_LED((mcp23017_led_t) led_front, ON);
		MCP23017_LED((mcp23017_led_t) led_back_1, ON);
		MCP23017_LED((mcp23017_led_t) led_back_2, ON);

		// avançar para o próximo LED
		led_position++;

		if (led_position >= 8) {
			led_position = 0;
		}

		break;

	case LED_PISCA_MATA_LEDS:

		MCP23017_All_LEDs_Off();

		phase_pisca = LED_PISCA_NAO;

		break;

	case LED_PISCA_NAO:

//FAZER ND
		return;

	default:
		phase_pisca = LED_PISCA_NAO;
		return;
	}

}

