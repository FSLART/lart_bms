/*
 * precharge.h
 *
 *  Created on: Nov 17, 2025
 *      Author: jpser
 */

#ifndef INC_PRECHARGE_H_
#define INC_PRECHARGE_H_

#include "main.h"
#include <stdbool.h>

typedef enum
{
	START = 0,
    OPEN_ALL,
    SWITCH_HVNEG,
    DELAY1,
    VERIFY1,
    SWITCH_PRECHARGE,
    DELAY2,
    VERIFY2,
    VERIFY_CURRENT,
    VERIFY_BUS_VOLT,
    SWITCH_HVPOS,
    DELAY3,
    VERIFY3,
    TURN_OFF_PRECHARGE,
	DELAY4,
    VERIFY4,
    END,
    WRONG,
	KILL,
	RX_CAN

} PrechargeState_t;

void Precharge_Init(void);
void Precharge_CAN_Init(void);

PrechargeState_t Precharge_GetState(void);

void Precharge_Update(void);

void PreCharge_CAN_Rx(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data);

bool OnPrechargeComplete(PrechargeState_t);
bool IsTheStateOK(PrechargeState_t);
bool IsBusVoltageOK(void);
bool IsCurrentOK(void);

//feebacks interrupt callbacks
void Feedback_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_PRECHARGE_H_ */
