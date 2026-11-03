/*
 * can_messages.h
 *
 * Protocolo CAN pelo DBC (powertrain_t26 e handcart_t26). O unico sitio com
 * os simbolos do DBC: os modulos recebem valores ja desempacotados.
 *
 * RX, CAN1: 0x083 START_PRE_CHARGE (VCU), 0x750 VCU_STATES (VCU viva),
 *           0x521..0x528 ISA do pack, 0x0DC START_PROGRAMMER
 * RX, CAN2: 0x084 HANDCART_SWITCH_FEEDBACK, 0x18FF50E5 CHARGER_STATUS_P1000,
 *           0x521..0x528 ISA do handcart, 0x0DC START_PROGRAMMER
 * TX, CAN1 (e CAN2 a carregar, se can2_mirror_charging):
 *   a cada can_master_ms: 0x701 MSC_1, 0x702 PRE_CHARGE, 0x703 MSC_2 (faults),
 *   0x704 MSC_3, 0x705 SOC, 0x706 MSC_4 (balanceamento, um slave de cada vez),
 *   0x709 AMS_SDC_FEEDBACK
 *   a cada can_slaves_ms: 0x600..0x653, 7 tramas por slave (ate 12 no DBC)
 * TX, CAN2: 0x1806E5F4 BMS_CHARGING_REQUEST_P1000 a cada charge_command_ms
 *   com o handcart presente (Control = 0 so no estado CHARGING)
 */

#ifndef COMMS_CAN_MESSAGES_H_
#define COMMS_CAN_MESSAGES_H_

#include "brain.h"

void CanMessages_Init(void);
void CanMessages_Process(uint32_t now_ms);    // RX dos dois barramentos
void CanMessages_Transmit(uint32_t now_ms);   // periodicos
bool CanMessages_TakeBootloaderRequest(void); // 0x0DC com programming_request = 1 (consome o pedido)

#endif /* COMMS_CAN_MESSAGES_H_ */
