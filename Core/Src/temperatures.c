/*
 * temperatures.c
 *
 *  Created on: Oct 17, 2025
 *      Author: jpser
 */

#include "main.h"
#include "temperatures.h"
#include "math.h"
#include "uartDMA.h"

extern ADC_HandleTypeDef hadc1;

#define TEMP_V25        0.76f      // Volts at 25 °C (typ)
#define TEMP_AVG_SLOPE  0.0025f    // Volts/°C (typ) = 2.5 mV/°C
#define VDDA_ASSUMED    3.3f
#define ADC_MAX_12BIT   4095.0f

void read_mcu_temp(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);        // 10 ms timeout is plenty here
    uint32_t raw = HAL_ADC_GetValue(&hadc1);      // 12-bit result
    HAL_ADC_Stop(&hadc1);

    float vsense = (raw / ADC_MAX_12BIT) * VDDA_ASSUMED;   // Volts
    float temp_c = ((vsense - TEMP_V25) / TEMP_AVG_SLOPE) + 25.0f;
    printConsole("MCU Temp: %f C \n\n", temp_c);
}
