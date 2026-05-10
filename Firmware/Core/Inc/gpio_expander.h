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

    MCP23017_GPB0 = 0,
    MCP23017_GPB1,
    MCP23017_GPB2,
    MCP23017_GPB3,
    MCP23017_GPB4,
    MCP23017_GPB5,
    MCP23017_GPB6,
    MCP23017_GPB7

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

void MCP23017_LED(mcp23017_led_t led, mcp23017_led_state_t state);

uint8_t MCP23017_Read_DIP_Port(void);

uint8_t MCP23017_Read_DIP(mcp23017_dip_t dip);

void MCP23017_All_LEDs_Off(void);

void MCP23017_StartupAnimation_Update(void);



#endif /* INC_GPIO_EXPANDER_H_ */
