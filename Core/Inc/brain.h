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

/* ===== BMS STATE MACHINE ===== */
typedef enum {
    BALANCING,
    CHARGING,
    IDLE,
    ONMISSION,
    STARTUP,
    INACTIVE
} BmsStates;

extern volatile BmsStates bmsState;
extern volatile BmsStates bmsCurrState;
extern volatile BmsStates bmsPrevState;



void brain_start(void);

void brain_loop(void);


uint32_t getRuntimeMs(void);
uint32_t getRuntimeMsDiff(uint32_t startTime);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
