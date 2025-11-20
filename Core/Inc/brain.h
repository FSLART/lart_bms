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

/* BMS error codes */
typedef enum {
	ERROR_NONE              = 0x00,
	ERROR_SDC_TRIGGERED     = 0x01,  // Bit 0
	ERROR_IMD_TRIGGERED     = 0x02,  // Bit 1
	ERROR_CONTACTORS_MISMATCH  = 0x04,  // Bit 2
	ERROR_TIMER_FAILURE     = 0x08,  // Bit 3
	ERROR_CAN_FAILED      = 0x10,  // Bit 4
	ERROR_OVERVOLTAGE       = 0x20,   // Bit 5
	ERROR_OVERCURRENT       = 0x30,   // Bit 5
	ERROR_BMS_OW			= 0x40,   // Bit 6
	ERROR_BMS_FAIL			= 0x50   // Bit 6
} ErrorCode_t;

/* BMS error statud */
typedef struct {
	int errorCounter;
	bool errorSDC;
	bool errorIMD;
	bool errorContactorsMismatch;
	bool errorTimer;
	bool errorCAN;
	bool errorOvervoltage;
	bool errorOvercurrent;
	bool errorOW;
	bool errorBMS;
} ErrorStatus_t;

extern ErrorStatus_t errorStatus;

void brain_start(void);

void brain_loop(void);


uint32_t getRuntimeMs(void);
uint32_t getRuntimeMsDiff(uint32_t startTime);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
