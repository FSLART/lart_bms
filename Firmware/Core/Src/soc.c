/*
 * soc.c
 *
 *  Created on: Apr 26, 2026
 *      Author: jpser
 *
 * Cells: Molicel INR-21700-P45B (Li-ion NMC, 4.5 Ah, 4.2 V max, 2.7 V min)
 * Pack:  3p144s (3 cells in parallel, 144 in series)
 */

#include "soc.h"
#include "can.h"
#include "adbms_application.h"
#include "uartDMA.h"
#include "dbc/powertrain_t26.h"

/*
 * +1 : the As value goes UP when the pack is being discharged
 * -1 : the As value goes DOWN when the pack is being discharged
 */
#define DISCHARGE_SIGN (-1)

/*
 * Pack capacity:
 * Your pack is 3P, so capacity is 3 * 4.5Ah = 13.5Ah.
 * The number of series cells does NOT multiply Ah capacity.
 */
#define SOC_CELL_CAPACITY_AH     4.5
#define SOC_PARALLEL_CELLS       3.0

// SOC lookup table para INR-21700-P45B @25ºC acho
typedef struct {
	uint16_t cell_mV;
	float soc_percent;
} cell_voltage_point_t;

cell_voltage_point_t cell_voltage_table[] = { { 2500, 0 }, { 3000, 1 }, { 3200, 2 }, { 3300, 4 }, { 3400, 8 }, { 3500, 14 }, { 3550, 20 }, { 3600, 27 }, { 3650, 35 }, { 3700, 43 }, { 3750, 52 }, { 3800, 60 }, { 3850, 68 }, { 3900, 74 }, { 3950, 80 }, { 4000, 85 }, { 4050, 90 }, { 4100, 94 }, { 4150, 97 }, { 4200, 100 } };

//macro de merda que ochatgpt fez,
#define CELL_VOLTAGE_TABLE_LEN  (sizeof(cell_voltage_table) / sizeof(cell_voltage_table[0]))

//SOC incial dps de chamar SOC_Init()
float soc_initial_percent = 0;

//SSOC atual
float soc_now_percent = 0;

//ISA Ah primeira medicao
int32_t as_baseline = 0;

//ISA Ah ultima medicao
int32_t as_latest = 0;

//baseline flG qnd obtida
bool baseline_valid = false;

//SOC_Init() chamado?
bool init_done = false;

//returns negative As when the pack has charged and vice verse
int32_t SOC_GetDischarged_As(int32_t as_now) {
	int32_t delta_as = as_now - as_baseline;

	return (int32_t) (DISCHARGE_SIGN * delta_as);
}

//Cell voltage in mV to a SOC percentage
float CellVoltageToSoc(uint16_t cell_mV) {

	/* below the table -> treat as empty */
	if (cell_mV <= cell_voltage_table[0].cell_mV) {
		return cell_voltage_table[0].soc_percent;
	}

	/* above the table -> treat as full */
	if (cell_mV >= cell_voltage_table[CELL_VOLTAGE_TABLE_LEN - 1].cell_mV) {
		return cell_voltage_table[CELL_VOLTAGE_TABLE_LEN - 1].soc_percent;
	}

	/* find the segment that contains cell_mV and interpolate */
	for (uint8_t i = 0; i < CELL_VOLTAGE_TABLE_LEN - 1; i++) {

		uint16_t v_low = cell_voltage_table[i].cell_mV;
		uint16_t v_high = cell_voltage_table[i + 1].cell_mV;

		if (cell_mV >= v_low && cell_mV <= v_high) {

			float soc_low = cell_voltage_table[i].soc_percent;
			float soc_high = cell_voltage_table[i + 1].soc_percent;

			float voltage_span = (float) (v_high - v_low);
			float soc_span = soc_high - soc_low;

			float ratio = (float) (cell_mV - v_low) / voltage_span;
			return soc_low + ratio * soc_span;
		}
	}

	/* should never reach here, but be safe */
	return 0;
}

//dar clamp ao valor
float ClampPercent(float value) {

	if (value < 0) {
		return 0;
	}
	if (value > 100) {
		return 100;
	}
	return value;
}

void SOC_Init(uint16_t min_cell_mV) {

	soc_initial_percent = CellVoltageToSoc(min_cell_mV);
	soc_now_percent = soc_initial_percent;

	/* baseline will be captured on the first IVT As reading */
	baseline_valid = false;
	as_baseline = 0;
	as_latest = 0;

	init_done = true;

	printfDebug("SOC init: minCell=%u mV -> SOC=%.1f%%\r\n", min_cell_mV, soc_initial_percent);
}

void SOC_NotifyAsReading(int32_t as_now) {

	// 3 celulas paralelo * 4.5 Ah / cell = 13.5 Ah
	//float pack_capacity_Ah = slaves_found * ( 3 * 4.5);
	const float pack_capacity_Ah = SOC_PARALLEL_CELLS * SOC_CELL_CAPACITY_AH;

	const float pack_capacity_As = pack_capacity_Ah * 3600;

	if (pack_capacity_As <= 0) {
		return;
	}

	/* if SOC was never initialised we can still track delta later */
	as_latest = as_now;

	/* first message after init or after an IVT reset:
	 * use this reading as the new zero point */
	if (!baseline_valid) {
		as_baseline = as_now;
		baseline_valid = true;
		return;
	}

	/* charge moved since the baseline, in Ampere-seconds */
	//int32_t delta_as = as_now - as_baseline;
	/* turn it into "charge taken out of the pack" (positive when discharging) */
	//float used_as = (float) (DISCHARGE_SIGN * delta_as);
	/* SOC drops as we drain the pack */
	//float drop_percent = (used_as / pack_capacity_As) * 100;
	//soc_now_percent = ClampPercent(soc_initial_percent - drop_percent);
	//Send tyo CAN
	//SOC_SendCAN(soc_now_percent);
	/*
	 * discharged_as:
	 *   positive = energy removed from pack  -> SOC decreases
	 *   negative = energy added to pack      -> SOC increases
	 */
	int32_t discharged_as = SOC_GetDischarged_As(as_now);

	float soc_change_percent = ((float) discharged_as / pack_capacity_As) * 100;

	/*
	 * If discharged_as is positive:
	 *   soc_now = initial - positive  -> SOC goes down
	 *
	 * If discharged_as is negative:
	 *   soc_now = initial - negative  -> SOC goes up
	 */
	soc_now_percent = ClampPercent(soc_initial_percent - soc_change_percent);

	SOC_SendCAN(soc_now_percent);
}

void SOC_NotifyIVTReset(void) {

	/* sensor was reset: drop the baseline so the next reading
	 * becomes the new zero. Treat the current SOC as our new
	 * starting point so we keep tracking smoothly. */
	soc_initial_percent = soc_now_percent;
	baseline_valid = false;
	as_baseline = 0;
}

float SOC_GetPercent(void) {
	return soc_now_percent;
}

int32_t SOC_GetUsedCharge_As(void) {

	if (!baseline_valid) {
		return 0;
	}
	return SOC_GetDischarged_As(as_latest);
}

bool SOC_IsReady(void) {
	return init_done && baseline_valid;
}

void SOC_DumpUART(void) {

	printfDebug("SOC state:\r\n");
	printfDebug("  init done    : %s\r\n", init_done ? "yes" : "no");
	printfDebug("  baseline ok  : %s\r\n", baseline_valid ? "yes" : "no");
	printfDebug("  initial SOC  : %.2f %%\r\n", soc_initial_percent);
	printfDebug("  current SOC  : %.2f %%\r\n", soc_now_percent);
	printfDebug("  As baseline  : %ld\r\n", as_baseline);
	printfDebug("  As latest    : %ld\r\n", as_latest);
	printfDebug("  charge used  : %ld As\r\n", SOC_GetUsedCharge_As());
}

/* Pack and send the current SOC over CAN.
 *
 * SOC_Integer (byte 0)   – whole-number SOC, 0-100
 * SOC_Float   (bytes 1-2)– precise SOC with 0.01% resolution
 *
 * Encoding formula: raw = (physical - offset) / scale
 *   offset = 0.01, scale = 0.01
 *   so: raw = (soc_percent - 0.01) / 0.01
 *
 * Example: 50.00% -> raw = (50.00 - 0.01) / 0.01 = 4999
 */
void SOC_SendCAN(float soc_percent) {

	struct powertrain_t26_master_soc_accumulator_t msg = { 0 };
	uint8_t data[POWERTRAIN_T26_MASTER_SOC_ACCUMULATOR_LENGTH];
	int packed_length;

	msg.soc_integer = (uint8_t) soc_percent;
	msg.soc_float = (uint16_t) (soc_percent * 100);

	packed_length = powertrain_t26_master_soc_accumulator_pack(data, &msg, sizeof(data));
	if (packed_length >= 0 && msg.soc_float < (100 * 100) && msg.soc_float >= 0 && msg.soc_integer < 100 && msg.soc_integer >= 0) {
		CAN_TX_Add_To_Queue(&hcan1, POWERTRAIN_T26_MASTER_SOC_ACCUMULATOR_FRAME_ID, POWERTRAIN_T26_MASTER_SOC_ACCUMULATOR_LENGTH, data);
	}

}

