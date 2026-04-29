/*
 * fan_management.h
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */

#ifndef INC_FAN_MANAGEMENT_H_
#define INC_FAN_MANAGEMENT_H_

#include "main.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

uint8_t Get_Fan_PWM(void);

void Update_Fan_Temperature(uint16_t max_temperature);

void Fan_Start(void);

void Fan_Update(void);

#endif /* INC_FAN_MANAGEMENT_H_ */
