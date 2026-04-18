/*
 * cell_balancing.h
 *
 *  Created on: Mar 18, 2026
 *      Author: jpser
 */

#ifndef INC_CELL_BALANCING_H_
#define INC_CELL_BALANCING_H_

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include "adBms6830Data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BALANCING_CELL_COUNT 12
#define BALANCING_PWM_MAX    15 //do datasheet do adbms6830

typedef enum {
	BALANCE_OUTPUT_PWM = 0,
	BALANCE_OUTPUT_DCC
} balance_output_mode_t;

typedef enum {
	BALANCE_PARITY_NONE = 0,
	BALANCE_PARITY_ODD,   // cells 1,3,5,7,9,11
	BALANCE_PARITY_EVEN   // cells 2,4,6,8,10,12
} balance_parity_t;

typedef struct {
	uint16_t min_rough_balacing_mV; //minimum volts to start a rough balacing, of only high volts cells comapred to minimum cell
	uint16_t deadband_mV;     // below this delta, no balancing
	uint16_t min_cell_mV;      // minimum allowed cell voltage for balancing
	uint16_t max_cell_mV;   // maximum sane cell voltage
	bool use_filtered_cells;  //wqd
} balance_config_t;

typedef struct {
	uint16_t cell_mV[BALANCING_CELL_COUNT];
	uint16_t target_mV;
	uint16_t delta_mV[BALANCING_CELL_COUNT];
	uint8_t pwm[BALANCING_CELL_COUNT];   // 0..15
	uint16_t dcc_mask;                    // bits 0..11
	balance_parity_t active_parity;
	bool balancing_allowed;
} balance_result_t;

typedef enum {
    BALANCE_STAGE_ROUGH = 0,   // first stage: only >100 mV above latched min volts
	BALANCE_STAGE_FINE,         // second stage: normal-ish balancing
	BALANCE_END // acabou
} balance_stage_t;

uint16_t BatteryPack_FindMinVoltageGlobally(const cell_asic *ic_array, uint8_t total_ic, const balance_config_t *cfg);

balance_stage_t BatteryPack_DetermineBalanceStage(const cell_asic *ic_array, uint8_t total_ic, const balance_config_t *cfg, uint16_t global_min_mV) ;

void Balance_InitDefaultConfig(balance_config_t *cfg);

void Balance_ComputeModule(const cell_asic *ic, const balance_config_t *cfg, balance_result_t *out, uint16_t global_min_mV, balance_stage_t balancing_stage);

void Balance_ApplyToIc(cell_asic *ic, const balance_result_t *result, uint16_t global_min_mV, uint8_t ic_index);

void Balance_ForceParity(balance_result_t *result, balance_parity_t forced_parity, balance_output_mode_t mode);

void CellBalancing_CAN_Init(void);

void CellBalancing_CAN_Rx(const CAN_RxHeaderTypeDef *hdr, const uint8_t *data);

void Balance_SetOutputMode(balance_config_t *cfg, balance_output_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* INC_CELL_BALANCING_H_ */
