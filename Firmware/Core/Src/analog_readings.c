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
#include "can.h"
#include "dbc/powertrain_t26.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim3;

/* Factory calibration addresses for STM32F412 */
#define TS_CAL1_ADDR        ((uint16_t*)0x1FFF7A2C)   /* ADC raw @ 30C, VDDA = 3.3V */
#define TS_CAL2_ADDR        ((uint16_t*)0x1FFF7A2E)   /* ADC raw @110C, VDDA = 3.3V */

#define TEMP_CAL1_TEMPC     30
#define TEMP_CAL2_TEMPC     110

/* For 12-bit ADC resolution */
#define ADC_MAX_COUNTS      4095

/* Typical STM32 internal reference voltage */
#define VREFINT_TYP_VOLTS   1.21

#define MCS1802_SENSITIVITY   0.264   // 264 mV/A = 0.264 V/A do datasheet

/* DMA buffer:
 * rank 1 -> TEMP
 * rank 2 -> VREFINT
 * rank 3 -> ams_master_current
 */
uint16_t ADCdmaBuffer[ADC_DMA_BUF_LEN];
AnalogReadings_t adc_readings = {0};

static float AnalogReadings_ComputeVDDA(uint16_t raw_vref)
{
    if (raw_vref == 0) {
        return 0;
    }

    return (VREFINT_TYP_VOLTS * ADC_MAX_COUNTS) / (float)raw_vref;
}

static float AnalogReadings_ComputeCurrent(uint16_t raw_ams_master_current, float vdda)
{
    float vout = ((float)raw_ams_master_current * vdda) / ADC_MAX_COUNTS;  // ADC → volts

    float zero_current = vdda * 0.5;                        // VCC/2 offset

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
    adc_readings.raw_ams_master_current = 0;
    adc_readings.raw_temp = 0;
    adc_readings.raw_vref = 0;
    adc_readings.vdda = 0.0;
    adc_readings.ams_master_current = 0.0;
    adc_readings.mcu_temp_c = 0.0;
    adc_readings.busy = false;
    adc_readings.data_ready = false;

    for (uint32_t i = 0; i < ADC_DMA_BUF_LEN; i++) {
        ADCdmaBuffer[i] = 0;
    }

    HAL_TIM_Base_Start(&htim3);
    AnalogReadings_Start();
}

void AnalogReadings_Start(void)
{
    if (adc_readings.busy) {
        return;
    }

    adc_readings.busy = true;
    adc_readings.data_ready = false;

    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)ADCdmaBuffer, ADC_DMA_BUF_LEN);
}

//Conversion Complete Callbacks
void AnalogReadings_ConvCpltCallback(void)
{
    uint32_t sum_ams_master_current = 0;
    uint32_t sum_temp = 0;
    uint32_t sum_vref = 0;

    for (uint32_t i = 0; i < ADC_SAMPLES_PER_CH; i++) {
        sum_temp += ADCdmaBuffer[(i * ADC_CHANNEL_COUNT) + 0];
        sum_vref += ADCdmaBuffer[(i * ADC_CHANNEL_COUNT) + 1];
        sum_ams_master_current += ADCdmaBuffer[(i * ADC_CHANNEL_COUNT) + 2];
    }

    //HAL_ADC_Stop_DMA(&hadc1);

    adc_readings.raw_ams_master_current = (uint16_t)(sum_ams_master_current / ADC_SAMPLES_PER_CH);
    adc_readings.raw_temp = (uint16_t)(sum_temp / ADC_SAMPLES_PER_CH);
    adc_readings.raw_vref = (uint16_t)(sum_vref / ADC_SAMPLES_PER_CH);

    adc_readings.vdda = AnalogReadings_ComputeVDDA(adc_readings.raw_vref);
    adc_readings.ams_master_current = AnalogReadings_ComputeCurrent(adc_readings.raw_ams_master_current, adc_readings.vdda);
    adc_readings.mcu_temp_c = AnalogReadings_ComputeTempC(adc_readings.raw_temp, adc_readings.vdda);

    adc_readings.busy = false;
    adc_readings.data_ready = true;
}

//n sei pk q tem de ser const
const AnalogReadings_t *AnalogReadings_Get(void)
{
    return &adc_readings;
}

/*HAL_StatusTypeDef AnalogReadings_CAN_Send(CAN_HandleTypeDef *hcan)
{
    uint8_t data[POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH];
    int len;

    if (hcan == NULL) {
        return HAL_ERROR;
    }

    if (!adc_readings.data_ready) {
        return HAL_ERROR;
    }

    struct powertrain_t26_master_msc_id_1_t msg = {0};

    msg.mcu_vref = powertrain_t26_master_msc_id_1_mcu_vref_encode(adc_readings.vdda);
    msg.mcu_temperature = powertrain_t26_master_msc_id_1_mcu_temperature_encode(adc_readings.mcu_temp_c);
    msg.ams_current_draw = powertrain_t26_master_msc_id_1_ams_current_draw_encode(adc_readings.ams_master_current);

    len = powertrain_t26_master_msc_id_1_pack(data, &msg, sizeof(data));
    if (len < 0) {
        return HAL_ERROR;
    }

    return CAN_TX_Add_To_Queue(
        hcan,
		POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID,
		POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH,
        data
    );
}*/


/* PERIODICA PRINT OF VALUES
 * const AnalogReadings_t *adc = AnalogReadings_Get();

		if (adc->data_ready)
		{
			printfDebug("ADC RAW: IN14=%u TEMP=%u VREF=%u\r\n",
		           adc->raw_ams_master_current,
		           adc->raw_temp,
		           adc->raw_vref);

		    printfDebug("VDDA: %.3f V\r\n", adc->vdda);

		    printfDebug("Current: %.3f A\r\n", adc->ams_master_current);

		    printfDebug("MCU Temp: %.2f C\r\n", adc->mcu_temp_c);
		}
 *
 */
