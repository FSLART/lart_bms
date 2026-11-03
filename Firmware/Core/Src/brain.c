/*
 * brain.c
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 *
 * Arranque e ciclo principal. O brain_loop chama o passo de cada modulo por
 * uma ordem fixa:
 *
 *   entradas   feedbacks/SDC, CAN recebido, ADC da placa, consola, RN4871
 *   medicao    cadeia ADBMS (maquinas proprias, tick de 200 ms do TIM6)
 *   decisoes   ISA, SOC, carregador, precarga, ventoinhas, estado do AMS
 *   AMS_ERROR  depois de todos os faults do ciclo
 *   saidas     contactores (unico escritor), CAN, JSON, UART, EEPROM
 *
 * Nenhum passo espera: o WWDG corta o brain_loop aos ~65 ms.
 */

#include "main.h"
#include "brain.h"
#include "adBms_Application.h"
#include "config_store.h"
#include "ams_error.h"
#include "contactors.h"
#include "precharge.h"
#include "charger.h"
#include "isa.h"
#include "soc.h"
#include "fans.h"
#include "board.h"
#include "bsp.h"
#include "can_bus.h"
#include "can_messages.h"
#include "uart_link.h"
#include "rn4871.h"
#include "console.h"
#include "live_json.h"
#include "boot_ctrl.h"

#include <stdbool.h>
#include <string.h>

#define STARTUP_TIMEOUT_MS 5000u   // sem uma leitura completa ate aqui sai do STARTUP na mesma (os faults seguram o AMS_ERROR)
#define RESET_DELAY_MS     200u    // deixa sair a resposta da consola antes de reiniciar

// Snapshot global do BMS (ver brain.h); arranca tudo a zero
bms_t bms;

//Heatbeat trigger flag
volatile bool toggleHeartbeat = false;

typedef enum {
	REQUEST_NONE = 0,
	REQUEST_RESET,
	REQUEST_BOOTLOADER,
	REQUEST_ROLLBACK
} system_request_t;

static uint32_t start_ms;
static bool startup_done;
static system_request_t pending_request;
static uint32_t request_ms;

const char* brain_state_name(ams_state_t state) {

	switch (state) {
	case AMS_BALANCING:
		return "BALANCING";
	case AMS_CHARGING:
		return "CHARGING";
	case AMS_IDLE:
		return "IDLE";
	case AMS_ONMISSION:
		return "ONMISSION";
	case AMS_STARTUP:
		return "STARTUP";
	case AMS_DISCHARGE_TEST:
		return "DISCHARGE_TEST";
	case AMS_RESET_ISA:
		return "RESET_ISA";
	case AMS_FAULT:
		return "FAULT";
	default:
		return "?";
	}
}

// Brain INITILIAZATION
void brain_start(void) {

	uint32_t now_ms = HAL_GetTick();

	start_ms = now_ms;
	startup_done = false;
	pending_request = REQUEST_NONE;
	request_ms = 0;

	// Watchdog Fault Acknoledge: fica visivel ate ao proximo arranque (o IWDG e o do bootloader nas imagens em teste)
	bms.system.reset_flags = watchdog_flag;
	bms.system.wwdg_reset = (watchdog_flag & (RCC_CSR_WWDGRSTF | RCC_CSR_IWDGRSTF)) != 0u;
	bms.faults.watchdog_reset = bms.system.wwdg_reset;
	bms.system.state = AMS_STARTUP;
	bms.system.previous_state = AMS_STARTUP;
	bms.system.state_since_ms = now_ms;

	// falha segura primeiro: linha em erro e contactores abertos
	AmsError_Init(now_ms);
	Contactors_Init(now_ms);

	ConfigStore_Load();                       // EEPROM -> bms.config (ou defaults + config_invalid)
	ConfigStore_SetAdbmsCheck(ADBMS_Init());  // topologia, regra dos 30 % de NTC e o tick do TIM6
	BootCtrl_Init(now_ms);                    // o que o bootloader deixou na RAM e no journal

	Precharge_Init(now_ms);
	Charger_Init();
	Isa_Init();
	Soc_Init();
	Board_Init();
	Fans_Init();
	CanBus_Init();
	CanMessages_Init();
	UartLink_Init();
	Console_Init();
	Rn4871_Init(now_ms);
	LiveJson_Init();
}

/* estado do AMS para o CAN (0x701) e para a UI; so informativo, as decisoes
 * estao nos modulos */
static void update_state(uint32_t now_ms) {

	ams_state_t state;

	if (!startup_done && ((bms.cells.cells_valid && bms.cells.temps_valid) || ((now_ms - start_ms) >= STARTUP_TIMEOUT_MS))) {
		startup_done = true;
	}

	if (!startup_done) {
		state = AMS_STARTUP;
	} else if (bms.ams_error.active) {
		state = AMS_FAULT;
	} else if (Charger_SessionActive()) {
		state = AMS_CHARGING;
	} else if (bms.precharge.state != KILL) {
		state = AMS_ONMISSION;
	} else if ((bms.balancing.stage == BALANCING_ROUGH) || (bms.balancing.stage == BALANCING_FINE)) {
		state = AMS_BALANCING;
	} else {
		state = AMS_IDLE;
	}

	if (state != bms.system.state) {
		bms.system.previous_state = bms.system.state;
		bms.system.state = state;
		bms.system.state_since_ms = now_ms;
	}
}

/* reboot, bootloader (consola, 0x0DC ou 0x601) e voltar ao firmware
 * anterior: so com a HV desligada e fora de um carregamento. Um pedido com a
 * HV ligada e deitado fora (nao fica a espera para disparar mais tarde).
 * Pequeno atraso para a resposta sair */
static void service_requests(uint32_t now_ms) {

	bool safe = bms.system.hv_safe && !Charger_SessionActive();
	bool bootloader = CanMessages_TakeBootloaderRequest() | Console_TakeBootloaderRequest();
	bool rollback = Console_TakeRollbackRequest();
	bool reboot = Console_TakeRebootRequest();

	if (safe && (bootloader || rollback || reboot) && (pending_request == REQUEST_NONE)) {
		pending_request = bootloader ? REQUEST_BOOTLOADER : (rollback ? REQUEST_ROLLBACK : REQUEST_RESET);
		request_ms = now_ms;
	}

	if (pending_request == REQUEST_NONE) {
		return;
	}
	if (!safe) {
		pending_request = REQUEST_NONE;
		return;
	}

	if ((now_ms - request_ms) >= RESET_DELAY_MS) {
		system_request_t request = pending_request;

		pending_request = REQUEST_NONE;
		if (request == REQUEST_BOOTLOADER) {
			BootCtrl_RequestBootloader();   // o bootloader ve o pedido na RAM e fica
		} else if ((request == REQUEST_ROLLBACK) && !BootCtrl_Reject()) {
			return;                         // o journal nao aceitou: nao reinicia
		}
		Bsp_SystemReset();
	}
}

void brain_loop(void) {

	uint32_t start_us = adBmsMicros();
	uint32_t now_ms = HAL_GetTick();

	bms.system.uptime_ms = now_ms;
	Bsp_IwdgRefresh();

	// entradas
	Contactors_ReadInputs(now_ms);
	CanMessages_Process(now_ms);
	Board_Step(now_ms);
	Console_Step(now_ms);
	Rn4871_Step(now_ms);

	// medicao
	ADBMS_Run();

	// decisoes
	Isa_Step(now_ms);
	Soc_Step(now_ms);
	Charger_Step(now_ms);
	Precharge_Step(now_ms);
	Fans_Step(now_ms);
	update_state(now_ms);
	AmsError_Step(now_ms);
	BootCtrl_Step(now_ms);

	// saidas
	Contactors_Apply();
	CanMessages_Transmit(now_ms);
	CanBus_Service(now_ms, Charger_SessionActive());
	LiveJson_Step(now_ms);
	UartLink_Service(now_ms);
	ConfigStore_Step(now_ms);
	service_requests(now_ms);

	if (toggleHeartbeat) {
		toggleHeartbeat = false;
		Bsp_ToggleHeartbeat();
	}

	// orcamento do WWDG: ~65 ms por brain_loop
	bms.system.loops++;
	bms.system.loop_us = adBmsMicros() - start_us;
	if (bms.system.loop_us > bms.system.loop_max_us) {
		bms.system.loop_max_us = bms.system.loop_us;
	}
}

// Timer interrupt callback: o HAL chama isto a partir do TIMx_IRQHandler do CubeMX
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {

	if (htim->Instance == TIM6) {
		ADBMS_Tick();   // tick de 200 ms da cadeia ADBMS
	}
}

// feedbacks dos contactores: o EXTI so conta flancos, quem decide e o debounce por tempo
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

	Contactors_OnEdge(GPIO_Pin);
}

// Called each SysTick interrupt for HEARTBEAT LED
void HAL_SYSTICK_Callback(void) {

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

}
