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
#include "gpio_expander.h"

#define MAX_CAN_RX_CALLBACKS 15  // Número de callbacks registados, tipo CAN_RegisterRxCallback(PreCharge_CAN_Rx);
#define CAN_TX_QUEUE_SIZE    1024 //must be power of 2 only when working with bit masks

CanRxCallback_t can1RxCallbacks[MAX_CAN_RX_CALLBACKS];
CanRxCallback_t can2RxCallbacks[MAX_CAN_RX_CALLBACKS];

uint8_t can1CallbackCounter = 0;
uint8_t can2CallbackCounter = 0;

//CAN housekeeping
uint8_t can1Started = 0;
uint8_t can2Started = 0;
uint32_t lastCanRecoverTry_time = 0;

HAL_StatusTypeDef CAN_Init(CAN_HandleTypeDef *hcan) {

	CAN_FilterTypeDef filter = { 0 };
	uint8_t busIdx = FAULT_CAN_BUS_2;

	if (hcan == NULL) {
		return HAL_ERROR;
	}

	if (hcan == &hcan1) {
		busIdx = FAULT_CAN_BUS_1;
	}

	/* ID=0 + Mask=0 -> accept everything */
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
	} else if (hcan == &hcan2) {
		filter.FilterBank = 14;
	} else {
		return HAL_ERROR;
	}

	if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK) {
		RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);
		return HAL_ERROR;
	}

	// Without this, HAL_CAN_RxFifo0MsgPendingCallback will never fire
	if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING)
			!= HAL_OK) {
		RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);
		return HAL_ERROR;
	}

	if (HAL_CAN_Start(hcan) != HAL_OK) {
		RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);

		if (hcan == &hcan1) {
			can1Started = 0;
		} else {
			can2Started = 0;
		}

		return HAL_ERROR;
	}

	if (hcan == &hcan1) {
		can1Started = 1;
	} else {
		can2Started = 1;
	}

	KILL_ERROR(FAULT_CAN_INIT_ERROR);
	MCP23017_LED(LED_CAN, OFF);
	return HAL_OK;

	return HAL_OK;
}

HAL_StatusTypeDef CAN_RegisterRxCallback(CanRxCallback_t callback) {
	if (can1CallbackCounter >= MAX_CAN_RX_CALLBACKS) {
		return HAL_ERROR;
	}

	can1RxCallbacks[can1CallbackCounter] = callback;
	can1CallbackCounter++;

	return HAL_OK;
}

HAL_StatusTypeDef CAN2_RegisterRxCallback(CanRxCallback_t callback) {
	if (can2CallbackCounter >= MAX_CAN_RX_CALLBACKS) {
		return HAL_ERROR;
	}

	can2RxCallbacks[can2CallbackCounter] = callback;
	can2CallbackCounter++;

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
	MCP23017_LED(LED_CAN, OFF);

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) {

		// FUDEU
		RAISE_ERROR(FAULT_CAN_RECEIVE_ERROR, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);
		return;
	}

	// Send it to everyone who registered
	if (hcan == &hcan1) {
		for (uint8_t i = 0; i < can1CallbackCounter; i++) {
			if (can1RxCallbacks[i] != NULL) {
				can1RxCallbacks[i](&rxHeader, rxData);
			}
		}
	} else if (hcan == &hcan2) {
		for (uint8_t i = 0; i < can2CallbackCounter; i++) {
			if (can2RxCallbacks[i] != NULL) {
				can2RxCallbacks[i](&rxHeader, rxData);
			}
		}
	}
}

/* ----------- CAN TX QUEUE ----------- */

typedef struct {
	CAN_HandleTypeDef *hcan;
	uint32_t id;
	uint8_t dlc;
	uint8_t ide;
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

		TxH.StdId = 0;
		TxH.ExtId = 0;

		if (item->ide == CAN_ID_EXT) {
			TxH.ExtId = item->id & 0x1FFFFFFF;
			TxH.IDE = CAN_ID_EXT;
		} else {
			TxH.StdId = item->id & 0x7FF;
			TxH.IDE = CAN_ID_STD;
		}

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
			MCP23017_LED(LED_CAN, ON);
			//break;
		} else {
			KILL_ERROR(FAULT_CAN_MAILBOX_FULL);
			MCP23017_LED(LED_CAN, OFF);
		}

		if (HAL_CAN_AddTxMessage(item->hcan, &TxH, item->data, &mailbox)
				!= HAL_OK) {
			RAISE_ERROR(FAULT_CAN_SEND_ERROR, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);
			break;  // Como dou brek, não tento outra vez
		} else {
			KILL_ERROR(FAULT_CAN_SEND_ERROR);
			MCP23017_LED(LED_CAN, OFF);
		}

		// Drop the item we just sent
		canTxTail++;
		if (canTxTail >= CAN_TX_QUEUE_SIZE) {
			canTxTail = 0;
		}
	}
}

static HAL_StatusTypeDef CAN_TX_Add_To_Queue_Internal(CAN_HandleTypeDef *hcan,
		uint32_t canID, uint8_t dlc, const uint8_t *data, uint8_t ide) {
	if (dlc > 8) {
		return HAL_ERROR;
	}

	if (CanTx_IsFull()) {
		// Queue overflow - just drop it
		return HAL_OK;
	}

	canTxQueue[canTxHead].hcan = hcan;
	canTxQueue[canTxHead].ide = ide;

	if (ide == CAN_ID_EXT) {
		canTxQueue[canTxHead].id = canID & 0x1FFFFFFF;
	} else {
		canTxQueue[canTxHead].id = canID & 0x7FF;
	}

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

//Add standard 11-bit CAN message to queue
HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID,
		uint8_t dlc, const uint8_t *data) {
	return CAN_TX_Add_To_Queue_Internal(hcan, canID, dlc, data, CAN_ID_STD);
}

//Add extended 29-bit CAN message to queue
HAL_StatusTypeDef CAN_TX_Add_Extended_To_Queue(CAN_HandleTypeDef *hcan,
		uint32_t canID, uint8_t dlc, const uint8_t *data) {
	return CAN_TX_Add_To_Queue_Internal(hcan, canID, dlc, data, CAN_ID_EXT);
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

	uint8_t busIdx = FAULT_CAN_BUS_2;
	if (hcan == &hcan1) {
		busIdx = FAULT_CAN_BUS_1;
	}

	uint32_t error = HAL_CAN_GetError(hcan);
	HAL_CAN_StateTypeDef state = HAL_CAN_GetState(hcan);

	if (error == HAL_CAN_ERROR_NONE) {
		KILL_ERROR(FAULT_CAN_SEND_ERROR);
		MCP23017_LED(LED_CAN, OFF);
		return;
	}

	if (error != HAL_CAN_ERROR_NONE) {
		RAISE_ERROR(FAULT_CAN_SEND_ERROR, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);
	}

	if (error == HAL_CAN_ERROR_BOF) {
		RAISE_ERROR(FAULT_CAN_BUS_OFF, .channel_idx = busIdx);
		MCP23017_LED(LED_CAN, ON);
	}

	//if ((state == HAL_CAN_STATE_ERROR) || (state == HAL_CAN_STATE_RESET) || (state == HAL_CAN_STATE_ERROR)) {
	if ((state == HAL_CAN_STATE_ERROR) || (state == HAL_CAN_STATE_RESET)) {

		HAL_CAN_Stop(hcan);
		HAL_CAN_DeInit(hcan);

		if (HAL_CAN_Init(hcan) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);
			return;
		}

		if (CAN_Init(hcan) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_INIT_ERROR, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);
			return;
		}

		KILL_ERROR(FAULT_CAN_INIT_ERROR);
		MCP23017_LED(LED_CAN, OFF);
	}

	//CAN_PrintHalError(error);
	HAL_CAN_ResetError(hcan);
}

void CAN_PrintHalError(uint32_t error) {
	printfDebug("HAL_CAN error = 0x%08lX\r\n", error);

	if (error & HAL_CAN_ERROR_EWG) {
		printfDebug(" - HAL_CAN_ERROR_EWG: error warning\r\n");
	}

	if (error & HAL_CAN_ERROR_EPV) {
		printfDebug(" - HAL_CAN_ERROR_EPV: error passive\r\n");
	}

	if (error & HAL_CAN_ERROR_BOF) {
		printfDebug(" - HAL_CAN_ERROR_BOF: bus off\r\n");
	}

	if (error & HAL_CAN_ERROR_STF) {
		printfDebug(" - HAL_CAN_ERROR_STF: stuff error\r\n");
	}

	if (error & HAL_CAN_ERROR_FOR) {
		printfDebug(" - HAL_CAN_ERROR_FOR: form error\r\n");
	}

	if (error & HAL_CAN_ERROR_ACK) {
		printfDebug(" - HAL_CAN_ERROR_ACK: no ACK received\r\n");
	}

	if (error & HAL_CAN_ERROR_BR) {
		printfDebug(" - HAL_CAN_ERROR_BR: bit recessive error\r\n");
	}

	if (error & HAL_CAN_ERROR_BD) {
		printfDebug(" - HAL_CAN_ERROR_BD: bit dominant error\r\n");
	}

	if (error & HAL_CAN_ERROR_CRC) {
		printfDebug(" - HAL_CAN_ERROR_CRC: CRC error\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_ALST0) {
		printfDebug(
				" - HAL_CAN_ERROR_TX_ALST0: arbitration lost mailbox 0\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_TERR0) {
		printfDebug(" - HAL_CAN_ERROR_TX_TERR0: transmit error mailbox 0\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_ALST1) {
		printfDebug(
				" - HAL_CAN_ERROR_TX_ALST1: arbitration lost mailbox 1\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_TERR1) {
		printfDebug(" - HAL_CAN_ERROR_TX_TERR1: transmit error mailbox 1\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_ALST2) {
		printfDebug(
				" - HAL_CAN_ERROR_TX_ALST2: arbitration lost mailbox 2\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_TERR2) {
		printfDebug(" - HAL_CAN_ERROR_TX_TERR2: transmit error mailbox 2\r\n");
	}

	if (error & HAL_CAN_ERROR_TIMEOUT) {
		printfDebug(" - HAL_CAN_ERROR_TIMEOUT\r\n");
	}

	if (error & HAL_CAN_ERROR_NOT_INITIALIZED) {
		printfDebug(" - HAL_CAN_ERROR_NOT_INITIALIZED\r\n");
	}

	if (error & HAL_CAN_ERROR_NOT_READY) {
		printfDebug(" - HAL_CAN_ERROR_NOT_READY\r\n");
	}

	if (error & HAL_CAN_ERROR_NOT_STARTED) {
		printfDebug(" - HAL_CAN_ERROR_NOT_STARTED\r\n");
	}

	if (error & HAL_CAN_ERROR_PARAM) {
		printfDebug(" - HAL_CAN_ERROR_PARAM\r\n");
	}
}

