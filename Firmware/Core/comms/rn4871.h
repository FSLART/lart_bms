/*
 * rn4871.h
 *
 * Supervisor do modulo BLE RN4871 no UART2 (so TX/RX ligados, sem RST_N nem
 * RTS/CTS). Maquina em bms.bt.state:
 *
 *   BOOT (espera o modulo) -> ENTER_CMD ("$$$") -> GET_NAME ("GN")
 *     -> SET_NAME ("SN,<bt_name>" + reboot, so se o nome for diferente)
 *     -> EXIT_CMD ("---") -> DATA
 *   DATA: UART transparente. Os %...% do modulo (%CONNECT%, %DISCONNECT%,
 *     %REBOOT%...) sao tratados aqui; o resto vai para a consola
 *   a cada bt_probe_s: PROBE_ENTER ("$$$" -> "CMD>") -> PROBE_EXIT ("---")
 *     sem resposta -> REBOOT ("$$$" + "R,1" as cegas)
 *   bt_reboot_s > 0: reboot periodico as cegas (o comportamento antigo)
 *
 * Fora de DATA nada do BMS vai para o UART2: o modulo leria o JSON como
 * comandos. E tudo o que sai e filtrado de '$' (um "$$$" no meio do texto
 * metia o modulo em modo de comando).
 *
 * Dono de: bms.bt.
 */

#ifndef COMMS_RN4871_H_
#define COMMS_RN4871_H_

#include "brain.h"

void Rn4871_Init(uint32_t now_ms);
void Rn4871_Step(uint32_t now_ms);                         // le o UART2 e avanca a maquina
bool Rn4871_DataMode(void);                                // pode-se escrever dados
bool Rn4871_Write(const char *text, uint16_t length);      // so em DATA; tudo-ou-nada
void Rn4871_RequestReboot(void);                           // comando "bt reboot" da consola
const char* Rn4871_StateName(bt_state_t state);

#endif /* COMMS_RN4871_H_ */
