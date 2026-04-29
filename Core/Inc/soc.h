/*
 * soc.h
 *
 *  Created on: Apr 26, 2026
 *      Author: jpser
 */

#ifndef INC_SOC_H_
#define INC_SOC_H_

#include <stdint.h>
#include <stdbool.h>

void SOC_Init(uint16_t min_cell_mV);

void SOC_NotifyAsReading(int32_t as_now);

void SOC_NotifyIVTReset(void);

float SOC_GetPercent(void);

int32_t SOC_GetUsedCharge_As(void);

bool SOC_IsReady(void);

void SOC_DumpUART(void);

void SOC_SendCAN(float soc_percent);

#endif /* INC_SOC_H_ */
