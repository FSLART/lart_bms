#include "main.h"
#include "IVT_CAN.h"
#include "uartDMA.h"

/* Variables -------------------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;	// CAN module for PT bus
CAN_FilterTypeDef sFilterConfig = { 0 };
CAN_RxHeaderTypeDef RxHeader;
CAN_TxHeaderTypeDef TxHeader;

uint8_t IVT_commandReceivedFlag = 0;
uint8_t IVT_configComplete = 0;
int32_t IVT_Current;
int32_t vDCLink; //U1 of IVT-s
int32_t vBatt;  // U2 of IVT-S
int32_t voltageU3_mV;
int32_t tempIVT;  // Temp from IVT sensor
int32_t IVT_Power; //Wattage from IVT
int8_t IVT_DC_Supply; //VSupply from IVT

//CAN variables
uint8_t RxData[8];

bool commsCheck = false;

uint32_t lastTime = 0;

//structure all cute
typedef struct {

	// RT values
	int32_t vBatt;
	int32_t iBatt;
	int32_t power;
	int32_t temp;
	int32_t SOH;
	int32_t SOC;

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

/**
 * @brief  Configures FDCAN to receive all CAN messages by setting wildcard filters.
 * @details Installs two mask filters—one for standard IDs (0x000–0x7FF) and one for
 *          extended IDs (0x00000000–0x1FFFFFFF)—each with a zero mask so that all IDs
 *          pass through. Routes all messages into RX FIFO 0, enables the new-message
 *          interrupt, and starts the FDCAN peripheral.
 * @param  hfdcan  Pointer to the initialized FDCAN handle (e.g., &hcan1).
 * @retval None
 */
void IVT_CAN_Setup_AllMessages(CAN_HandleTypeDef *hcan) {

	CAN_FilterTypeDef sFilterConfig = { 0 };

	// Use 32-bit filter scale and mask mode with zeros -> accept everything
	sFilterConfig.FilterBank = 0;
	sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
	sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
	sFilterConfig.FilterIdHigh = 0x0000;      // for 11-bit IDs in high word: StdId << 5
	sFilterConfig.FilterIdLow = 0x0000;
	sFilterConfig.FilterMaskIdHigh = 0x0000;  // mask=0 -> match all
	sFilterConfig.FilterMaskIdLow = 0x0000;
	sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	sFilterConfig.FilterActivation = ENABLE;
	sFilterConfig.SlaveStartFilterBank = 14; // if dual CAN, adjust; otherwise leave default

	if (HAL_CAN_ConfigFilter(hcan, &sFilterConfig) != HAL_OK) {
		printf("Error setting wildcard filter\n");
	}

	/* --- 3) Activate the RX FIFO0 “new message” interrupt --- */
	if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
		printf("Error activating RX notification\n");
	}

	/* --- 4) Start the FDCAN controller --- */
	if (HAL_CAN_Start(hcan) != HAL_OK) {
		printf("Error starting CAN\n");
	}
}

/**
 * @brief  Configures FDCAN filters for IVT-S sensor data and starts the controller.
 * @details Sets up individual mask filters to accept only the specific IVT-S CAN IDs
 *          for voltage (U1, U2, U3), current, temperature, power, and command acknowledgments.
 *          Routes all accepted messages to RX FIFO0, enables the RX FIFO0 interrupt, and
 *          starts the FDCAN peripheral.
 * @param  hfdcan  Pointer to the initialized FDCAN handle (e.g., &hcan1).
 * @retval None
 */
void IVT_CAN_Setup(CAN_HandleTypeDef *hfdcan) {

	// Filter for IVT-S U1 voltage
	CAN_FilterTypeDef sFilterConfig = { 0 };

	sFilterConfig.FilterBank = 0;                              // choose an unused filter bank
	sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;         // mask mode (use mask for exact match)
	sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;        // 32-bit scale
	sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;    // route to FIFO0
	sFilterConfig.FilterActivation = ENABLE;

	// Put the 11-bit standard ID into the high half-word (shift left by 5)
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTU1_CANID << 5) & 0xFFFF);
	sFilterConfig.FilterIdLow = 0x0000;

	// Mask: set the 11 ID bits to 1 so we require an exact StdId match
	// (0x07FF << 5) == 0xFFE0
	sFilterConfig.FilterMaskIdHigh = (uint16_t) ((0x07FFU << 5) & 0xFFFF);
	sFilterConfig.FilterMaskIdLow = 0x0000;

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for U1 (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for IVT-S U2 voltage data
	sFilterConfig.FilterBank = 1;                          // use filter bank 1
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTU2_CANID << 5) & 0xFFFF);

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for U2 (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for IVT-S U3 voltage data
	sFilterConfig.FilterBank = 2;                          // use filter bank 2
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTU3_CANID << 5) & 0xFFFF); // IVT-S U3 voltage CAN ID

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for U3 (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for IVT-S Current data
	sFilterConfig.FilterBank = 3;
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTI_CANID << 5) & 0xFFFF); // IVT-S U3 voltage CAN ID

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for I (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for IVT-S Temperature data
	sFilterConfig.FilterBank = 4;
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTT_CANID << 5) & 0xFFFF); // IVT-S U3 voltage CAN ID

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for T (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for IVT-S Power data
	sFilterConfig.FilterBank = 5;
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESULTW_CANID << 5) & 0xFFFF); // IVT-S U3 voltage CAN ID

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for W (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	// Filter for command acknowledgement
	sFilterConfig.FilterBank = 6;
	sFilterConfig.FilterIdHigh = (uint16_t) ((IVT_RESPONSE_CANID << 5) & 0xFFFF); // IVT-S U3 voltage CAN ID

	if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
		printf("Error configuring CAN filter for ACK (bank %lu)\r\n", (unsigned long) sFilterConfig.FilterBank);
	}

	/* Enable "message pending" notification for RX FIFO 0 */
	if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
		printf("Error activating CAN RX FIFO0 notification\r\n");
	}

	/* Start CAN peripheral */
	if (HAL_CAN_Start(&hcan1) != HAL_OK) {
		printf("Error starting CAN peripheral\r\n");
	}

}

/**
 * @brief Sends a CAN message via the FDCAN TX FIFO.
 * @param hfdcan: Pointer to the FDCAN handle (e.g., &hcan1).
 * @param canID: CAN ID of the message (standard 11-bit).
 * @param dataLength: Length of the message data in bytes (0 to 8 for Classic CAN).
 * @param data: Pointer to the message data array (up to 8 bytes for Classic CAN).
 * @retval HAL_StatusTypeDef: Returns HAL_OK if successful, otherwise HAL_ERROR.
 */
HAL_StatusTypeDef IVT_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData) {
	CAN_TxHeaderTypeDef TxH;
	uint32_t txMailbox;

	if (dataLength > 8)
		return HAL_ERROR;

	TxH.StdId = (uint32_t) canID & 0x7FF;
	TxH.ExtId = 0;
	TxH.IDE = CAN_ID_STD;
	TxH.RTR = CAN_RTR_DATA;
	TxH.DLC = (uint8_t) dataLength;
	TxH.TransmitGlobalTime = DISABLE;

	if (HAL_CAN_AddTxMessage(hcan, &TxH, (uint8_t*) TxData, &txMailbox) != HAL_OK) {
		return HAL_ERROR;
	}
	return HAL_OK;
}

/**
 * @brief  Configures the IVT-S sensor by sending a sequence of CAN commands.
 *         Each command must be acknowledged within 2 ms or a retry/failure is triggered.
 * @note   This routine blocks while waiting for each command’s acknowledgment flag:
 *         IVT_commandReceivedFlag must be set before proceeding to the next step.
 * @retval None
 */
void IVT_CAN_Config(void) {

	uint32_t startTime;

	// Configure IVT. Take response delays into account, if response time is too long --> raise CAN error
	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, ~8, IVT_STOP_CMD) == HAL_OK) { // Stop measurement to configure results

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {
			commsCheck = false; // send UI Can error
			printf("Fudeu CAN \n\n");
			IVT_CAN_Config();
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag
	commsCheck = true; // clear UI Can error

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_CURRENT_CMD) == HAL_OK) { // Configure current result command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {
			printf("Fudeu CAN \n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_U1_CMD) == HAL_OK) { // Configure voltage U1 command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN \n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_U2_CMD) == HAL_OK) { // Configure voltage U2 command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN \n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_U3_CMD) == HAL_OK) { // Configure voltage U3 command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN \n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_T_CMD) == HAL_OK) { // Configure voltage U3 command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN - IVT_CONFIG_T_CMD\n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_W_CMD) == HAL_OK) { // Configure voltage U3 command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN - IVT_CONFIG_W_CMD\n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_STORE_CMD) == HAL_OK) { // Store config results command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 1000) {

			printf("Fudeu CAN - IVT_STORE_CMD\n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_RESET_SYSERROR_CMD) == HAL_OK) { // Store config results command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 4000) {

			printf("Fudeu CAN - IVT_RESET_SYSERROR_CMD\n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_RESET_MEASURERROR_CMD) == HAL_OK) { // Store config results command

		printf("Command sent\r\n");
	}
	startTime = HAL_GetTick();
	while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

		if ((HAL_GetTick() - startTime) > 4000) {

			printf("Fudeu CAN - IVT_RESET_MEASURERROR_CMD\n\n");
			return;
		}
	}
	IVT_commandReceivedFlag = 0; // Reset flag

	if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_START_CMD) == HAL_OK) { // Start command

		printf("Command sent\r\n");
	}
	IVT_commandReceivedFlag = 0;

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

	printf("IVT-S config success\r\n");

}

/**
 * @brief  Rx FIFO 0 callback for the IVT CAN Bus.
 * @param  hfdcan pointer to an FDCAN_HandleTypeDef structure that contains
 *         the configuration information for the specified FDCAN.
 * @param  RxFifo0ITs indicates which Rx FIFO 0 interrupts are signaled.
 *         This parameter can be any combination of @arg FDCAN_Rx_Fifo0_Interrupts.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
	CAN_RxHeaderTypeDef RxHeader;
	uint8_t RxData[8];

	if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK) {
		Error_Handler();
	}

	lastTime = HAL_GetTick();

	// Convert RxH + RxBuf into your existing logic:
	uint32_t id = RxHeader.StdId;      // if standard ID
	uint32_t dlc = RxHeader.DLC;

	// Process data based on CAN ID
	switch (id) {
	case IVT_RESPONSE_CANID:
		if (RxData[0] == 0xB4) // Check if command is acknowledged
				{
			if (RxData[1] == 0x01 && RxData[2] == 0x01) // Start command
					{
				IVT_commandReceivedFlag = 1;
			}
			if (RxData[1] == 0x00 && RxData[2] == 0x01) // Stop command
					{
				IVT_commandReceivedFlag = 1;
			}
		}

		// Commands for configs
		if ((RxData[0] == 0xA0) || (RxData[0] == 0xA1) || (RxData[0] == 0xB2) || (RxData[0] == 0xA2) || (RxData[0] == 0xA3) || (RxData[0] == 0xA4) || (RxData[0] == 0xA5) || (RxData[0] == 0xB0)) {
			IVT_commandReceivedFlag = 1;
		}

		// Commands for SYSTEM error feedback
		if (RxData[0] == 0x81) {
			IVT_PROCESS_SYSERRORS(RxData);
		}

		// Commands for Measurment error feedback
		if (RxData[0] == 0x80) {
			IVT_PROCESS_MEASURERRORS(RxData);
		}
		break;

	case IVT_RESULTI_CANID:
		// IVT-S Current data: Data Byte 0 (DB0) = 0x00
		// Current data is stored in bytes 2-5
		IVT_Current = ((RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5]);
		ivt.iBatt = IVT_Current;

		//printf("IVT-S Current Data: %ld mA\r\n", IVT_Current);
		break;

	case IVT_RESULTU1_CANID:
		// IVT-S Voltage data (U1): Data Byte 0 (DB0) = 0x01
		// Voltage U1 data is stored in bytes 2-5
		vBatt = 0;
		vBatt = ((RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5]);
		ivt.vBatt = vBatt;

		//printf("IVT-S Voltage Data (U1): %ld mV\r\n", vDCLink);
		break;

	case IVT_RESULTU2_CANID:
		// IVT-S Voltage data (U2): Data Byte 0 (DB0) = 0x02
		// Voltage U2 data is stored in bytes 2-5
		//vBatt = (RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5];
//
//			  // If the state is Precharge,
//			  if ( currentState == PreCharge_State
//				   && (prechargeTriggerOK < 5))
//			  {
//				  prechargeTriggerOK++;
//			  }
		//printf("IVT-S Voltage Data (U2): %ld mV\r\n", vBatt);
		break;

	case IVT_RESULTU3_CANID:
		// IVT-S Voltage data (U3): Data Byte 0 (DB0) = 0x03
		// Voltage U3 data is stored in bytes 2-5
		//voltageU3_mV = (RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5];
		//printf("IVT-S Voltage Data (U3): %ld mV\r\n", voltageU3_mV);
		//vBatt = (RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5];

		// If the state is Precharge,
		//if (currentState == PreCharge_State && (prechargeTriggerOK < 5)) {
		// prechargeTriggerOK++;
		//}
		//break;*/

	case IVT_RESULTT_CANID:
		// IVT-S Temperature data: Data Byte 0 (DB0) = 0x01
		// Temperature data is stored in bytes 2-5
		//tempIVT = ((RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5]) - 273.15;
		tempIVT = ((RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5]);
		ivt.temp = tempIVT;
		//printf("IVT-S Temperature Data: %ld .C\r\n", tempIVT);
		break;

	case IVT_RESULTW_CANID:
		// IVT-S Power data: Data Byte 0 (DB0) = 0x01
		// Power data is stored in bytes 2-5
		IVT_Power = ((RxData[2] << 24) | (RxData[3] << 16) | (RxData[4] << 8) | RxData[5]);
		ivt.power = IVT_Power;
		//printf("IVT-S Power Data: %ld W\r\n", IVT_Power);
		break;

	default:
		// Unexpected CAN IDs get ignored

		// Print unknown ID and DLC
		//uint32_t id = RxHeader.Identifier;
		//uint32_t dlc = RxHeader.DataLength;  // for classical CAN, 0…8 = number of bytes
		printf("Unknown CAN ID 0x%03lX, DLC=%lu, Data:", id, dlc);

		// Dump each byte in hex
		for (uint32_t i = 0; i < dlc; i++) {
			printf(" %02X", RxData[i]);
		}
		printf("\r\n");

		break;
	}
//	      printf("\r\n");
}

/**
 * @brief  Initiates a fault check by requesting system error status from the IVT-S sensor.
 * @details Sends the “Get system errors” command (0x41/0x00) over CAN.
 *          An optional measurement-error request (IVT_MEASURERROR_CMD) is commented out.
 * @note   The response will be handled in HAL_FDCAN_RxFifo0Callback().
 * @retval None
 */
void IVT_FAULT_CHECK(void) {

// ez can error check
if ((HAL_GetTick() - lastTime) > 1000) {
	commsCheck = false;
} else {
	commsCheck = true;
}

if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_SYSERROR_CMD) != HAL_OK) {
	printf("Error sending SYSERROR_CMD\r\n");
}

/*if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_MEASURERROR_CMD) != HAL_OK) {
 printf("Error sending IVT_MEASURERROR_CMD\r\n");
 }*/
}

/**
 * @brief  Processes the system-error bitmask and updates ivt.faults accordingly.
 * @param  RxData  The 8-byte payload from the IVT_RESPONSE_CANID frame.
 *                 RxData[1] == 0x00 indicates the bitmask sub-index.
 */
void IVT_PROCESS_SYSERRORS(uint8_t *RxData) {
if (RxData[1] != 0x00) {
	printf("Error getting SYSERRORS\r\n");
	return;  // not the bitmask response
}

uint8_t low = RxData[2];  // bits 0–7
uint8_t high = RxData[3];  // bits 8–15

printf("		SYSERRORS low:  ");
for (int i = 7; i >= 0; i--) {
	// Test bit i and print '1' or '0'
	printf("%c", (low & (1 << i)) ? '1' : '0');
}
printf("\r\n");

printf("		SYSERRORS high: ");
for (int i = 7; i >= 0; i--) {
	// Test bit i and print '1' or '0'
	printf("%c", (high & (1 << i)) ? '1' : '0');
}
printf("\r\n");

// Clear all system-error flags before updating
ivt.faults.CAN = false;
ivt.faults.power = false;
ivt.faults.current = false;
ivt.faults.vRef = false;
ivt.faults.temp = false;

// Map bits 0..3 → CAN errors
if (low & (1 << 0))
	//ivt.faults.CAN = true;  // Error Code CRC
	if (low & (1 << 1))
		//ivt.faults.CAN = true;  // Error Parameter CRC
		if (low & (1 << 2))
			//ivt.faults.CAN = true;  // Error CAN Rx Data
			if (low & (1 << 3))
				ivt.faults.CAN = true;  // Error CAN Tx Data
if (!commsCheck)
	ivt.faults.CAN = true;  // Error CAN innitial comms

// bit 4 - overtemp
if (low & (1 << 4))
	ivt.faults.temp = true;

// bit 5 - undertemp
if (low & (1 << 5))
	ivt.faults.temp = true;

// bit 6 - power failure
if (low & (1 << 6))
	ivt.faults.power = true;

// bits 8..15: other system flags – map these as you see fit
// bit 8 = system init error - treat as a CAN fault
//if (high & (1 << 0)) ivt.faults.CAN = true;   // Error System Init

// Finally, ask for measurement errors if desired:
if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_MEASURERROR_CMD) != HAL_OK) {
	printf("Error sending IVT_MEASURERROR_CMD\r\n");
}
}

/**
 * @brief  Processes the measurement-error bitmask and updates ivt.faults accordingly.
 * @param  RxData  The 8-byte payload from the IVT_RESPONSE_CANID frame.
 *                 RxData[1] == 0x00 indicates the bitmask sub-index.
 */
void IVT_PROCESS_MEASURERRORS(uint8_t *RxData) {
if (RxData[1] != 0x00) {
	printf("Error getting MEASURERRORS\r\n");
	return;
}

uint8_t low = RxData[2];  // bits 0–7
uint8_t high = RxData[3];  // bits 8–15

printf("		MEASURERRORS low:  ");
for (int i = 7; i >= 0; i--) {
	// Test bit i and print '1' or '0'
	printf("%c", (low & (1 << i)) ? '1' : '0');
}
printf("\r\n");

printf("		MEASURERRORS high: ");
for (int i = 7; i >= 0; i--) {
	// Test bit i and print '1' or '0'
	printf("%c", (high & (1 << i)) ? '1' : '0');
}
printf("\r\n");

// Clear all measurement-error flags before updating
ivt.faults.U1_oc = false;
ivt.faults.U2_oc = false;
ivt.faults.U3_oc = false;
ivt.faults.current_oc = false;
ivt.faults.ntc_l_oc = false;
ivt.faults.ntc_h_oc = false;
ivt.faults.adc = false;

// bits 0..3 → ADC errors
// (you can choose which struct flag to assign; here's an example)
if (low & (1 << 0))
	ivt.faults.adc = true;   // ADC interrupt → mark a generic current fault
if (low & (1 << 1))
	ivt.faults.adc = true;   // Overflow ADC ch1
if (low & (1 << 2))
	ivt.faults.adc = true;   // Underflow ADC ch1
if (low & (1 << 3))
	ivt.faults.adc = true;   // Overflow ADC ch2/U1–U3
if (low & (1 << 4))
	ivt.faults.adc = true;   // Underflow ADC ch2/U1–U3
if (low & (1 << 5))
	ivt.faults.vRef = true;      // Vref implausibility
if (low & (1 << 6))
	ivt.faults.current = true;   // I1–I2 delta

// bits 8..15 → open-circuit & calibration
if (high & (1 << 0))
	ivt.faults.current_oc = true;  // I1 open circuit
if (high & (1 << 1))
	ivt.faults.U1_oc = true;
if (high & (1 << 2))
	ivt.faults.U2_oc = true;
if (high & (1 << 3))
	ivt.faults.U3_oc = true;
if (high & (1 << 4))
	ivt.faults.ntc_h_oc = true;
if (high & (1 << 5))
	ivt.faults.ntc_l_oc = true;
}

void IVT_SET_BITRATE(void) {
uint32_t startTime;

// Configure IVT. Take response delays into account, if response time is too long --> raise CAN error
if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_STOP_CMD) == HAL_OK) { // Stop measurement to configure results

	printf("Command sent\r\n");
}
startTime = HAL_GetTick();
while (!(IVT_commandReceivedFlag && ((HAL_GetTick() - startTime) >= 2))) {

	if ((HAL_GetTick() - startTime) > 1000) {
		commsCheck = false; // send UI Can error
		printf("Fudeu CAN - stop\n\n");
		IVT_SET_BITRATE();
		return;
	}
}
IVT_commandReceivedFlag = 0; // Reset flag

if (IVT_CAN_SendMessage(&hcan1, IVT_COMMAND_CANID, 8, IVT_CONFIG_CANRATE_CMD) == HAL_OK) { // Configure current result command

	printf("Command sent\r\n");
}
IVT_commandReceivedFlag = 0; // Reset flag

}

/**
 * @brief  Serializes the global `ivt` structure to JSON and sends it over UART.
 * @note   Uses printf() for all output. Produces a single JSON object.
 */
void send_ivt_ui(void) {
// Start JSON array with one object
printf("[");

// Start the ivt object with index = 0
printf("{\"ivt\":0,");

// --- Real-time values ---
printf("\"vBatt\":%ld,", ivt.vBatt);
printf("\"iBatt\":%ld,", ivt.iBatt);
printf("\"power\":%ld,", ivt.power);
printf("\"temp\":%ld,", ivt.temp);
printf("\"SOH\":%ld,", ivt.SOH);
printf("\"SOC\":%ld,", ivt.SOC);

// --- Configuration parameters ---
printf("\"conf_CANbit\":%.0f,", ivt.conf_CANbit);
printf("\"conf_maxTemp\":%.2f,", ivt.conf_maxTemp);
printf("\"conf_minTemp\":%.2f,", ivt.conf_minTemp);
printf("\"conf_minCurrent\":%.2f,", ivt.conf_minCurrent);
printf("\"conf_maxCurrent\":%.2f,", ivt.conf_maxCurrent);
printf("\"conf_maxU1\":%.2f,", ivt.conf_maxU1);
printf("\"conf_minU1\":%.2f,", ivt.conf_minU1);
printf("\"conf_maxU2\":%.2f,", ivt.conf_maxU2);
printf("\"conf_minU2\":%.2f,", ivt.conf_minU2);
printf("\"conf_maxU3\":%.2f,", ivt.conf_maxU3);
printf("\"conf_minU3\":%.2f,", ivt.conf_minU3);

// --- Fault flags ---
printf("\"faults\":{");
printf("\"CAN\":%s,", ivt.faults.CAN ? "true" : "false");
printf("\"power\":%s,", ivt.faults.power ? "true" : "false");
printf("\"current\":%s,", ivt.faults.current ? "true" : "false");
printf("\"vRef\":%s,", ivt.faults.vRef ? "true" : "false");
printf("\"U3_oc\":%s,", ivt.faults.U3_oc ? "true" : "false");
printf("\"U2_oc\":%s,", ivt.faults.U2_oc ? "true" : "false");
printf("\"U1_oc\":%s,", ivt.faults.U1_oc ? "true" : "false");
printf("\"current_oc\":%s,", ivt.faults.current_oc ? "true" : "false");
printf("\"ntc_l_oc\":%s,", ivt.faults.ntc_l_oc ? "true" : "false");
printf("\"ntc_h_oc\":%s,", ivt.faults.ntc_h_oc ? "true" : "false");
printf("\"adc\":%s,", ivt.faults.adc ? "true" : "false");
printf("\"temp\":%s", ivt.faults.temp ? "true" : "false");
printf("}");

// Close the ivt object
printf("}");

// End JSON array
printf("]\n");  // End of JSON array
}

/*void send_ivt_ui(void) {
 int tempe = 320 + rand() % 10;
 int32_t corrente = 97 + rand() % 24;
 int32_t tensao = 41500 + rand() % 60;
 int32_t pot = corrente*tensao*0.001*0.001;

 // Start JSON array with one object
 printf("[");

 // Start the ivt object with index = 0
 printf("{\"ivt\":0,");

 // --- Real-time values ---
 printf("\"vBatt\":%ld,", tensao);
 printf("\"iBatt\":%ld,", corrente);
 printf("\"power\":%ld,", pot);
 printf("\"temp\":%ld,", tempe);
 printf("\"SOH\":\"Err\",");
 printf("\"SOC\":\"Err\",");

 // --- Configuration parameters ---
 printf("\"conf_CANbit\":1,");
 printf("\"conf_maxTemp\":125,");
 printf("\"conf_minTemp\":10,");
 printf("\"conf_minCurrent\":-5,");
 printf("\"conf_maxCurrent\":10,");
 printf("\"conf_maxU1\":50.4,");
 printf("\"conf_minU1\":-0.5,");
 printf("\"conf_maxU2\":\"Err\",");
 printf("\"conf_minU2\":\"Err\",");
 printf("\"conf_maxU3\":\"Err\",");
 printf("\"conf_minU3\":\"Err\",");

 // --- Fault flags ---
 printf("\"faults\":{");
 printf("\"CAN\":false,");
 printf("\"power\":false,");
 printf("\"current\":false,");
 printf("\"vRef\":false,");
 printf("\"U3_oc\":true,");
 printf("\"U2_oc\":true,");
 printf("\"U1_oc\":false,");
 printf("\"current_oc\":false,");
 printf("\"ntc_l_oc\":false,");
 printf("\"ntc_h_oc\":false,");
 printf("\"adc\":false,");
 printf("\"temp\":false");
 printf("}");

 // Close the ivt object
 printf("}");

 // End JSON array
 printf("]\n");  // End of JSON array
 }*/

