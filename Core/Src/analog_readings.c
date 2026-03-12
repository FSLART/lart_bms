/*
 * analog_readings.c
 *
 *  Created on: Oct 17, 2025
 *      Author: jpser
 */

#include <analog_readings.h>
#include "main.h"
#include "brain.h"
#include "math.h"
#include "uartDMA.h"

extern ADC_HandleTypeDef hadc1;

/* Factory calibration addresses for STM32F412 */
#define TS_CAL1_ADDR        ((uint16_t*)0x1FFF7A2C)   /* ADC raw @ 30C, VDDA = 3.3V */
#define TS_CAL2_ADDR        ((uint16_t*)0x1FFF7A2E)   /* ADC raw @110C, VDDA = 3.3V */

#define TEMP_CAL1_TEMPC     30.0f
#define TEMP_CAL2_TEMPC     110.0f

/* For 12-bit ADC resolution */
#define ADC_MAX_COUNTS      4095.0f

/* Typical STM32 internal reference voltage */
#define VREFINT_TYP_VOLTS   1.21f

#define MCS1802_SENSITIVITY   0.264f   // 264 mV/A = 0.264 V/A do datasheet

/* DMA buffer:
 * rank 1 -> ams_master_current
 * rank 2 -> TEMP
 * rank 3 -> VREFINT
 * repeated ADC_SAMPLES_PER_CH times
 */
static uint16_t s_adcDmaBuf[ADC_DMA_BUF_LEN];
static AnalogReadings_t s_analog = {0};

static float AnalogReadings_ComputeVDDA(uint16_t raw_vref)
{
    if (raw_vref == 0u) {
        return 0.0f;
    }

    return (VREFINT_TYP_VOLTS * ADC_MAX_COUNTS) / (float)raw_vref;
}

static float AnalogReadings_ComputeCurrent(uint16_t raw_ams_master_current, float vdda)
{
    float vout = ((float)raw_ams_master_current * vdda) / ADC_MAX_COUNTS;  // ADC → volts

    float zero_current = vdda * 0.5f;                        // VCC/2 offset

    return (vout - zero_current) / MCS1802_SENSITIVITY;      // amps
}

static float AnalogReadings_ComputeTempC(uint16_t raw_temp, float vdda)
{
    const float ts_cal1 = (float)(*TS_CAL1_ADDR);
    const float ts_cal2 = (float)(*TS_CAL2_ADDR);

    if (vdda <= 0.0f) {
        return 0.0f;
    }

    const float raw_equiv_3v3_12b = ((float)raw_temp) * (3.3f / vdda);

    return ((raw_equiv_3v3_12b - ts_cal1) * (TEMP_CAL2_TEMPC - TEMP_CAL1_TEMPC) / (ts_cal2 - ts_cal1))
           + TEMP_CAL1_TEMPC;
}

void AnalogReadings_Init(void)
{
    s_analog.raw_ams_master_current = 0u;
    s_analog.raw_temp = 0u;
    s_analog.raw_vref = 0u;
    s_analog.vdda = 0.0f;
    s_analog.ams_master_current = 0.0f;
    s_analog.mcu_temp_c = 0.0f;
    s_analog.busy = false;
    s_analog.data_ready = false;

    for (uint32_t i = 0; i < ADC_DMA_BUF_LEN; i++) {
        s_adcDmaBuf[i] = 0u;
    }
}

void AnalogReadings_Start(void)
{
    if (s_analog.busy) {
        return;
    }

    s_analog.busy = true;
    s_analog.data_ready = false;

    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adcDmaBuf, ADC_DMA_BUF_LEN);
}

//Conversion Complete Callbacks
void AnalogReadings_ConvCpltCallback(void)
{
    uint32_t sum_ams_master_current = 0u;
    uint32_t sum_temp = 0u;
    uint32_t sum_vref = 0u;

    for (uint32_t i = 0; i < ADC_SAMPLES_PER_CH; i++) {
        sum_ams_master_current += s_adcDmaBuf[(i * ADC_CHANNEL_COUNT) + 0u];
        sum_temp += s_adcDmaBuf[(i * ADC_CHANNEL_COUNT) + 1u];
        sum_vref += s_adcDmaBuf[(i * ADC_CHANNEL_COUNT) + 2u];
    }

    HAL_ADC_Stop_DMA(&hadc1);

    s_analog.raw_ams_master_current = (uint16_t)(sum_ams_master_current / ADC_SAMPLES_PER_CH);
    s_analog.raw_temp = (uint16_t)(sum_temp / ADC_SAMPLES_PER_CH);
    s_analog.raw_vref = (uint16_t)(sum_vref / ADC_SAMPLES_PER_CH);

    s_analog.vdda = AnalogReadings_ComputeVDDA(s_analog.raw_vref);
    s_analog.ams_master_current = AnalogReadings_ComputeCurrent(s_analog.raw_ams_master_current, s_analog.vdda);
    s_analog.mcu_temp_c = AnalogReadings_ComputeTempC(s_analog.raw_temp, s_analog.vdda);

    s_analog.busy = false;
    s_analog.data_ready = true;
}

const AnalogReadings_t *AnalogReadings_Get(void)
{
    return &s_analog;
}
