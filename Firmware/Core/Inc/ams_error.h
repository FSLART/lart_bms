/*
 * ams_error.h
 *
 *  Created on: Jul 19, 2026
 *      Author: jpser
 */

#ifndef INC_AMS_ERROR_H_
#define INC_AMS_ERROR_H_

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * BANCADA: fontes de AMS_ERROR ligadas/desligadas
 *
 * 1 = a fonte dispara a linha AMS_ERROR
 * 0 = a fonte deixa de disparar (o fault continua a ser reportado no
 *     fault manager e no dashboard, so nao actua na linha)
 *
 * 2026-09-09: so a sobretemperatura fica activa, a pedido, para trabalho de
 * bancada. POR TUDO A 1 OUTRA VEZ ANTES DE LEVAR O CARRO A PISTA.
 * --------------------------------------------------------------------------- */
#define AMS_ERR_SRC_OVERTEMPERATURE   1   /* UNICA proteccao activa */
#define AMS_ERR_SRC_OVERVOLTAGE       0
#define AMS_ERR_SRC_UNDERVOLTAGE      0
#define AMS_ERR_SRC_OPENWIRE          0   /* open wire de celula e de NTC */
#define AMS_ERR_SRC_ISA_TIMEOUT       0   /* ISA do pack, CAN1 (powertrain) */
#define AMS_ERR_SRC_HANDCART_TIMEOUT  0   /* 0x084 do handcart, CAN2 */
#define AMS_ERR_SRC_PEC_FLOOD         0
#define AMS_ERR_SRC_SLAVE_COUNT       0
#define AMS_ERR_SRC_CAN_QUEUE_FULL    0

/* Deixado A 1 de proposito: nao e' uma proteccao de celula, e' o check de
 * integridade dos contactores da maquina de precarga, que de qualquer forma
 * ja manda a sequencia para KILL. Por a 0 se tambem se quiser mudo. */
#define AMS_ERR_SRC_CONTACTOR_MISMATCH 1

/* dois niveis apenas:
 * - Trigger/Clear        -> erro clearable (limpa em runtime)
 * - TriggerLatched       -> erro permanente, so um power cycle limpa */
void AMS_Error_Init(void);
void AMS_Error_Trigger(void);
void AMS_Error_TriggerLatched(void);
void AMS_Error_Clear(void);
uint8_t AMS_Error_IsActive(void);
uint8_t AMS_Error_IsPermanent(void);

#endif /* INC_AMS_ERROR_H_ */
