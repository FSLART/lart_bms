/*
 * board.h
 *
 * Medidas da propria placa master pelo ADC1 do F412 (DMA, trigger do TIM3):
 * temperatura do MCU, VDDA (pelo VREFINT) e a corrente do master (MCS1802 no
 * PC4). Se o DMA parar de completar buffers -> mcu_adc_error.
 *
 * Dono de: bms.board e faults.mcu_adc_error.
 */

#ifndef APP_BOARD_H_
#define APP_BOARD_H_

#include "brain.h"

void Board_Init(void);
void Board_Step(uint32_t now_ms);

#endif /* APP_BOARD_H_ */
