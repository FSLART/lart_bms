/*
 * precharge.h
 *
 * Sequencia de precarga e vigia dos contactores com a HV ligada. Maquina em
 * bms.precharge.state (estados = VAL_ do DBC, 0x702):
 *
 *   KILL --pedido--> RX_CAN -> START -> OPEN_ALL -> SWITCH_HVNEG
 *     -> WAIT_FOR_AIR_NEG_TO_CLOSE (precharge_air_wait_ms) -> CHECKING_AIR_NEG_IS_CLOSED
 *     -> SWITCH_PRECHARGE -> WAIT_FOR_PRECHARGE_TO_CLOSE -> CHECKING_PRECHARGE_IS_CLOSED
 *     -> VERIFY_BUS_VOLT (bus >= precharge_bus_percentage % das celulas, ou so tempo)
 *     -> VERIFY_CURRENT (|I| <= precharge_end_current_mA, se ligado)
 *     -> SWITCH_HVPOS -> WAIT_FOR_AIR_POS_TO_CLOSE -> CHECKING_AIR_POS_IS_CLOSED
 *     -> TURN_OFF_PRECHARGE -> WAIT_FOR_PRECHARGE_TO_OPEN -> CHECKING_PRECHARGE_IS_OPEN
 *     -> HV_ON
 *   qualquer check falhado -> WRONG -> KILL
 *
 * Pedidos: VCU no CAN1 (0x083, por nivel: 1 arranca, 0 mata) ou o charger
 * (bms.precharge.handcart_start / kill_request, pelo switch do handcart).
 * Os comandos dos contactores sao funcao so do estado; o contactors.c e que
 * escreve nos pinos. AMS_ERROR ou SDC aberto -> KILL sem fault de mismatch.
 *
 * Dono de: bms.precharge (menos os feedbacks), faults.contactor_mismatch,
 * precharge_timeout, precharge_failure e vcu_timeout.
 */

#ifndef APP_PRECHARGE_H_
#define APP_PRECHARGE_H_

#include "brain.h"

#define PRECHARGE_SOURCE_NONE     0u
#define PRECHARGE_SOURCE_VCU      1u
#define PRECHARGE_SOURCE_HANDCART 2u

// bits do expected_mask / actual_mask e do contactor_check_mask
#define CONTACTOR_BIT_AIR_NEG 0x01u
#define CONTACTOR_BIT_PRE     0x02u
#define CONTACTOR_BIT_AIR_POS 0x04u
#define CONTACTOR_BIT_DISCH   0x08u

void Precharge_Init(uint32_t now_ms);
void Precharge_Step(uint32_t now_ms);

void Precharge_OnVcuRequest(uint8_t request, uint32_t now_ms);   // trama 0x083
void Precharge_OnVcuAlive(uint32_t now_ms);                      // 0x750 VCU_STATES

const char* Precharge_StateName(precharge_state_t state);

#endif /* APP_PRECHARGE_H_ */
