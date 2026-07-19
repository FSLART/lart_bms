/*
 * charger.c
 *
 *  Created on: May 10, 2026
 *      Author: jpser
 */

#include "charger.h"

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "brain.h"
#include "can.h"
#include "uartDMA.h"
#include "handcart_t26.h"

#include "gpio_expander.h"

#include "precharge.h"
#include "adbms_to_CAN.h"
#include "isa_ivt-s.h"

extern CAN_HandleTypeDef hcan2;

#define CHARGER_COMMAND_PERIOD_MS 1000

#define CHARGER_STATUS_TIMEOUT_MS  3000

//set values
#define CHARGER_MAX_VOLTAGE_V 504
#define CHARGER_MAX_CURRENT_A 6

//charging protection limits
#define CHARGER_CELL_TARGET_MV      4200   // stop charging when highest cell gets here
#define CHARGER_MAX_TEMP_cC         6000   // 60.00 C, g_pack_tmax_cC is in centi-degrees
#define CHARGER_MAX_CURRENT_CUT_mA  8000   // 8 A from the ISA, cut charging above this
#define CHARGER_STOP_SETTLE_MS      1000   // wait for current to die before opening contactors

//internal charging sequence, only runs while the VCU keeps the request on
typedef enum {
	CHARGER_ST_WAIT_HV = 0,  // wait for the precharge to reach HV_ON
	CHARGER_ST_CHARGING,     // request charge and watch voltage/temp/current
	CHARGER_ST_STOPPING,     // stop sent, waiting settle before opening contactors
	CHARGER_ST_DONE          // latched, nothing more until the VCU toggles the request
} ChargerChargeState_t;

ChargerChargeState_t charge_state = CHARGER_ST_WAIT_HV;
uint32_t stop_settle_start_ms = 0;

uint8_t charger_is_requested = 0;
uint16_t requested_voltage_raw = 0;
uint16_t requested_current_raw = 0;
int32_t last_charger_command_ms = 0;

struct handcart_t26_charger_status_t last_charger_status;
uint8_t charger_status_valid = 0;
uint32_t last_charger_status_ms = 0;

void Charger_CAN_Init(void) {

	// tudo o que e carregamento vive no CAN2 (250k): o pedido de START_CHARGING
	// do handcart e o dialogo com o carregador chegam ambos por aqui
	CAN2_RegisterRxCallback(Charger_CAN_Requests_RX);
	CAN2_RegisterRxCallback(Charger_CAN_Comms_RX);

	requested_voltage_raw = handcart_t26_bms_charging_request_max_charging_voltage_encode(CHARGER_MAX_VOLTAGE_V);
	requested_current_raw = handcart_t26_bms_charging_request_max_charging_current_encode(CHARGER_MAX_CURRENT_A);
}

void Charger_CAN_Comms_RX(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {
	if ((hdr == 0) || (data == 0)) {
		return;
	}

	// Status from the EV Europe
	if ((hdr->IDE == CAN_ID_EXT) && (hdr->ExtId == HANDCART_T26_CHARGER_STATUS_FRAME_ID)) {

		if (handcart_t26_charger_status_unpack(&last_charger_status, data, hdr->DLC) == 0) {

			charger_status_valid = 1;
			last_charger_status_ms = HAL_GetTick();
		}
	}
}

void Charger_CAN_Requests_RX(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {
	if ((hdr == 0) || (data == 0)) {
		return;
	}

	// Charge order from the Handcart switch (0x084, sent every 100 ms)
	if ((hdr->IDE == CAN_ID_STD) && (hdr->StdId == HANDCART_T26_HANDCART_SWITCH_FEEDBACK_FRAME_ID)) {
		struct handcart_t26_handcart_switch_feedback_t rx;

		if (handcart_t26_handcart_switch_feedback_unpack(&rx, data, hdr->DLC) != 0) {
			return;
		}

		if (rx.switch_feedback > 0) {

			charger_is_requested = 1;

			if (AMS_State != IDLE || AMS_State == CHARGING) {
				return;
			}

			AMS_State = CHARGING;

			MCP23017_LED(LED_CHARGING_STATUS, ON);

			printfDebug("Handcart switch ON -> AMS_State = CHARGING\r\n");

		} else {

			charger_is_requested = 0;

			if (AMS_State != CHARGING) {
				return;
			}

			Charger_Stop();

			AMS_State = IDLE;
			printfDebug("Handcart switch OFF -> AMS_State = IDLE\r\n");
		}

		return;
	}

	// Status from the EV Europe
	if ((hdr->IDE == CAN_ID_EXT) && (hdr->ExtId == HANDCART_T26_CHARGER_STATUS_FRAME_ID)) {

		if (handcart_t26_charger_status_unpack(&last_charger_status, data, hdr->DLC) == 0) {

			charger_status_valid = 1;
			last_charger_status_ms = HAL_GetTick();
		}
	}
}

void Charger_Update(void) {
	uint32_t now = HAL_GetTick();

	if (charger_is_requested == 0) {
		// Handcart dropped the request, sequence goes back to the start
		charge_state = CHARGER_ST_WAIT_HV;
		return;
	}

	switch (charge_state) {

	case CHARGER_ST_WAIT_HV:
		// precharge is started by the Handcart, we just wait for HV
		if (Precharge_GetState() == HV_ON) {
			printfDebug("Charger: HV_ON confirmed -> start charging\r\n");
			Charger_SendRequest(true);
			last_charger_command_ms = now;
			charge_state = CHARGER_ST_CHARGING;
		}
		break;

	case CHARGER_ST_CHARGING:

		// HV dropped on its own (precharge fault) -> stop asking for charge
		if (Precharge_GetState() != HV_ON) {
			printfDebug("Charger: lost HV_ON -> charging paused\r\n");
			Charger_SendRequest(false);
			charge_state = CHARGER_ST_WAIT_HV;
			break;
		}

		// highest cell reached the target -> normal end of charge
		if (g_pack_vmax_mV >= CHARGER_CELL_TARGET_MV) {
			printfDebug("Charger: cell at %u mV (target %u) -> charge complete\r\n", g_pack_vmax_mV, CHARGER_CELL_TARGET_MV);
			Charger_SendRequest(false);
			stop_settle_start_ms = now;
			charge_state = CHARGER_ST_STOPPING;
			break;
		}

		// pack too hot -> cut everything now
		if (g_pack_tmax_cC >= CHARGER_MAX_TEMP_cC) {
			printfDebug("Charger: pack at %d cC (limit %d) -> charge CUT\r\n", g_pack_tmax_cC, CHARGER_MAX_TEMP_cC);
			Charger_SendRequest(false);
			Precharge_ForceKill();
			charge_state = CHARGER_ST_DONE;
			break;
		}

		// charging current too high (ISA reads charge current as negative)
		int32_t ivt_mA = IVT_GetCurrent_mA();
		if (ivt_mA < 0)
			ivt_mA = -ivt_mA;

		if (ivt_mA > CHARGER_MAX_CURRENT_CUT_mA) {
			printfDebug("Charger: ISA at %ld mA (limit %d) -> charge CUT\r\n", ivt_mA, CHARGER_MAX_CURRENT_CUT_mA);
			Charger_SendRequest(false);
			Precharge_ForceKill();
			charge_state = CHARGER_ST_DONE;
			break;
		}

		// charger went quiet (was talking, nothing for 3s) -> cut
		if (charger_status_valid && !Charger_HasStatus()) {
			printfDebug("Charger: status timeout -> charge CUT\r\n");
			Charger_SendRequest(false);
			Precharge_ForceKill();
			charge_state = CHARGER_ST_DONE;
			break;
		}

		// charger reporting a fault on its side -> cut
		if (Charger_HasStatus() && (last_charger_status.hw_failure || last_charger_status.temp_otp || last_charger_status.input_voltage_fault || last_charger_status.starting_state_fault)) {
			printfDebug("Charger: fault flags hw:%u otp:%u vin:%u start:%u -> charge CUT\r\n", last_charger_status.hw_failure, last_charger_status.temp_otp, last_charger_status.input_voltage_fault, last_charger_status.starting_state_fault);
			Charger_SendRequest(false);
			Precharge_ForceKill();
			charge_state = CHARGER_ST_DONE;
			break;
		}

		//loop message for charger
		if ((now - last_charger_command_ms) >= CHARGER_COMMAND_PERIOD_MS) {
			Charger_SendRequest(true);
			last_charger_command_ms = now;
		}
		break;

	case CHARGER_ST_STOPPING:
		// let the current die down before opening the contactors
		if ((now - stop_settle_start_ms) >= CHARGER_STOP_SETTLE_MS) {
			Precharge_ForceKill();
			printfDebug("Charger: contactors open, charge finished\r\n");
			charge_state = CHARGER_ST_DONE;
		}
		break;

	case CHARGER_ST_DONE:
		// latched: even if the voltage sags below the target we do not
		// restart, only a new request from the Handcart resets the sequence
		break;

	default:
		charge_state = CHARGER_ST_WAIT_HV;
		break;
	}
}

void Charger_Stop(void) {

	Charger_SendRequest(false);
	last_charger_command_ms = HAL_GetTick();

	charge_state = CHARGER_ST_WAIT_HV;

	MCP23017_LED(LED_CHARGING_STATUS, OFF);

	AMS_State = IDLE;
}

void Charger_SendRequest(bool enable) {
	uint8_t data[8];
	int len;

	struct handcart_t26_bms_charging_request_t msg;
	handcart_t26_bms_charging_request_init(&msg);

	msg.max_charging_voltage = requested_voltage_raw;
	msg.max_charging_current = requested_current_raw;

	// 0 - charge, 1 - no charge para carregador
	if (enable != false) {
		msg.control = 0;
	} else {
		msg.control = 1;
	}

	len = handcart_t26_bms_charging_request_pack(data, &msg, sizeof(data));
	if (len < 0) {
		return;
	}

	CAN_TX_Add_Extended_To_Queue(&hcan2, HANDCART_T26_BMS_CHARGING_REQUEST_FRAME_ID, (uint8_t) len, data);
}

bool Charger_IsRequestedCurrentOK(void) {
	//TODO: ASK ISA CURRENT CHECKKKK
	return true;
}

uint8_t Charger_IsRequested(void) {
	return charger_is_requested;
}

uint16_t Charger_GetRequestedCurrentRaw(void) {
	return requested_current_raw;
}

uint8_t Charger_HasStatus(void) {
    uint32_t now = HAL_GetTick();

    if (charger_status_valid == 0) {
        return 0;
    }

    if ((now - last_charger_status_ms) > CHARGER_STATUS_TIMEOUT_MS) {
        return 0;
    }

    return 1;
}

