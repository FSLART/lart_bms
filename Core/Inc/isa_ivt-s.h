#ifndef INC_ISA_IVT_S_H_
#define INC_ISA_IVT_S_H_

#include "main.h"
#include <stdio.h>
#include <stdbool.h>

/* IVT-S CAN IDs */
#define IVT_COMMAND_CANID ((uint16_t)0x411)
#define IVT_DEBUG_CANID ((uint16_t)0x510)
#define IVT_RESPONSE_CANID ((uint16_t)0x511)
#define IVT_RESULTI_CANID ((uint16_t)0x521)
#define IVT_RESULTU1_CANID ((uint16_t)0x522)
#define IVT_RESULTU2_CANID ((uint16_t)0x523)
#define IVT_RESULTU3_CANID ((uint16_t)0x524)
#define IVT_RESULTT_CANID ((uint16_t)0X525)
#define IVT_RESULTW_CANID ((uint16_t)0X526)

/* Private variables --------------------------------------------------------*/
/* IVT-S CAN Command Constants */
static const uint8_t IVT_STOP_CMD[8]           = {0x34, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}; // Stop Mode
static const uint8_t IVT_CONFIG_CURRENT_CMD[8] = {0x20, 0x02, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00}; // Cyclic Current 100ms
static const uint8_t IVT_CONFIG_CANRATE_CMD[8] = {0x3A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // CAN Bitrate: 1mb/s - restart
static const uint8_t IVT_CONFIG_U1_CMD[8]      = {0x21, 0x02, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00}; // U1: cyclic 100 ms
static const uint8_t IVT_CONFIG_U2_CMD[8]      = {0x22, 0x02, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00}; // U2: cyclic 100 ms
static const uint8_t IVT_CONFIG_U3_CMD[8]      = {0x23, 0x02, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00}; // Disable U3
static const uint8_t IVT_CONFIG_T_CMD[8]       = {0x24, 0x02, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00}; // temp: cyclic 120 ms
static const uint8_t IVT_CONFIG_W_CMD[8]       = {0x25, 0x02, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00}; // power: cyclic 120 ms
static const uint8_t IVT_STORE_CMD[8]          = {0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Store Config
static const uint8_t IVT_START_CMD[8]          = {0x34, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}; // Start Mode

static const uint8_t IVT_RESET_MEASURERROR_CMD[8]   = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Reset: Measurement Error
static const uint8_t IVT_RESET_SYSERROR_CMD[8]      = {0x30, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Reset: System Error

static const uint8_t IVT_SYSERROR_CMD[8]       = {0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Retrieve Sys Errors
static const uint8_t IVT_MEASURERROR_CMD[8]    = {0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Retrieve Measurement Errors

static const uint8_t IVT_CONFIG_I_THRE_POS_CMD[8]      = {0x35, 0x80, 0x64, 0x80, 0x01, 0x00, 0x00, 0x00}; // SET_THRESHOLD_POS: 100A, 1A
static const uint8_t IVT_CONFIG_I_THRE_NEG_CMD[8]      = {0x35, 0x7F, 0x9C, 0x80, 0x01, 0x00, 0x00, 0x00}; // SET_THRESHOLD_NEG: -100A, 1A

// IVT data structures - faults
typedef struct
{
	bool CAN;
	bool power;
	bool current;
	bool vRef;
	bool U3_oc;
	bool U2_oc;
	bool U1_oc;
	bool current_oc;
	bool ntc_l_oc;
	bool ntc_h_oc;
	bool adc;
	bool temp;
} IVT_faults_t;

/* Function prototypes -------------------------------------------------------*/

//extern void Error_Handler(void);
void IVT_CAN_Setup(CAN_HandleTypeDef *hcan);
void IVT_CAN_Setup_AllMessages(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef IVT_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData);
void IVT_CAN_Config(void);
void IVT_FAULT_CHECK(void);
void IVT_PROCESS_SYSERRORS(uint8_t *RxData);
void IVT_PROCESS_MEASURERRORS(uint8_t *RxData);
void send_ivt_ui(void);
void IVT_SET_BITRATE(void);

void IVT_CAN_OnMessage(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data);
void IVT_Init(void);  // optional init to register the callback


#endif /* INC_PTC_FDCAN_H_ */
