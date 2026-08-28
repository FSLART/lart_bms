/*
 * cell_balancing.c
 *
 *  Created on: Mar 18, 2026
 *      Author: jpser
 */

#include "cell_balancing.h"
#include <string.h>

#include "uartDMA.h"
#include "can.h"
#include "brain.h"
#include "dbc/powertrain_t26.h"
#include "adBms_Application.h"


void Balance_InitDefaultConfig(balance_config_t *cfg) {
	if (cfg == 0) {
		return;
	}

	cfg->min_rough_balacing_mV = 100;  //minimum volts to start a rough balacing, of only high volts cells comapred to minimum cell
	cfg->deadband_mV = 8; // a deadband é a variação que se ignora, devido as oscilações imprevíosivies na leitua dos adcs
	cfg->min_cell_mV = 3000; //proteger célulass danificadas, ignorar balanceamento em células abaixo deste valor
	cfg->max_cell_mV = 4250; //sanity check, acima desta tensão considerar unsafe o balanceamento
	cfg->use_filtered_cells = false; //utlizar canal de leitura com filtro digital interno do adbms6830, NOT WORKINGGGGGG

	/*
	 * Lógica DCC:
	 *   delta_mV = Vcell - global_min_mV
	 *
	 *   ROUGH (delta >= min_rough_balacing_mV): bit on
	 *   FINE  (delta >  deadband_mV):           bit on
	 *   END   (todos dentro da deadband):       mask = 0
	 */

}

uint16_t cell_code_to_mV(int16_t code) {

	int32_t mv = ((int32_t) code + 10000) * 150 / 1000;   // same as *0.15 mV

	//Safe aproach i guess
	if (mv < 0) {
		mv = 0;
	}
	if (mv > 65535) {
		mv = 65535;
	}

	return (uint16_t) mv;
}

uint16_t BatteryPack_FindMinVoltageGlobally(const cell_asic *ic_array, uint8_t total_ic, const balance_config_t *cfg) {

	if ((ic_array == 0) || (cfg == 0) || (total_ic == 0)) {
		return 0xFFFF;
	}

	uint16_t global_min_mV = 0xFFFF;
	bool valid_found = false;

	for (uint8_t m = 0; m < total_ic; m++) {
		for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
			/* Registos de média (RDAC): são estes que o ciclo de balanceamento
			 * refresca em BAL_CYCLE_READ_AVG; c_codes ficam obsoletos em BALANCING */
			int16_t code = ic_array[m].acell.ac_codes[i];

			uint16_t cell_mV = cell_code_to_mV(code);

			if ((cell_mV >= cfg->min_cell_mV) && (cell_mV <= cfg->max_cell_mV)) {
				if ((!valid_found) || (cell_mV < global_min_mV)) {
					global_min_mV = cell_mV;
					valid_found = true;
				}
			}
		}
	}

	if (!valid_found) {
		return 0xFFFF;
	}

	return global_min_mV;
}

balance_stage_t BatteryPack_DetermineBalanceStage(const cell_asic *ic_array, uint8_t total_ic, const balance_config_t *cfg, uint16_t global_min_mV) {

	if ((ic_array == 0) || (cfg == 0) || (global_min_mV == 0) || (total_ic == 0U)) {
		return BALANCE_END;
		//TODO: FAULT MANAGMEN
	}

	if (global_min_mV == 0xFFFF) {
		return BALANCE_END;
		//TODO: FAULT MANAGMEN
	}

	bool any_above_deadband = false;
	bool any_above_rough = false;

	for (uint8_t m = 0; m < total_ic; m++) {
		for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
			int16_t code = ic_array[m].acell.ac_codes[i];
			uint16_t cell_mV = cell_code_to_mV(code);

			if ((cell_mV < cfg->min_cell_mV) || (cell_mV > cfg->max_cell_mV)) {
				//skip - might be dangerous
				continue;
			}

			uint16_t delta_mV = 0U;
			if (cell_mV > global_min_mV) {
				delta_mV = (uint16_t) (cell_mV - global_min_mV);
			}

			if (delta_mV > cfg->deadband_mV) {
				any_above_deadband = true;
			}

			if (delta_mV >= cfg->min_rough_balacing_mV) {
				any_above_rough = true;
			}
		}
	}

	if (any_above_rough) {
		return BALANCE_STAGE_ROUGH;
	}

	if (any_above_deadband) {
		return BALANCE_STAGE_FINE;
	}

	return BALANCE_END;
}

void Balance_ComputeModule(const cell_asic *ic, const balance_config_t *cfg, balance_result_t *out, uint16_t global_min_mV, balance_stage_t balancing_stage) {

	//Safety Check
	if ((ic == 0) || (cfg == 0) || (out == 0)) {
		return;
	}

	//Limpa o output, para garantir que n ter valores random
	//ong chat pela dica
	memset(out, 0, sizeof(*out));

	//uint16_t min_cell_in_mV = 0xFFFF; //65535 mV (65V) impedir processamento caso variavel não seja utilizada
	bool valid_found = false; //tracking de pelo menos uma célula que precsiade balanceamanto

	// 1) Ler as tensões das células
	for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {

		//Registos de média (RDAC), refrescados em cada ciclo de balanceamento
		int16_t code = ic->acell.ac_codes[i];

		//adc pra mv
		uint16_t cell_in_mv = cell_code_to_mV(code);

		//guardar o resultado
		out->cell_mV[i] = cell_in_mv;

		//VALIDAÇÃO DA MEDICÇÃO
		if ((cell_in_mv >= cfg->min_cell_mV) && (cell_in_mv <= cfg->max_cell_mV)) {
			valid_found = true;
		}
	}

	//sanity check
	if (!valid_found || (global_min_mV == 0xFFFF)) {
		out->balancing_allowed = false;
		return;
	}
	out->target_mV = global_min_mV;

	// Optional: do not balance if module has obvious fault flags
	/*if (ic->statc.cs_flt || ic->statc.vde || ic->statc.vdel) {
	 out->balancing_allowed = false;
	 out->active_parity = BALANCE_PARITY_NONE;
	 return;
	 }*/

	for (uint8_t i = 0U; i < BALANCING_CELL_COUNT; i++) {
		uint16_t cell_mV = out->cell_mV[i];

		if ((cell_mV < cfg->min_cell_mV) || (cell_mV > cfg->max_cell_mV)) {
			out->delta_mV[i] = 0;
			continue;
		}

		uint16_t delta_mV = 0U;
		if (cell_mV > global_min_mV) {
			delta_mV = (uint16_t) (cell_mV - global_min_mV);
		}

		out->delta_mV[i] = delta_mV;

		if (delta_mV >= cfg->deadband_mV) {

			switch (balancing_stage) { // first step, igore all under 100mv

			case BALANCE_STAGE_ROUGH:

				//TODO: rough balacing in here but with dcc mask
				//TODO: take account min_rough_balacing_mV and retun BALANCE_STAGE_FINE when the delta is infeirior to min_rough_balacing_mV

				if (delta_mV >= cfg->min_rough_balacing_mV) {
					out->dcc_mask |= (uint16_t) (1U << i);
				}

				break;

			case BALANCE_STAGE_FINE:

				//uint16_t cell_mask = (uint16_t) (1U << i);

				// Check if the cell is already active in the DCC mask
				//bbool cell_already_enabled = (out->dcc_mask & cell_mask) != 0U;

				/*if (!cell_already_enabled) {
				 // Add this cell to the list of cells to discharge
				 out->dcc_mask = out->dcc_mask | cell_mask;
				 }*/

				if (delta_mV > cfg->deadband_mV) {
					out->dcc_mask |= (uint16_t) (1 << i);
				}

				break;

			case BALANCE_END:

				//out->dcc_mask = 0;
				// switch the ams starte machine to idle, and resetr all bools and shit related to balacing
				//TODO: return BALANCE_END and handle this on adbms_application

				break;

			default:
				out->dcc_mask = 0;
				// switch the ams starte machine to idle, and resetr all bools and shit related to balacing
				//TODO: return BALANCE_END and handle this on adbms_application
				break;
			}

		}

	}

	if (out->dcc_mask == 0) {
		out->balancing_allowed = false;
		return;
	}

	out->balancing_allowed = true;

}

void Balance_ApplyToIc(cell_asic *ic, const balance_result_t *result, uint16_t global_min_mV, uint8_t ic_index) {
	if ((ic == 0) || (result == 0)) {
		return;
	}

	//DCC mask
	ic->tx_cfgb.dcc = result->dcc_mask;

	// ---- DEBUG PRINT ----
	printfDebug("IC%u | DCC: ", ic_index);

	for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
		uint8_t dcc_on = (result->dcc_mask >> i) & 0x01;
		printfDebug("%d", dcc_on);

		if (i < (BALANCING_CELL_COUNT - 1)) {
			printfDebug(",");
		}
	}

	printfDebug(" | MIN:%umV\r\n", global_min_mV);

	printfDebug("\r\n");
}

void CellBalancing_CAN_Init(void) {
	CAN_RegisterRxCallback(CellBalancing_CAN_Rx);
}

void CellBalancing_CAN_Rx(CAN_RxHeaderTypeDef *hdr, uint8_t *data) {
	if ((hdr == 0) || (data == 0)) {
		return;
	}

	if (hdr->IDE != CAN_ID_STD) {
		return;
	}

	if (hdr->StdId != POWERTRAIN_T26_START_BALANCING_FRAME_ID) {
		return;
	}

	struct powertrain_t26_start_balancing_t msg;

	if (powertrain_t26_start_balancing_unpack(&msg, data, hdr->DLC) < 0) {
		return;
	}

	if (msg.balancing_request == 1) {
		//AMS_State = CHARGING;
		AMS_State = BALANCING;
	} else {
		//AMS_State = STARTUP;
		balanceStage = BALANCE_END;
	}
}
