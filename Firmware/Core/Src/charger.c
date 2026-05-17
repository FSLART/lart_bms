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
#include "eveurope_charger.h"
#include "dbc/powertrain_t26.h"

#include "gpio_expander.h"

extern CAN_HandleTypeDef hcan2;

#define CHARGER_COMMAND_PERIOD_MS 1000

#define CHARGER_STATUS_TIMEOUT_MS  3000

//set values
#define CHARGER_MAX_VOLTAGE_V 504
#define CHARGER_MAX_CURRENT_A 6

uint8_t charger_is_requested = 0;
uint16_t requested_voltage_raw = 0;
uint16_t requested_current_raw = 0;
int32_t last_charger_command_ms = 0;

struct eveurope_charger_charger_status_t last_charger_status;
uint8_t charger_status_valid = 0;
uint32_t last_charger_status_ms = 0;

void Charger_CAN_Init(void) {
	CAN_RegisterRxCallback(Charger_CAN_Rx);

	requested_voltage_raw = eveurope_charger_bms_charging_request_max_charging_voltage_encode(CHARGER_MAX_VOLTAGE_V);
	requested_current_raw = eveurope_charger_bms_charging_request_max_charging_current_encode(CHARGER_MAX_CURRENT_A);
}

void Charger_CAN_Rx(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {
	if ((hdr == 0) || (data == 0)) {
		return;
	}

	// Start charging request from the  VCU
	if ((hdr->IDE == CAN_ID_STD) && (hdr->StdId == POWERTRAIN_T26_START_CHARGING_FRAME_ID)) {
		struct powertrain_t26_start_charging_t rx;

		if (powertrain_t26_start_charging_unpack(&rx, data, hdr->DLC) != 0) {
			return;
		}

		if (rx.charging_request == POWERTRAIN_T26_START_CHARGING_CHARGING_REQUEST_CHARGING_ON_CHOICE) {

			charger_is_requested = 1;

			if (AMS_State != IDLE || AMS_State == CHARGING) {
				return;
			}

			AMS_State = CHARGING;

			MCP23017_LED(LED_CHARGING_STATUS, ON);

			printfDebug("Charging request ON -> AMS_State = CHARGING\r\n");

		} else if (rx.charging_request == POWERTRAIN_T26_START_CHARGING_CHARGING_REQUEST_CHARGING_OFF_CHOICE) {

			charger_is_requested = 0;

			if (AMS_State != CHARGING) {
				return;
			}

			Charger_Stop();

			AMS_State = IDLE;
			printfDebug("Charging request OFF -> AMS_State = IDLE\r\n");
		}

		return;
	}

	// Status from the EV Europe
	if ((hdr->IDE == CAN_ID_EXT) && (hdr->ExtId == EVEUROPE_CHARGER_CHARGER_STATUS_FRAME_ID)) {

		if (eveurope_charger_charger_status_unpack(&last_charger_status, data, hdr->DLC) == 0) {

			charger_status_valid = 1;
			last_charger_status_ms = HAL_GetTick();
		}
	}
}

void Charger_Update(void) {
	uint32_t now = HAL_GetTick();

	if (charger_is_requested == 0) {
		return;
	}

	//TODO; check isa current
	if (!Charger_IsRequestedCurrentOK()) {
		Charger_Stop();
		return;
	}

	//loop message for charger
	if ((now - last_charger_command_ms) >= CHARGER_COMMAND_PERIOD_MS) {
		Charger_SendRequest(true);
		last_charger_command_ms = now;
	}
}

void Charger_Stop(void) {
	Charger_SendRequest(false);
	last_charger_command_ms = HAL_GetTick();

	MCP23017_LED(LED_CHARGING_STATUS, OFF);

	AMS_State = IDLE;
}

void Charger_SendRequest(bool enable) {
	uint8_t data[8];
	int len;

	struct eveurope_charger_bms_charging_request_t msg;
	eveurope_charger_bms_charging_request_init(&msg);

	msg.max_charging_voltage = requested_voltage_raw;
	msg.max_charging_current = requested_current_raw;

	// 0 - charge, 1 - no charger
	if (enable != false) {
		msg.control = 0;
	} else {
		msg.control = 1;
	}

	len = eveurope_charger_bms_charging_request_pack(data, &msg, sizeof(data));
	if (len < 0) {
		return;
	}

	CAN_TX_Add_Extended_To_Queue(&hcan2, EVEUROPE_CHARGER_BMS_CHARGING_REQUEST_FRAME_ID, (uint8_t) len, data);
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

