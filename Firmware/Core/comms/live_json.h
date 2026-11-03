/*
 * live_json.h
 *
 * Uma linha JSON com tudo o que o BMS mede e decide (FS 2027 EV 5.8.15: ver
 * todos os valores a qualquer momento, por exemplo num portatil), no BLE
 * (live_period_ms) e no UART1 (live_uart1_ms). Formato do LIVE_DEBUG_JSON.md
 * do firmware antigo, que a BMS_UI (backend/decode/live_json.py) ja le:
 * as mesmas chaves, so inteiros, a unidade no sufixo. Campos novos so se
 * acrescentam (a UI ignora o que nao conhece).
 *
 * Tambem manda as linhas de evento "AMS_ERROR line -> ERROR" / "-> OK".
 */

#ifndef COMMS_LIVE_JSON_H_
#define COMMS_LIVE_JSON_H_

#include "brain.h"

#define LIVE_JSON_BUFFER 6144u

void LiveJson_Init(void);
void LiveJson_Step(uint32_t now_ms);
uint16_t LiveJson_Build(char *out, uint16_t size, uint8_t console, uint32_t now_ms);   // 0 = nao coube

#endif /* COMMS_LIVE_JSON_H_ */
