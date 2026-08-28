/*
 * ams_error.c
 *
 *  Created on: Jul 19, 2026
 *      Author: jpser
 *
 *  Unico ficheiro autorizado a mexer no pino AMS_ERROR. Toda a gente
 *  chama Trigger/Clear em vez de escrever o GPIO diretamente, senao
 *  ninguem arbitra e qualquer loop consegue des-latchar um erro sem
 *  querer (era o que acontecia no IDLE, que reescrevia o pino a cada
 *  50ms).
 *
 *  Dois niveis: clearable (Trigger/Clear) e permanente (TriggerLatched,
 *  so um power cycle limpa). O permanente ganha sempre ao clearable.
 *
 *  Polaridade do pino: RESET = OK, SET = ERRO.
 */

#include "ams_error.h"

#include "main.h"
#include "uartDMA.h"

static uint8_t ams_error_active = 0;

/* latch permanente: uma vez a 1 nunca mais volta a 0 em runtime, so o
 * arranque (zerar do .bss) o limpa - nenhuma funcao escreve 0 aqui */
static uint8_t ams_error_permanent = 0;

static void AMS_Error_WritePin(void) {

	if ((ams_error_active != 0) || (ams_error_permanent != 0)) {
		HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_SET);
	} else {
		HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, GPIO_PIN_RESET);
	}
}

void AMS_Error_Init(void) {

	// fail-safe: arranca em ERRO ate alguem provar que esta tudo bem
	// (o deteta-slaves no STARTUP chama Clear quando a chain bate certo)
	ams_error_active = 1;
	AMS_Error_WritePin();
}

void AMS_Error_Trigger(void) {

	if (ams_error_active == 0) {
		printfDebug("AMS_ERROR line -> ERROR\r\n");
	}

	ams_error_active = 1;
	AMS_Error_WritePin();
}

void AMS_Error_TriggerLatched(void) {

	if (ams_error_permanent == 0) {
		printfDebug("AMS_ERROR line -> ERROR (PERMANENT, only reboot clears)\r\n");
	}

	ams_error_permanent = 1;
	AMS_Error_WritePin();
}

void AMS_Error_Clear(void) {

	if ((ams_error_active != 0) && (ams_error_permanent == 0)) {
		printfDebug("AMS_ERROR line -> OK\r\n");
	}

	ams_error_active = 0;

	// se houver latch permanente o WritePin mantem o pino em erro na mesma
	AMS_Error_WritePin();
}

uint8_t AMS_Error_IsActive(void) {

	if ((ams_error_active != 0) || (ams_error_permanent != 0)) {
		return 1;
	}

	return 0;
}

uint8_t AMS_Error_IsPermanent(void) {
	return ams_error_permanent;
}
