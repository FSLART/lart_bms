/*
 * board.c
 *
 * Ver board.h. Contas em inteiros com as calibracoes de fabrica.
 */

#include "board.h"
#include "bsp.h"

#define BOARD_PERIOD_MS     100u
#define ADC_STALL_MS        500u    // o DMA completa um buffer a cada ~33 ms
#define ADC_FULL_SCALE      4095
#define CAL_VDDA_MV         3300    // as calibracoes de fabrica sao a 3,3 V

void Board_Init(void) {

	Bsp_AdcStart();
	bms.board.adc_seen = Bsp_AdcConversions();
	bms.board.adc_checked_ms = HAL_GetTick();
	bms.board.updated_ms = 0;
}

void Board_Step(uint32_t now_ms) {

	bms_board_t *board = &bms.board;

	if ((now_ms - board->updated_ms) < BOARD_PERIOD_MS) {
		return;
	}
	board->updated_ms = now_ms;

	// o DMA parou?
	board->adc_conversions = Bsp_AdcConversions();
	if (board->adc_conversions != board->adc_seen) {
		board->adc_seen = board->adc_conversions;
		board->adc_checked_ms = now_ms;
		bms.faults.mcu_adc_error = false;
	} else if ((now_ms - board->adc_checked_ms) > ADC_STALL_MS) {
		bms.faults.mcu_adc_error = true;
	}

	uint16_t temp_raw;
	uint16_t vref_raw;
	uint16_t current_raw;

	if (!Bsp_AdcAverage(&temp_raw, &vref_raw, &current_raw)) {
		return;
	}

	int32_t vdda_mV = (CAL_VDDA_MV * (int32_t) Bsp_VrefintCal()) / vref_raw;
	board->vdda_mV = (uint16_t) vdda_mV;

	// sensor de temperatura: a calibracao e a 3,3 V, por isso escala-se pela VDDA real
	int32_t temp_at_cal = ((int32_t) temp_raw * vdda_mV) / CAL_VDDA_MV;
	int32_t cal1 = Bsp_TsCal1();
	int32_t cal2 = Bsp_TsCal2();

	if (cal2 > cal1) {
		board->mcu_temp_dC = (int16_t) (300 + ((temp_at_cal - cal1) * 800) / (cal2 - cal1));
	}

	// MCS1802: zero a VDDA/2, sensibilidade da config
	int32_t vout_mV = ((int32_t) current_raw * vdda_mV) / ADC_FULL_SCALE;
	int32_t current_mA = ((vout_mV - (vdda_mV / 2)) * 1000) / (int32_t) bms.config.board_current_mV_per_A + bms.config.board_current_offset_mA;

	board->current_mA = (uint16_t) ((current_mA < 0) ? 0 : ((current_mA > 65535) ? 65535 : current_mA));
}
