/*
 * can.c
 *
 *  Created on: Nov 20, 2025
 *      Author: jpser
 */

#include "can.h"
#include "brain.h"
#include "uartDMA.h"
#include "fault_manager.h"

#define MAX_CAN_RX_CALLBACKS 15  // Número de callbacks registados, tipo CAN_RegisterRxCallback(PreCharge_CAN_Rx);
#define CAN_TX_QUEUE_SIZE    1024 //must be power of 2 only when working with bit masks

CanRxCallback_t rxCallbacks[MAX_CAN_RX_CALLBACKS];
uint8_t callbackCounter = 0;

//CAN housekeeping
uint8_t can1Started = 0;
uint8_t can2Started = 0;
uint32_t lastCanRecoverTry_time = 0;

HAL_StatusTypeDef CAN_Init(CAN_HandleTypeDef *hcan) {
	CAN_FilterTypeDef filter = { 0 };

	// ID=0 + Mask=0 -> match everything
	filter.FilterMode = CAN_FILTERMODE_IDMASK;
	filter.FilterScale = CAN_FILTERSCALE_32BIT;
	filter.FilterIdHigh = 0x0000;
	filter.FilterIdLow = 0x0000;
	filter.FilterMaskIdHigh = 0x0000;
	filter.FilterMaskIdLow = 0x0000;
	filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	filter.FilterActivation = ENABLE;
	filter.SlaveStartFilterBank = 14;

	// Pick a filter bank in the half that belongs to this CAN
	if (hcan == &hcan1) {
		filter.FilterBank = 0;
	} else {
		filter.FilterBank = 14;   // CAN2 must use a bank >= SlaveStartFilterBank
	}

	if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK) {
		uint8_t busIdx = FAULT_CAN_BUS_2;
		if (hcan == &hcan1) {
			busIdx = FAULT_CAN_BUS_1;
		}
		RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
		return HAL_ERROR;
	}

	// Without this, HAL_CAN_RxFifo0MsgPendingCallback will never fire
	if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
		uint8_t busIdx = FAULT_CAN_BUS_2;
		if (hcan == &hcan1) {
			busIdx = FAULT_CAN_BUS_1;
		}

		RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
		return HAL_ERROR;
	}

	return HAL_OK;
}

HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t callback) {
	if (callbackCounter >= MAX_CAN_RX_CALLBACKS) {
		return HAL_ERROR;
	}
	rxCallbacks[callbackCounter] = callback;
	callbackCounter++;
	return HAL_OK;
}

// HAL calls this when a message lands on FIFO0
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
	CAN_RxHeaderTypeDef rxHeader;
	uint8_t rxData[8];

	uint8_t busIdx = FAULT_CAN_BUS_2;
	if (hcan == &hcan1) {
		busIdx = FAULT_CAN_BUS_1;
	}

	//kill before checking for error
	KILL_ERROR(FAULT_CAN_RECEIVE_ERROR);

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) {

		// FUDEU
		RAISE_ERROR(FAULT_CAN_RECEIVE_ERROR, .channel_idx = busIdx);
		return;
	}

	// Send it to everyone who registered
	for (uint8_t i = 0; i < callbackCounter; i++) {
		if (rxCallbacks[i] != NULL) {
			rxCallbacks[i](&rxHeader, rxData);
		}
	}
}

/* ----------- CAN TX QUEUE ----------- */

typedef struct {
	CAN_HandleTypeDef *hcan;
	uint32_t id;
	uint8_t dlc;
	uint8_t data[8];
} CanTxItem_t;

uint16_t canTxHead = 0;
uint16_t canTxTail = 0;
CanTxItem_t canTxQueue[CAN_TX_QUEUE_SIZE];

// check if the queuueeu is empty
uint8_t CanTx_IsEmpty(void) {
	if (canTxHead == canTxTail) {
		return 1;
	}
	return 0;
}

//A queue está cheia????
uint8_t CanTx_IsFull(void) {
	uint16_t next = canTxHead + 1;
	if (next >= CAN_TX_QUEUE_SIZE) {
		next = 0;
	}
	if (next == canTxTail) {
		return 1;
	}
	return 0;
}

//Função de background pra cagar tudo pro CAN
void CanTx_ProcessQueue(void) {
	while (CanTx_IsEmpty() == 0) {

		CanTxItem_t *item = &canTxQueue[canTxTail];

		CAN_TxHeaderTypeDef TxH;
		uint32_t mailbox;

		TxH.StdId = item->id;
		TxH.ExtId = 0;
		TxH.IDE = CAN_ID_STD;
		TxH.RTR = CAN_RTR_DATA;
		TxH.DLC = item->dlc;
		TxH.TransmitGlobalTime = DISABLE;

		uint8_t busIdx = FAULT_CAN_BUS_2;
		if (item->hcan == &hcan1) {
			busIdx = FAULT_CAN_BUS_1;
		}

		// No free mailbox? Stop, will try again next time
		if (HAL_CAN_GetTxMailboxesFreeLevel(item->hcan) == 0) {
			RAISE_ERROR(FAULT_CAN_MAILBOX_FULL, .channel_idx = busIdx);
			break;
		} else {
			KILL_ERROR(FAULT_CAN_MAILBOX_FULL);
		}

		if (HAL_CAN_AddTxMessage(item->hcan, &TxH, item->data, &mailbox) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_SEND_ERROR, .channel_idx = busIdx);
			break;  // Como dou brek, não tento outra vez
		} else {
			KILL_ERROR(FAULT_CAN_SEND_ERROR);
		}

		// Drop the item we just sent
		canTxTail++;
		if (canTxTail >= CAN_TX_QUEUE_SIZE) {
			canTxTail = 0;
		}
	}
}

//Add message to queue
HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data) {
	if (dlc > 8) {
		return HAL_ERROR;
	}

	if (CanTx_IsFull()) {
		// Queue overflow - just drop it
		return HAL_OK;
	}

	canTxQueue[canTxHead].hcan = hcan;
	canTxQueue[canTxHead].id = canID & 0x7FF;   // standard 11-bit ID
	canTxQueue[canTxHead].dlc = dlc;

	for (uint8_t i = 0; i < dlc; i++) {
		canTxQueue[canTxHead].data[i] = data[i];
	}

	canTxHead++;
	if (canTxHead >= CAN_TX_QUEUE_SIZE) {
		canTxHead = 0;
	}

	return HAL_OK;
}

uint8_t CAN_IsStarted(CAN_HandleTypeDef *hcan) {
	if (hcan == &hcan1) {
		return can1Started;
	}

	if (hcan == &hcan2) {
		return can2Started;
	}

	return 0;
}

// Stop and start the CAN, update the started flag
HAL_StatusTypeDef CAN_Restart(CAN_HandleTypeDef *hcan) {
	HAL_CAN_Stop(hcan);

	if (HAL_CAN_Start(hcan) == HAL_OK) {
		if (hcan == &hcan1) {
			can1Started = 1;
		}

		if (hcan == &hcan2) {
			can2Started = 1;
		}
		return HAL_OK;
	}

	if (hcan == &hcan1) {
		can1Started = 0;
	}

	if (hcan == &hcan2) {
		can2Started = 0;
	}
	return HAL_ERROR;
}

/*void CAN_Service(CAN_HandleTypeDef *hcan) {
	uint32_t now = HAL_GetTick();

	uint8_t busIdx = FAULT_CAN_BUS_2;
	if (hcan == &hcan1) {
		busIdx = FAULT_CAN_BUS_1;
	}

	// Dont try to recover too often
	if ((now - lastCanRecoverTry_time) < 100) {
		return;
	}

	// Not started yet -> try to start
	if (CAN_IsStarted(hcan) == 0) {
		if (HAL_CAN_Start(hcan) == HAL_OK) {

			if (hcan == &hcan1) {
				can1Started = 1;
			}

			if (hcan == &hcan2) {
				can2Started = 1;
			}

			KILL_ERROR(FAULT_CAN_INIT_ERROR);

		} else {

			RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);

			if (hcan == &hcan1) {
				can1Started = 0;
			}

			if (hcan == &hcan2) {
				can2Started = 0;
			}

			CAN_Restart(hcan);
		}
		lastCanRecoverTry_time = now;
	} else {
		KILL_ERROR(FAULT_CAN_INIT_ERROR);
	}

	// Bus-off try restart
	uint32_t can_error = HAL_CAN_GetError(hcan);
	if ((can_error & HAL_CAN_ERROR_BOF) != 0) {
		CAN_Restart(hcan);
		lastCanRecoverTry_time = now;
		return;
	}

	// CAN not in a running state try restart
	HAL_CAN_StateTypeDef can_state = HAL_CAN_GetState(hcan);
	if (can_state == HAL_CAN_STATE_RESET || can_state == HAL_CAN_STATE_READY) {
		CAN_Restart(hcan);
		lastCanRecoverTry_time = now;
		return;
	}
}*/

//possible hard fault solve
void CAN_Service(CAN_HandleTypeDef *hcan) {
	static uint32_t last_can1_try = 0;
	static uint32_t last_can2_try = 0;

	uint32_t now = HAL_GetTick();

	static uint32_t last_try = 1;

	if (hcan == NULL) {
		return;
	}

	if (hcan == &hcan1) {
		last_try = last_can1_try;
	} else if (hcan == &hcan2) {
		last_try = last_can2_try;
	} else {
		return;
	}

	// Only try recovery every 100 ms
	if ((now - last_try) < 100) {
		return;
	}

	last_try = now;

	uint32_t error = HAL_CAN_GetError(hcan);
	HAL_CAN_StateTypeDef state = HAL_CAN_GetState(hcan);

	if (error != HAL_CAN_ERROR_NONE) {
		RAISE_ERROR(FAULT_CAN_BUS_OFF);
	}

	//if ((state == HAL_CAN_STATE_ERROR) || (state == HAL_CAN_STATE_RESET) || (state == HAL_CAN_STATE_ERROR)) {
	if ( (state == HAL_CAN_STATE_ERROR) || (state == HAL_CAN_STATE_RESET) ) {

		HAL_CAN_Stop(hcan);
		HAL_CAN_DeInit(hcan);

		if (HAL_CAN_Init(hcan) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_INIT_ERROR);
			return;
		}

		if (CAN_Init(hcan) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_INIT_ERROR);
			return;
		}

		if (HAL_CAN_Start(hcan) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_INIT_ERROR);
			return;
		}

		KILL_ERROR(FAULT_CAN_INIT_ERROR);
	}
}

