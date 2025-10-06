/*
 * time_rtc.c
 *
 *  Created on: Oct 6, 2025
 *      Author: jpser
 */

#include "time_rtc.h"
#include <stdio.h>

/* Private */
static RTC_HandleTypeDef *rtc         = &hrtc;

void RTC_Time_Get(char *buf, size_t buf_len) {
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    /* Per HAL rule: read time first, then date */
    HAL_RTC_GetTime(rtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(rtc, &d, RTC_FORMAT_BIN);

    if (buf && buf_len) {
        snprintf(buf, buf_len, "%04u-%02u-%02u %02u:%02u:%02u",
                 2000u + d.Year, d.Month, d.Date,
                 t.Hours, t.Minutes, t.Seconds);
    }
}

void RTC_Time_GetRaw(RTC_TimeTypeDef *t, RTC_DateTypeDef *d) {
    if (!t || !d) return;
    HAL_RTC_GetTime(rtc, t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(rtc, d, RTC_FORMAT_BIN);
}
