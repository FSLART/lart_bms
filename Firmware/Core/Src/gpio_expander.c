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
volatile uint8_t mcp23017_gpiob_state = 0x00;

/* Set whenever the LED bitmask changes. MCP23017_Flush() (main loop only)
 * writes the new state over I2C and clears it. This is what makes
 * MCP23017_LED() safe to call from interrupts: it never touches I2C itself. */
volatile bool mcp23017_leds_dirty = false;

/* True only while the startup animation owns the LEDs. Starts false so
 * MCP23017_Flush() works even when the animation is disabled; the animation
 * itself sets it true while running and clears it when done. */
bool init_animation_busy = false;

/* Print I2C errors only once, otherwise a dead/absent expander spams the
 * UART on every access. The fault stays raised either way; the flag resets
 * on the first successful transfer so a recovered chip can report again. */
static bool mcp23017_error_printed = false;

// helper write MCP23017 register.
void MCP23017_Write_Register(uint8_t reg, uint8_t value) {
	HAL_StatusTypeDef result;

	result = HAL_I2C_Mem_Write(&hi2c1, MCP23017_ADDRESS, reg,
	I2C_MEMADD_SIZE_8BIT, &value, 1, 1);

	if (result != HAL_OK) {
		if (!mcp23017_error_printed) {
			mcp23017_error_printed = true;
			printfDebug("MCP23017 write fail reg=0x%02X err=0x%08lX (further errors muted)\r\n",
					reg, hi2c1.ErrorCode);
		}
		RAISE_ERROR(FAULT_GPIO_EXPANDER);
		return;
	}

	mcp23017_error_printed = false;
}

uint8_t MCP23017_Read_Register(uint8_t reg) {
	HAL_StatusTypeDef result;
	uint8_t value = 0;

	result = HAL_I2C_Mem_Read(&hi2c1, MCP23017_ADDRESS, reg,
	I2C_MEMADD_SIZE_8BIT, &value, 1, 1);

	if (result != HAL_OK) {
		if (!mcp23017_error_printed) {
			mcp23017_error_printed = true;
			printfDebug("MCP23017 read fail reg=0x%02X (further errors muted)\r\n", reg);
		}
		RAISE_ERROR(FAULT_GPIO_EXPANDER);
		return 0;
	}

	mcp23017_error_printed = false;

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

/* Control one MCP23017 output pin.
 *
 * SAFE TO CALL FROM INTERRUPTS. This function only updates the local bitmask
 * and marks it dirty - it never does I2C. The actual write to the chip
 * happens later in MCP23017_Flush(), called from brain_loop.
 *
 * (The old version wrote I2C directly from here. When called inside the CAN
 * RX interrupt, the blocking I2C transfer stalled the ISR long enough to
 * drop frames and brick the CAN bus.) */
void MCP23017_LED(mcp23017_led_t led, mcp23017_led_state_t state) {
	uint8_t bit_mask;

	if (led > LED_AUX2 || led < LED_UART) {
		RAISE_ERROR(FAULT_GPIO_EXPANDER);
		return;
	}

	//bits for leds
	bit_mask = 1 << led;

	/* The read-modify-write below can be interrupted (this function is called
	 * from both interrupts and the main loop), so briefly mask interrupts to
	 * avoid losing an LED update. This is nanoseconds, not an I2C transfer. */
	uint32_t primask = __get_PRIMASK();
	__disable_irq();

	if (state == ON) {

		mcp23017_gpiob_state |= bit_mask;

	} else if (state == OFF) {

		mcp23017_gpiob_state &= ~bit_mask;

	} else if (state == TOGGLE) {

		mcp23017_gpiob_state ^= bit_mask;

	} else {

		__set_PRIMASK(primask);
		RAISE_ERROR(FAULT_GPIO_EXPANDER);
		return;
	}

	mcp23017_leds_dirty = true;

	__set_PRIMASK(primask);
}

/* Push the LED bitmask to the chip if it changed since the last flush.
 *
 * MAIN LOOP ONLY - this is the single place that writes LED state over I2C.
 * Called once per brain_loop pass; does nothing when no LED changed, so the
 * I2C bus stays quiet most of the time.
 *
 * The dirty flag is cleared BEFORE the write on purpose: if an interrupt
 * changes an LED while the I2C transfer is in flight, the flag is set again
 * and the next flush picks up the change. Clearing after the write could
 * silently lose that update. */
void MCP23017_Flush(void) {

	if (!mcp23017_leds_dirty) {
		return;
	}

	// Skip while the startup animation owns the LEDs
	if (init_animation_busy) {
		return;
	}

	mcp23017_leds_dirty = false;

	MCP23017_Write_Register(MCP23017_OLATB, mcp23017_gpiob_state);
}

// read dip switch
uint8_t MCP23017_Read_DIP(mcp23017_dip_t dip) {
	uint8_t porta_value;
	uint8_t bit_mask;

	if (dip > MCP23017_GPA7) {
		RAISE_ERROR(FAULT_GPIO_EXPANDER);
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
	MCP23017_LED(LED_UART, OFF);
	MCP23017_LED(LED_ISOSPI, OFF);
	MCP23017_LED(LED_CAN, OFF);
	MCP23017_LED(LED_BALANCING_STATUS, OFF);
	MCP23017_LED(LED_CHARGING_STATUS, OFF);
	MCP23017_LED(LED_PRECHARGE_STATUS, OFF);
	MCP23017_LED(LED_AUX1, OFF);
	MCP23017_LED(LED_AUX2, OFF);
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

		init_animation_busy = true;

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

		/* Build the new frame: front LED + 2-LED tail, everything else off.
		 * One single I2C write per frame (this runs in the main loop, where
		 * a direct write is fine). */
		mcp23017_gpiob_state = (1 << led_front) | (1 << led_back_1) | (1 << led_back_2);

		MCP23017_Write_Register(MCP23017_OLATB, mcp23017_gpiob_state);

		// avançar para o próximo LED
		led_position++;

		if (led_position >= 8) {
			led_position = 0;
		}

		break;

	case LED_PISCA_MATA_LEDS:

		init_animation_busy = false;

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

