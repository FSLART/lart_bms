#ifndef EEPROM_UTILS_H
#define EEPROM_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdbool.h"
#include "string.h"

#include "main.h"
#include "ee24.h"
#include "uartDMA.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;
    UART_HandleTypeDef *huart;
} EEPROM_Comms;

// Function Prototypes
bool Write_EEPROM(EEPROM_Comms *comms, uint8_t type, uint16_t value, bool debug);
int  Read_EEPROM(EEPROM_Comms *comms, uint8_t type, bool debug);
//bool Analyze_EEPROM(EEPROM_Comms *comms);
void set_uart(EEPROM_Comms *comms, UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif // EEPROM_UTILS_H
