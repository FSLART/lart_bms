/*
 * bl_can.h
 *
 * bxCAN do F412 em polling: CAN1 (PA11/PA12) e CAN2 (PB5/PB13), so recebe
 * tramas standard de dados com o ID dos pedidos (0x602). 16 TQ, ponto de
 * amostragem a 87,5 %, como o can_bus.c da aplicacao.
 */

#ifndef BL_CAN_H_
#define BL_CAN_H_

#include <stdbool.h>
#include <stdint.h>

#define BL_CAN1 0u
#define BL_CAN2 1u

// false = o periferico nao respondeu; com o bus desligado devolve true e entra quando o bus aparecer
bool BlCan_Init(uint8_t bus, uint16_t kbps, uint32_t pclk1_hz, uint32_t request_id);
bool BlCan_Synced(uint8_t bus);   // ja viu o bus (saiu do modo de init)

bool BlCan_Receive(uint8_t bus, uint32_t *id, uint8_t data[8], uint8_t *dlc);

// so envia se ficarem mais de keep_free caixas livres (1 para o heartbeat e o diagnostico: guarda uma para as respostas)
bool BlCan_Send(uint8_t bus, uint32_t id, const uint8_t data[8], uint8_t keep_free, uint32_t *mailbox);
bool BlCan_TxPending(uint8_t bus, uint32_t mailbox);
void BlCan_Abort(uint8_t bus, uint32_t mailbox);
uint8_t BlCan_FreeMailboxes(uint8_t bus);
uint32_t BlCan_ErrorStatus(uint8_t bus);   // CAN_ESR (contadores e ultimo erro)

#endif /* BL_CAN_H_ */
