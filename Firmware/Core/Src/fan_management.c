/*
 * fan_management.c
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */
#include "fan_management.h"

extern TIM_HandleTypeDef htim12;

//TEMp VAUE
float temperature = 30;

uint8_t pwm_8bit = 0;

typedef struct
{
	float temperature;
	uint8_t pwm;
} fanTarget_table;

fanTarget_table fan_table[12] =
{
	//TEMP, PWM
	{35, 0},
	{36, 70},
	{38, 105},
	{40, 120},
	{42, 135},
	{44, 155},
	{46, 165},
	{47, 180},
	{49, 190},
	{51, 205},
	{53, 230},
	{55, 255}
};

uint8_t Get_Fan_PWM(void){

	return pwm_8bit;
}

void Update_Fan_Temperature(uint16_t max_temperature){

	temperature = (float)(max_temperature * 0.01);
}


void Fan_Start(void) {
	HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);

	// Start with fan off
	__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 0);
}

void Fan_Update(void)
{

	uint32_t timer_max = __HAL_TIM_GET_AUTORELOAD(&htim12);
	uint32_t pwm_timer_value = 0;

	// If temperature is below the first table value
	if (temperature <= fan_table[0].temperature)
	{
		pwm_8bit = fan_table[0].pwm;
	}

	// If temperature is above the last table value
	else if (temperature >= fan_table[11].temperature)
	{
		pwm_8bit = fan_table[11].pwm;
	}

	// Temperature is inside the table range
	else
	{
		for (uint8_t i = 0; i < 11; i++)
		{
			float temp_low = fan_table[i].temperature;
			float temp_high = fan_table[i + 1].temperature;

			if (temperature >= temp_low && temperature <= temp_high)
			{
				float pwm_low = fan_table[i].pwm;
				float pwm_high = fan_table[i + 1].pwm;

				float temp_position = (temperature - temp_low) / (temp_high - temp_low);

				float pwm_float = pwm_low + ((pwm_high - pwm_low) * temp_position);

				pwm_8bit = (uint8_t)(pwm_float + 0.5);

				break;
			}
		}
	}

	// Convert 0-255 value to timer compare value
	pwm_timer_value = ((uint32_t)pwm_8bit * timer_max) / 255;

	//clamp this bitch
	if(pwm_timer_value < 0){
		pwm_timer_value = 255;
	}

	if(pwm_timer_value > 255){
		pwm_timer_value = 255;
	}

	__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, pwm_timer_value);
}
