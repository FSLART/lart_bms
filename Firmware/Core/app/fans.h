/*
 * fans.h
 *
 * Duas ventoinhas com rampa linear e histerese (config fan_accu_cells e
 * fan_accu_lv):
 *   - celulas: temperatura maxima dos NTC; TIM12 CH1 (PB14)
 *   - eletronica LV: maior entre o die dos ADBMS e o MCU; saida por definir
 * Sem temperaturas validas ficam no maximo (falha segura). bypass_on (consola)
 * poe a ventoinha no maximo.
 *
 * Dono de: bms.fan_cells e bms.fan_lv (menos bypass_on, que e da consola).
 */

#ifndef APP_FANS_H_
#define APP_FANS_H_

#include "brain.h"

void Fans_Init(void);
void Fans_Step(uint32_t now_ms);
uint8_t Fans_Ramp(const bms_fan_cfg_t *config, int16_t temp_dC, int16_t hysteresis_dC, bool *running);   // 0..255

#endif /* APP_FANS_H_ */
