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

#define PRECHARGE_CAN_LOCKOUT_MS   15000u //esperar este tempo antes de processar uma nova requisição de inicialização de precarga
#define CONTACTOR_DELAY_MS   100u  // tempo de chekagewm da atracagem do contactor

//Internal variables
//PrechargeState_t state = RX_CAN;
PrechargeState_t state = WRONG;
uint32_t timer;
uint32_t delayStart = 0;
uint32_t canRxIgnoreUntil = 0;

//BYPASS FEEDBACKS
bool bypassDischarge = true; //bypasss discharge feedback check
bool bypassChecks = false; // bypass feedbacks checks

/* feedback counters */
int8_t fb_dsch = 0;
int8_t fb_air_neg = 0;
int8_t fb_air_pos = 0;
int8_t fb_pre = 0;

/**
 *******************************************************************************
 * Function: Precharge_Init
 * @brief Initialize the precharge state machine.
 *
 * @details This function initializes the precharge sequence by setting the
 *          state machine to the START state and resetting the delay timer.
 *          It also clears all contactor feedback counters to ensure that the
 *          precharge logic starts from a known and safe initial condition.
 *
 * Parameters:
 *
 * @return None
 *
 *******************************************************************************
 */
void Precharge_Init(void) {
	state = START;
	delayStart = 0;

	//reset feedback counters
	fb_dsch = 0;
	fb_air_neg = 0;
	fb_air_pos = 0;
	fb_pre = 0;
}

/**
 *******************************************************************************
 * Function: Precharge_CAN_Init
 * @brief Initialize CAN reception for the precharge module.
 *
 * @details This function registers the precharge CAN reception callback
 *          function to the CAN driver. Once registered, the callback will be
 *          triggered whenever a CAN message is received, allowing the
 *          precharge module to process relevant commands or status messages.
 *
 * Parameters:
 *
 * @return None
 *
 *******************************************************************************
 */
void Precharge_CAN_Init(void) {
	CAN_RegisterRxCallback(PreCharge_CAN_Rx);
}

/**
 *******************************************************************************
 * Function: Precharge_GetState
 * @brief Get current precharge state.
 *
 * @details This function returns the current state of the precharge state
 *          machine, allowing other modules in the system to monitor the
 *          progress or status of the precharge sequence.
 *
 * Parameters:
 *
 * @return PrechargeState_t
 *
 *******************************************************************************
 */
PrechargeState_t Precharge_GetState(void) {
	return state;
}

/**
 *******************************************************************************
 * Function: Precharge_Update
 * @brief Update the precharge state machine.
 *
 * @details This function executes the precharge sequence state machine. It
 *          reads the current system tick, evaluates the active precharge
 *          state, commands the contactors, applies the required delays, and
 *          verifies the expected feedback conditions before advancing to the
 *          next step. It also performs intermediate checks during capacitor
 *          charging, validates current and bus voltage conditions, handles the
 *          successful completion of the sequence, and forces the system into a
 *          safe shutdown state if any step fails.
 *
 * Parameters:
 *
 * @return None
 *
 *******************************************************************************
 */
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
		if (IsTheStateOK(state))
			state = SWITCH_HVNEG;
		break;

	case SWITCH_HVNEG:
		CloseAIR_negativo();
		delayStart = now;
		state = DELAY1;
		break;

	case DELAY1:
		if (now - delayStart >= CONTACTOR_DELAY_MS)
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
		static int xanato_counter = 0;

		if (xanato_counter == 0) {
			xanato_counter = delayStart;
		}

		if (now - delayStart >= 3000) {
			xanato_counter = 0; //Reset pra próxima
			state = VERIFY2;

		}
		//making sure the contactors are right during the charge of the capacitor
		else if (now - xanato_counter >= CONTACTOR_DELAY_MS && !bypassChecks) {
			xanato_counter += CONTACTOR_DELAY_MS;

			// run every CONTACTOR_DELAY_MS after the first CONTACTOR_DELAY_MS
			if (!IsTheStateOK(VERIFY2)) {
				xanato_counter = 0;   // reset before leaving
				state = WRONG;
			}
		}
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
		if (now - delayStart >= CONTACTOR_DELAY_MS)
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
		if (now - delayStart >= CONTACTOR_DELAY_MS)
			state = VERIFY4;
		break;

	case VERIFY4:
		if (IsTheStateOK(state) || bypassChecks)
			state = END;
		else
			state = WRONG;
		break;

	case END:
		if (!OnPrechargeComplete(state)) {
			state = KILL;
		}

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
/**
 *******************************************************************************
 * Function: OnPrechargeComplete
 * @brief Monitor system state after precharge completion.
 *
 * @details This function periodically verifies that the system remains in a
 *          valid state after the precharge sequence has completed. It allows
 *          the system to skip checks for a defined number of cycles and then
 *          validates the expected contactor feedback state using the provided
 *          state guard. If the validation fails, the function reports an
 *          invalid condition so the system can transition to a safe state.
 *
 * Parameters:
 *
 * @param [in]  state_guard              Expected precharge state used for
 *                                       validation checks
 *
 * @return bool                          Status of the post-precharge validation
 *
 *******************************************************************************
 */
bool OnPrechargeComplete(PrechargeState_t state_guard) {

	bool ok = false;

	static int ticks = 0;

	static int skip_ticks = 10000;

	if (ticks > skip_ticks) {

		ok = true;

		if (!IsTheStateOK(state_guard)) {
			ok = false;
		}

		ticks = 0;

	} else {
		ok = true;
		ticks++;
	}

	return ok;
}

/**
 *******************************************************************************
 * Function: IsTheStateOK
 * @brief Verify contactor feedback state for a given precharge step.
 *
 * @details This function checks whether the real contactor feedback signals
 *          match the expected contactor state for the selected precharge
 *          verification step. It reads the internal feedback counters,
 *          converts them into boolean contactor status values, prints debug
 *          information about the measured and expected states, and returns the
 *          validation result. It also supports bypassing the discharge
 *          feedback check when required by the precharge logic.
 *
 * Parameters:
 *
 * @param [in]  check_state              Precharge state to be validated
 *
 * @return bool                          Feedback state validation result
 *
 *******************************************************************************
 */
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

	printfDebug("STATE %d | REAL     DSCH:%d AIR-:%d AIR+:%d PRE:%d\r\n", check_state, fb_dsch, fb_air_neg, fb_air_pos, fb_pre);

	/*printfDebug(
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

		printfDebug("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:0 PRE:0\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && !air_pos_on && !pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY2:
		// after closing PRE
		//return (!dsch_on && air_neg_on && !air_pos_on && pre_on);

		printfDebug("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:0 PRE:1\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && !air_pos_on && pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY3:
		// after closing AIR+
		//return (!dsch_on && air_neg_on && air_pos_on && pre_on);

		printfDebug("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:1 PRE:1\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && air_pos_on && pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case VERIFY4:
		// after opening PRE again
		//return (!dsch_on && air_neg_on && air_pos_on && !pre_on);

		printfDebug("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:1 PRE:0\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && air_pos_on && !pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case END:
		// after opening PRE again
		//return (!dsch_on && air_neg_on && air_pos_on && !pre_on);

		printfDebug("STATE %d | EXPECT DSCH:1 AIR-:1 AIR+:1 PRE:0\r\n", check_state);
		ok = ((dsch_on || bypassDischarge) && air_neg_on && air_pos_on && !pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case WRONG:
		// wrong all off
		//return (!dsch_on && !air_neg_on && !air_pos_on && !pre_on);

		printfDebug("STATE %d | EXPECT DSCH:0 AIR-:0 AIR+:0 PRE:0\r\n", check_state);
		ok = ((!dsch_on || bypassDischarge) && !air_neg_on && !air_pos_on && !pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	case OPEN_ALL:
		// first check if all off
		//return (!dsch_on && !air_neg_on && !air_pos_on && !pre_on);

		printfDebug("STATE %d | EXPECT DSCH:0 AIR-:0 AIR+:0 PRE:0\r\n", check_state);
		ok = ((!dsch_on || bypassDischarge) && !air_neg_on && !air_pos_on && !pre_on);
		printfDebug("RESULT: %s\r\n", ok ? "OK" : "FAIL");
		return ok;

	default:
		printfDebug("STATE %d | UNKNOWN STATE\r\n", check_state);
		return false;
	}
}

/**
 *******************************************************************************
 * Function: IsBusVoltageOK
 * @brief Verify DC bus voltage condition.
 *
 * @details This function checks whether the DC bus voltage is within the
 *          expected operating range during the precharge sequence. It is used
 *          as a validation step before closing the main positive contactor.
 *          The current implementation returns a fixed valid condition and is
 *          intended to be replaced with the actual voltage measurement logic.
 *
 * Parameters:
 *
 * @return bool                          Bus voltage validation result
 *
 *******************************************************************************
 */
bool IsBusVoltageOK(void) {
	return true;
}

/**
 *******************************************************************************
 * Function: IsCurrentOK
 * @brief Verify current condition during precharge.
 *
 * @details This function checks whether the measured current is within the
 *          acceptable limits during the precharge sequence. It is used to
 *          validate that no abnormal current is present before allowing the
 *          precharge process to continue. The current implementation returns a
 *          fixed valid condition and is intended to be replaced with the
 *          actual current measurement and validation logic.
 *
 * Parameters:
 *
 * @return bool                          Current validation result
 *
 *******************************************************************************
 */
bool IsCurrentOK(void) {
	return true;
}

/**
 *******************************************************************************
 * Function: Feedback_EXTI_Callback
 * @brief Handle contactor feedback interrupts.
 *
 * @details This function is called from the external interrupt handler when a
 *          contactor feedback signal changes state. It reads the corresponding
 *          GPIO pin and updates the associated feedback counter by incrementing
 *          on a rising edge and decrementing on a falling edge. These counters
 *          are later used by the precharge logic to validate the real contactor
 *          states during the precharge sequence.
 *
 * Parameters:
 *
 * @param [in]  GPIO_Pin                GPIO pin that triggered the interrupt
 *
 * @return None
 *
 *******************************************************************************
 */
void Feedback_EXTI_Callback(uint16_t GPIO_Pin) {
	GPIO_PinState pin_state;

	switch (GPIO_Pin) {
	case GPIO_PIN_12:   // PC12 = MCU_DISCH_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
		fb_dsch += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDebug("MCU_DISCH_FB triggered\r\n");
		break;

	case GPIO_PIN_11:   // PC11 = MCU_AIR-_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11);
		fb_air_neg += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDebug("MCU_AIR-_FB triggered\r\n");
		break;

	case GPIO_PIN_10:   // PC10 = MCU_AIR+_FB
		pin_state = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_10);
		fb_air_pos += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDebug("MCU_AIR+_FB triggered\r\n");
		break;

	case GPIO_PIN_15:   // PA15 = MCU_PRE_FB
		pin_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
		fb_pre += (pin_state == GPIO_PIN_SET) ? 1 : -1;
		printfDebug("MCU_PRE_FB triggered\r\n");
		break;

	default:
		break;
	}
}

/**
 *******************************************************************************
 * Function: PreCharge_CAN_Rx
 * @brief Process CAN messages related to the precharge sequence.
 *
 * @details This function is called when a CAN message is received and checks
 *          whether the message corresponds to a precharge control request.
 *          It decodes the received CAN frame and evaluates the precharge
 *          request signal. If a valid start request is received while the
 *          system is in the KILL state, the precharge state machine is
 *          triggered. If the request is cleared, the system is forced back
 *          into the KILL state.
 *
 * Parameters:
 *
 * @param [in]  *hdr                     Pointer to the CAN RX header
 *
 * @param [in]  *data                    Pointer to the received CAN data buffer
 *
 * @return None
 *
 *******************************************************************************
 */
void PreCharge_CAN_Rx(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data) {
	//lastTime = HAL_GetTick();

	uint32_t id = hdr->StdId;
	uint32_t dlc = hdr->DLC;
	bool start_initiated = false;

	uint32_t now = HAL_GetTick();

	switch (id) {

	case AMS_START_PRE_CHARGE_FRAME_ID:
		struct ams_start_pre_charge_t prechargeInit;
		ams_start_pre_charge_unpack(&prechargeInit, data, dlc);

		/*if (prechargeInit.precharge_request > 0 && start_initiated == false && state == KILL) {
		 state = RX_CAN;
		 start_initiated = true;
		 }*/
		if (prechargeInit.precharge_request > 0) {
			//int32_t, this expression is always true. Unsigned subtraction is never negative, so >= 0 does nothing
			if ((state == KILL) && ((int32_t) (now - canRxIgnoreUntil) >= 0)) {
				state = RX_CAN;
				start_initiated = true;
				canRxIgnoreUntil = now + PRECHARGE_CAN_LOCKOUT_MS;
			}
		} else if (prechargeInit.precharge_request == 0) {
			state = KILL;
			canRxIgnoreUntil = 0;
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
