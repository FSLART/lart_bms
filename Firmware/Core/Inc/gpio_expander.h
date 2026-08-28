/*
 * gpio_expander.h
 *
 *  Created on: May 10, 2026
 *      Author: jpser
 */

#ifndef INC_GPIO_EXPANDER_H_
#define INC_GPIO_EXPANDER_H_

#include "main.h"
#include "uartdma.h"


// MCP23017 pins
typedef enum
{
    LED_UART = 0,
    LED_ISOSPI,
    LED_CAN,
    LED_BALANCING_STATUS,
    LED_CHARGING_STATUS,
    LED_PRECHARGE_STATUS,
    LED_AUX1,
    LED_AUX2

} mcp23017_led_t;

// dip switch
typedef enum
{
    MCP23017_GPA0 = 0,
    MCP23017_GPA1,
    MCP23017_GPA2,
    MCP23017_GPA3,
    MCP23017_GPA4,
    MCP23017_GPA5,
    MCP23017_GPA6,
    MCP23017_GPA7
} mcp23017_dip_t;

//LED actions
typedef enum
{
    OFF = 0,
    ON,
    TOGGLE

} mcp23017_led_state_t;


void MCP23017_Init(void);

/* Update an LED in the local bitmask. Interrupt-safe: never touches I2C.
 * The change reaches the chip on the next MCP23017_Flush(). */
void MCP23017_LED(mcp23017_led_t led, mcp23017_led_state_t state);

/* Write pending LED changes to the chip over I2C.
 * Call from the main loop only - never from an interrupt. */
void MCP23017_Flush(void);

uint8_t MCP23017_Read_DIP_Port(void);

uint8_t MCP23017_Read_DIP(mcp23017_dip_t dip);

void MCP23017_All_LEDs_Off(void);

void MCP23017_StartupAnimation_Update(void);



#endif /* INC_GPIO_EXPANDER_H_ */
