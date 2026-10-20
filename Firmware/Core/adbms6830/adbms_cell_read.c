/*
 * adbms_cell_read.c
 *
 * Ver adbms_cell_read.h. Pedidos SPI so atraves do adBms_Application.
 */

#include "adbms_cell_read.h"
#include "adBms_Application.h"

#define CELL_CONV_US          12000u   // ADCV com RD=1 e CONT=0: 8 ms (+ margem), ADBMS6830B datasheet Rev.0
#define CELL_MAX_PLAUSIBLE_MV 5000     // o C-ADC vai ate 5,5 V; acima disto nao e uma celula (FS 2027 T 11.9.2c)

/* So se todas as celulas do slave passam, o slave e publicado */
static bool cells_ok(uint8_t slave_index) {

	const cell_asic *chip = &IC[slave_index];

	if (!bms.adbms.config_ok[slave_index] || (chip->cccrc.cell_pec != 0u) || (chip->cccrc.stat_pec != 0u)) {
		return false;
	}

	/* SPIFLT: os dois SPI internos discordam. TMODCHK: modo de teste, nao
	 * confiar. THSD/SLEEP: o ADBMS reiniciou depois do CONFIG deste ciclo */
	if (chip->statc.spiflt || chip->statc.tmodchk || chip->statc.thsd || chip->statc.sleep) {
		return false;
	}

	for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {

		int16_t cell_code = chip->cell.c_codes[cell_index];
		int32_t cell_mV = ADBMS_CodeToMv(cell_code);

		if (!ADBMS_CodeValid(cell_code) || (cell_mV < 0) || (cell_mV > CELL_MAX_PLAUSIBLE_MV)) {
			return false;
		}

		// C-ADC e S-ADC discordam mais que o CTH
		if (((chip->statc.cs_flt >> cell_index) & 1u) != 0u) {
			return false;
		}
	}

	return true;
}

static void publish(uint32_t now_ms) {

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {

		if (!cells_ok(slave_index)) {
			continue;   // fica o ultimo valor bom; o adbms_timeout trata se durar
		}

		for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {
			bms.cells.cell_mV[slave_index][cell_index] = (uint16_t) ADBMS_CodeToMv(IC[slave_index].cell.c_codes[cell_index]);
		}
		bms.adbms.cells_valid_ms[slave_index] = now_ms;
	}
}

/* Resumo do pack e OV/UV. FS 2027 EV 5.8: tensao critica durante 500 ms abre
 * o SDC; aqui basta uma amostra (200 ms), menos de metade do tempo */
static void evaluate(uint32_t now_ms) {

	bms_cells_t *cells = &bms.cells;
	uint32_t sum_mV = 0;
	uint16_t cell_counter = 0;
	uint16_t highest_mV = 0;
	uint16_t lowest_mV = UINT16_MAX;
	bool overvoltage = false;
	bool undervoltage = false;

	for (uint8_t slave_index = 0; slave_index < cells->slaves_found; slave_index++) {

		if (!ADBMS_Fresh(bms.adbms.cells_valid_ms[slave_index], now_ms)) {
			continue;
		}

		for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {

			uint16_t cell_mV = cells->cell_mV[slave_index][cell_index];

			sum_mV += cell_mV;
			cell_counter++;

			if (cell_mV > highest_mV) {
				highest_mV = cell_mV;
				cells->cell_max_slave = (uint8_t) (slave_index + 1u);
				cells->cell_max_cell = (uint8_t) (cell_index + 1u);
			}
			if (cell_mV < lowest_mV) {
				lowest_mV = cell_mV;
				cells->cell_min_slave = (uint8_t) (slave_index + 1u);
				cells->cell_min_cell = (uint8_t) (cell_index + 1u);
			}

			if (cell_mV > bms.config.cell_max_mV) {
				overvoltage = true;
			}
			if (cell_mV < bms.config.cell_min_mV) {
				undervoltage = true;
			}
		}
	}

	if (cell_counter > 0u) {
		cells->cell_max_mV = highest_mV;
		cells->cell_min_mV = lowest_mV;
		cells->cell_avg_mV = (uint16_t) (sum_mV / cell_counter);
		cells->cell_delta_mV = (uint16_t) (highest_mV - lowest_mV);
		cells->updated_ms = now_ms;
		bms.accu.voltage_sum_mV = sum_mV;
	}

	bms.faults.cell_overvoltage = overvoltage;
	bms.faults.cell_undervoltage = undervoltage;
}

void CellRead_Start(void) {

	bms.adbms.cell_read = CELL_READ_START;
}

bool CellRead_Step(void) {

	uint32_t now_us = adBmsMicros();

	switch (bms.adbms.cell_read) {

	case CELL_READ_START:
		ADBMS_StartCells();
		bms.adbms.step_us = adBmsMicros();
		bms.adbms.cell_read = CELL_READ_WAIT;
		return false;

	case CELL_READ_WAIT:
		if ((now_us - bms.adbms.step_us) < CELL_CONV_US) {
			return false;
		}
		(void) ADBMS_ReadCells();   // slaves com PEC errado ficam marcados e nao sao publicados
		publish(HAL_GetTick());
		evaluate(HAL_GetTick());
		bms.adbms.cell_read = CELL_READ_DONE;
		return true;

	case CELL_READ_DONE:
		return true;

	default:
		// estado corrompido: acaba o ciclo; os dados ficam velhos e o timeout trata
		bms.adbms.cell_read = CELL_READ_DONE;
		return true;
	}
}
