/*
 * soc.h
 *
 * Estado de carga: semeado pela curva OCV da config (celula minima, com a
 * compensacao por temperatura e a queda na resistencia interna), depois
 * contagem de coulombs com a corrente da ISA do pack. Com o pack em repouso
 * (|I| < soc_rest_current_mA durante soc_rest_s) volta a ancorar na curva.
 *
 * Dono de: bms.soc e faults.soc_low.
 */

#ifndef APP_SOC_H_
#define APP_SOC_H_

#include "brain.h"

void Soc_Init(void);
void Soc_Step(uint32_t now_ms);

// puros, usados tambem nos testes
uint16_t Soc_PercentFromOcv(const bms_config_t *config, uint16_t ocv_mV);                 // % x100
uint16_t Soc_EstimateOcv(const bms_config_t *config, uint16_t cell_mV, int32_t pack_current_mA, int16_t temp_dC);

#endif /* APP_SOC_H_ */
