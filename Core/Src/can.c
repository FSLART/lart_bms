/*
 * can.c
 *
 *  Created on: Nov 20, 2025
 *      Author: jpser
 */

#include "can.h"
#include "brain.h"

#ifndef MAX_CAN_RX_CALLBACKS
#define MAX_CAN_RX_CALLBACKS 4
#endif

static CanRxCallback_t s_rxCallbacks[MAX_CAN_RX_CALLBACKS];
static uint8_t s_numCallbacks = 0;

HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t cb) {
	if (s_numCallbacks >= MAX_CAN_RX_CALLBACKS) {
		return HAL_ERROR;
	}
	s_rxCallbacks[s_numCallbacks++] = cb;
	return HAL_OK;
}

// This must exist in exactly ONE C file in the project
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
	CAN_RxHeaderTypeDef rxHeader;
	uint8_t rxData[8];

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) {
		//Error_Handler();
		//FUDEU
		return;
	}

	// Call all registered listeners
	for (uint8_t i = 0; i < s_numCallbacks; ++i) {
		if (s_rxCallbacks[i]) {
			s_rxCallbacks[i](&rxHeader, rxData);
		}
	}
}

/* ----------- CAN TX QUEUE ----------- */

#define CAN_TX_QUEUE_SIZE   256  // adjust as needed; must be power-of-2 only if you do bitmask tricks

typedef struct {
	CAN_HandleTypeDef *hcan;
	uint32_t id;
	uint8_t dlc;
	uint8_t data[8];
} CanTxItem_t;

uint16_t canTxHead = 0;
uint16_t canTxTail = 0;
CanTxItem_t canTxQueue[CAN_TX_QUEUE_SIZE];

//Is queue empty?
uint8_t CanTx_IsEmpty(void) {
	return (canTxHead == canTxTail);
}

//A queue está cheia????
uint8_t CanTx_IsFull(void) {
	uint16_t next = (uint16_t) ((canTxHead + 1U) % CAN_TX_QUEUE_SIZE);
	return (next == canTxTail);
}

//Função de background pra cagar tudo pro CAN
void CanTx_ProcessQueue(void) {
	// Try to send as many queued frames as there are free mailboxes - verys smart, thank you gpt :)
	while (!CanTx_IsEmpty()) {

		CanTxItem_t *item = &canTxQueue[canTxTail];

		CAN_TxHeaderTypeDef TxH;
		uint32_t mailbox;

		TxH.StdId = item->id;
		TxH.ExtId = 0U;
		TxH.IDE = CAN_ID_STD;
		TxH.RTR = CAN_RTR_DATA;
		TxH.DLC = item->dlc;
		TxH.TransmitGlobalTime = DISABLE;

		// No free mailbox? Stop, will try again next time
		if (HAL_CAN_GetTxMailboxesFreeLevel(item->hcan) == 0U) {
			break;
		}

		if (HAL_CAN_AddTxMessage(item->hcan, &TxH, item->data, &mailbox) != HAL_OK) {

			// TODO: log error, drop this frame and move on
			// printConsole("CAN TX ERR ID=0x%03lX\r\n", (unsigned long)item->id);

			break;// Como dou brek, não tento outra vez
		}

		// Pop from queue
		canTxTail = (uint16_t) ((canTxTail + 1U) % CAN_TX_QUEUE_SIZE);
	}
}

//Add message to queue
HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data) {
	if (dlc > 8U) {
		return HAL_ERROR;
	}

	if (CanTx_IsFull()) {
		// Queue overflow
		// TODO: return error s
		//return HAL_ERROR;
		return HAL_OK;
	}

	uint16_t pos = canTxHead;
	canTxQueue[pos].hcan = hcan;
	canTxQueue[pos].id = canID & 0x7FFU;   // standard ID
	canTxQueue[pos].dlc = dlc;

	for (uint8_t i = 0; i < dlc; i++) {
		canTxQueue[pos].data[i] = data[i];
	}

	// Advance head atomically-ish
	canTxHead = (uint16_t) ((pos + 1U) % CAN_TX_QUEUE_SIZE);

	return HAL_OK;
}

