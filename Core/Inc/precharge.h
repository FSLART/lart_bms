/*
 * precharge.h
 *
 *  Created on: Nov 17, 2025
 *      Author: jpser
 */

#ifndef INC_PRECHARGE_H_
#define INC_PRECHARGE_H_

typedef enum
{
    START = 0,
    OPEN_ALL,
    SWITCH_HVNEG,
    DELAY1,
    VERIFY1,
    WITCH_PRECHARGE,
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
    ERROR
} PrechargeState_t;

#endif /* INC_PRECHARGE_H_ */
