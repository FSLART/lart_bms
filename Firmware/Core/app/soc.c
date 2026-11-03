/*
 * soc.c
 *
 * Ver soc.h. Tudo em inteiros: mA x ms = uAs no acumulador.
 */

#include "soc.h"
#include <string.h>

#define MAX_SAMPLE_GAP_MS 1000u   // buraco maior nas tramas da ISA nao se integra (seria um salto)

void Soc_Init(void) {

	memset(&bms.soc, 0, sizeof(bms.soc));
}

uint16_t Soc_PercentFromOcv(const bms_config_t *config, uint16_t ocv_mV) {

	const uint16_t *curve_mV = config->soc_curve_mV;
	const uint8_t *curve_pct = config->soc_curve_pct;

	if (ocv_mV <= curve_mV[0]) {
		return (uint16_t) (curve_pct[0] * 100u);
	}
	if (ocv_mV >= curve_mV[CFG_SOC_POINTS - 1]) {
		return (uint16_t) (curve_pct[CFG_SOC_POINTS - 1] * 100u);
	}

	for (uint8_t point = 1; point < CFG_SOC_POINTS; point++) {

		if (ocv_mV <= curve_mV[point]) {

			uint32_t span_mV = (uint32_t) (curve_mV[point] - curve_mV[point - 1u]);
			uint32_t span_x100 = (uint32_t) (curve_pct[point] - curve_pct[point - 1u]) * 100u;
			uint32_t offset_mV = (uint32_t) (ocv_mV - curve_mV[point - 1u]);

			return (uint16_t) ((curve_pct[point - 1u] * 100u) + ((span_x100 * offset_mV) / span_mV));
		}
	}

	return 10000u;
}

// interpolacao linear numa tabela de CFG_TEMPCOMP_POINTS pela temperatura
static int32_t tempcomp(const bms_config_t *config, const int16_t *signed_table, const uint16_t *unsigned_table, int16_t temp_dC) {

	const int8_t *temps_C = config->temp_compensation_temp_C;
	int32_t temp_x10 = temp_dC;

#define TABLE_AT(index) ((signed_table != NULL) ? (int32_t) signed_table[index] : (int32_t) unsigned_table[index])

	if (temp_x10 <= (temps_C[0] * 10)) {
		return TABLE_AT(0);
	}
	if (temp_x10 >= (temps_C[CFG_TEMPCOMP_POINTS - 1] * 10)) {
		return TABLE_AT(CFG_TEMPCOMP_POINTS - 1);
	}

	for (uint8_t point = 1; point < CFG_TEMPCOMP_POINTS; point++) {

		int32_t upper_x10 = temps_C[point] * 10;

		if (temp_x10 <= upper_x10) {
			int32_t lower_x10 = temps_C[point - 1u] * 10;
			int32_t lower_value = TABLE_AT(point - 1u);
			int32_t upper_value = TABLE_AT(point);
			return lower_value + ((upper_value - lower_value) * (temp_x10 - lower_x10)) / (upper_x10 - lower_x10);
		}
	}

	return TABLE_AT(CFG_TEMPCOMP_POINTS - 1);
#undef TABLE_AT
}

/* OCV = tensao lida - I x R(T) + ajuste(T). Corrente > 0 = a carregar (a
 * tensao aos terminais fica acima da OCV); por celula = pack / paralelos */
uint16_t Soc_EstimateOcv(const bms_config_t *config, uint16_t cell_mV, int32_t pack_current_mA, int16_t temp_dC) {

	int32_t cell_current_mA = pack_current_mA / (int32_t) ((config->cells_parallel > 0u) ? config->cells_parallel : 1u);
	int32_t resistance_dmOhm = tempcomp(config, NULL, config->temp_compensation_resistance_mOhm, temp_dC);
	int32_t adjust_dmV = tempcomp(config, config->temp_compensation_adjust_mV, NULL, temp_dC);
	int64_t ocv_dmV = ((int64_t) cell_mV * 10) - (((int64_t) cell_current_mA * resistance_dmOhm) / 1000) + adjust_dmV;

	if (ocv_dmV < 0) {
		return 0;
	}
	return (uint16_t) ((ocv_dmV > 655350) ? 65535 : (ocv_dmV / 10));
}

static void anchor(uint32_t now_ms) {

	bms_soc_t *soc = &bms.soc;
	int32_t current_mA = bms.isa_pack.alive ? bms.isa_pack.current_mA : 0;
	int16_t temp_dC = (int16_t) ((bms.cells.temp_max_dC + bms.cells.temp_min_dC) / 2);

	soc->ocv_mV = Soc_EstimateOcv(&bms.config, bms.cells.cell_min_mV, current_mA, temp_dC);
	soc->initial_percent_x100 = Soc_PercentFromOcv(&bms.config, soc->ocv_mV);
	soc->used_mAms = 0;
	soc->used_As = 0;
	soc->rest_since_ms = 0;
	soc->anchors++;
	soc->ready = true;
	(void) now_ms;
}

void Soc_Step(uint32_t now_ms) {

	bms_soc_t *soc = &bms.soc;
	const bms_isa_t *isa = &bms.isa_pack;

	if (!soc->ready) {
		if (bms.cells.cells_valid && bms.cells.temps_valid) {
			anchor(now_ms);
			soc->last_sample_ms = isa->current_rx_ms;
		}
		return;
	}

	// contagem de coulombs: uma vez por trama de corrente nova
	if (isa->alive && (isa->current_rx_ms != soc->last_sample_ms)) {

		uint32_t gap_ms = isa->current_rx_ms - soc->last_sample_ms;

		if ((soc->last_sample_ms != 0u) && (gap_ms <= MAX_SAMPLE_GAP_MS)) {
			soc->used_mAms -= (int64_t) isa->current_mA * gap_ms;   // a carregar gasta-se menos
		}
		soc->last_sample_ms = isa->current_rx_ms;

		// repouso: so com a ISA viva se sabe que nao passa corrente
		if ((isa->current_mA < (int32_t) bms.config.soc_rest_current_mA) && (isa->current_mA > -(int32_t) bms.config.soc_rest_current_mA)) {
			if (soc->rest_since_ms == 0u) {
				soc->rest_since_ms = (now_ms != 0u) ? now_ms : 1u;
			} else if ((bms.config.soc_rest_s > 0u) && ((now_ms - soc->rest_since_ms) >= ((uint32_t) bms.config.soc_rest_s * 1000u))
					&& bms.cells.cells_valid && bms.cells.temps_valid) {
				anchor(now_ms);
			}
		} else {
			soc->rest_since_ms = 0;
		}
	}

	soc->used_As = (int32_t) (soc->used_mAms / 1000000);

	int64_t capacity_uAs = (int64_t) bms.config.capacity_mAh * 3600000;   // 1 mAh = 3,6 As
	int64_t used_x100 = (capacity_uAs > 0) ? ((soc->used_mAms * 10000) / capacity_uAs) : 0;
	int64_t percent_x100 = (int64_t) soc->initial_percent_x100 - used_x100;

	soc->percent_x100 = (uint16_t) ((percent_x100 < 0) ? 0 : ((percent_x100 > 10000) ? 10000 : percent_x100));
	bms.faults.soc_low = soc->percent_x100 < ((uint16_t) bms.config.soc_low_pct * 100u);
}
