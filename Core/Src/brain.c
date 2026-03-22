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

#include "adbms_main.h"
#include "cell_balancing.h"
#include "adbms_to_CAN.h"

#include "analog_readings.h"
#include "eeprom_utils.h"
#include "isa_ivt-s.h"
#include "contactors.h"
#include "uartDMA.h"
#include "master_to_CAN.h"
#include "precharge.h"
#include "can.h"
#include "powertrain_t26.h"

/* ================== LOCAL DEFINES / TYPES ================== */

#define STEERING_MAX 0xA1

/* EEPROM comms instance */
//static EEPROM_Comms eeprom_comms = { .hi2c = &hi2c1, .huart = &huart1 };
/* ================== MODULE STATE ================== */
volatile AMSStates_t AMS_State = FAULT;
volatile AMSStates_t AMS_Current_State = FAULT;
volatile AMSStates_t AMS_Previous_State = FAULT;

/* runtime bookkeeping / flags */
static volatile uint32_t runtime_sec = 0;
static volatile bool faultCheck = false;
static volatile bool updateUI = false;

/* timing helpers for state machine */
//static uint32_t timeDiff = 0;
static uint32_t timeStart = 0;
//static uint32_t timeCmmd = 0;
volatile bool toggleHeartbeat = false;

void brain_start(void) {

	HAL_CAN_Start(&hcan1);

	OpenAllContactors();

	// Set CS2 Pin to HIGH to disable second SPI on 6822 + MSTR should be high by default
	HAL_GPIO_WritePin(BMS_MSTR_GPIO_Port, BMS_MSTR_Pin, GPIO_PIN_SET);

	// Start Timers
	//HAL_TIM_Base_Start_IT(&htim8);
	//HAL_TIM_Base_Start_IT(&htim10);

	printfConsole("Start Program \n\r");
	printfDebug("Bluetooth, u up? \r\n");
	//OpenAllContactors();
	//startUI();

	//initilize slave comms
	//adbms_main();

	//char ts[20];
	//RTC_Time_Get(ts, sizeof(ts));
	//printConsole("%s\r\n", ts);

	AnalogReadings_Init();

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

	IVT_CAN_Setup_AllMessages(&hcan1);

	//inicializar o can pra receber a mensagem de precarga
	Precharge_CAN_Init();
	CellBalancing_CAN_Init();

	// Start Timer11 for falut check
	//HAL_TIM_Base_Start_IT(&htim11);

	//bmsState = INACTIVE;
	AMS_State = STARTUP;
	AMS_Previous_State = FAULT;
	AMS_Current_State = FAULT;
}

void brain_loop(void) {

	AMS_Current_State = AMS_State;        // Copy value to ensure value is not changed throughout the loop

	switch (AMS_Current_State) {

	case RESET_ISA:

		IVT_CAN_Config();
		IVT_SET_BITRATE();

		AMS_Current_State = AMS_Previous_State;

		break;

	case BALANCING:

		//if ((getRuntimeMsDiff(timeStart) > 100) || (AMS_Previous_State != AMS_Current_State)) {
			//timeStart = getRuntimeMs();

			adbms_main(AMS_Current_State);
		//}

		break;

	case IDLE:

		if ((getRuntimeMsDiff(timeStart) > 50) || (AMS_Previous_State != AMS_Current_State)) {
			//printfDma("	IDLE \n\n");
			timeStart = getRuntimeMs();

			adbms_main(AMS_Current_State);

			printfDebug("IDLE: %d ms \r\n", (int)(getRuntimeMs() - timeStart));

			//loop_count = 0;
			//adBmsWakeupIc(TOTAL_IC);
			//adBmsWriteData(TOTAL_IC, &IC[0], WRCFGA, Config, A);
			//adBmsWriteData(TOTAL_IC, &IC[0], WRCFGB, Config, B);
			// adBmsWakeupIc(TOTAL_IC);
			//adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			//Delay_ms(1); // ADCs are updated at their conversion rate is 1ms
			//adBms6830_Adcv(RD_ON, CONTINUOUS, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			//Delay_ms(1); // ADCs are updated at their conversion rate is 1ms
			//adBms6830_Adsv(CONTINUOUS, DISCHARGE_PERMITTED, CELL_OPEN_WIRE_DETECTION);
			//Delay_ms(8); // ADCs are updated at their conversion rate is 8ms
			/*while(loop_count < LOOP_MEASUREMENT_COUNT)
			 {
			 // measurement_loop();
			 //Delay_ms(MEASUREMENT_LOOP_TIME);
			 loop_count = loop_count + 1;
			 }*/
		}

		break;

	case STARTUP:

		adbms_main(AMS_Current_State);
		//TODO: implement startup shit that needs looping i gueess lol

		AMS_State = IDLE;
		//AMS_State = BALANCING;

		break;

	default:
		AMS_State = FAULT;

		break;

	}

	AMS_Previous_State = AMS_Current_State;

	// Fault Check Triggered
	if (faultCheck) {
		//printfDma("FAULT CHECK \r\n");
		//IVT_FAULT_CHECK();
		//bms_openWireCheck(&ow_status);
		ADBMS_CAN_SendAll(&hcan1, AMS_Current_State);
		//AnalogReadings_CAN_Send(&hcan1);
		Master_CAN_SendAll(&hcan1);
		//AnalogReadings_Start();
		faultCheck = false;

	}

	// ~Update UI Triggered
	if (updateUI) {

		//send_ivt_ui();
		//send_ad68_ui();
		updateUI = false;
	}

	if (toggleHeartbeat) {

		HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
		toggleHeartbeat = false;
	}

	//update precharge state machine if necessary
	Precharge_Update();

	//CAN housekeeping
	CAN_Service(&hcan1);

	//check if there are can messages to send
	CanTx_ProcessQueue();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
	if (hadc->Instance == ADC1) {
		AnalogReadings_ConvCpltCallback();
	}
}

/// Timer interrupt callback
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {

	//Timer to update ui - 800ms
	/*if (htim->Instance == TIM8) {
	 updateUI = true;
	 }*/
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == B1_Pin) {
		if (AMS_State == ONMISSION) {
			AMS_State = IDLE;
		} else {
			AMS_State = ONMISSION;
		}
	}

	Feedback_EXTI_Callback(GPIO_Pin);
}

// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

	static int counter_200ms = 0;
	static int counter_800ms = 0;
	static int counter_1000ms = 0;

	if (++counter_200ms >= 100) {
		counter_200ms = 0;
		faultCheck = true;
	}

	if (++counter_800ms >= 800) {
		counter_800ms = 0;
		updateUI = true;
	}

	if (++counter_1000ms >= 1000) {
		counter_1000ms = 0;
		runtime_sec += 1;
	}

	heartbeat();
}

void heartbeat(void) {

	static uint16_t ticks = 0;
	static uint16_t period = 700;   // start period

	//toggleHeartbeat = false;
	if (++ticks >= period) {

		ticks = 0;

		toggleHeartbeat = true;

		// halve the period
		period >>= 1;

		// reset when reaching 0 or 1
		if (period < 1) {
			period = 1000;
		}
	}

	/* HEARTBEAT*/
	/**static uint16_t ticks = 0;
	 static uint16_t beat_ticks = 0;

	 if (++ticks >= 800) {

	 ticks = 0;
	 beat_ticks = 1;
	 HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);

	 } else if (beat_ticks == 1 && ticks >= 50) {

	 beat_ticks = 0;
	 HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
	 }*/

}

uint32_t getRuntimeSeconds(void) {
	return runtime_sec;
}

uint32_t getRuntimeMs(void) {
	return HAL_GetTick();
}

uint32_t getRuntimeMsDiff(uint32_t startTime) {
	return HAL_GetTick() - startTime;
}

void RaiseError(ErrorCode_t errorcode) {

	/*switch(errorcode)
	 {
	 case ERROR_SDC_TRIGGERED:
	 errorStatus.errorSDC = true;
	 break;

	 }*/
}
