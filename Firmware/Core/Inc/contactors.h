/*
 * contactors.h
 *
 *  Created on: Oct 8, 2025
 *      Author: jpser
 */

#ifndef INC_CONTACTORS_H_
#define INC_CONTACTORS_H_

#include "main.h"

void OpenAllContactors(void);

void OpenPreCarga(void);
void OpenAIR_negativo(void);
void OpenAIR_positivo(void);
void OpenDescarga(void);

void ClosePreCarga(void);
void CloseAIR_negativo(void);
void CloseAIR_positivo(void);
void CloseDescarga(void);


#endif /* INC_CONTACTORS_H_ */
