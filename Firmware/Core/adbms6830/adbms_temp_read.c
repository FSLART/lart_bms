/*
 * adbms_temp_read.c
 *
 * Ver adbms_temp_read.h. Pedidos SPI so atraves do adBms_Application.
 */

#include "adbms_temp_read.h"
#include "adBms_Application.h"
#include <math.h>

/* ADAX2 aos 10 GPIOs a 110 Hz min: 91 ms; o ADAX (18 canais a 0,9 kHz) acaba
 * em 20 ms. ADBMS6830B datasheet Rev.0, Tabelas 3 e 4 */
#define AUX_CONV_US 100000u

/* NTC para o V-, resistencia fixa para o VREF2: da config (ntc_beta,
 * ntc_r25_ohm, ntc_r_fixed_ohm); defaults Amphenol NKA502C1, 5 k a 25 C,
 * B 3977, com 10 k para o VREF2 */

/* Plausibilidade: FS 2027 T 11.9.2c, sinal fora de gama e uma falha */
#define VREF2_MIN_MV 2950   // datasheet: 2,994..3,006 V
#define VREF2_MAX_MV 3050
#define NTC_MIN_DC   -400   // fora de -40..120 C = NTC aberto ou em curto
#define NTC_MAX_DC   1200
#define DIE_MIN_DC   -400
#define DIE_MAX_DC   1500
// AUX vs AUX2 do mesmo GPIO: config ntc_aux_match_mV (20 = 2,8 + 6 mV de erro maximo + ruido)

/* FS 2027 EV 5.8: temperatura critica durante 1 s abre o SDC. Pior caso aqui:
 * um ciclo + deteccao/config/celulas/open-wire (~80 ms) + a conversao do AUX2 */
_Static_assert((ADBMS_CYCLE_MS + 80u + (AUX_CONV_US / 1000u)) <= 500u, "temperatura avaliada em menos de metade de 1 s");

static bool ntc_disabled(uint8_t slave_index, uint8_t ntc_index) {

	return ((bms.config.ntc_disabled_mask[slave_index] >> ntc_index) & 1u) != 0u;
}

/* NTC em baixo e resistencia fixa para o VREF2 (medido no proprio ADBMS, por
 * isso a conta e ratiometrica). false = aberto, em curto ou fora de gama */
static bool ntc_to_dC(int32_t gpio_mV, int32_t vref_mV, int16_t *temperature_dC) {

	if ((gpio_mV <= 0) || (gpio_mV >= vref_mV)) {
		return false;
	}

	float ntc_ohm = (float) bms.config.ntc_r_fixed_ohm * (float) gpio_mV / (float) (vref_mV - gpio_mV);
	float kelvin = 1.0f / ((1.0f / 298.15f) + (logf(ntc_ohm / (float) bms.config.ntc_r25_ohm) / (float) bms.config.ntc_beta));
	long decicelsius = lroundf((kelvin - 273.15f) * 10.0f);

	if ((decicelsius < NTC_MIN_DC) || (decicelsius > NTC_MAX_DC)) {
		return false;
	}

	*temperature_dC = (int16_t) decicelsius;
	return true;
}

/* registos de temperatura do slave e VREF2 dentro do datasheet */
static bool registers_ok(uint8_t slave_index, int32_t *vref_mV) {

	const cell_asic *chip = &IC[slave_index];
	int16_t vref_code = (int16_t) chip->stata.vref2;

	if (!bms.adbms.config_ok[slave_index] || (chip->cccrc.aux_pec != 0u) || (chip->cccrc.raux_pec != 0u) || (chip->cccrc.stat_pec != 0u)) {
		return false;
	}

	*vref_mV = ADBMS_CodeToMv(vref_code);
	return ADBMS_CodeValid(vref_code) && (*vref_mV >= VREF2_MIN_MV) && (*vref_mV <= VREF2_MAX_MV);
}

static void publish(uint32_t now_ms) {

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {

		int32_t vref_mV;
		int16_t die_code = (int16_t) IC[slave_index].stata.itmp;
		int32_t die_dC = (ADBMS_CodeToMv(die_code) * 4) / 3 - 2730;   // (V / 7,5 mV/C) - 273 C, em 0,1 C

		if (!registers_ok(slave_index, &vref_mV) || !ADBMS_CodeValid(die_code) || (die_dC < DIE_MIN_DC) || (die_dC > DIE_MAX_DC)) {
			continue;   // fica o ultimo valor bom; o adbms_timeout trata se durar
		}

		bms.cells.die_dC[slave_index] = (int16_t) die_dC;

		for (uint8_t ntc_index = 0; ntc_index < bms.config.ntcs_per_slave; ntc_index++) {

			if (ntc_disabled(slave_index, ntc_index)) {
				bms.cells.ntc_open_wire[slave_index][ntc_index] = false;
				continue;
			}

			/* o mesmo GPIO medido pelo AUX e pelo AUX2, caminhos separados; o
			 * datasheet deixa a comparacao para o host */
			int16_t aux_code = IC[slave_index].aux.a_codes[ntc_index];
			int16_t aux2_code = IC[slave_index].raux.ra_codes[ntc_index];
			int32_t aux_difference_mV = ADBMS_CodeToMv(aux_code) - ADBMS_CodeToMv(aux2_code);
			int16_t temperature_dC = 0;

			if (aux_difference_mV < 0) {
				aux_difference_mV = -aux_difference_mV;
			}

			bool sensor_bad = !ADBMS_CodeValid(aux_code) || !ADBMS_CodeValid(aux2_code) || (aux_difference_mV > (int32_t) bms.config.ntc_aux_match_mV)
					|| !ntc_to_dC(ADBMS_CodeToMv(aux_code), vref_mV, &temperature_dC);

			bms.cells.ntc_open_wire[slave_index][ntc_index] = sensor_bad;
			if (!sensor_bad) {
				bms.cells.ntc_dC[slave_index][ntc_index] = temperature_dC;
			}
		}

		bms.adbms.temps_valid_ms[slave_index] = now_ms;
	}
}

/* OT/UT, NTC em falha e die quente. FS 2027 EV 5.8: uma amostra chega */
static void evaluate(uint32_t now_ms) {

	int16_t hottest_dC = INT16_MIN;
	int16_t coldest_dC = INT16_MAX;
	int16_t hottest_die_dC = INT16_MIN;
	uint8_t hottest_slave = 0;
	uint8_t hottest_ntc = 0;
	uint8_t coldest_slave = 0;
	uint8_t coldest_ntc = 0;
	bool any_reading = false;
	bool sensor_fault = false;
	bool die_overtemperature = false;

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {

		if (!ADBMS_Fresh(bms.adbms.temps_valid_ms[slave_index], now_ms)) {
			continue;
		}

		if (bms.cells.die_dC[slave_index] > bms.config.bal_die_stop_dC) {
			die_overtemperature = true;
		}
		if (bms.cells.die_dC[slave_index] > hottest_die_dC) {
			hottest_die_dC = bms.cells.die_dC[slave_index];
		}

		for (uint8_t ntc_index = 0; ntc_index < bms.config.ntcs_per_slave; ntc_index++) {

			if (ntc_disabled(slave_index, ntc_index)) {
				continue;
			}
			if (bms.cells.ntc_open_wire[slave_index][ntc_index]) {
				sensor_fault = true;   // FS 2027 EV 5.8: perder uma medicao abre o SDC
				continue;
			}

			int16_t temperature_dC = bms.cells.ntc_dC[slave_index][ntc_index];
			if (temperature_dC > hottest_dC) {
				hottest_dC = temperature_dC;
				hottest_slave = (uint8_t) (slave_index + 1u);
				hottest_ntc = (uint8_t) (ntc_index + 1u);
			}
			if (temperature_dC < coldest_dC) {
				coldest_dC = temperature_dC;
				coldest_slave = (uint8_t) (slave_index + 1u);
				coldest_ntc = (uint8_t) (ntc_index + 1u);
			}
			any_reading = true;
		}
	}

	if (any_reading) {
		bms.cells.temp_max_dC = hottest_dC;
		bms.cells.temp_min_dC = coldest_dC;
		bms.cells.temp_max_slave = hottest_slave;
		bms.cells.temp_max_ntc = hottest_ntc;
		bms.cells.temp_min_slave = coldest_slave;
		bms.cells.temp_min_ntc = coldest_ntc;
	}
	if (hottest_die_dC != INT16_MIN) {
		bms.cells.die_max_dC = hottest_die_dC;
	}

	bms.faults.overtemperature = any_reading && (hottest_dC > bms.config.temp_max_dC);
	bms.faults.undertemperature = any_reading && (coldest_dC < bms.config.temp_min_dC);
	bms.faults.temperature_warning = any_reading && ((hottest_dC > bms.config.temp_high_warn_dC) || (coldest_dC < bms.config.temp_low_warn_dC));
	bms.faults.ntc_open_wire = sensor_fault;
	bms.faults.die_overtemperature = die_overtemperature;
}

void TempRead_Start(void) {

	bms.adbms.temp_read = TEMP_READ_START;
}

bool TempRead_Step(void) {

	uint32_t now_us = adBmsMicros();

	switch (bms.adbms.temp_read) {

	case TEMP_READ_START:
		ADBMS_StartAux();
		bms.adbms.step_us = adBmsMicros();
		bms.adbms.temp_read = TEMP_READ_WAIT;
		return false;

	case TEMP_READ_WAIT:
		if ((now_us - bms.adbms.step_us) < AUX_CONV_US) {
			return false;
		}
		(void) ADBMS_ReadAuxAndStatus();   // slaves com PEC errado ficam marcados e nao sao publicados
		publish(HAL_GetTick());
		evaluate(HAL_GetTick());
		bms.adbms.temp_read = TEMP_READ_DONE;
		return true;

	case TEMP_READ_DONE:
		return true;

	default:
		// estado corrompido: acaba o ciclo; os dados ficam velhos e o timeout trata
		bms.adbms.temp_read = TEMP_READ_DONE;
		return true;
	}
}
