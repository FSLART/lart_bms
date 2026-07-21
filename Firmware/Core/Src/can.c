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
	if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
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
		/* Every frame ID on the CAN1 powertrain bus is an 11-bit standard ID.
		   An extended (29-bit) frame is either foreign traffic or a corrupted
		   capture; the CAN1 callbacks read rxHeader.StdId and switch on the
		   standard-ID defines, so an extended frame would decode as the wrong
		   signal with the wrong payload (id-space overlap). Drop it before
		   dispatch. CAN2 intentionally keeps extended frames. */
		if (rxHeader.IDE == CAN_ID_EXT) {
			return;
		}

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

typedef struct {
	uint16_t head;
	uint16_t tail;
	CanTxItem_t item[CAN_TX_QUEUE_SIZE];
} CanTxQueue_t;

uint16_t canTxHead = 0;
uint16_t canTxTail = 0;

CanTxQueue_t can1TxQueue = { 0 };
CanTxQueue_t can2TxQueue = { 0 };

// check if the queuueeu is empty
uint8_t CanTx_IsEmpty(CanTxQueue_t *queuue) {

	if (queuue->head == queuue->tail) {

		return 1;
	}

	return 0;
}

//A queue está cheia????
uint8_t CanTx_IsFull(CanTxQueue_t *queeue) {

	uint16_t next = queeue->head + 1;

	if (next >= CAN_TX_QUEUE_SIZE) {
		next = 0;
	}

	if (next == queeue->tail) {
		return 1;
	}

	return 0;
}

//Função de background pra cagar tudo pro CAN
void CanTx_ProcessSelectedQueue(CanTxQueue_t *queuue, CAN_HandleTypeDef *hcan) {

	uint8_t busIdx = FAULT_CAN_BUS_2;

	if (hcan == &hcan1) {
		busIdx = FAULT_CAN_BUS_1;
	}

	while (!CanTx_IsEmpty(queuue)) {

		// No free mailbox? Stop, will try again next time
		if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) == 0) {

			RAISE_ERROR(FAULT_CAN_MAILBOX_FULL, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);

			////break;
			return;
		}

		CanTxItem_t *item = &queuue->item[queuue->tail];

		CAN_TxHeaderTypeDef txh = { 0 };
		uint32_t mailbox = 0;

		if (item->ide == CAN_ID_EXT) {
			txh.ExtId = item->id & 0x1FFFFFFF;
			txh.IDE = CAN_ID_EXT;
		} else {
			txh.StdId = item->id & 0x7FF;
			txh.IDE = CAN_ID_STD;
		}

		txh.RTR = CAN_RTR_DATA;
		txh.DLC = item->dlc;
		txh.TransmitGlobalTime = DISABLE;

		if (HAL_CAN_GetTxMailboxesFreeLevel(item->hcan) == 0) {
			RAISE_ERROR(FAULT_CAN_MAILBOX_FULL, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);
			//break;
		} else {
			KILL_ERROR(FAULT_CAN_MAILBOX_FULL);
			MCP23017_LED(LED_CAN, OFF);
		}

		if (HAL_CAN_AddTxMessage(hcan, &txh, item->data, &mailbox) != HAL_OK) {
			RAISE_ERROR(FAULT_CAN_SEND_ERROR, .channel_idx = busIdx);
			MCP23017_LED(LED_CAN, ON);
			return;
		}

		// Drop the item we just sent
		queuue->tail++;
		if (queuue->tail >= CAN_TX_QUEUE_SIZE) {
			queuue->tail = 0;
		}

		//ok ehhehehhe
		KILL_ERROR(FAULT_CAN_MAILBOX_FULL);
		KILL_ERROR(FAULT_CAN_SEND_ERROR);
		MCP23017_LED(LED_CAN, OFF);
	}
}

static HAL_StatusTypeDef CAN_TX_Add_To_Queue_Internal(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data, uint8_t ide) {

	//placegolder
	CanTxQueue_t *queueueu = NULL;

	if (hcan == &hcan1) {

		queueueu = &can1TxQueue;

	} else if (hcan == &hcan2) {

		queueueu = &can2TxQueue;

	} else {

		return HAL_ERROR;

	}

	if (queueueu == NULL) {
		return HAL_ERROR;
	}

	if (data == NULL) {
		return HAL_ERROR;
	}

	if (dlc > 8) {
		return HAL_ERROR;
	}

	if (CanTx_IsFull(queueueu)) {
		uint8_t busIdx = FAULT_CAN_BUS_2;

		if (hcan == &hcan1) {
			busIdx = FAULT_CAN_BUS_1;
		}

		RAISE_ERROR(FAULT_CAN_MAILBOX_FULL, .channel_idx = busIdx);
		//return HAL_ERROR;

		// Queue overflow - just drop it
		//TODO: replace oleder messages with fresh ones
		return HAL_OK;
	}

	queueueu->item[queueueu->head].hcan = hcan;
	queueueu->item[queueueu->head].ide = ide;
	queueueu->item[queueueu->head].dlc = dlc;

	if (ide == CAN_ID_EXT) {
		queueueu->item[queueueu->head].id = canID & 0x1FFFFFFF;
	} else {
		queueueu->item[queueueu->head].id = canID & 0x7FF;
	}

	for (uint8_t i = 0; i < dlc; i++) {
		queueueu->item[queueueu->head].data[i] = data[i];
	}

	queueueu->head++;
	if (queueueu->head >= CAN_TX_QUEUE_SIZE) {
		queueueu->head = 0;
	}

	return HAL_OK;
}

//Add standard 11-bit CAN message to queue
HAL_StatusTypeDef CAN_TX_Add_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data) {

	return CAN_TX_Add_To_Queue_Internal(hcan, canID, dlc, data, CAN_ID_STD);
}

//Add extended 29-bit CAN message to queue
HAL_StatusTypeDef CAN_TX_Add_Extended_To_Queue(CAN_HandleTypeDef *hcan, uint32_t canID, uint8_t dlc, const uint8_t *data) {

	return CAN_TX_Add_To_Queue_Internal(hcan, canID, dlc, data, CAN_ID_EXT);
}

// Number of items currently queued for TX on this bus (for live debug)
uint16_t CanTx_GetQueueDepth(CAN_HandleTypeDef *hcan) {
	CanTxQueue_t *queue = NULL;

	if (hcan == &hcan1) {
		queue = &can1TxQueue;
	} else if (hcan == &hcan2) {
		queue = &can2TxQueue;
	} else {
		return 0;
	}

	uint16_t depth = (uint16_t) (queue->head - queue->tail);

	if (queue->head < queue->tail) {
		depth = (uint16_t) (CAN_TX_QUEUE_SIZE - queue->tail + queue->head);
	}

	return depth;
}

/* --- diagnostico do periferico bxCAN --- */

uint8_t CAN_GetState(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return (uint8_t) HAL_CAN_GetState(hcan);
}

uint32_t CAN_GetHwError(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return HAL_CAN_GetError(hcan);
}

/* ESR: TEC[31:24], REC[23:16], LEC[6:4], BOFF bit2, EPVF bit1, EWGF bit0 */
uint8_t CAN_GetTEC(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return (uint8_t) ((hcan->Instance->ESR >> 24) & 0xFFU);
}

uint8_t CAN_GetREC(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return (uint8_t) ((hcan->Instance->ESR >> 16) & 0xFFU);
}

uint8_t CAN_GetLEC(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return (uint8_t) ((hcan->Instance->ESR >> 4) & 0x07U);
}

uint8_t CAN_GetBusOff(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return 0;
	}
	return (uint8_t) ((hcan->Instance->ESR & CAN_ESR_BOFF) ? 1U : 0U);
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

	uint32_t *last_try;

	if (hcan == NULL) {
		return;
	}

	//TODO: maybe only triogger this every 2ms so ISA doesnt crash
	if (hcan == &hcan1) {

		CanTx_ProcessSelectedQueue(&can1TxQueue, &hcan1);

	} else {

		CanTx_ProcessSelectedQueue(&can2TxQueue, &hcan2);

	}

	if (hcan == &hcan1) {
		last_try = &last_can1_try;
	} else if (hcan == &hcan2) {
		last_try = &last_can2_try;
	} else {
		return;
	}

	// Only try recovery every 100 ms
	if ((now - *last_try) < 100) {
		return;
	}

	*last_try = now;

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
		printfDebug(" - HAL_CAN_ERROR_TX_ALST0: arbitration lost mailbox 0\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_TERR0) {
		printfDebug(" - HAL_CAN_ERROR_TX_TERR0: transmit error mailbox 0\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_ALST1) {
		printfDebug(" - HAL_CAN_ERROR_TX_ALST1: arbitration lost mailbox 1\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_TERR1) {
		printfDebug(" - HAL_CAN_ERROR_TX_TERR1: transmit error mailbox 1\r\n");
	}

	if (error & HAL_CAN_ERROR_TX_ALST2) {
		printfDebug(" - HAL_CAN_ERROR_TX_ALST2: arbitration lost mailbox 2\r\n");
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

