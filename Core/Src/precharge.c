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
//PrechargeState_t state = RX_CAN;
PrechargeState_t state = WRONG;
uint32_t timer;
uint32_t delayStart = 0;

//BYPASS
bool bypassDischarge = true; //bypasss discharge feedback check
bool bypassChecks = false; // bypass feedbacks checks

/* feedback counters */
int8_t fb_dsch = 0;
int8_t fb_air_neg = 0;
int8_t fb_air_pos = 0;
int8_t fb_pre = 0;

//Inicilaização
void Precharge_Init(void) {
	state = START;
	delayStart = 0;

	//reset feedback counters
	fb_dsch = 0;
	fb_air_neg = 0;
	fb_air_pos = 0;
	fb_pre = 0;
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

	case RX_CAN:
		state = START;
		delayStart = 0;
		break;

	case START:
		state = OPEN_ALL;
		break;

	case OPEN_ALL:
		OpenAllContactors();
		if (IsTheStateOK(state) )
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
		if (IsTheStateOK(state) || bypassChecks)
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
		if (IsTheStateOK(state) || bypassChecks)
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
		if (IsTheStateOK(state) || bypassChecks)
			state = TURN_OFF_PRECHARGE;
		else
			state = WRONG;
		break;

	case TURN_OFF_PRECHARGE:
		OpenPreCarga();
		delayStart = now;
		state = DELAY4;
		break;

	case DELAY4:
		if (now - delayStart >= PRECHARGE_DELAY_MS)
			state = VERIFY4;
		break;

	case VERIFY4:
		if (IsTheStateOK(state) || bypassChecks)
			state = END;
		else
			state = WRONG;
		break;

	case END:
		//OnPrechargeComplete();
		break;

	case WRONG:
		delayStart = 0;
		IsTheStateOK(state);
		state = KILL;
		break;

	case KILL:
		OpenAllContactors();
		break;

	default:
		//OnPrechargeError();
		state = WRONG;
		break;
	}
}

bool IsTheStateOK(PrechargeState_t check_state) {

	bool ok = false;

	//Triagem de erros
	/*if (fb_dsch < 0 || fb_dsch > 1); //RaiseError(CONTACTOR_STATE);
	 if (fb_air_neg < 0 || fb_air_neg > 1); //RaiseError(CONTACTOR_STATE)
	 if (fb_air_pos < 0 || fb_air_pos > 1); //RaiseError(CONTACTOR_STATE)
	 if (fb_pre < 0 || fb_pre > 1); //RaiseError(CONTACTOR_STATE)*/

	//o const protege as variaveis aqui para que elas n sejam alteradas durante o processamento desta função
	const bool dsch_on = (fb_dsch == 1);
	const bool air_neg_on = (fb_air_neg == 1);
	const bool air_pos_on = (fb_air_pos == 1);
	const bool pre_on = (fb_pre == 1);

	printfDmaBT(
	 "STATE %d | REAL     DSCH:%d AIR-:%d AIR+:%d PRE:%d\r\n",
	 check_state,
	 fb_dsch,
	 fb_air_neg,
	 fb_air_pos,
	 fb_pre
	 );

	/*printfDmaBT(
	 "STATE %d | DSCH:%d AIR-:%d AIR+:%d PRE:%d\r\n",
	 check_state,
	 dsch_on,
	 air_neg_on,
	 air_pos_on,
	 pre_on
	 );*/

	switch (check_state) {

	case VERIFY1:
		// after closing AIR-
		//return (!dsch_on && air_neg_on && !air_pos_on && !pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:0 PRE:0\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && !air_pos_on && !pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY2:
		// after closing PRE
		//return (!dsch_on && air_neg_on && !air_pos_on && pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:0 PRE:1\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && !air_pos_on && pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY3:
		// after closing AIR+
		//return (!dsch_on && air_neg_on && air_pos_on && pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:1 PRE:1\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && air_pos_on && pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY4:
		// after opening PRE again
		//return (!dsch_on && air_neg_on && air_pos_on && !pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:1 PRE:0\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && air_pos_on && !pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case WRONG:
		// wrong all off
		//return (!dsch_on && !air_neg_on && !air_pos_on && !pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:0 AIR-:0 AIR+:0 PRE:0\r\n", check_state);
		ok = ((!dsch_on || bypassDischarge) && !air_neg_on && !air_pos_on && !pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case OPEN_ALL:
		// first check if all off
		//return (!dsch_on && !air_neg_on && !air_pos_on && !pre_on);

		printfDmaBT("STATE %d | EXPECT DSCH:0 AIR-:0 AIR+:0 PRE:0\r\n", check_state);
		ok = ((!dsch_on || bypassDischarge) && !air_neg_on && !air_pos_on && !pre_on);
		printfDmaBT("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	default:
		printfDmaBT("STATE %d | UNKNOWN STATE\r\n", check_state);
		return false;
	}
}

bool IsBusVoltageOK(void) {
	return true;
}

bool IsCurrentOK(void) {
	return true;
}

void Feedback_EXTI_Callback(uint16_t GPIO_Pin) {
	GPIO_PinState pin_state;

	switch (GPIO_Pin) {
	case GPIO_PIN_12:   // PC12 = MCU_DISCH_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
		fb_dsch += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDmaBT("MCU_DISCH_FB triggered\r\n");
		break;

	case GPIO_PIN_11:   // PC11 = MCU_AIR-_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11);
		fb_air_neg += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDmaBT("MCU_AIR-_FB triggered\r\n");
		break;

	case GPIO_PIN_10:   // PC10 = MCU_AIR+_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_10);
		fb_air_pos += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDmaBT("MCU_AIR+_FB triggered\r\n");
		break;

	case GPIO_PIN_15:   // PA15 = MCU_PRE_FB
		pin_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
		fb_pre += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDmaBT("MCU_PRE_FB triggered\r\n");
		break;

	default:
		break;
	}
}

void PreCharge_CAN_Rx(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data) {
	//lastTime = HAL_GetTick();

	uint32_t id = hdr->StdId;
	uint32_t dlc = hdr->DLC;
	bool start_initiated = false;

	switch (id) {

	case AMS_START_PRE_CHARGE_FRAME_ID:
		struct ams_start_pre_charge_t prechargeInit;
		ams_start_pre_charge_unpack(&prechargeInit, data, dlc);

		if (prechargeInit.precharge_request > 0 && start_initiated == false && state == KILL) {
			state = RX_CAN;
			start_initiated = true;
		} else if (prechargeInit.precharge_request == 0) {
			state = KILL;
			start_initiated = false;
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
