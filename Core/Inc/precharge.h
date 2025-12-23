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
    VERIFY4,
    END,
    WRONG
} PrechargeState_t;

void Precharge_Init(void);
PrechargeState_t Precharge_GetState(void);
void Precharge_Update(void);

bool VerifyHVNEG_HVPOS_States(void);
bool IsBusVoltageOK(void);
bool IsCurrentOK(void);


#endif /* INC_PRECHARGE_H_ */
