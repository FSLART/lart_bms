/*
 * brain.c
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 */

#include "main.h"
#include "brain.h"

#include "math.h"
#include "stdio.h"
#include <stdbool.h>

#include "bms_cmdlist.h"
#include "bms_datatypes.h"
#include "bms_utility.h"
#include "bms_mcuWrapper.h"
#include "bms_libWrapper.h"

#include "eeprom_utils.h"

#include "isa_ivt-s.h"

#include "time_rtc.h"

#include "contactors.h"

#include "uartDMA.h"

#include "version.h"

#include "temperatures.h"

#include "precharge.h"

#include "ams.h"

#define BYPASS_CAN_ISA

/* ================== LOCAL DEFINES / TYPES ================== */

#define STEERING_MAX 0xA1
const float deltaThreshold = 0.010f; // volts

/* Open-wire status buffer */
bms_ow_status_t ow_status[TOTAL_IC][TOTAL_CELL];

/* EEPROM comms instance */
//static EEPROM_Comms eeprom_comms = { .hi2c = &hi2c1, .huart = &huart1 };
/* ================== MODULE STATE ================== */
volatile BmsStates bmsState = IDLE;
volatile BmsStates bmsCurrState = IDLE;
volatile BmsStates bmsPrevState = IDLE;

/* runtime bookkeeping / flags */
static volatile uint32_t runtime_sec = 0;
static volatile bool faultCheck = false;
static volatile bool updateUI = false;

/* timing helpers for state machine */
static uint32_t timeDiff = 0;
static uint32_t timeStart = 0;
static uint32_t timeCmmd = 0;

void brain_start(void) {

	OpenAllContactors();

	// Set CS2 Pin to HIGH to disable second SPI on 6822 + MSTR should be high by default
	HAL_GPIO_WritePin(BMS_MSTR_GPIO_Port, BMS_MSTR_Pin, GPIO_PIN_SET);

	// Start Timers
	HAL_TIM_Base_Start_IT(&htim8);
	HAL_TIM_Base_Start_IT(&htim10);

	// Initialise BMS configs (No commands sent)
	//bms_init();

	//uint32_t timeDiff = 0;
	//uint32_t timeStart;
	//uint32_t timeCmmd;

	//printfDma("bad \r");
	printConsole("Start Program \n\r");
	printfDmaBT("Bluetooth, u up?");
	//OpenAllContactors();
	startUI();

	//char ts[20];
	//RTC_Time_Get(ts, sizeof(ts));
	//printConsole("%s\r\n", ts);

	//printfDmaBT("hello");

	/*if (Write_EEPROM(&eeprom_comms, STEERING_MAX, 2334, true)) {
	 //printfDma("good \n");
	 } else {
	 printfDma("bad \n");
	 }

	 if (Read_EEPROM(&eeprom_comms, STEERING_MAX, true) > -1) {
	 printfDma("good \n");
	 } else {
	 printfDma("bad2 \n");
	 }*/

	bms_stopDischarge();
	HAL_Delay(200);         // Initialisation delay
	bms_wakeupChain();
	bms_init();             // Initialise BMS configs and send them
	bms_readSid();

	bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
	bms_delayMsActive(12);
	bms_readAvgCellVoltage();
	bms_getAuxMeasurement();
	bms_delayMsActive(12);
	bms_readSVoltage();

	IVT_CAN_Setup_AllMessages(&hcan1);
#ifndef BYPASS_CAN_ISA
	IVT_CAN_Config();
	IVT_SET_BITRATE();
	#endif
	/*bms_startTimer();
	 HAL_Delay(200);

	 uint32_t time = bms_getTimCount();
	 bms_stopTimer();

	 printfDma("gay: %ld us\n", time);*/

	//inicializar o can pra receber a mensagem de precarga
	Precharge_CAN_Init();

	// Start Timer11 for falut check
	HAL_TIM_Base_Start_IT(&htim11);

	bmsState = INACTIVE;
	//bmsState = IDLE;
	bmsPrevState = INACTIVE;
	bmsCurrState = INACTIVE;
}

void brain_loop(void) {

	bmsCurrState = bmsState;        // Copy value to ensure value is not changed throughout the loop

	//if (bmsPrevState != bmsCurrState) {
	//wakeup slavews
	bms_wakeupChain();

	switch (bmsCurrState) {
	case BALANCING:

		//if it has been minimum balancing time, can check again on balancing
		if ((getRuntimeMsDiff(timeCmmd) > 60000) || (bmsPrevState != bmsCurrState)) {
			printConsole("	ACTIVE %d \n\n", getRuntimeMsDiff(timeCmmd));

			timeCmmd = getRuntimeMs();

			printConsole("Measuring Cell Voltage: \n");
			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
			bms_delayMsActive(12);
			bms_readAvgCellVoltage();

			printConsole("Temp Measurements: \n");
			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_getAuxMeasurement();

			// Calculate the discharge threshold
			float discharge_threshold = bms_calculateBalancing(deltaThreshold);

			// Check if need to balance the cells
			if (discharge_threshold > 0) {
				printConsole("Start Discharge: sqn kk\n");
				bms_wakeupChain();
				//bms_startDischarge(discharge_threshold);

				bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
				bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
				bms_delayMsActive(12);
			}

			timeDiff = getRuntimeMsDiff(timeCmmd);
			printConsole("Runtime: %ld ms, CommandTime: %ld ms \n\n", getRuntimeMs(), timeDiff);

		} else if (getRuntimeMsDiff(timeStart) > 200) {

			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
			bms_delayMsActive(12);
			bms_readAvgCellVoltage();
		}

		break;

	case INACTIVE:

		if (Precharge_GetState() == RX_CAN) {
			bmsState = STARTUP;
		}

		if ((getRuntimeMsDiff(timeStart) > 800) || (bmsPrevState != bmsCurrState)) {
			//printfDma("	IDLE \n\n");
			timeStart = getRuntimeMs();

			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
			bms_delayMsActive(12);
			bms_readAvgCellVoltage();
			bms_getAuxMeasurement();
			//ad68_dump_csv_bt();
		}

		//printConsole("	INACTIVE \n\n");

		//bms_stopDischarge();
		//bmsState = IDLE;
		break;

	case IDLE:

		if ((getRuntimeMsDiff(timeStart) > 800) || (bmsPrevState != bmsCurrState)) {
			//printfDma("	IDLE \n\n");
			timeStart = getRuntimeMs();

			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
			bms_delayMsActive(12);
			bms_readAvgCellVoltage();
			bms_getAuxMeasurement();
			//ad68_dump_csv_bt();
		}

		break;
	case STARTUP:
		/*OpenAllContactors();
		 HAL_Delay(2000);
		 CloseAIR_negativo();
		 HAL_Delay(200);
		 ClosePreCarga();
		 HAL_Delay(2500);
		 CloseAIR_positivo();
		 HAL_Delay(500);
		 OpenPreCarga();
		 HAL_Delay(3000);
		 CloseDescarga();
		 HAL_Delay(1000);*/
		if ((getRuntimeMsDiff(timeStart) > 800) || (bmsPrevState != bmsCurrState)) {
			//printfDma("	IDLE \n\n");
			timeStart = getRuntimeMs();

			bms_wakeupChain();              // Wakeup needed every 4ms of Inactivity
			bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
			bms_delayMsActive(12);
			bms_readAvgCellVoltage();
			bms_getAuxMeasurement();
			//ad68_dump_csv_bt();
		}

		while (Precharge_GetState() != END) {
			if (Precharge_GetState() == START) {
				Precharge_Init();
				Precharge_Update();
			} else {
				Precharge_Update();
			}
		}

		bmsState = IDLE;
		break;

	default:
		break;

	}

	bmsPrevState = bmsCurrState;

	//bms_getAuxMeasurement();
	//bms_delayMsActive(20);
	//bms_startAdcvCont();            // Need to wait 8ms for the average register to fill up
	//bms_delayMsActive(12);
	//bms_readAvgCellVoltage();
	//bms_delayMsActive(200);
	// send_ad68_ui();
	// bms_delayMsActive(200);
	// bms_delayMsActive(2000);
	// send_ivt_ui();

	if (faultCheck) {
		//IVT_FAULT_CHECK();
		//bms_openWireCheck(&ow_status);
		CAN_Send_AD68_All(&hcan1);
		faultCheck = false;
		//ClosePreCarga();
		read_mcu_temp();

	}

	if (updateUI) {
		//send_ivt_ui();
		send_ad68_ui();
		updateUI = false;
		//OpenPreCarga();
	}

	//check if there are can messages to send
	CanTx_ProcessQueue();
}

/// Timer interrupt callback
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	// 1s timer
	if (htim == &htim10) {
		runtime_sec += 1;
		//printfDma("	gay 1000ms\n");
	}

	//Timer to check on errors - 300ms
	if (htim->Instance == TIM11) {
		faultCheck = true;
	}

	//Timer to update ui - 800ms
	if (htim->Instance == TIM8) {
		updateUI = true;
	}
}

// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

	/* HEARTBEAT*/
	static uint16_t ticks = 0;
	static uint16_t beat_ticks = 0;

	if (++ticks >= 800) {

		ticks = 0;
		beat_ticks = 1;
		HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);

	} else if (beat_ticks == 1 && ticks >= 50) {

		beat_ticks = 0;
		HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
	}
}

uint32_t getRuntimeMs(void) {
	return HAL_GetTick();
}

uint32_t getRuntimeMsDiff(uint32_t startTime) {
	return HAL_GetTick() - startTime;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == B1_Pin) {
		if (bmsState == ONMISSION) {
			bmsState = IDLE;
		} else {
			bmsState = ONMISSION;
		}
	}
}

void RaiseError(ErrorCode_t errorcode) {

	/*switch(errorcode)
	 {
	 case ERROR_SDC_TRIGGERED:
	 errorStatus.errorSDC = true;
	 break;

	 }*/
}
