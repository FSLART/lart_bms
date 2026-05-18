/*
 * charger.h
 *
 *  Created on: May 10, 2026
 *      Author: jpser
 */

#ifndef INC_CHARGER_H_
#define INC_CHARGER_H_

#include <stdbool.h>
#include <stdint.h>
#include "main.h"

void Charger_CAN_Init(void);

void Charger_CAN_Comms_RX(CAN_RxHeaderTypeDef *hdr, uint8_t *data);
void Charger_CAN_Requests_RX(CAN_RxHeaderTypeDef *hdr, uint8_t *data);

void Charger_Update(void);
void Charger_Stop(void);
void Charger_SendRequest(bool enable);

bool Charger_IsRequestedCurrentOK(void);
uint8_t Charger_IsRequested(void);

uint16_t Charger_GetRequestedCurrentRaw(void);

uint8_t Charger_HasStatus(void);

#endif /* INC_CHARGER_H_ */
