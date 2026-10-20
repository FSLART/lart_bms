/*
 * fake_chain.c - ver fake_chain.h.
 */

#include "fake_chain.h"
#include <math.h>
#include <string.h>

fake_chain_t chain;
uint32_t fake_now_us;
uint32_t watchdog_flag;   // no firmware vive no main.c
TIM_TypeDef fake_tim6;
TIM_HandleTypeDef htim6 = { .Instance = &fake_tim6 };

static uint32_t last_tick_us;

/* ================== AJUDAS ================== */

/* inverso de ADBMS_CodeToMv, arredondado para o codigo que volta ao mesmo mV */
static int16_t code_of(int32_t voltage_mV) {

	int32_t scaled_offset = (voltage_mV - 1500) * 20;
	return (int16_t) ((scaled_offset >= 0) ? (scaled_offset + 2) / 3 : (scaled_offset - 2) / 3);
}

int32_t fake_ntc_mV(double temp_c) {

	double ntc_ohm = 5000.0 * exp(3977.0 * ((1.0 / (temp_c + 273.15)) - (1.0 / 298.15)));
	return (int32_t) lround(3000.0 * ntc_ohm / (10000.0 + ntc_ohm));
}

void fake_reset(void) {

	memset(&chain, 0, sizeof(chain));
	chain.present = 12;
	chain.pec_type = Comm;   // nenhuma leitura usada pelo BMS: sem PEC injetado

	for (uint8_t slave_index = 0; slave_index < BMS_MAX_SLAVES; slave_index++) {
		for (uint8_t cell_index = 0; cell_index < BMS_MAX_CELLS; cell_index++) {
			chain.cell_mV[slave_index][cell_index] = 3700;
			chain.ow_pct[slave_index][cell_index][0] = 90;   // datasheet: 85..95 % com o fio bom
			chain.ow_pct[slave_index][cell_index][1] = 90;
		}
		for (uint8_t ntc_index = 0; ntc_index < BMS_MAX_NTCS; ntc_index++) {
			chain.gpio_mV[slave_index][ntc_index] = fake_ntc_mV(25.0);
		}
		chain.vref2_mV[slave_index] = 3000;
		chain.die_dC[slave_index] = 300;
	}

	fake_now_us = 1000000u;   // HAL_GetTick() = 0 seria "nunca houve dados"
	last_tick_us = fake_now_us;

	// TIM6 como no .ioc: 64 MHz / 64000 = 1 kHz, 200 contagens
	memset(&fake_tim6, 0, sizeof(fake_tim6));
	fake_tim6.PSC = 64000u - 1u;
	fake_tim6.ARR = 200u - 1u;
}

void run_ms(uint32_t ms) {

	for (uint32_t step_counter = 0; step_counter < (ms * 10u); step_counter++) {

		fake_now_us += 100u;

		// o caminho do firmware: TIM6_IRQHandler -> HAL -> callback no brain.c
		if ((fake_now_us - last_tick_us) >= 200000u) {
			last_tick_us = fake_now_us;
			HAL_TIM_PeriodElapsedCallback(&htim6);
		}

		brain_loop();
	}
}

static bool alive(uint8_t slave_index) {

	return !chain.asleep && (slave_index < chain.present) && (chain.wake_pulses >= chain.wake_pulses_needed[slave_index]);
}

static uint8_t pec_for(uint8_t slave_index, TYPE type) {

	if (!alive(slave_index)) {
		return 1;
	}
	if (type != chain.pec_type) {
		return 0;
	}
	if (chain.pec_always[slave_index]) {
		return 1;
	}
	if (chain.pec_fail_reads[slave_index] > 0u) {
		chain.pec_fail_reads[slave_index]--;
		return 1;
	}
	return 0;
}

/* ================== DRIVER DA ADI (FALSO) ================== */

void adBmsReadData(uint8_t chip_count, cell_asic *chips, uint8_t cmd_arg[2], TYPE type, GRP group) {

	(void) cmd_arg;

	for (uint8_t slave_index = 0; slave_index < chip_count; slave_index++) {

		cell_asic *chip = &chips[slave_index];
		uint8_t pec_result = pec_for(slave_index, type);

		switch (type) {
		case Sid:
			for (uint8_t byte_index = 0; byte_index < RSID; byte_index++) {
				chip->sid.sid[byte_index] = alive(slave_index) ? (uint8_t) (0x10u + slave_index + byte_index) : 0xFFu;
			}
			chip->cccrc.sid_pec = pec_result;
			break;

		case Config:
			if (group == A) {
				chip->rx_cfga = chain.cfga[slave_index];
				if (chain.config_corrupt[slave_index]) {
					chip->rx_cfga.gpo ^= 1u;
				}
			} else {
				chip->rx_cfgb = chain.cfgb[slave_index];
			}
			chip->cccrc.cfgr_pec = pec_result;
			break;

		case Cell:
			for (uint8_t cell_index = 0; cell_index < CELL; cell_index++) {
				chip->cell.c_codes[cell_index] = chain.cell_sentinel[slave_index][cell_index] ? INT16_MIN : code_of(chain.cell_mV[slave_index][cell_index]);
			}
			chip->cccrc.cell_pec = pec_result;
			break;

		case S_volt:
			for (uint8_t cell_index = 0; cell_index < CELL; cell_index++) {
				uint8_t pass_percent = chain.ow_pct[slave_index][cell_index][(chain.ow_mode == OW_ON_ODD_CH) ? 1 : 0];
				chip->scell.sc_codes[cell_index] = code_of(chain.cell_mV[slave_index][cell_index] * pass_percent / 100);
			}
			chip->cccrc.scell_pec = pec_result;
			break;

		case Aux:
			for (uint8_t ntc_index = 0; ntc_index < BMS_MAX_NTCS; ntc_index++) {
				chip->aux.a_codes[ntc_index] = code_of(chain.gpio_mV[slave_index][ntc_index]);
			}
			chip->cccrc.aux_pec = pec_result;
			break;

		case RAux:
			for (uint8_t ntc_index = 0; ntc_index < BMS_MAX_NTCS; ntc_index++) {
				chip->raux.ra_codes[ntc_index] = code_of(chain.gpio_mV[slave_index][ntc_index] + chain.gpio2_offset_mV[slave_index][ntc_index]);
			}
			chip->cccrc.raux_pec = pec_result;
			break;

		case Status:
			chip->stata.vref2 = (uint16_t) code_of(chain.vref2_mV[slave_index]);
			chip->stata.itmp = (uint16_t) code_of(((chain.die_dC[slave_index] + 2730) * 3) / 4);
			memset(&chip->statc, 0, sizeof(chip->statc));
			chip->statc.cs_flt = chain.cs_flt[slave_index];
			chip->statc.spiflt = chain.spiflt[slave_index];
			chip->cccrc.stat_pec = pec_result;
			break;

		default:
			break;
		}
	}
}

void adBmsWriteData(uint8_t chip_count, cell_asic *chips, uint8_t cmd_arg[2], TYPE type, GRP group) {

	(void) cmd_arg;

	for (uint8_t slave_index = 0; (slave_index < chip_count) && (slave_index < chain.present) && !chain.asleep; slave_index++) {
		if ((type == Config) && (group == A)) {
			chain.cfga[slave_index] = chips[slave_index].tx_cfga;
		} else if ((type == Config) && (group == B)) {
			chain.cfgb[slave_index] = chips[slave_index].tx_cfgb;
			chain.cfgb[slave_index].vov &= 0x0FFFu;   // o chip so guarda 12 bits
			chain.cfgb[slave_index].vuv &= 0x0FFFu;
		}
	}

	if ((type == Config) && (group == B)) {
		chain.cfgb_writes++;
	}
	if (type == Clrflag) {
		chain.clrflag_writes++;
	}
}

/* os DCC tem prioridade sobre o DCP=0 (datasheet Tabela 27): uma conversao das
 * celulas com um switch ligado mede com a corrente de descarga */
static void count_conversion_with_dcc(void) {

	for (uint8_t slave_index = 0; slave_index < chain.present; slave_index++) {
		if (chain.cfgb[slave_index].dcc != 0u) {
			chain.conversions_with_dcc++;
			return;
		}
	}
}

void adBms6830_Adcv(RD rd, CONT cont, DCP dcp, RSTF rstf, OW_C_S owcs) {

	(void) rd;
	(void) cont;
	(void) dcp;
	(void) rstf;
	(void) owcs;
	count_conversion_with_dcc();
}

void adBms6830_Adsv(CONT cont, DCP dcp, OW_C_S owcs) {

	(void) cont;
	(void) dcp;
	chain.ow_mode = owcs;
	count_conversion_with_dcc();
}

void adBms6830_Adax(OW_AUX owaux, PUP pup, CH ch) {

	(void) owaux;
	(void) pup;
	(void) ch;
}

void adBms6830_Adax2(CH ch) {

	(void) ch;
}

uint16_t SetOverVoltageThreshold(float voltage) {

	return (uint16_t) ((voltage - 1.5f) / (16.0f * 0.000150f));
}

uint16_t SetUnderVoltageThreshold(float voltage) {

	return (uint16_t) ((voltage - 1.5f) / (16.0f * 0.000150f));
}

/* ================== mcuWrapper (FALSO) ================== */

void adBmsWakeupIc(uint8_t total_ic) {

	(void) total_ic;
}

/* cada flanco -1 acorda mais um slave a dormir */
void adBmsCsLow(void) {

	if (chain.wake_pulses < UINT8_MAX) {
		chain.wake_pulses++;
	}
	if (chain.asleep && (chain.wake_pulses >= chain.present)) {
		chain.asleep = false;
	}
}

void adBmsCsHigh(void) {
}

uint32_t adBmsMicros(void) {

	return fake_now_us;
}

void adBmsTimerInit(void) {
}

uint32_t adBmsTimerClockHz(void) {

	return 64000000u;
}

uint32_t adBmsSpiErrors(void) {

	return 0u;
}

/* ================== HAL (FALSO) ================== */

uint32_t HAL_GetTick(void) {

	return fake_now_us / 1000u;
}

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim) {

	(void) htim;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim) {

	(void) htim;
	return HAL_OK;
}

void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub) {

	(void) irq;
	(void) preempt;
	(void) sub;
}

void HAL_NVIC_EnableIRQ(int irq) {

	(void) irq;
}
