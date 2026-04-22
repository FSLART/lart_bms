/*******************************************************************************
 Copyright (c) 2020 - Analog Devices Inc. All Rights Reserved.
 This software is proprietary & confidential to Analog Devices, Inc.
 and its licensor.
 ******************************************************************************
 * @file:    adbms_Application.c
 * @brief:   adbms application test cases
 * @version: $Revision$
 * @date:    $Date$
 * Developed by: ADIBMS Software team, Bangalore, India
 *****************************************************************************/
/*! \addtogroup APPLICATION
 *  @{
 */

/*! @addtogroup Application
 *  @{
 */
#include "common.h"
#include "adBms_Application.h"
#include "adBms6830CmdList.h"
#include "adBms6830GenericType.h"
#include "serialPrintResult.h"
#include "mcuWrapper.h"
#include "brain.h"
#include "cell_balancing.h"
#include "uartDMA.h"
#include "fault_manager.h"

/**
 *******************************************************************************
 * @brief Setup Variables
 * The following variables can be modified to configure the software.
 *******************************************************************************
 */

typedef enum {
	ADBMS_IDLE_READ_PREV = 0, ADBMS_IDLE_READ_AVG_START_AUX, ADBMS_IDLE_READ_AUX_START_RAUX, ADBMS_IDLE_READ_RAUX_STATUS, ADBMS_IDLE_OW_START_EVEN, ADBMS_IDLE_OW_READ_EVEN_START_ODD, ADBMS_IDLE_OW_READ_ODD_EVALUATE
} adbms_idle_phase_t;

typedef enum {
	BAL_CYCLE_INIT = 0, BAL_CYCLE_APPLY, BAL_CYCLE_ON_TIME, BAL_CYCLE_STOP_DISCHARGE, BAL_CYCLE_SETTLE, BAL_CYCLE_START_AVG, BAL_CYCLE_WAIT_AVG, BAL_CYCLE_READ_AVG, BAL_CYCLE_COMPUTE
} adbms_balancing_phase_t;

cell_asic IC[TOTAL_IC];

cell_asic SLAVE[TOTAL_IC];

/* ADC Command Configurations */
RD REDUNDANT_MEASUREMENT = RD_OFF;
CH AUX_CH_TO_CONVERT = AUX_ALL;
CONT CONTINUOUS_MEASUREMENT = SINGLE;
OW_C_S CELL_OPEN_WIRE_DETECTION = OW_OFF_ALL_CH;
OW_AUX AUX_OPEN_WIRE_DETECTION = AUX_OW_OFF;
PUP OPEN_WIRE_CURRENT_SOURCE = PUP_DOWN;
DCP DISCHARGE_PERMITTED = DCP_OFF;
RSTF RESET_FILTER = RSTF_OFF;
ERR INJECT_ERR_SPI_READ = WITHOUT_ERR;

/* Set Under Voltage and Over Voltage Thresholds */
const float OV_THRESHOLD = 4.1; /* Volt */
const float UV_THRESHOLD = 2.9; /* Volt */
const int OWC_Threshold = 2000; /* Cell Open wire threshold(mili volt) */
const int OWA_Threshold = 50000; /* Aux Open wire threshold(mili volt) */
const uint32_t LOOP_MEASUREMENT_COUNT = 1; /* Loop measurment count */
const uint16_t MEASUREMENT_LOOP_TIME = 10; /* milliseconds(mS)*/
uint32_t loop_count = 0;
uint32_t pladc_count;

/*Loop Measurement Setup These Variables are ENABLED or DISABLED Remember ALL CAPS*/
LOOP_MEASURMENT MEASURE_CELL = ENABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_AVG_CELL = ENABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_F_CELL = ENABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_S_VOLTAGE = ENABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_AUX = DISABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_RAUX = DISABLED; /*   This is ENABLED or DISABLED       */
LOOP_MEASURMENT MEASURE_STAT = DISABLED; /*   This is ENABLED or DISABLED       */

adbms_idle_phase_t adbmsPhase = ADBMS_IDLE_READ_PREV;
uint32_t adbmsPhaseStart = 0;
static balance_config_t g_balance_cfg;
static bool g_balance_cfg_initialized = false;

uint16_t global_min_mV = 0; //tem de ser global a puta, fdss
uint16_t balance_start_min_mV = 0xFFFF;
balance_stage_t balanceStage = BALANCE_STAGE_ROUGH;

static adbms_balancing_phase_t balPhase = BAL_CYCLE_INIT;
static uint32_t balPhaseStart = 0;
static const uint32_t BALANCE_ON_TIME_MS = 1920;
static const uint32_t BALANCE_SETTLE_MS = 15;
static const uint32_t AVG_CONV_WAIT_MS = 20;

void adbms_main(AMSStates_t ams_state) {

	switch (ams_state) {

	case BALANCING:
		if (!g_balance_cfg_initialized) {
			Balance_InitDefaultConfig(&g_balance_cfg);

			balanceStage = BALANCE_STAGE_ROUGH;
			balance_start_min_mV = BatteryPack_FindMinVoltageGlobally(&IC[0], TOTAL_IC, &g_balance_cfg);

			global_min_mV = balance_start_min_mV;   // freeze target for stage 1
			balPhase = BAL_CYCLE_INIT;

			g_balance_cfg_initialized = true;
		}

		switch (balPhase) {

		case BAL_CYCLE_INIT:

			if (balanceStage == BALANCE_END) {
				for (uint8_t module = 0; module < TOTAL_IC; module++) {
					IC[module].tx_cfgb.dcc = 0;
				}

				g_balance_cfg_initialized = false;
				balPhase = BAL_CYCLE_INIT;
				AMS_State = IDLE;
				adBms6830_init_config(TOTAL_IC, &IC[0]);

			} else {

				balPhase = BAL_CYCLE_COMPUTE;
			}

			balanceStage = BatteryPack_DetermineBalanceStage(&IC[0], TOTAL_IC, &g_balance_cfg, global_min_mV);

			break;

		case BAL_CYCLE_COMPUTE:
			for (uint8_t module = 0; module < TOTAL_IC; module++) {
				balance_result_t result;

				Balance_ComputeModule(&IC[module], &g_balance_cfg, &result, global_min_mV, balanceStage);

				Balance_ApplyToIc(&IC[module], &result, global_min_mV, module);
			}

			if (global_min_mV == 0xFFFF) {
				for (uint8_t module = 0; module < TOTAL_IC; module++) {
					IC[module].tx_cfgb.dcc = 0;
				}
			}

			balPhase = BAL_CYCLE_APPLY;
			break;

		case BAL_CYCLE_APPLY:
			adBmsWakeupIc(TOTAL_IC);
			adBmsWriteData(TOTAL_IC, &IC[0], WRCFGB, Config, B);
			//adBmsWriteData(TOTAL_IC, &IC[0], WRPWM1, Pwm, A);   // only 12 populated cells

			balPhaseStart = getRuntimeMs();
			balPhase = BAL_CYCLE_ON_TIME;

			break;

		case BAL_CYCLE_ON_TIME:
			if (getRuntimeMsDiff(balPhaseStart) >= BALANCE_ON_TIME_MS) {
				balPhase = BAL_CYCLE_STOP_DISCHARGE;
			}

			break;

		case BAL_CYCLE_STOP_DISCHARGE:
			for (uint8_t module = 0; module < TOTAL_IC; module++) {
				IC[module].tx_cfgb.dcc = 0;
			}

			adBmsWakeupIc(TOTAL_IC);
			adBmsWriteData(TOTAL_IC, &IC[0], WRCFGA, Config, A);
			adBmsWriteData(TOTAL_IC, &IC[0], WRCFGB, Config, B);
			//adBmsWriteData(TOTAL_IC, &IC[0], WRPWM1, Pwm, A);

			balPhaseStart = getRuntimeMs();
			balPhase = BAL_CYCLE_SETTLE;

			break;

		case BAL_CYCLE_SETTLE:
			if (getRuntimeMsDiff(balPhaseStart) >= BALANCE_SETTLE_MS) {
				balPhase = BAL_CYCLE_START_AVG;
			}

			break;

		case BAL_CYCLE_START_AVG:
			adBmsWakeupIc(TOTAL_IC);
			adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			//Read AUX
			adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
			//Read GPIOS
			adBms6830_Adax2(AUX_CH_TO_CONVERT);
			//adBms6830_Adcv(RD_OFF, SINGLE, DCP_OFF, RSTF_OFF, OW_OFF_ALL_CH);

			balPhaseStart = getRuntimeMs();
			balPhase = BAL_CYCLE_WAIT_AVG;

			break;

		case BAL_CYCLE_WAIT_AVG:
			if (getRuntimeMsDiff(balPhaseStart) >= AVG_CONV_WAIT_MS) {
				balPhase = BAL_CYCLE_READ_AVG;
			}
			break;

		case BAL_CYCLE_READ_AVG:
			adBmsWakeupIc(TOTAL_IC);
			adBmsReadData(TOTAL_IC, &IC[0], RDACA, AvgCell, A);
			adBmsReadData(TOTAL_IC, &IC[0], RDACB, AvgCell, B);
			adBmsReadData(TOTAL_IC, &IC[0], RDACC, AvgCell, C);
			adBmsReadData(TOTAL_IC, &IC[0], RDACD, AvgCell, D);
			adBmsReadData(TOTAL_IC, &IC[0], RDACE, AvgCell, E);
			adBmsReadData(TOTAL_IC, &IC[0], RDACF, AvgCell, F);

			adBmsReadData(TOTAL_IC, &IC[0], RDAUXA, Aux, A);
			adBmsReadData(TOTAL_IC, &IC[0], RDAUXB, Aux, B);
			adBmsReadData(TOTAL_IC, &IC[0], RDAUXC, Aux, C);
			adBmsReadData(TOTAL_IC, &IC[0], RDAUXD, Aux, D);

			adBmsReadData(TOTAL_IC, &IC[0], RDSTATA, Status, A);
			adBmsReadData(TOTAL_IC, &IC[0], RDSTATB, Status, B);
			adBmsReadData(TOTAL_IC, &IC[0], RDSTATC, Status, C);
			adBmsReadData(TOTAL_IC, &IC[0], RDSTATD, Status, D);
			adBmsReadData(TOTAL_IC, &IC[0], RDSTATE, Status, E);

			adBmsReadData(TOTAL_IC, &IC[0], RDRAXA, RAux, A);
			adBmsReadData(TOTAL_IC, &IC[0], RDRAXB, RAux, B);
			adBmsReadData(TOTAL_IC, &IC[0], RDRAXC, RAux, C);
			adBmsReadData(TOTAL_IC, &IC[0], RDRAXD, RAux, D);

			/*adBmsReadData(TOTAL_IC, &IC[0], RDCVA, Cell, A);
			 adBmsReadData(TOTAL_IC, &IC[0], RDCVB, Cell, B);
			 adBmsReadData(TOTAL_IC, &IC[0], RDCVC, Cell, C);
			 adBmsReadData(TOTAL_IC, &IC[0], RDCVD, Cell, D);
			 adBmsReadData(TOTAL_IC, &IC[0], RDCVE, Cell, E);
			 adBmsReadData(TOTAL_IC, &IC[0], RDCVF, Cell, F);*/

			memcpy(SLAVE, IC, sizeof(SLAVE));

			balPhase = BAL_CYCLE_INIT;

			break;

		default:
			balPhase = BAL_CYCLE_INIT;

			break;
		}

		break;

	case IDLE:

		g_balance_cfg_initialized = false; // making sure it is always false

		switch (adbmsPhase) {

		case ADBMS_IDLE_READ_PREV:

			adBmsWakeupIc(TOTAL_IC);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVA, Cell, A);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVB, Cell, B);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVC, Cell, C);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVD, Cell, D);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVE, Cell, E);
			adBmsReadData(TOTAL_IC, &IC[0], RDCVF, Cell, F);

			adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			adbmsPhaseStart = getRuntimeMs();
			adbmsPhase = ADBMS_IDLE_READ_AVG_START_AUX;
			break;

		case ADBMS_IDLE_READ_AVG_START_AUX:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(TOTAL_IC);
				adBmsReadData(TOTAL_IC, &IC[0], RDACA, AvgCell, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDACB, AvgCell, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDACC, AvgCell, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDACD, AvgCell, D);
				adBmsReadData(TOTAL_IC, &IC[0], RDACE, AvgCell, E);
				adBmsReadData(TOTAL_IC, &IC[0], RDACF, AvgCell, F);

				//Read AUX
				adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_AUX_START_RAUX;
			}
			break;

		case ADBMS_IDLE_READ_AUX_START_RAUX:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(TOTAL_IC);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXD, Aux, D);

				adBmsReadData(TOTAL_IC, &IC[0], RDSTATA, Status, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDSTATB, Status, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDSTATC, Status, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDSTATD, Status, D);
				adBmsReadData(TOTAL_IC, &IC[0], RDSTATE, Status, E);

				//Read GPIOS
				adBms6830_Adax2(AUX_CH_TO_CONVERT);
				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_RAUX_STATUS;
			}
			break;

		case ADBMS_IDLE_READ_RAUX_STATUS:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(TOTAL_IC);
				adBmsReadData(TOTAL_IC, &IC[0], RDRAXA, RAux, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDRAXB, RAux, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDRAXC, RAux, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDRAXD, RAux, D);
				//printVoltages(TOTAL_IC, &IC[0], Aux);

				/*  SNAPSHOT   */
				//memcpy(SLAVE, IC, sizeof(SLAVE));
				//printfDebug("After READ Aux\r\n");
				//printVoltages(TOTAL_IC, &IC[0], RAux);
				//printfDebug("Copy \r\n");
				//printVoltages(TOTAL_IC, &SLAVE[0], RAux);
				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_OW_START_EVEN;
			}
			break;

		case ADBMS_IDLE_OW_START_EVEN:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 5) {

				memcpy(SLAVE, IC, sizeof(SLAVE));

				adBmsWakeupIc(TOTAL_IC);
				// Start even-channel OW check
				adBms6830_Adsv(SINGLE, DCP_OFF, OW_ON_EVEN_CH);
				// Send ADAX with pull-up current and OW enabled
				adBms6830_Adax(AUX_OW_ON, PUP_UP, AUX_CH_TO_CONVERT);

				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_OW_READ_EVEN_START_ODD;
			}
			break;

		case ADBMS_IDLE_OW_READ_EVEN_START_ODD:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 15) {

				// Read S-volt results with even pull active
				adBmsWakeupIc(TOTAL_IC);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVA, S_volt, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVB, S_volt, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVC, S_volt, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVD, S_volt, D);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVE, S_volt, E);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVF, S_volt, F);

				//aux
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXD, Aux, D);

				// Save even-pull readings for even-numbered cells
				/*for (uint8_t slave = 0; slave < TOTAL_IC; slave++) {
					for (uint8_t cell = 0; cell < CELL; cell++) {
						IC[slave].owcell.cell_ow_all[cell] = IC[slave].scell.sc_codes[cell];
					}
				}*/

				// Save pull-up readings
		        for (uint8_t slave = 0; slave < TOTAL_IC; slave++) {
		            for (uint8_t g = 0; g < AUX; g++) {
		                IC[slave].gpio.aux_pup_up[g] = IC[slave].aux.a_codes[g];
		            }
		        }

				// Start odd-channel OW check
				adBms6830_Adsv(SINGLE, DCP_OFF, OW_ON_ODD_CH);

				// Now send ADAX with pull-down current
				adBms6830_Adax(AUX_OW_ON, PUP_DOWN, AUX_CH_TO_CONVERT);

				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_OW_READ_ODD_EVALUATE;
			}
			break;

		case ADBMS_IDLE_OW_READ_ODD_EVALUATE:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {

				// Save even-pull readings for even-numbered cells
				for (uint8_t slave = 0; slave < TOTAL_IC; slave++) {
					for (uint8_t cell = 0; cell < CELL; cell++) {
						IC[slave].owcell.cell_ow_even[cell] = IC[slave].scell.sc_codes[cell];
					}
				}

				adBmsWakeupIc(TOTAL_IC);
				// Read S-volt results with odd pull active
				adBmsReadData(TOTAL_IC, &IC[0], RDSVA, S_volt, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVB, S_volt, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVC, S_volt, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVD, S_volt, D);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVE, S_volt, E);
				adBmsReadData(TOTAL_IC, &IC[0], RDSVF, S_volt, F);

				//auz
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(TOTAL_IC, &IC[0], RDAUXD, Aux, D);

				// Save odd-pull readings and check all cells for open wire
				for (uint8_t slave = 0; slave < TOTAL_IC; slave++) {
					for (uint8_t cell = 0; cell < CELL; cell++) {
						IC[slave].owcell.cell_ow_odd[cell] = IC[slave].scell.sc_codes[cell];
					}
				}

				// Save pull-down readings
				for (uint8_t slave = 0; slave < TOTAL_IC; slave++) {
					for (uint8_t g = 0; g < AUX; g++) {
						IC[slave].gpio.aux_pup_down[g] = IC[slave].aux.a_codes[g];
					}
				}

				// Now evaluate: fill diag_result.cell_ow[]
				adBms6830_evaluate_cell_open_wire(TOTAL_IC, IC);
				// Evaluate aux OW measruments
				adBms6830_evaluate_aux_open_wire(TOTAL_IC, IC);

				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_PREV;
			}
			break;
		}
		break;

		//break;

	case STARTUP:
		g_balance_cfg_initialized = false;
		adBms6830_init_config(TOTAL_IC, &IC[0]);

		break;

	}
}

void run_command(int cmd) {
	switch (cmd) {

	case 1:
		adBms6830_write_read_config(TOTAL_IC, &IC[0]);
		adBms6830_clear_cell_measurement(TOTAL_IC);
		adBms6830_clear_aux_measurement(TOTAL_IC);
		adBms6830_clear_spin_measurement(TOTAL_IC);
		break;

	case 2:
		adBms6830_read_config(TOTAL_IC, &IC[0]);
		break;

	case 3:
		adBms6830_start_adc_cell_voltage_measurment(TOTAL_IC);
		break;

	case 4:
		adBms6830_read_cell_voltages(TOTAL_IC, &IC[0]);
		break;

	case 5:
		adBms6830_start_adc_s_voltage_measurment(TOTAL_IC);
		break;

	case 6:
		adBms6830_read_s_voltages(TOTAL_IC, &IC[0]);
		break;

	case 7:
		adBms6830_start_avgcell_voltage_measurment(TOTAL_IC);
		break;

	case 8:
		adBms6830_read_avgcell_voltages(TOTAL_IC, &IC[0]);
		break;

	case 9:
		adBms6830_start_fcell_voltage_measurment(TOTAL_IC);
		break;

	case 10:
		adBms6830_read_fcell_voltages(TOTAL_IC, &IC[0]);
		break;

	case 11:
		adBms6830_start_aux_voltage_measurment(TOTAL_IC, &IC[0]);
		break;

	case 12:
		adBms6830_read_aux_voltages(TOTAL_IC, &IC[0]);
		break;

	case 13:
		adBms6830_start_raux_voltage_measurment(TOTAL_IC, &IC[0]);
		break;

	case 14:
		adBms6830_read_raux_voltages(TOTAL_IC, &IC[0]);
		break;

	case 15:
		adBms6830_read_status_registers(TOTAL_IC, &IC[0]);
		break;

	case 16:
		loop_count = 0;
		adBmsWakeupIc(TOTAL_IC);
		adBmsWriteData(TOTAL_IC, &IC[0], WRCFGA, Config, A);
		adBmsWriteData(TOTAL_IC, &IC[0], WRCFGB, Config, B);
		adBmsWakeupIc(TOTAL_IC);
		adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
		Delay_ms(1); // ADCs are updated at their conversion rate is 1ms
		adBms6830_Adcv(RD_ON, CONTINUOUS, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
		Delay_ms(1); // ADCs are updated at their conversion rate is 1ms
		adBms6830_Adsv(CONTINUOUS, DISCHARGE_PERMITTED, CELL_OPEN_WIRE_DETECTION);
		Delay_ms(8); // ADCs are updated at their conversion rate is 8ms
		while (loop_count < LOOP_MEASUREMENT_COUNT) {
			measurement_loop();
			Delay_ms(MEASUREMENT_LOOP_TIME);
			loop_count = loop_count + 1;
		}
		printMenu();
		break;

	case 17:
		adBms6830_clear_cell_measurement(TOTAL_IC);
		break;

	case 18:
		adBms6830_clear_aux_measurement(TOTAL_IC);
		break;

	case 19:
		adBms6830_clear_spin_measurement(TOTAL_IC);
		break;

	case 20:
		adBms6830_clear_fcell_measurement(TOTAL_IC);
		break;

	case 21:
		adBms6830_write_config(TOTAL_IC, &IC[0]);
		break;

	case 0:
		printMenu();
		break;

	default:
#ifdef MBED
    pc.printf("Incorrect Option\n\n");
#else
		printf("Incorrect Option\n\n");
#endif
		break;
	}
}

/**
 *******************************************************************************
 * @brief Set configuration register A. Refer to the data sheet
 *        Set configuration register B. Refer to the data sheet
 *******************************************************************************
 */
void adBms6830_init_config(uint8_t tIC, cell_asic *ic) {
	for (uint8_t cic = 0; cic < tIC; cic++) {
		/* Init config A */
		ic[cic].tx_cfga.refon = PWR_UP;
//    ic[cic].cfga.cth = CVT_8_1mV;
//    ic[cic].cfga.flag_d = ConfigA_Flag(FLAG_D0, FLAG_SET) | ConfigA_Flag(FLAG_D1, FLAG_SET);
//    ic[cic].cfga.gpo = ConfigA_Gpo(GPO2, GPO_SET) | ConfigA_Gpo(GPO10, GPO_SET);
		ic[cic].tx_cfga.gpo = 0X3FF; /* All GPIO pull down off */
//    ic[cic].cfga.soakon = SOAKON_CLR;
//    ic[cic].cfga.fc = IIR_FPA256;

		/* Init config B */
//    ic[cic].cfgb.dtmen = DTMEN_ON;
		ic[cic].tx_cfgb.vov = SetOverVoltageThreshold(OV_THRESHOLD);
		ic[cic].tx_cfgb.vuv = SetUnderVoltageThreshold(UV_THRESHOLD);
//    ic[cic].cfgb.dcc = ConfigB_DccBit(DCC16, DCC_BIT_SET);
//    SetConfigB_DischargeTimeOutValue(tIC, &ic[cic], RANG_0_TO_63_MIN, TIME_1MIN_OR_0_26HR);
	}
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBmsWriteData(tIC, &ic[0], WRCFGB, Config, B);
}

/**
 *******************************************************************************
 * @brief Write and Read Configuration Register A/B
 *******************************************************************************
 */
void adBms6830_write_read_config(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBmsWriteData(tIC, &ic[0], WRCFGB, Config, B);
	adBmsReadData(tIC, &ic[0], RDCFGA, Config, A);
	adBmsReadData(tIC, &ic[0], RDCFGB, Config, B);
	printWriteConfig(tIC, &ic[0], Config, ALL_GRP);
	printReadConfig(tIC, &ic[0], Config, ALL_GRP);
}

/**
 *******************************************************************************
 * @brief Write Configuration Register A/B
 *******************************************************************************
 */
void adBms6830_write_config(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBmsWriteData(tIC, &ic[0], WRCFGB, Config, B);
	printWriteConfig(tIC, &ic[0], Config, ALL_GRP);
}

/**
 *******************************************************************************
 * @brief Read Configuration Register A/B
 *******************************************************************************
 */
void adBms6830_read_config(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDCFGA, Config, A);
	adBmsReadData(tIC, &ic[0], RDCFGB, Config, B);
	printReadConfig(tIC, &ic[0], Config, ALL_GRP);
}

/**
 *******************************************************************************
 * @brief Start ADC Cell Voltage Measurement
 *******************************************************************************
 */
void adBms6830_start_adc_cell_voltage_measurment(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("Cell conversion completed\n");
#else
	printf("Cell conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read Cell Voltages
 *******************************************************************************
 */
void adBms6830_idle_readings(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);

	adBmsReadData(tIC, &ic[0], RDCVA, Cell, A);
	adBmsReadData(tIC, &ic[0], RDCVB, Cell, B);
	adBmsReadData(tIC, &ic[0], RDCVC, Cell, C);
	adBmsReadData(tIC, &ic[0], RDCVD, Cell, D);
	adBmsReadData(tIC, &ic[0], RDCVE, Cell, E);
	adBmsReadData(tIC, &ic[0], RDCVF, Cell, F);

	adBmsReadData(tIC, &ic[0], RDRAXA, RAux, A);
	adBmsReadData(tIC, &ic[0], RDRAXB, RAux, B);
	adBmsReadData(tIC, &ic[0], RDRAXC, RAux, C);
	adBmsReadData(tIC, &ic[0], RDRAXD, RAux, D);

	adBmsReadData(tIC, &ic[0], RDAUXA, Aux, A);
	adBmsReadData(tIC, &ic[0], RDAUXB, Aux, B);
	adBmsReadData(tIC, &ic[0], RDAUXC, Aux, C);
	adBmsReadData(tIC, &ic[0], RDAUXD, Aux, D);

	//adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	//adBmsWriteData(tIC, &ic[0], WRCFGB, Config, B);

	//teporarly disable openwire
	/*adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
	 pladc_count = adBmsPollAdc(PLADC);
	 adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
	 pladc_count = pladc_count + adBmsPollAdc(PLADC);*/

	adBmsReadData(tIC, &ic[0], RDSTATA, Status, A);
	adBmsReadData(tIC, &ic[0], RDSTATB, Status, B);
	adBmsReadData(tIC, &ic[0], RDSTATC, Status, C);
	adBmsReadData(tIC, &ic[0], RDSTATD, Status, D);
	adBmsReadData(tIC, &ic[0], RDSTATE, Status, E);

	//printVoltages(tIC, &ic[0], Cell);
	//printVoltages(tIC, &ic[0], RAux);
	//printVoltages(tIC, &ic[0], Aux);
	//printPollAdcConvTime(pladc_count);
	//printStatus(tIC, &ic[0], Status, ALL_GRP);
}

/**
 *******************************************************************************
 * @brief Read Cell Voltages
 *******************************************************************************
 */
void adBms6830_read_cell_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDCVA, Cell, A);
	adBmsReadData(tIC, &ic[0], RDCVB, Cell, B);
	adBmsReadData(tIC, &ic[0], RDCVC, Cell, C);
	adBmsReadData(tIC, &ic[0], RDCVD, Cell, D);
	adBmsReadData(tIC, &ic[0], RDCVE, Cell, E);
	adBmsReadData(tIC, &ic[0], RDCVF, Cell, F);
	printVoltages(tIC, &ic[0], Cell);
}

/**
 *******************************************************************************
 * @brief Start ADC S-Voltage Measurement
 *******************************************************************************
 */
void adBms6830_start_adc_s_voltage_measurment(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	adBms6830_Adsv(CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, CELL_OPEN_WIRE_DETECTION);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("S-Voltage conversion completed\n");
#else
	printf("S-Voltage conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read S-Voltages
 *******************************************************************************
 */
void adBms6830_read_s_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDSVA, S_volt, A);
	adBmsReadData(tIC, &ic[0], RDSVB, S_volt, B);
	adBmsReadData(tIC, &ic[0], RDSVC, S_volt, C);
	adBmsReadData(tIC, &ic[0], RDSVD, S_volt, D);
	adBmsReadData(tIC, &ic[0], RDSVE, S_volt, E);
	adBmsReadData(tIC, &ic[0], RDSVF, S_volt, F);
	printVoltages(tIC, &ic[0], S_volt);
}

/**
 *******************************************************************************
 * @brief Start Avarage Cell Voltage Measurement
 *******************************************************************************
 */
void adBms6830_start_avgcell_voltage_measurment(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("Avg Cell voltage conversion completed\n");
#else
	printf("Avg Cell voltage conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read Avarage Cell Voltages
 *******************************************************************************
 */
void adBms6830_read_avgcell_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDACA, AvgCell, A);
	adBmsReadData(tIC, &ic[0], RDACB, AvgCell, B);
	adBmsReadData(tIC, &ic[0], RDACC, AvgCell, C);
	adBmsReadData(tIC, &ic[0], RDACD, AvgCell, D);
	adBmsReadData(tIC, &ic[0], RDACE, AvgCell, E);
	adBmsReadData(tIC, &ic[0], RDACF, AvgCell, F);
	printVoltages(tIC, &ic[0], AvgCell);
}

/**
 *******************************************************************************
 * @brief Start Filtered Cell Voltages Measurement
 *******************************************************************************
 */
void adBms6830_start_fcell_voltage_measurment(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("F Cell voltage conversion completed\n");
#else
	printf("F Cell voltage conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read Filtered Cell Voltages
 *******************************************************************************
 */
void adBms6830_read_fcell_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDFCA, F_volt, A);
	adBmsReadData(tIC, &ic[0], RDFCB, F_volt, B);
	adBmsReadData(tIC, &ic[0], RDFCC, F_volt, C);
	adBmsReadData(tIC, &ic[0], RDFCD, F_volt, D);
	adBmsReadData(tIC, &ic[0], RDFCE, F_volt, E);
	adBmsReadData(tIC, &ic[0], RDFCF, F_volt, F);
	printVoltages(tIC, &ic[0], F_volt);
}

/**
 *******************************************************************************
 * @brief Start AUX, VMV, V+ Voltages Measurement
 *******************************************************************************
 */
void adBms6830_start_aux_voltage_measurment(uint8_t tIC, cell_asic *ic) {
	for (uint8_t cic = 0; cic < tIC; cic++) {
		/* Init config A */
		ic[cic].tx_cfga.refon = PWR_UP;
		ic[cic].tx_cfga.gpo = 0X3FF; /* All GPIO pull down off */
	}
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("Aux voltage conversion completed\n");
#else
	printf("Aux voltage conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read AUX, VMV, V+ Voltages
 *******************************************************************************
 */
void adBms6830_read_aux_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDAUXA, Aux, A);
	adBmsReadData(tIC, &ic[0], RDAUXB, Aux, B);
	adBmsReadData(tIC, &ic[0], RDAUXC, Aux, C);
	adBmsReadData(tIC, &ic[0], RDAUXD, Aux, D);
	printVoltages(tIC, &ic[0], Aux);
}

/**
 *******************************************************************************
 * @brief Start Redundant GPIO Voltages Measurement
 *******************************************************************************
 */
void adBms6830_start_raux_voltage_measurment(uint8_t tIC, cell_asic *ic) {
	for (uint8_t cic = 0; cic < tIC; cic++) {
		/* Init config A */
		ic[cic].tx_cfga.refon = PWR_UP;
		ic[cic].tx_cfga.gpo = 0X3FF; /* All GPIO pull down off */
	}
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBms6830_Adax2(AUX_CH_TO_CONVERT);
	pladc_count = adBmsPollAdc(PLADC);
#ifdef MBED
  pc.printf("RAux voltage conversion completed\n");
#else
	printf("RAux voltage conversion completed\n");
#endif
	printPollAdcConvTime(pladc_count);
}

/**
 *******************************************************************************
 * @brief Read Redundant GPIO Voltages
 *******************************************************************************
 */
void adBms6830_read_raux_voltages(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsReadData(tIC, &ic[0], RDRAXA, RAux, A);
	adBmsReadData(tIC, &ic[0], RDRAXB, RAux, B);
	adBmsReadData(tIC, &ic[0], RDRAXC, RAux, C);
	adBmsReadData(tIC, &ic[0], RDRAXD, RAux, D);
	printVoltages(tIC, &ic[0], RAux);
}

/**
 *******************************************************************************
 * @brief Read Status Reg. A, B, C, D and E.
 *******************************************************************************
 */
void adBms6830_read_status_registers(uint8_t tIC, cell_asic *ic) {
	adBmsWakeupIc(tIC);
	adBmsWriteData(tIC, &ic[0], WRCFGA, Config, A);
	adBmsWriteData(tIC, &ic[0], WRCFGB, Config, B);
	adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
	pladc_count = adBmsPollAdc(PLADC);
	adBms6830_Adcv(REDUNDANT_MEASUREMENT, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
	pladc_count = pladc_count + adBmsPollAdc(PLADC);

	adBmsReadData(tIC, &ic[0], RDSTATA, Status, A);
	adBmsReadData(tIC, &ic[0], RDSTATB, Status, B);
	adBmsReadData(tIC, &ic[0], RDSTATC, Status, C);
	adBmsReadData(tIC, &ic[0], RDSTATD, Status, D);
	adBmsReadData(tIC, &ic[0], RDSTATE, Status, E);
	printPollAdcConvTime(pladc_count);
	printStatus(tIC, &ic[0], Status, ALL_GRP);
}

/**
 *******************************************************************************
 * @brief Loop measurment.
 *******************************************************************************
 */
void measurement_loop() {
	if (MEASURE_CELL == ENABLED) {
		adBmsReadData(TOTAL_IC, &IC[0], RDCVA, Cell, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDCVB, Cell, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDCVC, Cell, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDCVD, Cell, D);
		adBmsReadData(TOTAL_IC, &IC[0], RDCVE, Cell, E);
		adBmsReadData(TOTAL_IC, &IC[0], RDCVF, Cell, F);
		printVoltages(TOTAL_IC, &IC[0], Cell);
	}

	if (MEASURE_AVG_CELL == ENABLED) {
		adBmsReadData(TOTAL_IC, &IC[0], RDACA, AvgCell, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDACB, AvgCell, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDACC, AvgCell, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDACD, AvgCell, D);
		adBmsReadData(TOTAL_IC, &IC[0], RDACE, AvgCell, E);
		adBmsReadData(TOTAL_IC, &IC[0], RDACF, AvgCell, F);
		printVoltages(TOTAL_IC, &IC[0], AvgCell);
	}

	if (MEASURE_F_CELL == ENABLED) {
		adBmsReadData(TOTAL_IC, &IC[0], RDFCA, F_volt, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDFCB, F_volt, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDFCC, F_volt, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDFCD, F_volt, D);
		adBmsReadData(TOTAL_IC, &IC[0], RDFCE, F_volt, E);
		adBmsReadData(TOTAL_IC, &IC[0], RDFCF, F_volt, F);
		printVoltages(TOTAL_IC, &IC[0], F_volt);
	}

	if (MEASURE_S_VOLTAGE == ENABLED) {
		adBmsReadData(TOTAL_IC, &IC[0], RDSVA, S_volt, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDSVB, S_volt, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDSVC, S_volt, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDSVD, S_volt, D);
		adBmsReadData(TOTAL_IC, &IC[0], RDSVE, S_volt, E);
		adBmsReadData(TOTAL_IC, &IC[0], RDSVF, S_volt, F);
		printVoltages(TOTAL_IC, &IC[0], S_volt);
	}

	if (MEASURE_AUX == ENABLED) {
		adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
		adBmsPollAdc(PLAUX1);
		adBmsReadData(TOTAL_IC, &IC[0], RDAUXA, Aux, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDAUXB, Aux, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDAUXC, Aux, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDAUXD, Aux, D);
		printVoltages(TOTAL_IC, &IC[0], Aux);
	}

	if (MEASURE_RAUX == ENABLED) {
		adBmsWakeupIc(TOTAL_IC);
		adBms6830_Adax2(AUX_CH_TO_CONVERT);
		adBmsPollAdc(PLAUX2);
		adBmsReadData(TOTAL_IC, &IC[0], RDRAXA, RAux, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDRAXB, RAux, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDRAXC, RAux, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDRAXD, RAux, D);
		printVoltages(TOTAL_IC, &IC[0], RAux);
	}

	if (MEASURE_STAT == ENABLED) {
		adBmsReadData(TOTAL_IC, &IC[0], RDSTATA, Status, A);
		adBmsReadData(TOTAL_IC, &IC[0], RDSTATB, Status, B);
		adBmsReadData(TOTAL_IC, &IC[0], RDSTATC, Status, C);
		adBmsReadData(TOTAL_IC, &IC[0], RDSTATD, Status, D);
		adBmsReadData(TOTAL_IC, &IC[0], RDSTATE, Status, E);
		printStatus(TOTAL_IC, &IC[0], Status, ALL_GRP);
	}
}

/**
 *******************************************************************************
 * @brief Clear Cell measurement reg.
 *******************************************************************************
 */
void adBms6830_clear_cell_measurement(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	spiSendCmd(CLRCELL);
#ifdef MBED
  pc.printf("Cell Registers Cleared\n\n");
#else
	printf("Cell Registers Cleared\n\n");
#endif
}

/**
 *******************************************************************************
 * @brief Clear Aux measurement reg.
 *******************************************************************************
 */
void adBms6830_clear_aux_measurement(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	spiSendCmd(CLRAUX);
#ifdef MBED
  pc.printf("Aux Registers Cleared\n\n");
#else
	printf("Aux Registers Cleared\n\n");
#endif
}

/**
 *******************************************************************************
 * @brief Clear spin measurement reg.
 *******************************************************************************
 */
void adBms6830_clear_spin_measurement(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	spiSendCmd(CLRSPIN);
#ifdef MBED
  pc.printf("Spin Registers Cleared\n\n");
#else
	printf("Spin Registers Cleared\n\n");
#endif
}

/**
 *******************************************************************************
 * @brief Clear fcell measurement reg.
 *******************************************************************************
 */
void adBms6830_clear_fcell_measurement(uint8_t tIC) {
	adBmsWakeupIc(tIC);
	spiSendCmd(CLRFC);
#ifdef MBED
  pc.printf("Fcell Registers Cleared\n\n");
#else
	printf("Fcell Registers Cleared\n\n");
#endif
}

/** @}*/
/** @}*/

void adBms6830_evaluate_cell_open_wire(uint8_t tIC, cell_asic *ic) {

	for (uint8_t slave = 0; slave < tIC; slave++) {
		for (uint8_t cell = 0; cell < CELL; cell++) {

			int32_t raw_code;
			if ((cell % 2) == 0) {
				raw_code = ic[slave].owcell.cell_ow_even[cell];
			} else {
				raw_code = ic[slave].owcell.cell_ow_odd[cell];
			}

			/* sc_codes are raw 150µV/LSB, no offset — unlike a_codes */
			//int32_t voltage_mV = raw_code * 150 / 1000;
			int32_t voltage_mV = (int32_t) ((raw_code + 10000) * 0.150);

			if (voltage_mV < OWC_Threshold) {
				ic[slave].diag_result.cell_ow[cell] = 1;
				printfDebug("OW FAULT: IC%u Cell%u (%ldmV)\r\n", slave + 1, cell + 1, voltage_mV);
				RAISE_ERROR(FAULT_OW_DETECTED_CELL, .slave_idx = slave, .cell_idx = cell + 1, .measured_value = (float )voltage_mV);
			} else {
				ic[slave].diag_result.cell_ow[cell] = 0;
			}
		}
	}
}

void adBms6830_evaluate_aux_open_wire(uint8_t tIC, cell_asic *ic) {

	for (uint8_t slave = 0; slave < tIC; slave++) {
		for (uint8_t gpio = 0; gpio < AUX; gpio++) {

			/*int32_t pup_mV = (ic[slave].gpio.aux_pup_up[gpio] + 10000) * 150 / 1000;
			int32_t pdown_mV = (ic[slave].gpio.aux_pup_down[gpio] + 10000) * 150 / 1000;*/
			//int32_t diff_mV = pup_mV - pdown_mV;

			int32_t pup = ic[slave].gpio.aux_pup_up[gpio];
			int32_t pdown = ic[slave].gpio.aux_pup_down[gpio];

			/*if (diff_mV < 0) {
				diff_mV = -diff_mV;
			}*/
			//TODO: DEFINIR THESHOLDS PARA OPEN WIRE NO GPIO
			if (pup > 10000 || pdown > 0) {
				ic[slave].diag_result.aux_ow[gpio] = 1;
				printfDebug("AUX OW FAULT: IC%u GPIO%u diff (%ldmV) \r\n", slave, gpio + 1, pdown);
				RAISE_ERROR(FAULT_OW_DETECTED_RTH, .slave_idx = slave, .channel_idx = gpio, .measured_value = (float )pdown);
			} else {
				ic[slave].diag_result.aux_ow[gpio] = 0;
			}
		}
	}
}
