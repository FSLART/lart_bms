/*
 * bl_hw.h
 *
 * Hardware do bootloader, direto nos registos (sem HAL: tem de caber em
 * 32 KB e nao pode depender do CubeMX). Os pinos sao os do master V2:
 *
 *   PC8   AMS_ERROR       SET = em erro (o bootloader nunca a larga)
 *   PB0   AIR+            SET = aberto
 *   PB1   AIR-            SET = aberto
 *   PC5   pre-carga       SET = aberto
 *   PB10  LED heartbeat
 *   PA11/PA12  CAN1 (powertrain)   PB5/PB13  CAN2 (carregamento)
 *   PA9   USART1 TX (cabo da consola, 230400)
 */

#ifndef BL_HW_H_
#define BL_HW_H_

#include <stdbool.h>
#include <stdint.h>

// contactores abertos e AMS_ERROR em erro: a primeira coisa depois do reset
void BlHw_SafeOutputs(void);

// HSE 8 MHz -> PLL 64 MHz, APB1/APB2 32 MHz (como a aplicacao). false = sem HSE (PLL no HSI, ou so HSI)
bool BlHw_ClockInit(void);
uint32_t BlHw_PclkHz(void);     // APB1 = APB2: 32 MHz com o PLL, 16 MHz sem ele

void BlHw_TimerInit(void);      // TIM2 a contar ms (conta mesmo com o CPU parado num apagar da flash)
uint32_t BlHw_Millis(void);
void BlHw_UartInit(void);
void BlHw_UartWrite(const char *text, uint32_t length);
void BlHw_Led(bool on);

// RCC->CSR do ultimo reset; limpa-o para o proximo arranque ver so o seu
uint32_t BlHw_TakeResetFlags(void);

// IWDG a ~BOOT_IWDG_TIMEOUT_MS (so nas imagens em teste; nao se desliga ate ao proximo reset)
void BlHw_ArmIwdg(void);

void BlHw_Reset(void) __attribute__((noreturn));

// larga os perifericos, repoe o relogio do reset e salta para a aplicacao
void BlHw_Jump(uint32_t vectors) __attribute__((noreturn));

#endif /* BL_HW_H_ */
