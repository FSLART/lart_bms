/*
 * cell_balancing.c
 *
 *  Created on: Mar 18, 2026
 *      Author: jpser
 */

#include "cell_balancing.h"
#include <string.h>
#include "uartDMA.h"

uint16_t cell_code_to_mV(int16_t code) {

    int32_t mv = ((int32_t)code + 10000) * 150 / 1000;   // same as *0.15 mV

	//Safe aproach i guess
    if (mv < 0) {
        mv = 0;
    }
    if (mv > 65535) {
        mv = 65535;
    }

    return (uint16_t)mv;
}

bool is_cell_odd_or_even(uint8_t cell) {
	// Verificação se o número dea célula é par ou ímpar
	uint8_t cell_number = cell + 1;
	if ((cell_number % 2) != 0) //pelo resto da divisão
			{
		return true;   // odd cell
	} else {
		return false;  // even cell
	}
}

uint8_t keep_pwm_within_value(uint32_t pwm) {
	//como é unsigned int, n há valores inferior a 0 e como o pwm max da analgo é definido aqui BALANCING_PWM_MAX
	if (pwm > BALANCING_PWM_MAX) {
		return BALANCING_PWM_MAX;
	}
	return (uint8_t) pwm;
}

uint16_t BatteryPack_FindMinVoltageGlobally(const cell_asic *ic_array, uint8_t total_ic, const balance_config_t *cfg) {

	if ((ic_array == 0) || (cfg == 0) || (total_ic == 0)) {
		return 0xFFFF;
	}

	uint16_t global_min_mV = 0xFFFF;
	bool valid_found = false;

	for (uint8_t m = 0; m < total_ic; m++) {
		for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
			//int16_t code = cfg->use_filtered_cells ? ic_array[m].scell.sc_codes[i] : ic_array[m].scell.sc_codes[i]; // Filtered not implmemnnted
			int16_t code = ic_array[m].cell.c_codes[i]; // Filtered not implmemnnted

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

void Balance_InitDefaultConfig(balance_config_t *cfg) {
	if (cfg == 0) {
		return;
	}

	cfg->deadband_mV = 2; // a deadband é a variação que se ignora, devido as oscilações imprevíosivies na leitua dos adcs
	cfg->fullscale_mV = 30; // Ganho linear máximo pasra o colocar o pwm ao máximo, define o declive da equação que define o pwm para cada célula
	cfg->min_cell_mV = 3000; //proteger célulass danificadas, ignorar balanceamento em células abaixo deste valor
	cfg->max_cell_mV = 4200; //sanity check, acima desta tensão considerar unsafe o balanceamento
	cfg->min_pwm_to_enable = 1; //mínimo pwm a ser considero, se valor da equação dera valor inferiroir a 1, o pwm vai para 0
	cfg->use_filtered_cells = false; //utlizar canal de leitura com filtro digital interno do adbms6830, NOT WORKINGGGGGG

	/* Equação pro PWM (controlo linear proporcional - sem feedback):
	 *
	 *   PWM = ((delta_mV - deadband_mV) / (fullscale_mV - deadband_mV)) * 15
	 *
	 *   onde:
	 *   delta_mV = Vcell_mV - Vtarget_mV
	 *   Vtarget_mV = tensão mínima global do pack
	 *
	 */
}

void Balance_ComputeModule(const cell_asic *ic, const balance_config_t *cfg, balance_result_t *out, uint16_t global_min_mV) {

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

		//ESCOLHER QUE TIPO DE CANAL SE VAI FAZER A LEITURA DAS TENSÕES       nÃO IMPLEMENTADO YET
		//int16_t code = cfg->use_filtered_cells ? ic->scell.sc_codes[i] : ic->scell.sc_codes[i];
		int16_t code = ic->cell.c_codes[i];

		//adc pra mv
		uint16_t cell_in_mv = cell_code_to_mV(code);

		//guardar o resultado
		out->cell_mV[i] = cell_in_mv;

		//VALIDAÇÃO DA MEDICÇÃO
		if ((cell_in_mv >= cfg->min_cell_mV) && (cell_in_mv <= cfg->max_cell_mV)) {
			valid_found = true;
		}
	}

	//Encontrar a tensão mínima dentro do slave
	if (!valid_found) {
		out->balancing_allowed = false;
		out->active_parity = BALANCE_PARITY_NONE;
		return;
	}

	//sanity check
	if (global_min_mV == 0xFFFF) {
		out->balancing_allowed = false;
		out->active_parity = BALANCE_PARITY_NONE;
		return;
	}

	out->target_mV = global_min_mV;

	// Optional: do not balance if module has obvious fault flags
	/*if (ic->statc.cs_flt || ic->statc.vde || ic->statc.vdel) {
	 out->balancing_allowed = false;
	 out->active_parity = BALANCE_PARITY_NONE;
	 return;
	 }*/

	// 2) Compute raw deltas and PWM candidates
	uint32_t odd_total_delta = 0;
	uint32_t even_total_delta = 0;

	for (uint8_t i = 0U; i < BALANCING_CELL_COUNT; i++) {
		uint16_t cell_mV = out->cell_mV[i];

		if ((cell_mV < cfg->min_cell_mV) || (cell_mV > cfg->max_cell_mV)) {
			out->delta_mV[i] = 0;
			out->pwm[i] = 0;
			continue;
		}

		uint16_t delta_mV = 0U;
		if (cell_mV > global_min_mV) {
			delta_mV = (uint16_t) (cell_mV - global_min_mV);
		}

		out->delta_mV[i] = delta_mV;

		uint8_t pwm = 0;

		if (delta_mV > cfg->deadband_mV) {
			uint32_t numerator = (uint32_t) (delta_mV - cfg->deadband_mV) * BALANCING_PWM_MAX;
			uint32_t denominator = (cfg->fullscale_mV > cfg->deadband_mV) ? (cfg->fullscale_mV - cfg->deadband_mV) : 1U;

			pwm = keep_pwm_within_value((numerator + (denominator / 2U)) / denominator);

			if (pwm < cfg->min_pwm_to_enable) {
				pwm = 0;
			}
		}

		out->pwm[i] = pwm;

		if (is_cell_odd_or_even(i)) {
			odd_total_delta += delta_mV;
		} else {
			even_total_delta += delta_mV;
		}
	}

	if ((odd_total_delta == 0) && (even_total_delta == 0)) {
		out->balancing_allowed = false;
		out->active_parity = BALANCE_PARITY_NONE;
		memset(out->pwm, 0, sizeof(out->pwm));
		out->dcc_mask = 0;
		return;
	}

	out->balancing_allowed = true;
	out->active_parity = BALANCE_PARITY_NONE;
	out->dcc_mask = 0U;
}

void Balance_ApplyToIc(cell_asic *ic, const balance_result_t *result) {
	if ((ic == 0) || (result == 0)) {
		return;
	}

	for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
		ic->PwmA.pwma[i] = result->pwm[i] & 0x0FU;
	}

	//DCC mask
	ic->tx_cfgb.dcc = result->dcc_mask;

	// ---- DEBUG PRINT ----
	printfUI("PWM: ");

	for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
		printfUI("%d", result->pwm[i]);

		if (i < (BALANCING_CELL_COUNT - 1)) {
			printfUI(",");
		}
	}

	printfUI(" | DCC:0x%03X\r\n", result->dcc_mask);
}

void Balance_ForceParity(balance_result_t *result, balance_parity_t forced_parity) {
	if (result == 0) {
		return;
	}

	result->active_parity = forced_parity;
	result->dcc_mask = 0;

	for (uint8_t i = 0; i < BALANCING_CELL_COUNT; i++) {
		bool odd_cell = is_cell_odd_or_even(i);
		bool keep_pwm = false;

		if ((forced_parity == BALANCE_PARITY_ODD) && odd_cell) {
			keep_pwm = true;
		}

		if ((forced_parity == BALANCE_PARITY_EVEN) && (!odd_cell)) {
			keep_pwm = true;
		}

		if (!keep_pwm) {
			result->pwm[i] = 0;
		}

		if (result->pwm[i] > 0) {
			result->dcc_mask |= (uint16_t)(1 << i);
		}
	}
}
