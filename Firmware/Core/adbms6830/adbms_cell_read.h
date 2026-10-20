/*
 * adbms_cell_read.h
 *
 * Leitura das tensoes das celulas, uma vez por ciclo de 200 ms.
 * Maquina de estados em bms.adbms.cell_read:
 *   START (ADCV com redundancia) -> WAIT (8 ms) -> DONE
 * So publica no bms o que passou nos testes (PEC, sentinelas, gama, C-ADC vs
 * S-ADC, flags de diagnostico) e avalia OV/UV. As temperaturas estao no
 * adbms_temp_read.
 */

#ifndef ADBMS_CELL_READ_H_
#define ADBMS_CELL_READ_H_

#include <stdbool.h>

void CellRead_Start(void);
bool CellRead_Step(void);   // true = acabou este ciclo

#endif /* ADBMS_CELL_READ_H_ */
