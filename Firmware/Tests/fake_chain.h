/*
 * fake_chain.h - cadeia ADBMS6822 -> ADBMS6830B simulada, para os testes no PC.
 *
 * Substitui o driver da ADI (adBmsReadData/WriteData, ADCV...) e o mcuWrapper:
 * responde como a cadeia verdadeira e deixa injetar falhas. O resto (brain.c,
 * adBms_Application.c e os sub-modulos) e o codigo real.
 */

#ifndef FAKE_CHAIN_H
#define FAKE_CHAIN_H

#include "adBms_Application.h"

typedef struct {
	uint8_t  present;                                       // slaves que respondem
	bool     asleep;                                        // ninguem responde ate receber pulsos de CS
	uint8_t  wake_pulses;                                   // flancos -1 do CS desde o reset
	uint8_t  wake_pulses_needed[BMS_MAX_SLAVES];            // slave lento a acordar (0 = acordado)
	int32_t  cell_mV[BMS_MAX_SLAVES][BMS_MAX_CELLS];
	bool     cell_sentinel[BMS_MAX_SLAVES][BMS_MAX_CELLS];  // registo a 0x8000
	uint8_t  ow_pct[BMS_MAX_SLAVES][BMS_MAX_CELLS][2];      // S-ADC com OW [par, impar], % da celula
	int32_t  gpio_mV[BMS_MAX_SLAVES][BMS_MAX_NTCS];         // AUX
	int32_t  gpio2_offset_mV[BMS_MAX_SLAVES][BMS_MAX_NTCS]; // AUX2 - AUX
	int32_t  vref2_mV[BMS_MAX_SLAVES];
	int32_t  die_dC[BMS_MAX_SLAVES];
	uint16_t cs_flt[BMS_MAX_SLAVES];
	bool     spiflt[BMS_MAX_SLAVES];
	TYPE     pec_type;                                      // leituras onde se injeta PEC errado
	uint8_t  pec_fail_reads[BMS_MAX_SLAVES];                // as proximas N leituras desse tipo falham
	bool     pec_always[BMS_MAX_SLAVES];
	bool     config_corrupt[BMS_MAX_SLAVES];                // CFGA lido != escrito
	cfa_     cfga[BMS_MAX_SLAVES];                          // o que esta escrito em cada chip
	cfb_     cfgb[BMS_MAX_SLAVES];
	OW_C_S   ow_mode;                                       // ultimo ADSV
	uint32_t cfgb_writes;
	uint32_t clrflag_writes;
	uint32_t conversions_with_dcc;                          // ADCV/ADSV com algum DCC ligado (medicao estragada)
} fake_chain_t;

extern fake_chain_t chain;
extern uint32_t fake_now_us;

void fake_reset(void);                         // cadeia saudavel: 12 slaves, 3700 mV, 25 C
int32_t fake_ntc_mV(double temp_c);            // tensao no GPIO para essa temperatura (VREF2 = 3 V)
void run_ms(uint32_t ms);                      // brain_loop em passos de 100 us, tick de 200 ms

extern TIM_HandleTypeDef htim6;                // no firmware vem do main.c (CubeMX)

#endif
