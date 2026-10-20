/*
 * adBms_Application.h
 */

#ifndef __APPLICATION_H
#define __APPLICATION_H

#include "adbms_main.h"
#include "brain.h"

//cada ciclo a 200ms o que da um coeficimente de seguranca de 2.5
#define ADBMS_CYCLE_MS 200u

// tempo maximo com dados fudidos dos slave
#define ADBMS_DATA_TIMEOUT_MS 600u

// raw results from the spi buffer
extern cell_asic IC[BMS_MAX_SLAVES];

//se a config n for valida, o init da return false
bool ADBMS_Init(void);
void ADBMS_Run(void);
void ADBMS_Tick(void);   // apartir HAL_TIM_PeriodElapsedCallback do TIM6

// pedidos aos 6830
void ADBMS_StartCells(void);
bool ADBMS_ReadCells(void);
void ADBMS_StartAux(void);
bool ADBMS_ReadAuxAndStatus(void);
void ADBMS_StartOpenWire(OW_C_S channels);
bool ADBMS_ReadSCells(void);
bool ADBMS_WriteDcc(void);

// converter bits adc para mv
static inline int32_t ADBMS_CodeToMv(int16_t code)
{
	return 1500 + ((int32_t) code * 3) / 20;
}

// 0x7FFF = nunca convertido (arranque/sleep), 0x8000 = registo limpo
static inline bool ADBMS_CodeValid(int16_t code)
{
	return (code != INT16_MAX) && (code != INT16_MIN);
}

// dados sao fresh, nao old
static inline bool ADBMS_Fresh(uint32_t valid_ms, uint32_t now)
{
	return (valid_ms != 0u) && ((now - valid_ms) <= ADBMS_DATA_TIMEOUT_MS);
}

#endif
