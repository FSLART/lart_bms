/*
 * contactors.c
 *
 *  Created on: Oct 8, 2025
 *      Author: jpser
 */
#include "contactors.h"

void OpenAllContactors(void){
	OpenPreCarga();
	OpenAIR_negativo();
	OpenAIR_positivo();
	//OpenDescarga();
}


void OpenPreCarga(void){
	HAL_GPIO_WritePin(CONTACT_PRE_GPIO_Port, CONTACT_PRE_Pin, GPIO_PIN_SET);
}



void OpenAIR_negativo(void){
	HAL_GPIO_WritePin(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin, GPIO_PIN_SET);
}



void OpenAIR_positivo(void){
	HAL_GPIO_WritePin(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin, GPIO_PIN_SET);
}


/*void OpenDescarga(void){
	HAL_GPIO_WritePin(CONTACT_DSCH_GPIO_Port, CONTACT_DSCH_Pin, GPIO_PIN_SET);
}*/


void ClosePreCarga(void){
	HAL_GPIO_WritePin(CONTACT_PRE_GPIO_Port, CONTACT_PRE_Pin, GPIO_PIN_RESET);
}



void CloseAIR_negativo(void){
	HAL_GPIO_WritePin(CONTACT_AIR_negativo_GPIO_Port, CONTACT_AIR_negativo_Pin, GPIO_PIN_RESET);
}



void CloseAIR_positivo(void){
	HAL_GPIO_WritePin(CONTACT_AIR_positivo_GPIO_Port, CONTACT_AIR_positivo_Pin, GPIO_PIN_RESET);
}


/*void CloseDescarga(void){
	HAL_GPIO_WritePin(CONTACT_DSCH_GPIO_Port, CONTACT_DSCH_Pin, GPIO_PIN_RESET);
}*/
