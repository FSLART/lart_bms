/*
 * ams_error.h
 *
 *  Created on: Jul 19, 2026
 *      Author: jpser
 */

#ifndef INC_AMS_ERROR_H_
#define INC_AMS_ERROR_H_

#include <stdint.h>

void AMS_Error_Init(void);
void AMS_Error_Trigger(void);
void AMS_Error_TriggerLatched(void);
void AMS_Error_Clear(void);
uint8_t AMS_Error_IsActive(void);
uint8_t AMS_Error_IsPermanent(void);

#endif /* INC_AMS_ERROR_H_ */
