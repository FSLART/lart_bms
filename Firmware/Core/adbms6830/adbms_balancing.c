/*
 * adbms_balancing.c
 *
 * Ver adbms_balancing.h. Pedidos SPI so atraves do adBms_Application.
 */

#include "adbms_balancing.h"
#include "adBms_Application.h"

#define BAL_ROUGH_MV 100u     // ROUGH: so as celulas mais de 100 mV acima do alvo (menos calor por slave)
#define BAL_HOLD_MS  60000u   // depois de um OT do die, 60 s sem balancear (senao oscila no limite)

static bool usable(uint16_t cell_mV) {

	return (cell_mV >= bms.config.bal_min_cell_mV) && (cell_mV <= bms.config.bal_max_cell_mV);
}

/* So com a HV desligada (precarga em KILL), sem AMS_ERROR e com medicoes boas
 * de todos os slaves: nunca descarregar as cegas */
static bool allowed(void) {

	return (bms.precharge.state == KILL) && !bms.ams_error.active && !bms.faults.adbms_timeout && !bms.faults.slave_missing && !bms.faults.cell_open_wire
			&& !bms.faults.die_overtemperature && !bms.balancing.die_ot_hold;
}

/* menor celula utilizavel e delta max-min, so com celulas dentro da janela */
static uint16_t usable_spread(uint16_t *lowest_cell_mV) {

	uint16_t lowest_mV = UINT16_MAX;
	uint16_t highest_mV = 0;

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {
		for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {

			uint16_t cell_mV = bms.cells.cell_mV[slave_index][cell_index];

			if (usable(cell_mV)) {
				lowest_mV = (cell_mV < lowest_mV) ? cell_mV : lowest_mV;
				highest_mV = (cell_mV > highest_mV) ? cell_mV : highest_mV;
			}
		}
	}

	*lowest_cell_mV = lowest_mV;
	return (highest_mV > lowest_mV) ? (uint16_t) (highest_mV - lowest_mV) : 0u;
}

/* Mascaras deste ciclo: descarrega as celulas acima de alvo + limiar */
static void compute(uint16_t threshold_mV) {

	bms_balancing_t *balancing = &bms.balancing;
	uint16_t discharging_counter = 0;

	balancing->worst_delta_mV = 0;
	balancing->worst_slave = 0;
	balancing->worst_cell = 0;

	for (uint8_t slave_index = 0; slave_index < BMS_MAX_SLAVES; slave_index++) {

		uint16_t dcc_mask = 0;

		for (uint8_t cell_index = 0; (slave_index < bms.cells.slaves_found) && (cell_index < bms.config.cells_per_slave); cell_index++) {

			uint16_t cell_mV = bms.cells.cell_mV[slave_index][cell_index];

			if (!usable(cell_mV) || (cell_mV <= balancing->target_mV)) {
				continue;
			}

			uint16_t above_target_mV = (uint16_t) (cell_mV - balancing->target_mV);

			if (above_target_mV > balancing->worst_delta_mV) {
				balancing->worst_delta_mV = above_target_mV;
				balancing->worst_slave = (uint8_t) (slave_index + 1u);
				balancing->worst_cell = (uint8_t) (cell_index + 1u);
			}
			if (above_target_mV > threshold_mV) {
				dcc_mask |= (uint16_t) (1u << cell_index);
				discharging_counter++;
			}
		}

		balancing->dcc_mask[slave_index] = dcc_mask;
	}

	balancing->cells_discharging = (uint8_t) ((discharging_counter > 255u) ? 255u : discharging_counter);
}

static void clear_masks(void) {

	memset(bms.balancing.dcc_mask, 0, sizeof(bms.balancing.dcc_mask));
	bms.balancing.cells_discharging = 0;
}

void Balancing_Stop(void) {

	clear_masks();
	bms.balancing.stage = BALANCING_OFF;
}

void Balancing_Step(void) {

	bms_balancing_t *balancing = &bms.balancing;
	uint32_t now_ms = HAL_GetTick();
	uint16_t stop_mV = (uint16_t) (bms.config.bal_delta_mV / 2u);   // acaba a metade do delta de arranque (histerese)

	// OT do die: parar e esperar que arrefeca
	if (bms.faults.die_overtemperature) {
		balancing->die_ot_hold = true;
		balancing->hold_since_ms = now_ms;
	} else if (balancing->die_ot_hold && ((now_ms - balancing->hold_since_ms) >= BAL_HOLD_MS)) {
		balancing->die_ot_hold = false;
	}

	switch (balancing->stage) {

	case BALANCING_OFF: {
		uint16_t lowest_cell_mV;

		if (allowed() && (usable_spread(&lowest_cell_mV) > bms.config.bal_delta_mV)) {
			balancing->target_mV = lowest_cell_mV;   // congelado: com o minimo vivo nunca convergia (testado)
			balancing->started_ms = now_ms;
			balancing->stage = BALANCING_ROUGH;
		}
		break;
	}

	case BALANCING_ROUGH:
	case BALANCING_FINE:
		if (!allowed()) {
			balancing->stage = BALANCING_END;
		}
		break;

	case BALANCING_END:
	default:
		balancing->stage = BALANCING_OFF;
		break;
	}

	if (balancing->stage == BALANCING_ROUGH) {
		compute(BAL_ROUGH_MV);
		if (balancing->worst_delta_mV <= BAL_ROUGH_MV) {
			balancing->stage = BALANCING_FINE;
		}
	}
	if (balancing->stage == BALANCING_FINE) {
		compute(stop_mV);
		if (balancing->worst_delta_mV <= stop_mV) {
			balancing->stage = BALANCING_END;
		}
	}
	if ((balancing->stage != BALANCING_ROUGH) && (balancing->stage != BALANCING_FINE)) {
		clear_masks();
	}

	// o CONFIG deste ciclo ja deixou os DCC a 0: so e preciso escrever para ligar
	if (balancing->cells_discharging > 0u) {

		if (!ADBMS_WriteDcc()) {
			// estado da descarga desconhecido: parar e tentar escrever zeros
			Balancing_Stop();
			(void) ADBMS_WriteDcc();
		}
	}
}
