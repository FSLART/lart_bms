/*
 * can.h
 *
 *  Created on: Nov 20, 2025
 *      Author: jpser
 */

#ifndef INC_CAN_H_
#define INC_CAN_H_

#include "main.h"   // for CAN_HandleTypeDef, CAN_RxHeaderTypeDef

typedef void (*CanRxCallback_t)(CAN_RxHeaderTypeDef *hdr, uint8_t *data);

void CanTx_ProcessQueue(void);

HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data);

/**
 * Register a callback that will be called on every received CAN message on FIFO0.
 * Returns HAL_OK on success, HAL_ERROR if the callback list is full.
 */
HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t callback);

HAL_StatusTypeDef CAN_Init(CAN_HandleTypeDef *hcan);


//CAN housekeeping
void CAN_Service(CAN_HandleTypeDef *hcan);
uint8_t CAN_IsStarted(CAN_HandleTypeDef *hcan);


#endif /* INC_CAN_H_ */
