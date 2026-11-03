/*
 * isa.h
 *
 * Duas ISA IVT-S com os MESMOS IDs (0x521..0x528) em barramentos diferentes:
 * a do pack no CAN1 (alimenta o SOC, a sobrecorrente e a precarga) e a do
 * handcart no CAN2 (corrente de carga). Nunca misturar: cada barramento
 * entrega as tramas a sua bms_isa_t.
 *
 * A "vida" de uma ISA conta so a trama da CORRENTE (0x521): com as outras a
 * chegar e a corrente calada, o valor congelado ja nao vale.
 *
 * Dono de: bms.isa_pack, bms.isa_handcart, bms.accu (menos voltage_sum_mV),
 * faults.isa_pack_timeout, isa_pack_error, overcurrent, pack_voltage_mismatch.
 */

#ifndef APP_ISA_H_
#define APP_ISA_H_

#include "brain.h"

#define ISA_ERROR_OCS         0x01u   // byte 1 bit 4: sobrecorrente vista pela propria ISA
#define ISA_ERROR_CHANNEL     0x02u
#define ISA_ERROR_MEASUREMENT 0x04u
#define ISA_ERROR_SYSTEM      0x08u

void Isa_Init(void);
bool Isa_OnFrame(bms_isa_t *isa, bool invert, uint32_t id, const uint8_t *data, uint8_t dlc, uint32_t now_ms);   // false = nao e da ISA
void Isa_Step(uint32_t now_ms);

#endif /* APP_ISA_H_ */
