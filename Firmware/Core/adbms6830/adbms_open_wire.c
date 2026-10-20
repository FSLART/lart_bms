/*
 * adbms_open_wire.c
 *
 */

#include "adbms_open_wire.h"
#include "adBms_Application.h"
#include <limits.h>

#define OW_CONV_US      20000u   // menos de 20 ms dava fake fault de openwire
#define OW_MIN_GAIN_PCT 70       // gain during open wire detection - ta no datsheet n faço puto de ideia
#define OW_PASS_INVALID INT_MIN  // pec error, n fazer evaluation

// guarda cada leitura
static void save_pass(bool odd_pass) {

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {

		cell_asic *chip = &IC[slave_index];

		for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {

			int pass_code = (chip->cccrc.scell_pec != 0u) ? OW_PASS_INVALID : chip->scell.sc_codes[cell_index];

			if (odd_pass) {
				chip->owcell.cell_ow_odd[cell_index] = pass_code;
			} else {
				chip->owcell.cell_ow_even[cell_index] = pass_code;
			}
		}
	}
}

// check out the openwire readings
static void evaluate(void) {

	bool any_open_wire = false;

	for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {

		const cell_asic *chip = &IC[slave_index];
		bool usable = (chip->cccrc.cell_pec == 0u) && bms.adbms.config_ok[slave_index];

		for (uint8_t cell_index = 0; cell_index < bms.config.cells_per_slave; cell_index++) {

			int even_code = chip->owcell.cell_ow_even[cell_index];
			int odd_code = chip->owcell.cell_ow_odd[cell_index];
			int16_t cell_code = chip->cell.c_codes[cell_index];

			// sem medicao de referencia valida fica o resultado anterior
			if (usable && (even_code != OW_PASS_INVALID) && (odd_code != OW_PASS_INVALID) && ADBMS_CodeValid(cell_code)) {

				int32_t cell_mV = ADBMS_CodeToMv(cell_code);
				int32_t even_mV = ADBMS_CodeToMv((int16_t) even_code);
				int32_t odd_mV = ADBMS_CodeToMv((int16_t) odd_code);
				int32_t lowest_pass_mV = (even_mV < odd_mV) ? even_mV : odd_mV;

				bms.cells.cell_open_wire[slave_index][cell_index] = (cell_mV < bms.config.cell_open_wire_mV)
						|| ((lowest_pass_mV * 100) < (cell_mV * OW_MIN_GAIN_PCT));
			}

			any_open_wire = any_open_wire || bms.cells.cell_open_wire[slave_index][cell_index];
		}
	}

	// FS 2027 EV 5.8 - openwire ams erro trigger
	bms.faults.cell_open_wire = any_open_wire;
}

void OpenWire_Start(void) {

	bms.adbms.open_wire = OPEN_WIRE_START;
}

bool OpenWire_Step(void) {

	uint32_t now_us = adBmsMicros();
	uint32_t waited_us = now_us - bms.adbms.step_us;

	switch (bms.adbms.open_wire) {

	case OPEN_WIRE_START:
		ADBMS_StartOpenWire(OW_ON_EVEN_CH);
		bms.adbms.step_us = now_us;
		bms.adbms.open_wire = OPEN_WIRE_WAIT_EVEN;
		return false;

	case OPEN_WIRE_WAIT_EVEN:
		if (waited_us < OW_CONV_US) {
			return false;
		}
		(void) ADBMS_ReadSCells();
		save_pass(false);
		ADBMS_StartOpenWire(OW_ON_ODD_CH);
		bms.adbms.step_us = adBmsMicros();   // conta a partir do ADSV, nao do inicio da leitura
		bms.adbms.open_wire = OPEN_WIRE_WAIT_ODD;
		return false;

	case OPEN_WIRE_WAIT_ODD:
		if (waited_us < OW_CONV_US) {
			return false;
		}
		(void) ADBMS_ReadSCells();
		save_pass(true);
		evaluate();
		bms.adbms.open_wire = OPEN_WIRE_DONE;
		return true;

	case OPEN_WIRE_DONE:
		return true;

	default:
		bms.adbms.open_wire = OPEN_WIRE_DONE;
		return true;
	}
}
