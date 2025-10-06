/*
 * time_rtc.h
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */

#ifndef INC_TIME_RTC_H_
#define INC_TIME_RTC_H_

#include "stm32f4xx_hal.h"
#include "main.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Formats current RTC time into "YYYY-MM-DD HH:MM:SS".
   Requires that MX_RTC_Init() has already been called. */
void RTC_Time_Get(char *buf, size_t buf_len);

/* Raw structs, if you prefer */
void RTC_Time_GetRaw(RTC_TimeTypeDef *t, RTC_DateTypeDef *d);

#ifdef __cplusplus
}
#endif

#endif /* INC_TIME_RTC_H_ */
