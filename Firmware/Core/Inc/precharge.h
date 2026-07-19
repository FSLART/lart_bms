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
    WAIT_FOR_AIR_NEG_TO_CLOSE,
    CHECKING_AIR_NEG_IS_CLOSED,
    SWITCH_PRECHARGE,
    WAIT_FOR_PRECHARGE_TO_CLOSE,
    CHECKING_PRECHARGE_IS_CLOSED,
    VERIFY_CURRENT,
    VERIFY_BUS_VOLT,
    SWITCH_HVPOS,
    WAIT_FOR_AIR_POS_TO_CLOSE,
	CHECKING_AIR_POS_IS_CLOSED,
    TURN_OFF_PRECHARGE,
	WAIT_FOR_PRECHARGE_TO_OPEN,
    CHECKING_PRECHARGE_IS_OPEN,
	HV_ON,
    WRONG,
	KILL,
	RX_CAN

} PrechargeState_t;

void Precharge_Init(void);
void Precharge_CAN_Init(void);

PrechargeState_t Precharge_GetState(void);

void Precharge_ForceKill(void);

void Precharge_Update(void);

void PreCharge_CAN_Rx(CAN_RxHeaderTypeDef *hdr, uint8_t *data);
void PreCharge_CAN2_Rx(CAN_RxHeaderTypeDef *hdr, uint8_t *data);

bool OnPrechargeComplete(PrechargeState_t);
bool IsTheStateOK(PrechargeState_t);
bool IsBusVoltageOK(void);
bool IsCurrentOK(void);

//feebacks interrupt callbacks
void Feedback_EXTI_Callback(uint16_t GPIO_Pin);

void Feedback_DebounceUpdate(void);


#endif /* INC_PRECHARGE_H_ */
