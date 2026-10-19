/*
 * brain.h
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 */

#ifndef INC_BRAIN_H_
#define INC_BRAIN_H_


#include <stdbool.h>
#include <stdint.h>


/* Watchdog reset flag raw RCC->CSR captured before peripherals initilise */
extern uint32_t watchdog_flag;

void brain_start(void);

void brain_loop(void);

void heartbeat(void);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
