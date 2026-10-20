/*
 * adbms_open_wire.h
 *
 * Open-wire dos fios de sense das celulas, uma vez por ciclo de 200 ms.
 * Maquina de estados em bms.adbms.open_wire:
 *   START -> WAIT_EVEN (ADSV, OW nos canais pares) -> WAIT_ODD -> DONE
 * Escreve bms.cells.cell_open_wire e bms.faults.cell_open_wire.
 * Os NTC abertos ou em curto sao detetados na leitura (adbms_cell_read).
 */

#ifndef ADBMS_OPEN_WIRE_H_
#define ADBMS_OPEN_WIRE_H_

#include <stdbool.h>

void OpenWire_Start(void);
bool OpenWire_Step(void);   // true = acabou este ciclo

#endif /* ADBMS_OPEN_WIRE_H_ */
