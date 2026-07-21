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

HAL_StatusTypeDef CAN_TX_Add_Extended_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data);

/**
 * Register a callback that will be called on every received CAN message on FIFO0.
 * Returns HAL_OK on success, HAL_ERROR if the callback list is full.
 */
HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t callback);

HAL_StatusTypeDef CAN2_RegisterRxCallback(CanRxCallback_t callback);

HAL_StatusTypeDef CAN_Init(CAN_HandleTypeDef *hcan);


//CAN housekeeping
void CAN_Service(CAN_HandleTypeDef *hcan);
uint8_t CAN_IsStarted(CAN_HandleTypeDef *hcan);
uint16_t CanTx_GetQueueDepth(CAN_HandleTypeDef *hcan);

/* diagnostico do periferico bxCAN (live debug) */
uint8_t  CAN_GetState(CAN_HandleTypeDef *hcan);     // HAL_CAN_StateTypeDef
uint32_t CAN_GetHwError(CAN_HandleTypeDef *hcan);   // HAL_CAN_GetError() bitmask
uint8_t  CAN_GetTEC(CAN_HandleTypeDef *hcan);       // transmit error counter (ESR)
uint8_t  CAN_GetREC(CAN_HandleTypeDef *hcan);       // receive error counter (ESR)
uint8_t  CAN_GetLEC(CAN_HandleTypeDef *hcan);       // last error code (ESR 6:4)
uint8_t  CAN_GetBusOff(CAN_HandleTypeDef *hcan);    // 1 = bus-off (ESR BOFF)

void CAN_PrintHalError(uint32_t error);


#endif /* INC_CAN_H_ */
