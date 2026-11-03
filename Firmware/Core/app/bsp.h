/*
 * bsp.h
 *
 * O unico sitio que sabe que pino e qual (para alem do CubeMX). A logica dos
 * modulos chama estas funcoes e nunca o HAL diretamente, por isso compila e
 * testa no PC com um bsp falso (Tests/fake_bsp.c).
 *
 * Polaridades da placa master v2:
 *   contactores (PB0 AIR+, PB1 AIR-, PC5 PRE): RESET = fechado, SET = aberto
 *   AMS_ERROR (PC8): SET = ERRO, RESET = OK
 *   feedbacks (PC10 AIR+, PC11 AIR-, PA15 PRE, PC12 descarga, PC7 SDC): alto = fechado
 */

#ifndef APP_BSP_H_
#define APP_BSP_H_

#include <stdbool.h>
#include <stdint.h>

// bits de Bsp_ReadInputs(); a ordem e a de bms.precharge.fb_edges[]
#define BSP_IN_AIR_POS 0x01u
#define BSP_IN_AIR_NEG 0x02u
#define BSP_IN_PRE     0x04u
#define BSP_IN_DISCH   0x08u
#define BSP_IN_SDC     0x10u
#define BSP_IN_COUNT   5u

uint8_t Bsp_ReadInputs(void);                                    // nivel atual, BSP_IN_*
uint8_t Bsp_InputFromPin(uint16_t gpio_pin);                     // pino do EXTI -> BSP_IN_*, 0 = outro
void Bsp_SetContactors(bool air_pos, bool air_neg, bool precharge);   // true = fechar
void Bsp_SetAmsError(bool error);
void Bsp_ToggleHeartbeat(void);

void Bsp_FanInit(uint16_t pwm_hz);
void Bsp_SetFanPwm(uint8_t pwm);                                 // 0..255, ventoinha das celulas

// ADC1: TEMP, VREFINT, PC4 (corrente do master), por DMA a partir do TIM3
void Bsp_AdcStart(void);
bool Bsp_AdcAverage(uint16_t *temp_raw, uint16_t *vref_raw, uint16_t *current_raw);
uint32_t Bsp_AdcConversions(void);                               // cresce a cada buffer completo
uint16_t Bsp_VrefintCal(void);                                   // calibracao de fabrica (3,3 V, 30 C)
uint16_t Bsp_TsCal1(void);                                       // sensor a 30 C
uint16_t Bsp_TsCal2(void);                                       // sensor a 110 C

// EEPROM 24FC08 no I2C1: 1 KB, paginas de 16 B, 5 ms por escrita
#define BSP_EEPROM_SIZE      1024u
#define BSP_EEPROM_PAGE      16u
bool Bsp_EepromPresent(void);
bool Bsp_EepromRead(uint16_t address, uint8_t *data, uint16_t length);
bool Bsp_EepromWritePage(uint16_t address, const uint8_t *data, uint8_t length);   // nao passa de uma pagina
bool Bsp_EepromReady(void);                                     // acabou a escrita interna
void Bsp_EepromRecover(void);                                   // reinicia o I2C depois de um erro

void Bsp_SystemReset(void);
void Bsp_JumpToBootloader(void);

#endif /* APP_BSP_H_ */
