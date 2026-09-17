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
 * Fontes de AMS_ERROR ligadas/desligadas
 *
 * 1 = a fonte dispara a linha AMS_ERROR
 * 0 = a fonte deixa de disparar. O fault CONTINUA a ser reportado no fault
 *     manager, no CAN e no live_debug -- so nao actua na linha.
 *
 * Estado (2026-09-11):
 *   ligadas    -> sobretensao, sobretemperatura, colapso de celula (open wire)
 *   desligadas -> subtensao, timeout da ISA do pack, timeout do handcart
 * --------------------------------------------------------------------------- */
#define AMS_ERR_SRC_OVERVOLTAGE       1   /* SAFETY_CELL_OV_V, adbms_to_CAN.c */
#define AMS_ERR_SRC_OVERTEMPERATURE   1   /* SAFETY_CELL_OT_C, adbms_to_CAN.c */
#define AMS_ERR_SRC_OW_VOLTAGE        1   /* celula < SAFETY_CELL_OW_V (colapsada) */
#define AMS_ERR_SRC_UNDERVOLTAGE      0   /* SAFETY_CELL_UV_V */
#define AMS_ERR_SRC_ISA_TIMEOUT       0   /* ISA do pack, CAN1 */
#define AMS_ERR_SRC_HANDCART_TIMEOUT  0   /* 0x084 do handcart, CAN2 */

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
