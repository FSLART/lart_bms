/*
 * temperatures.c
 *
 *  Created on: Oct 17, 2025
 *      Author: jpser
 */

#include "main.h"
#include "brain.h"
#include "temperatures.h"
#include "math.h"
#include "uartDMA.h"

extern ADC_HandleTypeDef hadc1;

// Factory calibration addresses for STM32F412
#define TS_CAL1_ADDR        ((uint16_t*)0x1FFF7A2C)   // TS ADC reading @ 30°C, Vdda=3.3V
#define TS_CAL2_ADDR        ((uint16_t*)0x1FFF7A2E)   // TS ADC reading @110°C, Vdda=3.3V

#define TEMP_CAL1_TEMPC     30.0f
#define TEMP_CAL2_TEMPC     110.0f

#define TEMP_V25        0.76f      // Volts at 25 °C (typ)
#define TEMP_AVG_SLOPE  0.0025f    // Volts/°C (typ) = 2.5 mV/°C
#define VDDA_ASSUMED    3.3f
#define ADC_MAX_12BIT   1023.0f

void read_mcu_temp(void) {
	HAL_ADC_Start(&hadc1);
	HAL_ADC_PollForConversion(&hadc1, 5);
	uint32_t raw = HAL_ADC_GetValue(&hadc1);      // 12-bit result
	HAL_ADC_Stop(&hadc1);

	float vsense = (raw / ADC_MAX_12BIT) * VDDA_ASSUMED;   // Volts
	float temp_c = ((vsense - TEMP_V25) / TEMP_AVG_SLOPE) + 25.0f;
	printConsole("MCU Temp: %f C \n\n", temp_c);
}
