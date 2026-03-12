/*
 * analog_readings.h
 *
 *  Created on: Oct 17, 2025
 *      Author: jpser
 */

#ifndef INC_ANALOG_READINGS_H_
#define INC_ANALOG_READINGS_H_

#include <stdint.h>
#include <stdbool.h>

#define ADC_CHANNEL_COUNT    3u
#define ADC_SAMPLES_PER_CH   10u
#define ADC_DMA_BUF_LEN      (ADC_CHANNEL_COUNT * ADC_SAMPLES_PER_CH)

typedef struct {
    uint16_t raw_ams_master_current;
    uint16_t raw_temp;
    uint16_t raw_vref;

    float vdda;
    float ams_master_current;
    float mcu_temp_c;

    bool busy;
    bool data_ready;
} AnalogReadings_t;

void AnalogReadings_Init(void);
void AnalogReadings_Start(void);
void AnalogReadings_ConvCpltCallback(void);

const AnalogReadings_t *AnalogReadings_Get(void);

#endif /* INC_ANALOG_READINGS_H_ */
