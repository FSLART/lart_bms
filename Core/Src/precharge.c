/*
 * precharge.c
 *
 *  Created on: Nov 17, 2025
 *      Author: jpser
 */
#include "precharge.h"

#include <stdbool.h>

#include "contactors.h"

#include "can.h"

#include "dbc/ams.h"

#include "uartDMA.h"

#define PRECHARGE_DELAY_MS   200u  // tempo de chekagewm da atracagem do contactor

//Internal variables
PrechargeState_t state = WRONG;
uint32_t timer;
uint32_t delayStart = 0;

//BYPASS
bool bypassChecks = true;

//Inicilaização
void Precharge_Init(void) {
	state = START;
	delayStart = 0;
}

void Precharge_CAN_Init(void) {
	CAN_RegisterRxCallback(PreCharge_CAN_Rx);
}

//Get current state
PrechargeState_t Precharge_GetState(void) {
	return state;
}

void Precharge_Update(void) {
	uint32_t now = HAL_GetTick();

	switch (state) {
	case START:
		state = OPEN_ALL;
		break;

	case OPEN_ALL:
		OpenAllContactors();
		state = SWITCH_HVNEG;
		break;

	case SWITCH_HVNEG:
		CloseAIR_negativo();
		delayStart = now;
		state = DELAY1;
		break;

	case DELAY1:
		if (now - delayStart >= PRECHARGE_DELAY_MS)
			state = VERIFY1;
		break;

	case VERIFY1:
		if (VerifyHVNEG_HVPOS_States() || bypassChecks)
			state = SWITCH_PRECHARGE;
		else
			state = WRONG;
		break;

	case SWITCH_PRECHARGE:
		ClosePreCarga();
		delayStart = now;
		state = DELAY2;
		break;

	case DELAY2:
		if (now - delayStart >= 3000)
			state = VERIFY2;
		break;

	case VERIFY2:
		if (VerifyHVNEG_HVPOS_States() || bypassChecks)
			state = VERIFY_CURRENT;
		else
			state = WRONG;
		break;

	case VERIFY_CURRENT:
		if (IsCurrentOK() || bypassChecks)
			state = VERIFY_BUS_VOLT;
		else
			state = WRONG;
		break;

	case VERIFY_BUS_VOLT:
		if (IsBusVoltageOK() || bypassChecks)
			state = SWITCH_HVPOS;
		else
			state = WRONG;
		break;

	case SWITCH_HVPOS:
		CloseAIR_positivo();
		delayStart = now;
		state = DELAY3;
		break;

	case DELAY3:
		if (now - delayStart >= PRECHARGE_DELAY_MS)
			state = VERIFY3;
		break;

	case VERIFY3:
		if (VerifyHVNEG_HVPOS_States() || bypassChecks)
			state = TURN_OFF_PRECHARGE;
		else
			state = WRONG;
		break;

	case TURN_OFF_PRECHARGE:
		OpenPreCarga();
		state = VERIFY4;
		break;

	case VERIFY4:
		if (VerifyHVNEG_HVPOS_States() || bypassChecks)
			state = END;
		else
			state = WRONG;
		break;

	case END:
		//OnPrechargeComplete();
		break;

	case WRONG:
		Precharge_Init();
		break;

	case KILL:
		OpenAllContactors();
		break;
	default:
		//OnPrechargeError();
		Precharge_Init();
		break;
	}
}

bool VerifyHVNEG_HVPOS_States(void) {
	return false;
}

bool IsBusVoltageOK(void) {
	return false;
}

bool IsCurrentOK(void) {
	return false;
}

void PreCharge_CAN_Rx(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data) {
	//lastTime = HAL_GetTick();

	uint32_t id = hdr->StdId;
	uint32_t dlc = hdr->DLC;

	switch (id) {

	case AMS_START_PRE_CHARGE_FRAME_ID:
		struct ams_start_pre_charge_t prechargeInit;
		ams_start_pre_charge_unpack(&prechargeInit, data, dlc);

		if(prechargeInit.precharge_request > 0){
			state = RX_CAN;
		}else if(prechargeInit.precharge_request == 0){
			state = KILL;
			Precharge_Update();
		}

	default:
		/*printConsole("Unknown CAN ID 0x%03lX, DLC=%lu, Data:", id, dlc);
		for (uint32_t i = 0; i < dlc; i++) {
			printConsole(" %02X", data[i]);
		}
		printConsole("\r\n");*/
		break;
	}
}
