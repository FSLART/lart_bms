/*
 * ams_error.h
 *
 * Linha AMS_ERROR (PC8). Este modulo e o UNICO que escreve no pino, e e o
 * unico leitor de bms.faults que decide: le todos os faults, filtra pelos que
 * abrem a linha (config ams_error_enable + os obrigatorios da FS 2027) e
 * escreve o pino em cada brain_loop.
 *
 *   - arranca em ERRO (fail-safe) e so passa a OK depois da primeira leitura
 *     completa da cadeia (bms.system.state sai de STARTUP)
 *   - todas as fontes sao limpaveis: quem segura o SDC aberto ate ao reset
 *     manual e o latch em hardware (FS 2027 EV 6.1.6); aqui a linha segue a
 *     condicao real, com um tempo minimo em erro (ams_error_min_on_ms)
 *
 * Dono de: bms.ams_error.
 */

#ifndef APP_AMS_ERROR_H_
#define APP_AMS_ERROR_H_

#include "brain.h"

void AmsError_Init(uint32_t now_ms);
void AmsError_Step(uint32_t now_ms);
uint64_t AmsError_EnabledMask(void);   // config + obrigatorios

#endif /* APP_AMS_ERROR_H_ */
