/*******************************************************************************
Copyright (c) 2020 - Analog Devices Inc. All Rights Reserved.
This software is proprietary & confidential to Analog Devices, Inc.
and its licensor.
******************************************************************************
* @file:    adbms_main.h
* @brief:   adbms main Heade file
* @version: $Revision$
* @date:    $Date$
* Developed by: ADIBMS Software team, Bangalore, India
*****************************************************************************/
/** @addtogroup MAIN
*  @{
*
*/

/** @addtogroup ADBMS_MAIN MAIN
*  @{
*
*/
#ifndef _ADBMS6830_MAIN_H
#define _ADBMS6830_MAIN_H

#include "main.h"
#include "brain.h"
#include "common.h"
#include "adBms6830Data.h"
#include "adBms6830GenericType.h"
#include "adBms6830ParseCreate.h"
#include "mcuWrapper.h"

typedef enum {
	ADBMS_END = 0, ADBMS_ONGOING, ADBMS_START
} adbms_result_state;

/* Last state returned by adbms_main() - exposed for live debug snapshot */
extern volatile adbms_result_state adbms_current_state;

adbms_result_state adbms_main(AMSStates_t);

uint16_t adBms6830_FindMinVoltageGlobally(void);

#endif
/** @}*/
/** @}*/
