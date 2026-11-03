/*
 * can_bus.h
 *
 * Driver dos dois bxCAN:
 *   CAN1 = powertrain (VCU, ISA do pack, telemetria)
 *   CAN2 = carregamento (handcart, carregador EV Europe, ISA do handcart)
 *
 * - bitrate da config (can1_kbps/can2_kbps), 16 TQ e ponto de amostragem a
 *   87,5 % (o CubeMX tinha 37,5 %, fragil com cabos compridos)
 * - filtros por hardware em modo lista: so entram os IDs que o BMS usa
 * - RX: a interrupcao so copia a trama para um ring; o main loop drena
 * - TX: duas filas por barramento (alta: master, precarga, carregador;
 *   baixa: telemetria dos slaves), servidas so no main loop. Uma mailbox
 *   presa mais de 1 s (ninguem da ACK) e abortada
 *
 * Dono de: bms.can1, bms.can2, faults.can1_bus_off, can2_bus_off,
 * can_tx_queue_full.
 */

#ifndef COMMS_CAN_BUS_H_
#define COMMS_CAN_BUS_H_

#include "brain.h"

#define CAN_BUS_1     0u
#define CAN_BUS_2     1u
#define CAN_BUS_COUNT 2u

typedef struct {
	uint32_t id;
	bool     extended;    // 29 bits
	uint8_t  dlc;
	uint8_t  data[8];
} can_frame_t;

void CanBus_Init(void);
bool CanBus_Send(uint8_t bus, const can_frame_t *frame, bool high_priority);   // false = fila cheia
bool CanBus_Receive(uint8_t bus, can_frame_t *frame);                          // false = nada
void CanBus_Service(uint32_t now_ms, bool can2_required);                      // filas -> mailboxes, erros
uint16_t CanBus_FreeSlots(uint8_t bus, bool high_priority);

#endif /* COMMS_CAN_BUS_H_ */
