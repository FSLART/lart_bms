/*
 * charger.h
 *
 * Carregamento no handcart: switch do handcart (0x084, CAN2), carregador
 * EV Europe OEM3 650-6 em protocolo P1000 (0x1806E5F4 / 0x18FF50E5, CAN2) e a
 * ISA do handcart (CAN2) para a corrente de carga. Maquina em
 * bms.charger.state:
 *
 *   switch OFF->ON (com a precarga em KILL) pede a precarga
 *   WAIT_HV --HV_ON + ISA do handcart bate com a soma das celulas--> PRESTART_STOP
 *   PRESTART_STOP (charge_prestart_ms com Control=1) --> CHARGING
 *   CHARGING --celula max >= cell_high_warn_mV ou switch OFF--> STOPPING
 *   STOPPING --|I| < charge_open_mA ou charge_settle_ms--> abre os contactores -> DONE
 *   cortes de seguranca (SDC, ISA calada, temperatura, corrente, carregador
 *   calado ou em falha, AMS_ERROR) --> abre ja -> DONE
 *   DONE e um latch: so recomeca com o switch OFF->ON
 *
 * O pedido ao carregador (bms.charger.req_*) e enviado pelo can_messages.c.
 * Para abrir os contactores pede bms.precharge.kill_request; para arrancar,
 * bms.precharge.handcart_start. Nao chama a precarga diretamente.
 *
 * Dono de: bms.charger, faults.handcart_timeout, charger_timeout, charger_fault.
 */

#ifndef APP_CHARGER_H_
#define APP_CHARGER_H_

#include "brain.h"

typedef struct {
	uint16_t output_voltage_dV;
	uint16_t output_current_dA;
	bool     hw_failure;
	bool     temp_otp;
	bool     input_voltage_fault;
	bool     starting_state_fault;
	bool     comm_timeout;
	int16_t  temperature_C;
} charger_status_t;

void Charger_Init(void);
void Charger_OnSwitch(bool switch_on, uint32_t now_ms);                       // 0x084
void Charger_OnStatus(const charger_status_t *status, uint32_t now_ms);       // 0x18FF50E5
void Charger_Step(uint32_t now_ms);
bool Charger_SessionActive(void);                                             // ha um carregamento em curso

const char* Charger_StateName(charger_state_t state);
const char* Charger_EndName(charge_end_t reason);

#endif /* APP_CHARGER_H_ */
