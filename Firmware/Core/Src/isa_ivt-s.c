#include "isa_ivt-s.h"
#include "fault_manager.h"
#include "main.h"
#include "brain.h"
#include "uartDMA.h"
#include "can.h"
#include "soc.h"
#include "dbc/powertrain_t26.h"

#define SAFETY_PACK_OV_mV   599000
#define SAFETY_PACK_UV_mV   2.8 * 144 * 1000
#define SAFETY_PACK_OC_mA   80000
#define SAFETY_PACK_UC_mA   (-80000)
#define SAFETY_ISA_OT_dC    600 // 0.1 °C/LSB,

extern CAN_HandleTypeDef hcan1;	// CAN module for PT bus

//structure all cute
typedef struct {

	// realtime values
	int32_t vBatt;
	int32_t iBatt;
	int32_t power;
	int32_t temp;
	int32_t SOH;
	int32_t SOC;
	int32_t coulombs_As;

	//configs
	float conf_CANbit;
	float conf_maxTemp;
	float conf_minTemp;
	float conf_minCurrent;
	float conf_maxCurrent;
	float conf_maxU1;
	float conf_minU1;
	float conf_maxU2;
	float conf_minU2;
	float conf_maxU3;
	float conf_minU3;

	//faults
	IVT_faults_t faults;

} ivt_t;

ivt_t ivt;

uint8_t IVT_commandReceivedFlag = 0;
uint8_t IVT_configComplete = 0;
bool commsCheck = false;
uint32_t lastTime = 0;

/**
 * @brief  Configures FDCAN filters for IVT-S sensor data and starts the controller.
 * @details Sets up individual mask filters to accept only the specific IVT-S CAN IDs
 *          for voltage (U1, U2, U3), current, temperature, power, and command acknowledgments.
 *          Routes all accepted messages to RX FIFO0, enables the RX FIFO0 interrupt, and
 *          starts the FDCAN peripheral.
 * @param  hfdcan  Pointer to the initialized FDCAN handle (e.g., &hcan1).
 * @retval None
 */
void IVT_CAN_Setup(CAN_HandleTypeDef *hcan) {

	// register IVT listener for all CAN messages
	CAN_RegisterRxCallback(IVT_CAN_OnMessage);

	/*CAN_FilterTypeDef f = { 0 };
	 f.FilterBank = 0;
	 f.FilterMode = CAN_FILTERMODE_IDMASK;
	 f.FilterScale = CAN_FILTERSCALE_32BIT;
	 f.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	 f.FilterActivation = CAN_FILTER_ENABLE;
	 f.SlaveStartFilterBank = 14;

	 //match every ID
	 f.FilterIdHigh = 0x0000;
	 f.FilterIdLow = 0x0000;
	 f.FilterMaskIdHigh = 0x0000;
	 f.FilterMaskIdLow = 0x0000;

	 if (HAL_CAN_ConfigFilter(hcan, &f) != HAL_OK) {
	 printfConsole("Error setting wildcard filter\r\n");
	 }

	 if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
	 printfConsole("Error activating CAN RX FIFO0 notification\r\n");
	 }*/
}

// config the ivts sensor
void IVT_CAN_Config(void) {

	const uint8_t *cmds[] = { IVT_STOP_CMD, IVT_CONFIG_CURRENT_CMD, IVT_CONFIG_U1_CMD, IVT_CONFIG_U2_CMD, IVT_CONFIG_U3_CMD, IVT_CONFIG_T_CMD, IVT_CONFIG_W_CMD, IVT_CONFIG_AS_CMD, IVT_CONFIG_WH_CMD, IVT_STORE_CMD, IVT_START_CMD, };

	for (uint8_t i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); i++) {
		if (CAN_TX_Add_To_Queue(&hcan1, IVT_COMMAND_CANID, 8, cmds[i]) != HAL_OK) {
			printfConsole("Fudeu CAN - IVT cmd %u\r\n", i);
		}
	}

	commsCheck = true;
	IVT_commandReceivedFlag = 0;

	// We don't read these back yet; mark as unknown
	ivt.conf_CANbit = 1;
	ivt.conf_maxTemp = -1;
	ivt.conf_minTemp = -1;
	ivt.conf_minCurrent = -1;
	ivt.conf_maxCurrent = -1;
	ivt.conf_maxU1 = -1;
	ivt.conf_minU1 = -1;
	ivt.conf_maxU2 = -1;
	ivt.conf_minU2 = -1;
	ivt.conf_maxU3 = -1;
	ivt.conf_minU3 = -1;

	printfConsole("IVT-S config success\r\n");

}

void IVT_CAN_OnMessage(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {

	lastTime = HAL_GetTick();

	switch (hdr->StdId) {

	case IVT_RESPONSE_CANID: {
		struct powertrain_t26_ivt_msg_response_t resp;
		if (powertrain_t26_ivt_msg_response_unpack(&resp, data, 8) != 0) {
			break;
		}

		// Start/stop ack: 0xB4 with byte2 == 0x01
		if (resp.ivt_id_response == 0xB4 && data[2] == 0x01) {
			IVT_commandReceivedFlag = 1;
			break;
		}

		// Generic config-command acks
		if ((resp.ivt_id_response >= 0xA0 && resp.ivt_id_response <= 0xA5) || resp.ivt_id_response == 0xB0 || resp.ivt_id_response == 0xB2) {
			IVT_commandReceivedFlag = 1;
			break;
		}

		// Error reports
		if (resp.ivt_id_response == 0x80) {
			IVT_PROCESS_MEASURERRORS(&resp);
		} else if (resp.ivt_id_response == 0x81) {
			IVT_PROCESS_SYSERRORS(&resp);
		}
		break;
	}

	case IVT_RESULTI_CANID: {
		struct powertrain_t26_ivt_msg_result_i_t r;
		if (powertrain_t26_ivt_msg_result_i_unpack(&r, data, 8) == 0) {
			ivt.iBatt = r.ivt_result_i;     // mA
		}

		Check_PackVoltage_and_Current();

		break;
	}

	case IVT_RESULTU1_CANID: {
		struct powertrain_t26_ivt_msg_result_u1_t r;
		if (powertrain_t26_ivt_msg_result_u1_unpack(&r, data, 8) == 0) {
			ivt.vBatt = r.ivt_result_u1;    // mV
		}

		Check_PackVoltage_and_Current();

		break;
	}

	case IVT_RESULTT_CANID: {
		struct powertrain_t26_ivt_msg_result_t_t r;
		if (powertrain_t26_ivt_msg_result_t_unpack(&r, data, 8) == 0) {
			ivt.temp = r.ivt_result_t;      // 0.1 °C raw
		}

		Check_PackVoltage_and_Current();

		break;
	}

	case IVT_RESULTW_CANID: {
		struct powertrain_t26_ivt_msg_result_w_t r;
		if (powertrain_t26_ivt_msg_result_w_unpack(&r, data, 8) == 0) {
			ivt.power = r.ivt_result_w;     // W
		}

		break;
	}

	case IVT_RESULTAS_CANID: {

		struct powertrain_t26_ivt_msg_result_as_t r;

		if (powertrain_t26_ivt_msg_result_as_unpack(&r, data, 8) == 0) {

			ivt.coulombs_As = r.ivt_result_as;

			//UPDATE SOCOC
			SOC_NotifyAsReading(ivt.coulombs_As);

			ivt.SOC = (int32_t) (SOC_GetPercent() * 100);
		}
		break;
	}

	default:
		break;
	}
}

void IVT_FAULT_CHECK(void) {

	if ((HAL_GetTick() - lastTime) > 1000) {
		commsCheck = false;
	} else {
		commsCheck = true;
	}

	if (CAN_TX_Add_To_Queue(&hcan1, IVT_COMMAND_CANID, 8, IVT_SYSERROR_CMD) != HAL_OK) {
		printfConsole("Error sending SYSERROR_CMD\r\n");
	}
}

void IVT_PROCESS_SYSERRORS(const struct powertrain_t26_ivt_msg_response_t *resp) {

	// We only handle the bitmask response (item index 0)
	if (resp->_81_resp_system_error_item != 0x00) {
		printfConsole("Error getting SYSERRORS\r\n");
		return;
	}

	uint16_t mask = resp->_81_resp_system_error_count_mask;

	// Print the mask for debugging
	printfConsole("		SYSERRORS: 0x%04X\r\n", mask);

	// Reset flags this function owns before re-evaluating
	ivt.faults.CAN = false;
	ivt.faults.power = false;
	ivt.faults.current = false;
	ivt.faults.vRef = false;
	ivt.faults.temp = false;

	// CAN-related errors
	if (mask & (1 << 3))
		ivt.faults.CAN = true;   // CAN Tx data
	if (!commsCheck)
		ivt.faults.CAN = true;   // initial comms

	// Temperature
	if (mask & (1 << 4))
		ivt.faults.temp = true;  // overtemp
	if (mask & (1 << 5))
		ivt.faults.temp = true;  // undertemp

	// Power
	if (mask & (1 << 6))
		ivt.faults.power = true; // power failure

	// Once we have the system errors, ask for the measurement errors too
	if (CAN_TX_Add_To_Queue(&hcan1, IVT_COMMAND_CANID, 8, IVT_MEASURERROR_CMD)) {
		printfConsole("Error sending IVT_MEASURERROR_CMD\r\n");
	}
}

void IVT_PROCESS_MEASURERRORS(const struct powertrain_t26_ivt_msg_response_t *resp) {

	/*
	 * *   bit 0..4 : ADC errors (interrupt, over/underflow on ch1 and ch2)
	 *   bit 5    : Vref implausibility
	 *   bit 6    : I1-I2 delta
	 *   bit 8    : I open circuit
	 *   bit 9    : U1 open circuit
	 *   bit 10   : U2 open circuit
	 *   bit 11   : U3 open circuit
	 *   bit 12   : NTC high open circuit
	 *   bit 13   : NTC low open circuit
	 */

	if (resp->_80_resp_meas_error_item != 0x00) {
		printfConsole("Error getting MEASURERRORS\r\n");
		return;
	}

	uint16_t mask = resp->_80_resp_meas_error_count_mask;

	printfConsole("		MEASURERRORS: 0x%04X\r\n", mask);

	// Reset flags this function owns
	ivt.faults.U1_oc = false;
	ivt.faults.U2_oc = false;
	ivt.faults.U3_oc = false;
	ivt.faults.current_oc = false;
	ivt.faults.ntc_l_oc = false;
	ivt.faults.ntc_h_oc = false;
	ivt.faults.adc = false;

	// ADC errors
	if (mask & 0x001F)
		ivt.faults.adc = true;

	if (mask & (1 << 5))
		ivt.faults.vRef = true;  // Vref
	if (mask & (1 << 6))
		ivt.faults.current = true;  // I1-I2

	// Open circuits
	if (mask & (1 << 8))
		ivt.faults.current_oc = true;
	if (mask & (1 << 9))
		ivt.faults.U1_oc = true;
	if (mask & (1 << 10))
		ivt.faults.U2_oc = true;
	if (mask & (1 << 11))
		ivt.faults.U3_oc = true;
	if (mask & (1 << 12))
		ivt.faults.ntc_h_oc = true;
	if (mask & (1 << 13))
		ivt.faults.ntc_l_oc = true;
}

void IVT_SET_BITRATE(void) {
	uint32_t startTime;

// Configure IVT. Take response delays into account, if response time is too long --> raise CAN error
	if (CAN_TX_Add_To_Queue(&hcan1, IVT_COMMAND_CANID, 8, IVT_STOP_CMD) != HAL_OK) { // Stop measurement to configure results

		printfConsole("Fudeu CAN - IVT_STOP_CMD\n\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {
			commsCheck = false; // send UI Can error
			printfConsole("Fudeu CAN - stop\n\n");
			IVT_SET_BITRATE();
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (CAN_TX_Add_To_Queue(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_CANRATE_CMD) != HAL_OK) { // Configure current result command

		printfConsole("Fudeu CAN - IVT_CONFIG_CANRATE_CMD\n\n");
	}
	IVT_commandReceivedFlag = 0; // Reset flag

}

//apenas serve pra dar pull à info
int32_t IVT_GetCurrent_mA(void) {
	return ivt.iBatt;     // mA
}

int32_t IVT_GetPackVoltage_mV(void) {
	return ivt.vBatt;     // mV (U1)
}

int32_t IVT_GetTemperature_dC(void) {
	return ivt.temp;     // x0.1 ºC
}

void Check_PackVoltage_and_Current(void) {
	int32_t pack_mV = IVT_GetPackVoltage_mV();
	int32_t pack_mA = IVT_GetCurrent_mA();
	int32_t ISA_dC = IVT_GetTemperature_dC();

	if (pack_mV > 1000) {

		if (pack_mV > SAFETY_PACK_OV_mV) {
			RAISE_ERROR(FAULT_PACK_VOLTAGE_MISMATCH, .measured_value = (float ) pack_mV, .threshold_value = (float) SAFETY_PACK_OV_mV);
			//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
		}

		if (pack_mV < SAFETY_PACK_UV_mV) {
			RAISE_ERROR(FAULT_PACK_VOLTAGE_MISMATCH, .measured_value = (float ) pack_mV, .threshold_value = (float) SAFETY_PACK_UV_mV);
			//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
		}

	}

	if (pack_mA > SAFETY_PACK_OC_mA) {
		//TODO: não há código de overcurrent dedicado, uso este só para registar
		RAISE_ERROR(FAULT_CURRENT_SENSOR_ERROR, .measured_value = (float ) pack_mA, .threshold_value = (float) SAFETY_PACK_OC_mA);
		//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
	}

	if (pack_mA < SAFETY_PACK_UC_mA) {
		//TODO: não há código de undercurrent dedicado, uso este só para registar
		RAISE_ERROR(FAULT_CURRENT_SENSOR_ERROR, .measured_value = (float ) pack_mA, .threshold_value = (float) SAFETY_PACK_UC_mA);
		//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
	}

	if (ISA_dC > SAFETY_ISA_OT_dC) {
		RAISE_ERROR(FAULT_OVERTEMPERATURE, .measured_value = (float) ISA_dC / 10.0f, .threshold_value = (float) SAFETY_ISA_OT_dC / 10.0f);
		//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
	}
}

void send_ivt_ui(void) {

	printfUI("[{\"ivt\":0,");

	// real-time values
	printfUI("\"vBatt\":%ld,", ivt.vBatt);
	printfUI("\"iBatt\":%ld,", ivt.iBatt);
	printfUI("\"power\":%ld,", ivt.power);
	printfUI("\"temp\":%ld,", ivt.temp);
	printfUI("\"SOH\":%ld,", ivt.SOH);
	printfUI("\"SOC\":%ld,", ivt.SOC);

	// config readbacks
	printfUI("\"conf_CANbit\":%.0f,", ivt.conf_CANbit);
	printfUI("\"conf_maxTemp\":%.2f,", ivt.conf_maxTemp);
	printfUI("\"conf_minTemp\":%.2f,", ivt.conf_minTemp);
	printfUI("\"conf_minCurrent\":%.2f,", ivt.conf_minCurrent);
	printfUI("\"conf_maxCurrent\":%.2f,", ivt.conf_maxCurrent);
	printfUI("\"conf_maxU1\":%.2f,", ivt.conf_maxU1);
	printfUI("\"conf_minU1\":%.2f,", ivt.conf_minU1);
	printfUI("\"conf_maxU2\":%.2f,", ivt.conf_maxU2);
	printfUI("\"conf_minU2\":%.2f,", ivt.conf_minU2);
	printfUI("\"conf_maxU3\":%.2f,", ivt.conf_maxU3);
	printfUI("\"conf_minU3\":%.2f,", ivt.conf_minU3);

	// fault flags
	printfUI("\"faults\":{");
	printfUI("\"CAN\":%s,", ivt.faults.CAN ? "true" : "false");
	printfUI("\"power\":%s,", ivt.faults.power ? "true" : "false");
	printfUI("\"current\":%s,", ivt.faults.current ? "true" : "false");
	printfUI("\"vRef\":%s,", ivt.faults.vRef ? "true" : "false");
	printfUI("\"U3_oc\":%s,", ivt.faults.U3_oc ? "true" : "false");
	printfUI("\"U2_oc\":%s,", ivt.faults.U2_oc ? "true" : "false");
	printfUI("\"U1_oc\":%s,", ivt.faults.U1_oc ? "true" : "false");
	printfUI("\"current_oc\":%s,", ivt.faults.current_oc ? "true" : "false");
	printfUI("\"ntc_l_oc\":%s,", ivt.faults.ntc_l_oc ? "true" : "false");
	printfUI("\"ntc_h_oc\":%s,", ivt.faults.ntc_h_oc ? "true" : "false");
	printfUI("\"adc\":%s,", ivt.faults.adc ? "true" : "false");
	printfUI("\"temp\":%s", ivt.faults.temp ? "true" : "false");
	printfUI("}");

	printfUI("}]\n");
}
