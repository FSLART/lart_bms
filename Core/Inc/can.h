/*
 * can.h
 *
 *  Created on: Nov 20, 2025
 *      Author: jpser
 */

#ifndef INC_CAN_H_
#define INC_CAN_H_

#include "main.h"   // for CAN_HandleTypeDef, CAN_RxHeaderTypeDef

typedef void (*CanRxCallback_t)(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data);

void CanTx_ProcessQueue(void);

HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data);

/**
 * Register a callback that will be called on every received CAN message on FIFO0.
 * Returns HAL_OK on success, HAL_ERROR if the callback list is full.
 */
HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t cb);


#endif /* INC_CAN_H_ */
