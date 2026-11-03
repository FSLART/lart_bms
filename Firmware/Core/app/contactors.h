/*
 * contactors.h
 *
 * Feedbacks dos contactores e do SDC (com debounce) e as saidas dos
 * contactores. Este modulo e o UNICO que escreve nos pinos dos contactores:
 * a precarga so pede (bms.precharge.trigger_*), e aqui decide-se a saida.
 * Com AMS_ERROR ativo ou SDC aberto fica tudo aberto, venha o pedido que vier.
 *
 * Dono de: bms.precharge.*_fb, sdc_closed, inputs_valid, fb_edges,
 * bms.system.hv_safe e faults.sdc_open.
 */

#ifndef APP_CONTACTORS_H_
#define APP_CONTACTORS_H_

#include "brain.h"

void Contactors_Init(uint32_t now_ms);
void Contactors_ReadInputs(uint32_t now_ms);   // inicio do brain_loop
void Contactors_Apply(void);                   // fim do brain_loop, depois da precarga e do AMS_ERROR
void Contactors_OnEdge(uint16_t gpio_pin);     // EXTI: so conta flancos (diagnostico de EMI)

#endif /* APP_CONTACTORS_H_ */
