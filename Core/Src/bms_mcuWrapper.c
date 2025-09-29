/*
 * bms_mcuWrapper.cpp
 *
 *  Created on: Nov 24, 2024
 *      Author: amrlxyz
 */

#include "bms_mcuWrapper.h"
#include "main.h"

#define WAKEUP_DELAY 1       /// 1ms for 2950   /* BMS ic wakeup delay  */

static TIM_HandleTypeDef *htim         = &htim2;       /* MCU TIM handler */
static TIM_HandleTypeDef *htim_delay         = &htim5;       /* MCU TIM handler */

#define GPIO_PORT   BMS_CS_GPIO_Port
#define CS_PIN      BMS_CS_Pin


void bms_csLow(void)
{
    HAL_GPIO_WritePin(GPIO_PORT, CS_PIN, GPIO_PIN_RESET);
    //printfDma("	cs low\n");
}


void bms_csHigh(void)
{
    HAL_GPIO_WritePin(GPIO_PORT, CS_PIN, GPIO_PIN_SET);
    //printfDma("	cs high\n");
}


void bms_startTimer(void)
{
    HAL_TIM_Base_Start(htim);
}

void bms_stopTimer(void)
{
    HAL_TIM_Base_Stop(htim);
    __HAL_TIM_SetCounter(htim, 0);
}


void bms_stopTimer_delay(void)
{
    HAL_TIM_Base_Stop(htim_delay);
    __HAL_TIM_SetCounter(htim_delay, 0);
}

void bms_startTimer_delay(void)
{
    HAL_TIM_Base_Start(htim_delay);
}

// Get time elapsed in us
uint32_t bms_getTimCount_delay(void)
{
    return((uint32_t)__HAL_TIM_GetCounter(htim_delay));
}


// Get time elapsed in us
uint32_t bms_getTimCount(void)
{
    return((uint32_t)__HAL_TIM_GetCounter(htim));
}


// Wake up all the IC in the daisy chain
void bms_wakeupChain(void)
{
    for (uint8_t ic = 0; ic < TOTAL_IC; ic++)
    {
        bms_csLow();
        HAL_Delay(WAKEUP_DELAY);
        bms_csHigh();
        HAL_Delay(WAKEUP_DELAY);
    }
}


void bms_delayUs(uint32_t us)
{
	bms_startTimer_delay();
    while(bms_getTimCount_delay() < us); // Do nothing while timer still counting
    bms_stopTimer_delay();
}


void bms_delayMsActive(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        bms_csLow();
        bms_delayUs(500);
        bms_csHigh();
        bms_delayUs(500);
    }
}




