/*
 * precharge.c
 *
 *  Created on: Nov 17, 2025
 *      Author: jpser
 */
#include "precharge.h"

#define PRECHARGE_DELAY_MS   200u  // tempo de chekagewm da atracagem do contactor

//Internal variables
PrechargeState_t g_state = START;
uint32_t timer;

//Inicilaização
void Precharge_Init(void)
{
    state = START;
    delayStart = 0;
}

//Get current state
PrechargeState_t Precharge_GetState(void)
{
    return state;
}

void Precharge_Update(void)
{
    uint32_t now = GetTimeMs();

    switch (state)
    {
        case START:
            state = OPEN_ALL;
            break;

        case OPEN_ALL:
            OpenAllRelays();
            state = SWITCH_HVNEG;
            break;

        case SWITCH_HVNEG:
            CloseHVNEG();
            delayStart = now;
            state = DELAY1;
            break;

        case DELAY1:
            if (now - delayStart >= PRECHARGE_DELAY_MS)
                state = VERIFY1;
            break;

        case VERIFY1:
            if (VerifyHVNEG_HVPOS_States())
                state = SWITCH_PRECHARGE;
            else
                state = ERROR;
            break;

        case SWITCH_PRECHARGE:
            ClosePrecharge();
            delayStart = now;
            state = DELAY2;
            break;

        case DELAY2:
            if (now - delayStart >= PRECHARGE_DELAY_MS)
                state = VERIFY2;
            break;

        case VERIFY2:
            if (VerifyHVNEG_HVPOS_States())
                state = VERIFY_CURRENT;
            else
                state = ERROR;
            break;

        case VERIFY_CURRENT:
            if (IsCurrentOK())
                state = VERIFY_BUS_VOLT;
            else
                state = ERROR;
            break;

        case VERIFY_BUS_VOLT:
            if (IsBusVoltageOK())
                state = SWITCH_HVPOS;
            else
                state = ERROR;
            break;

        case SWITCH_HVPOS:
            CloseHVPOS();
            delayStart = now;
            state = DELAY3;
            break;

        case DELAY3:
            if (now - delayStart >= PRECHARGE_DELAY_MS)
                state = VERIFY3;
            break;

        case VERIFY3:
            if (VerifyHVNEG_HVPOS_States())
                state = TURN_OFF_PRECHARGE;
            else
                state = ERROR;
            break;

        case TURN_OFF_PRECHARGE:
            OpenPrecharge();
            state = VERIFY4;
            break;

        case VERIFY4:
            if (VerifyHVNEG_HVPOS_States())
                state = END;
            else
                state = ERROR;
            break;

        case END:
            OnPrechargeComplete();
            break;

        case ERROR:
        default:
            OnPrechargeError();
            break;
    }
}
