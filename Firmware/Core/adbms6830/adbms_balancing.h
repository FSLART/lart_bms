/*
 * adbms_balancing.h
 *
 * Balanceamento passivo (DCC on/off), decidido uma vez por ciclo de 200 ms.
 * Maquina de estados em bms.balancing.stage:
 *   OFF -> ROUGH (so celulas > 100 mV acima do alvo) -> FINE -> END -> OFF
 * Alvo = celula minima congelada no arranque. O CONFIG de cada ciclo poe os
 * DCC a 0 antes do ADCV e do open-wire; o Balancing_Step volta a escrever a
 * mascara a seguir, por isso descarrega durante o AUX e o resto do ciclo.
 */

#ifndef ADBMS_BALANCING_H_
#define ADBMS_BALANCING_H_

void Balancing_Step(void);   // uma vez por ciclo, depois do open-wire
void Balancing_Stop(void);   // mascaras a zero ja; o proximo CONFIG desliga os DCC no chip

#endif /* ADBMS_BALANCING_H_ */
