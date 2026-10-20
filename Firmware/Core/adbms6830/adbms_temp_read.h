/*
 * adbms_temp_read.h
 *
 * Leitura dos NTC das celulas e da temperatura de cada ADBMS6830B, uma vez
 * por ciclo de 200 ms. Maquina de estados em bms.adbms.temp_read:
 *   START (ADAX + ADAX2) -> WAIT (ate 100 ms) -> DONE
 * Cada NTC e medido por dois caminhos (AUX e AUX2) que tem de concordar. So
 * publica no bms o que passou nos testes e avalia OT/UT, NTC em falha e o
 * sobreaquecimento dos chips.
 */

#ifndef ADBMS_TEMP_READ_H_
#define ADBMS_TEMP_READ_H_

#include <stdbool.h>

void TempRead_Start(void);
bool TempRead_Step(void);   // true = acabou este ciclo

#endif /* ADBMS_TEMP_READ_H_ */
