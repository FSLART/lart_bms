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
#include "bootloader_jumper.h"

#include "analog_readings.h"
#include "fan_management.h"
#include "bms_eeprom_config.h"
#include "isa_ivt-s.h"
#include "contactors.h"
#include "uartDMA.h"
#include "master_to_CAN.h"
#include "precharge.h"
#include "can.h"
#include "fault_manager.h"
#include "gpio_expander.h"
#include "charger.h"
#include "live_debug.h"
#include "ams_error.h"

#include "powertrain_t26.h"
#include "handcart_t26.h"

#include "soc.h"

/* ================== LOCAL DEFINES / TYPES ================== */

/* EEPROM nig init */
EE24_HandleTypeDef eep24fc08;

/* ================== MODULE STATE ================== */
volatile AMSStates_t AMS_State = FAULT;
volatile AMSStates_t AMS_Current_State = FAULT;
volatile AMSStates_t AMS_Previous_State = FAULT;

/* runtime bookkeeping / flags */
uint32_t runtime_sec = 0;
bool faultCheck = false;
bool updateUI = false;

/* timing helpers for state machine */
//static uint32_t timeDiff = 0;
uint32_t timeStart = 0;
//static uint32_t timeCmmd = 0;
bool toggleHeartbeat = false;

void brain_start(void) {

	// linha AMS_ERROR arranca em ERRO (fail-safe, como o SET original);
	// a partir daqui so o ams_error.c mexe no pino
	AMS_Error_Init();

	OpenAllContactors();

	FaultManager_Init();

	//HAL_CAN_Start(&hcan1);
	//CAN_Service(&hcan1);
	CAN_Init(&hcan1);

	//HAL_CAN_Start(&hcan1);
	//CAN_Service(&hcan2);
	CAN_Init(&hcan2);

	// Set CS2 Pin to HIGH to disable second SPI on 6822 + MSTR should be high by default
	HAL_GPIO_WritePin(BMS_MSTR_GPIO_Port, BMS_MSTR_Pin, GPIO_PIN_SET);

	// Start Timers
	//HAL_TIM_Base_Start_IT(&htim8);
	//HAL_TIM_Base_Start_IT(&htim10);
	//HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1); // start pwm
	//__HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, 0);
	Fan_Start();

	printfConsole("Start Program \n\r");
	printfDebug("Bluetooth, u up? \r\n");
	//RN4871_SetName();
	//OpenAllContactors();
	//startUI();

	//initilize slave comms
	//adbms_main();

	//char ts[20];
	//RTC_Time_Get(ts, sizeof(ts));
	//printConsole("%s\r\n", ts);

	AnalogReadings_Init();

	if (!BmsConfig_Init(&eep24fc08, &hi2c1)) {
		printfDebug("EEPROM init failed, using code defaults\n");
		BmsConfig_LoadDefaults();
	}

	//MCP23017_Init();

	//BmsConfig_DumpEEPROM(&eep24fc08);

	//IVT_CAN_Setup_AllMessages(&hcan1);
	IVT_CAN_Setup(&hcan1);
	//IVT_CAN_Config();

	//inicializar as callbakc para o CAN
	Precharge_CAN_Init();
	CellBalancing_CAN_Init();
	Charger_CAN_Init();

	if (watchdog_flag & RCC_CSR_WWDGRSTF) {
		RAISE_ERROR(FAULT_WATCHDOG_RESET);
		//HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_SET);
		printfDebug("BOOT: WWDG reset detected!\r\n");
	}

	// Start Timer11 for falut check
	//HAL_TIM_Base_Start_IT(&htim11);

	Setup_Bootloader_Jumper();

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

	case CHARGING:

		if ((getRuntimeMsDiff(timeStart) > 50) || (AMS_Previous_State != AMS_Current_State)) {
			timeStart = getRuntimeMs();

			adbms_main(AMS_Current_State);

			Charger_Update();

		}

		break;

	case IDLE:

		if ((getRuntimeMsDiff(timeStart) > 50) || (AMS_Previous_State != AMS_Current_State)) {
			//printfDma("	IDLE \n\n");
			timeStart = getRuntimeMs();

			adbms_main(AMS_Current_State);

			//printfDebug("IDLE: %d ms \r\n", (int)(getRuntimeMs() - timeStart));

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

			// --- PWM sweep ---
			/*{
			 static uint8_t pwm_val = 0;
			 static int8_t  pwm_dir = 1;         // +1 = up, -1 = down
			 static uint32_t pwm_last = 0;

			 if (getRuntimeMsDiff(pwm_last) >= 10) {  // step every 10ms → full sweep in ~2.5s
			 pwm_last = getRuntimeMs();

			 pwm_val += pwm_dir;

			 if (pwm_val == 255) pwm_dir = -1;
			 if (pwm_val == 0)   pwm_dir =  1;

			 __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_1, pwm_val);
			 }
			 }*/

			//uint16_t min_cell_mV = adBms6830_FindMinVoltageGlobally();
			//RESET SOC
			//SOC_Init(min_cell_mV);
		}

		break;

	case STARTUP:

		//MCP23017_StartupAnimation_Update();

		//printfDma("	STARTUP \n\n");
		//TODO: implement startup shit that needs looping i gueess lol

		//static uint8_t adbmsLoopCounter = 0;

		//if (adbms_main(AMS_Current_State) == ADBMS_END && adbmsLoopCounter > 3) {
		if (adbms_main(AMS_Current_State) == ADBMS_END) {

			//encontrar a celula com menor tensão
			uint16_t min_cell_mV = adBms6830_FindMinVoltageGlobally();

			//RESET SOC
			SOC_Init(min_cell_mV);

			//MCP23017_All_LEDs_Off();

			AMS_State = IDLE;

		} /*else if (adbms_main(AMS_Current_State) == ADBMS_END) {
		 adbmsLoopCounter++;
		 }*/

		break;

	default:
		AMS_State = FAULT;

		break;

	}

	AMS_Previous_State = AMS_Current_State;

	// Fault Check Triggered
	if (faultCheck) {

		if (AMS_Current_State == IDLE || AMS_Current_State == CHARGING) {
			BMS_SafetyCheck();
		}

		//printfDma("FAULT CHECK \r\n");
		//IVT_FAULT_CHECK();
		//funcao_de_merda_pq_eu_errei_o_pinout_do_sensor_de_corrente();
		//bms_openWireCheck(&ow_status);
		ADBMS_CAN_SendAll(&hcan1, AMS_Current_State);
		//AnalogReadings_CAN_Send(&hcan1);
		Master_CAN_SendAll(&hcan1);

		// repetir a telemetria toda no CAN2 para o handcart/carregador
		// tambem a ver tensoes/temperaturas (IDs 0x600-0x706, sem conflito
		// com nada que viva no barramento do carregador)
		ADBMS_CAN_SendAll(&hcan2, AMS_Current_State);
		Master_CAN_SendAll(&hcan2);
		//FaultManager_CAN_Send(&hcan1);

		Fan_Update();
		faultCheck = false;

	}

	// ~Update UI Triggered
	if (updateUI) {
		FaultManager_DumpUART();
		//SOC_DumpUART();
		//send_ivt_ui();
		//send_ad68_ui();
		updateUI = false;
	}

	if (toggleHeartbeat) {

		//TODO: gpio expander
		HAL_GPIO_TogglePin(MCU_HEARTBEAT_GPIO_Port, MCU_HEARTBEAT_Pin);
		toggleHeartbeat = false;
	}

	if (triggerJumpToBootloader) {

		JumpToBootloader(); //Agora em Thread Mode
	}

	//update precharge state machine if necessary
	Precharge_Update();

	//push any pending LED changes to the GPIO expander (single I2C write,
	//only when something changed - LEDs set from interrupts land here)
	//NOTE: intentionally disabled for now - expander I2C comms off.
	//All MCP23017_LED() calls are safe and just update the local bitmask;
	//un-comment this single line to bring the LEDs back.
	//MCP23017_Flush();

	//refresh live debug snapshot every 500ms (debugger-only, cheap)
	static uint32_t liveDebugTimer = 0;
	if (getRuntimeMsDiff(liveDebugTimer) >= 500) {
		liveDebugTimer = getRuntimeMs();
		LiveDebug_Update();
	}

	//CAN housekeeping
	CAN_Service(&hcan1);
	CAN_Service(&hcan2);

	//check if there are can messages to send
	//CanTx_ProcessQueue(); - now done in CAN serivice

	//MCP23017_StartupAnimation_Update();

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
	/*if (GPIO_Pin == B1_Pin) {
	 if (AMS_State == ONMISSION) {
	 AMS_State = IDLE;
	 } else {
	 AMS_State = ONMISSION;
	 }
	 }*/

	Feedback_EXTI_Callback(GPIO_Pin);
}

// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

	static int counter_200ms = 0;
	static int counter_800ms = 0;
	static int counter_1000ms = 0;

	if (++counter_200ms >= 250) {
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
