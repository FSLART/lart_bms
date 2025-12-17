/*
 * bms_libWrapper.cpp
 *
 *  Created on: Nov 24, 2024
 *      Author: amrlxyz
 */

/*
 * Compatible commands
 * ADBMS2950 == ADBMS6830
 *
 * RDCFGA
 * RDCFGB
 * ADI1     = ADCV
 * ADI2     = ADSV
 * RDI      = RDFCA or RDCVA
 * RDVB     = RDFCB or RDCVB
 * RDIVB1   = RDFCC or RDCVC
 * RDIACC   = RDACA
 * RDVBACC  = RDACB
 * RDIVB1ACC= RDACC
 *
 */

/*
 * Commands Notes
 *
 * -- 6830 --
 * ADCV : Start ADC
 * ADSV : Start redundancy ADC
 * RDCVA: Read Cell Voltage A
 * RDFCA: Read Filtered Cell A
 * RDACA: Read Averaged Cell A
 *
 * -- 2950 --
 * ADIx: Start IxADC and VBxADC
 * RDI : Read Read I1ADC and I2ADC results
 *
 */

#include "bms_libWrapper.h"
#include "bms_datatypes.h"
#include "bms_utility.h"
#include "bms_mcuWrapper.h"
#include "bms_cmdlist.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "main.h"
#include "brain.h"

#include "uartDMA.h"

#include "time_rtc.h"

#include "can.h"
#include "dbc/ams.h"

//Delay times for timers
#define WAIT_8MS   8U
#define WAIT_12MS  12U
#define WAIT_100MS  100U

uint8_t txData[TOTAL_IC][DATA_LEN];
uint8_t rxData[TOTAL_IC][DATA_LEN];
uint16_t rxPec[TOTAL_IC];
uint8_t rxCc[TOTAL_IC];

typedef struct {
	ad68_cfa_t cfa_Tx;
	ad68_cfa_t cfa_Rx;
	ad68_cfb_t cfb_Tx;
	ad68_cfb_t cfb_Rx;

	ad68_pwma_t pwma;
	ad68_pwmb_t pwmb;

	float v_avgCell[TOTAL_CELL];
	float v_avgCell_sum;
	float v_avgCell_avg;
	float v_avgCell_min;
	float v_avgCell_max;
	float v_avgCell_delta;

	float v_sCell[TOTAL_CELL];

	float v_tempSens[RTH_PER_MODULE];
	float v_segment;

	float temp_cell[RTH_PER_MODULE];
	float temp_ic;
} ic_ad68_t;

//ic_ad29_t ic_ad29;
ic_ad68_t ic_ad68[TOTAL_AD68];

void bms_resetConfig(void) {
	// Obtained from RDCFG after reset
	// Flipped due to Little endian
//    uint64_t const ad68_cfaDefault = 0x01 00 00 FF 03 00;
	uint64_t const ad68_cfaDefault = 0x0003FF000001;
//    uint64_t const ad68_cfbDefault = 0x00 F8 7F 00 00 00;
	uint64_t const ad68_cfbDefault = 0x0000007FF800;

	// Copy defaults to Tx Buffer
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		memcpy(&ic_ad68[ic].cfa_Tx, &ad68_cfaDefault, DATA_LEN);
		memcpy(&ic_ad68[ic].cfb_Tx, &ad68_cfbDefault, DATA_LEN);
	}

	ad68_cfa_t ad68_cfaT;
	memcpy(&ad68_cfaT, &ad68_cfaDefault, DATA_LEN);
}

void bms_init(void) {
	bms_resetConfig();
}

void bms_writeConfigA(void) {
	// Fill buffer for ad2950 first
	//memcpy(txData[0], &ic_ad29.cfa_Tx, DATA_LEN);

	// Fill buffer with the other ad6830 data
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		memcpy(txData[ic], &ic_ad68[ic].cfa_Tx, DATA_LEN);
	}

	// write config A
	bms_transmitData(WRCFGA, txData);
}

void bms_writeConfigB(void) {
	// Fill buffer for ad2950 first
	//memcpy(txData[0], &ic_ad29.cfb_Tx, DATA_LEN);

	// Fill buffer with the other ad6830 data
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		memcpy(txData[ic], &ic_ad68[ic].cfb_Tx, DATA_LEN);
	}

	// write config B
	bms_transmitData(WRCFGB, txData);
}

void bms_writePwmA(void) {
	// Fill padding bytes for ad29
	//memset(txData[0], 0x00, DATA_LEN);

	// Fill buffer with the other ad6830 data
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		memcpy(txData[ic], &ic_ad68[ic].pwma, DATA_LEN);
	}

	// write config A
	bms_transmitData(WRPWM1, txData);
}

void bms_writePwmB(void) {
	// Fill padding bytes for ad29
	//memset(txData[0], 0x00, DATA_LEN);

	// Fill buffer with the other ad6830 data
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		memcpy(txData[ic], &ic_ad68[ic].pwmb, DATA_LEN);
	}

	// write config B
	bms_transmitData(WRPWM2, txData);
}

void bms68_setGpo45(uint8_t twoBitIndex) {
	// GPIO Output: 1 = No pulldown (Default), 0 = Pulldown
	// Only for pin 4 and 5
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		ic_ad68[ic].cfa_Tx.gpo1to8 = ((twoBitIndex) << 3) | (0xFF ^ (0x3 << 3));
	}

	bms_writeConfigA();
}

void bms_printRawData(uint8_t data[TOTAL_IC][DATA_LEN], uint8_t cc[TOTAL_IC]) {
	for (int ic = 0; ic < TOTAL_IC; ic++) {
		printConsole("IC%d: ", ic + 1);
		for (int j = 0; j < 6; j++)             // For every byte recieved (6 bytes)
				{
			printConsole("0x%02X, ", data[ic][j]);    // Print each of the bytes
		}
		printConsole("CC: %d |   ", cc[ic]);
	}
	printConsole("\n\n");
}

bool bms_checkRxFault(uint8_t data[TOTAL_IC][DATA_LEN], uint16_t pec[TOTAL_IC], uint8_t cc[TOTAL_IC]) {
	bool faultDetected = false;
	bool errorIndex[TOTAL_IC];

	if (!bms_checkRxPec(data, pec, cc, errorIndex)) {
		printConsole("WARNING! PEC ERROR - IC:");
		for (int ic = 0; ic < TOTAL_IC; ic++) {
			if (!errorIndex[ic]) {
				printConsole(" %d,", ic + 1);
			}
		}
		printConsole("\n");
		faultDetected = true;
	}

	return faultDetected;

	// TODO: Add command counter fault checker
	// TODO: Add fault handler for PEC fault
}

// used mostly for debugging purposes
void bms_readSid(void) {
	bms_receiveData(RDSID, rxData, rxPec, rxCc);
	printConsole("SID: \n");
	bms_checkRxFault(rxData, rxPec, rxCc);
	bms_printRawData(rxData, rxCc);
}

void bms_readConfigA(void) {
	bms_receiveData(RDCFGA, rxData, rxPec, rxCc);
	//printConsole("CFGA: \n");
	bms_checkRxFault(rxData, rxPec, rxCc);
	bms_printRawData(rxData, rxCc);
}

void bms_readConfigB(void) {
	bms_receiveData(RDCFGB, rxData, rxPec, rxCc);
	//printConsole("CFGB: \n");
	bms_checkRxFault(rxData, rxPec, rxCc);
	bms_printRawData(rxData, rxCc);
}

void bms_startAdcvCont(void) {
	// 6830
	// For DCP = 0
	// If RD = 0 and CONT = 1, PWM discharge is permitted
	// If RD = 1 and CONT = 0, PWM discharge interrupted temporarily until RD conversion finished (8ms typ)
	// If RD = 1 and CONT = 1, PWM discharge stopped

	ADCV.CONT = 1;      // Continuous
	ADCV.RD = 1;      // Redundant Measurement
	ADCV.DCP = 0;      // Discharge permitted
	ADCV.RSTF = 0;      // Reset filter
	ADCV.OW = 0b00;   // Open wire on C-ADCS and S-ADCs

	// Behaviour of 2950 (ADI1 Command)
	//

	bms_transmitCmd((uint8_t*) &ADCV);
}

void bms_parseVoltage(uint8_t rawData[TOTAL_IC][DATA_LEN], float vArr[TOTAL_CELL], uint8_t cell_index) {
	// Does not take care of 2950
	for (int ic = 0; ic < TOTAL_IC; ic++) {
		for (int c = cell_index * 3; c < (cell_index * 3 + 3); c++) {
			*((float*) ((uint8_t*) vArr + (ic) * sizeof(ic_ad68_t)) + c) = *((int16_t*) (rawData[ic] + (c - cell_index * 3) * 2)) * 0.00015 + 1.5;
//            vArr[ic-1][c] = *((int16_t *)(rawData[ic] + (c-cell_index*3)*2)) * 0.00015 + 1.5;

			if (cell_index == 5) {
				break;
			}
		}
	}
}

void bms_parseAuxVoltage(uint8_t rawData[TOTAL_IC][DATA_LEN], float vArr[RTH_PER_MODULE], uint8_t cell_index, uint8_t muxIndex) {
	// Does not take care of 2950
	for (int ic = 0; ic < TOTAL_IC; ic++) {

		if (cell_index == 4) {
			/* ----- ITEMP from RDSTATA (index 4) ----- */
			ic_ad68[ic].temp_ic = (*((int16_t*) (rawData[ic] + 2)) * 0.00015 + 1.5) / 0.0075 - 273;
			//return;
			//break;
			continue;  // nothing else to do for RDSTATA
		}

		/* ----- AUX groups A/B/C/D ----- */
		uint8_t cellArrIndex = cell_index * 3;

		for (int c = cellArrIndex; c < (cellArrIndex + 3); c++) {
			/*if (c >= 2 && c <= 6)
			 continue; // Skip digital output pins
			 int ci = c;
			 if (c > 6) {
			 ci -= 5;
			 }*/

			/* ----- v_segment from RDAUXD (cell_index == 3) using VPV ----- */
			if (cell_index == 3) {
				ic_ad68[ic].v_segment = (*((int16_t*) (rawData[ic] + 4)) * 0.00015 + 1.5) * 25;
				//continue;
				//break;
			}

			if (c >= RTH_PER_MODULE) {
				continue; // Skip unwanted reads, dont overflow
			}
			int ci = c;

			//TODO: This WILL corrupt memory once you have more than 1 IC, This assumes vArr is actually the start of an array of ic_ad68_t, which it is not
			//*((float*) ((uint8_t*) vArr + (ic - 1) * sizeof(ic_ad68_t)) + (ci + 8 * muxIndex)) = *((int16_t*) (rawData[ic] + (c - cellArrIndex) * 2)) * 0.00015 + 1.5;
			*((float*) ((uint8_t*) vArr + (ic) * sizeof(ic_ad68_t)) + ci) = *((int16_t*) (rawData[ic] + (c - cellArrIndex) * 2)) * 0.00015f + 1.5f;

			//            vArr[ic-1][ci + 8*muxIndex] = *((int16_t *)(rawData[ic] + (c-cellArrIndex)*2)) * 0.00015 + 1.5;

			/*if (cell_index == 3) {
				ic_ad68[ic].v_segment = (*((int16_t*) (rawData[ic] + 4)) * 0.00015 + 1.5) * 25;
				//continue;
				break;
			}*/
		}

		/*float *ptr = (float*) ((uint8_t*) vArr + (ic - 1) * sizeof(ic_ad68_t));

		 switch (cell_index) {
		 case 0: // RDAUXA: GPAR0–2
		 ptr[0] = (*((int16_t*) (rawData[ic] + 0)) * 0.00015f + 1.5f); // GPAR0
		 ptr[1] = (*((int16_t*) (rawData[ic] + 2)) * 0.00015f + 1.5f); // GPAR1
		 ptr[2] = (*((int16_t*) (rawData[ic] + 4)) * 0.00015f + 1.5f); // GPAR2
		 break;

		 case 1: // RDAUXB: GPAR3–5
		 ptr[3] = (*((int16_t*) (rawData[ic] + 0)) * 0.00015f + 1.5f); // GPAR3
		 ptr[4] = (*((int16_t*) (rawData[ic] + 2)) * 0.00015f + 1.5f); // GPAR4
		 ptr[5] = (*((int16_t*) (rawData[ic] + 4)) * 0.00015f + 1.5f); // GPAR5
		 break;

		 case 2: // RDAUXC: GPCR5, GPCR6
		 ptr[6] = (*((int16_t*) (rawData[ic] + 0)) * 0.00015f + 1.5f); // GPCR5
		 ptr[7] = (*((int16_t*) (rawData[ic] + 2)) * 0.00015f + 1.5f); // GPCR6
		 break;

		 case 3: // RDAUXD: GPDR0, GPDR1, plus v_segment
		 ptr[8] = (*((int16_t*) (rawData[ic] + 0)) * 0.00015f + 1.5f); // GPDR0
		 ptr[9] = (*((int16_t*) (rawData[ic] + 2)) * 0.00015f + 1.5f); // GPDR1

		 // v_segment derived from GPDR2 (offset +4)
		 ic_ad68[ic - 1].v_segment = (*((int16_t*) (rawData[ic] + 4)) * 0.00015f + 1.5f) * 25;
		 break;

		 default:
		 break;
		 }*/
	}
}

/*void bms_parseAuxVoltage(uint8_t rawData[TOTAL_IC][DATA_LEN], float vArr[TOTAL_CELL], uint8_t cell_index, uint8_t muxIndex) {
 // Does not take care of 2950
 for (int ic = 1; ic < TOTAL_IC; ic++) {
 if (cell_index == 4) {
 ic_ad68[ic - 1].temp_ic = (*((int16_t*) (rawData[ic] + 2)) * 0.00015 + 1.5) / 0.0075 - 273;
 continue;
 }

 uint8_t cellArrIndex = cell_index * 3;

 for (int c = cellArrIndex; c < (cellArrIndex + 3); c++) {
 //if (c == 3 || c == 4) continue; // Skip digital output pins
 int ci = c;
 if (c > 4) {
 ci -= 2;
 }

 *((float*) ((uint8_t*) vArr + (ic - 1) * sizeof(ic_ad68_t)) + (ci + 8 * muxIndex)) = *((int16_t*) (rawData[ic] + (c - cellArrIndex) * 2)) * 0.00015 + 1.5;
 //            vArr[ic-1][ci + 8*muxIndex] = *((int16_t *)(rawData[ic] + (c-cellArrIndex)*2)) * 0.00015 + 1.5;

 if (cell_index == 3) {
 ic_ad68[ic - 1].v_segment = (*((int16_t*) (rawData[ic] + 4)) * 0.00015 + 1.5) * 25;
 break;
 }
 }
 }
 }*/

void bms_calculateStats(void) {
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		float min = 999.0;
		float max = -999.0;
		float sum = 0;

		for (int c = 0; c < TOTAL_CELL; c++) {
			float voltage = ic_ad68[ic].v_avgCell[c];
			sum += voltage;
			if (voltage > max) {
				max = voltage;
			}
			if (voltage < min) {
				min = voltage;
			}
		}
		ic_ad68[ic].v_avgCell_min = min;
		ic_ad68[ic].v_avgCell_max = max;
		ic_ad68[ic].v_avgCell_sum = sum;
		//ic_ad68[ic].v_avgCell_avg   = sum / 16.0;
		ic_ad68[ic].v_avgCell_avg = sum / TOTAL_CELL;
		ic_ad68[ic].v_avgCell_delta = max - min;

	}
}

void bms_printVoltage(float vArr[TOTAL_CELL]) {
	printfDmaBT("| IC |");
	for (int i = 0; i < TOTAL_CELL; i++) {
		printfDmaBT("   %2d   |", i + 1);
	}
	printfDmaBT("  Sum   |  Delta |\n");

	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		printfDmaBT("| %2d |", ic);
		for (int c = 0; c < TOTAL_CELL; c++) {
			printfDmaBT("%8.5f|", *((float*) ((uint8_t*) vArr + ic * sizeof(ic_ad68_t)) + c));
		}

		printfDmaBT("%8.5f|", ic_ad68[ic].v_avgCell_sum);
		printfDmaBT("%8.5f|", ic_ad68[ic].v_avgCell_delta);
		printfDma("\n");
	}
}

void bms_printTemps(float tArr[RTH_PER_MODULE]) {
	printfDma("| IC |");
	for (int i = 0; i < TOTAL_CELL; i++) {
		printfDma("  %2d   |", i + 1);
	}
	printfDma("\n");

	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		printfDma("| %2d |", ic);
		for (int c = 0; c < TOTAL_CELL; c++) {
//            printfDma("%6.1f |", tArr[ic][c]);
			printfDma("%6.1f |", *((float*) ((uint8_t*) tArr + ic * sizeof(ic_ad68_t)) + c));

		}
		printfDma("\n");
	}
}

void bms_readAvgCellVoltage(void) {
	uint8_t *cmdList[] = { RDACA, RDACB, RDACC, RDACD, RDACE, RDACF };
//    float  vBuffer[TOTAL_AD68][TOTAL_CELL];

	for (int i = 0; i < 6; i++) {
		bms_receiveData(cmdList[i], rxData, rxPec, rxCc);
		if (bms_checkRxFault(rxData, rxPec, rxCc)) {
			return;
		}
		bms_parseVoltage(rxData, ic_ad68[0].v_avgCell, i);
	}

//    for (int ic = 0; ic < TOTAL_AD68; ic++)
//    {
//        memcpy(ic_ad68[ic].v_avgCell, vBuffer[ic], sizeof(vBuffer[ic]));
//    }

	bms_calculateStats();
	//bms_printVoltage(ic_ad68[0].v_avgCell);
}

/*void bms_readSVoltage(void) {
 uint8_t *cmdList[] = { RDSVA, RDSVB, RDSVC, RDSVD, RDSVE, RDSVF };

 for (int i = 0; i < 6; i++) {
 bms_receiveData(cmdList[i], rxData, rxPec, rxCc);
 if (bms_checkRxFault(rxData, rxPec, rxCc)) {
 return;
 }
 bms_parseVoltage(rxData, ic_ad68[0].v_sCell, i);
 }
 }*/

void bms_readSVoltage(void) {
	uint8_t *cmdList[] = { RDSVA, RDSVB, RDSVC, RDSVD, RDSVE, RDSVF };

	for (int i = 0; i < 6; i++) {
		bms_receiveData(cmdList[i], rxData, rxPec, rxCc);
		if (bms_checkRxFault(rxData, rxPec, rxCc)) {
			return;
		}
		bms_parseVoltage(rxData, ic_ad68[0].v_sCell, i);
	}

	//bms_printVoltage(ic_ad68[0].v_sCell);
}

void bms_getAuxVoltage(uint8_t muxIndex) {
	uint8_t *cmdList[] = { RDAUXA, RDAUXB, RDAUXC, RDAUXD, RDSTATA };

	for (int i = 0; i < 5; i++) {
		bms_receiveData(cmdList[i], rxData, rxPec, rxCc);
		if (bms_checkRxFault(rxData, rxPec, rxCc)) {
			return;
		}
		//printfDma("		oK: %d \n", i);
		bms_parseAuxVoltage(rxData, ic_ad68[0].v_tempSens, i, muxIndex);
		//print_rxData(rxData);
	}
}

/*void bms_openWireCheck(void) {
 ADSV.CONT = 1;      // Continuous
 ADSV.DCP = 0;      // Discharge permitted

 bms_startTimer();

 ADSV.OW = 0b00;   // Open wire on C-ADCS and S-ADCs
 bms_transmitCmd((uint8_t*) &ADSV);
 //    bms_transmitPoll(PLSADC);
 bms_delayMsActive(12);
 bms_readSVoltage();
 //bms_printVoltage(ic_ad68[0].v_sCell);
 uint32_t time1 = bms_getTimCount();
 // S and C is compared

 ADSV.CONT = 0;      // Continuous
 ADSV.DCP = 0;      // Discharge permitted

 bms_wakeupChain();
 ADSV.OW = 0b10;   // Open wire on C-ADCS and S-ADCs
 bms_transmitPoll((uint8_t*) &ADSV);
 //    bms_transmitPoll(PLSADC);
 bms_readSVoltage();
 //bms_printVoltage(ic_ad68[0].v_sCell);
 uint32_t time2 = bms_getTimCount();
 bms_readSVoltage();
 ADSV.OW = 0b01;   // Open wire on C-ADCS and S-ADCs
 bms_transmitPoll((uint8_t*) &ADSV);
 //    bms_transmitPoll(PLSADC);
 bms_readSVoltage();
 //bms_printVoltage(ic_ad68[0].v_sCell);

 uint32_t time = bms_getTimCount();
 bms_stopTimer();

 printfDma("ow time: %ld us\n", time);
 printfDma("ow time 1: %ld us\n", time1);
 printfDma("ow time 2: %ld us\n", time2);
 }*/

/*void bms_openWireCheck(void) {
 bms_startTimer();

 //bms_wakeupChain();


 // Open Wire EVEN Check
 ADSV.CONT = 1;      // Continuous
 ADSV.OW = 0b01;   // Open wire on C-ADCS and S-ADCs
 bms_transmitCmd((uint8_t*) &ADSV);
 bms_delayMsActive(8);
 bms_readSVoltage();


 // Open Wire ODD Check
 ADSV.CONT = 1;      // Continuous
 ADSV.OW = 0b10;   // Open wire on C-ADCS and S-ADCs
 bms_transmitCmd((uint8_t*) &ADSV);
 bms_delayMsActive(8);
 bms_readSVoltage();


 // Turn off Open Wire Check
 ADSV.CONT = 0;      // Continuous
 ADSV.OW = 0b00;   // Open wire on C-ADCS and S-ADCs
 bms_transmitCmd((uint8_t*) &ADSV);


 uint32_t time = bms_getTimCount();
 bms_stopTimer();
 printfDma("ow time: %ld us\n", time);

 }*/

float convertCellTemp(float cellVoltage) {
	static float VREF2 = 3.0; //IC VREF2 ~ 3.0V
	static int R1 = 10000;  // Fixed resistor (10k)
	static int R0 = 10000;  // Thermistor nominal resistance at 25°C
	static int BETA = 3977; //Beta
	static float T0_K = 298.15; // 25°C in Kelvin

	// Check if in range
	/*if (cellVoltage > 2.9 || cellVoltage < 1.30) {
	 // Voltage out of range
	 return 777.7;
	 }*/

	volatile float Rt = R1 * cellVoltage / (VREF2 - cellVoltage);
	return (1.0 / ((1.0 / T0_K) + (1.0 / BETA) * log(Rt / R0))) - 273.15;

	/*// From datasheet
	 static const float tempValues[]    = { -40,  -35,  -30,  -25,  -20,  -15,  -10,   -5,    0,    5,   10,   15,   20,   25,   30,   35,   40,   45,   50,   55,   60,   65,   70,   75,   80,   85,   90,   95,  100,  105,  110,  115,  120};
	 static const float voltageValues[] = {2.44, 2.42, 2.40, 2.38, 2.35, 2.32, 2.27, 2.23, 2.17, 2.11, 2.05, 1.99, 1.92, 1.86, 1.80, 1.74, 1.68, 1.63, 1.59, 1.55, 1.51, 1.48, 1.45, 1.43, 1.40, 1.38, 1.37, 1.35, 1.34, 1.33, 1.32, 1.31, 1.30};
	 static const int   numDataPoints   = sizeof(tempValues) / sizeof(tempValues[0]);

	 // Check if in range
	 if (cellVoltage > 2.44 || cellVoltage < 1.30)
	 {
	 // Voltage out of range
	 return 777.7;
	 }

	 int idx;
	 for (idx = 0; idx < numDataPoints - 1; idx++)
	 {
	 if (cellVoltage > voltageValues[idx + 1]) break;
	 }

	 float x1 = voltageValues[idx];
	 float x2 = voltageValues[idx + 1];
	 float y1 = tempValues[idx];
	 float y2 = tempValues[idx + 1];

	 return y1 + (cellVoltage - x1) * (y2 - y1) / (x2 - x1);*/
}

void bms_parseTemps(void) {
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int c = 0; c < RTH_PER_MODULE; c++) {
			ic_ad68[ic].temp_cell[c] = convertCellTemp(ic_ad68[ic].v_tempSens[c]);
			//printfDma("temp_cell %d: %.2f\n", ic, ic_ad68[ic].temp_cell[c]);
		}
	}
}

void bms_getAuxMeasurement(void) {
	ADAX.OW = 0b0;
	ADAX.CH = 0b0000;
	ADAX.CH4 = 0b0;
	ADAX.PUP = 0b0;

//    bms_startTimer();

	bms_wakeupChain();
	/*bms68_setGpo45(0b10);
	 bms_delayMsActive(5);

	 bms_transmitCmd((uint8_t*) &ADAX);
	 bms_transmitPoll(PLAUX1);

	 bms_getAuxVoltage(0);
	 bms68_setGpo45(0b11);
	 bms_delayMsActive(5);

	 bms_transmitCmd((uint8_t*) &ADAX);
	 bms_transmitPoll(PLAUX1);

	 bms_getAuxVoltage(1);
	 bms68_setGpo45(0b00);*/

	bms_transmitCmd((uint8_t*) &ADAX);
	bms_transmitPoll(PLAUX1);

	bms_getAuxVoltage(0);

	bms_parseTemps();
	//bms_printVoltage(ic_ad68[0].v_tempSens);
	//bms_printTemps(ic_ad68[0].temp_cell);

	//printfDma("dieTemp: %f\n", ic_ad68[0].temp_ic);
	//printfDma("SegVoltage: %f\n", ic_ad68[0].v_segment);
	//printfDma("{\"dieTemp\": %.2f, \"SegVoltage\": %.2f}\n", ic_ad68[0].temp_ic, ic_ad68[0].v_segment);
	//bms_printRthTempsJson();

//    uint32_t time = bms_getTimCount();
//    bms_stopTimer();
//    printfDma("PT: %ld us\n", time);
}

/*void bms_setPwm(ic_ad68_t *ic, uint8_t cell) {
 const uint8_t dutyCycle = 0b0111;
 cell++;                                 // Change from 0 indexing to 1 indexing

 switch (cell) {
 case 1:
 ic->pwma.pwm1 = dutyCycle;
 break;
 case 2:
 ic->pwma.pwm2 = dutyCycle;
 break;
 case 3:
 ic->pwma.pwm3 = dutyCycle;
 break;
 case 4:
 ic->pwma.pwm4 = dutyCycle;
 break;
 case 5:
 ic->pwma.pwm5 = dutyCycle;
 break;
 case 6:
 ic->pwma.pwm6 = dutyCycle;
 break;
 case 7:
 ic->pwma.pwm7 = dutyCycle;
 break;
 case 8:
 ic->pwma.pwm8 = dutyCycle;
 break;
 case 9:
 ic->pwma.pwm9 = dutyCycle;
 break;
 case 10:
 ic->pwma.pwm10 = dutyCycle;
 break;
 case 11:
 ic->pwma.pwm11 = dutyCycle;
 break;
 case 12:
 ic->pwma.pwm12 = dutyCycle;
 break;
 case 13:
 ic->pwmb.pwm13 = dutyCycle;
 break;
 case 14:
 ic->pwmb.pwm14 = dutyCycle;
 break;
 case 15:
 ic->pwmb.pwm15 = dutyCycle;
 break;
 case 16:
 ic->pwmb.pwm16 = dutyCycle;
 break;
 default:
 // Handle invalid cases
 break;
 }
 }*/

void bms_setPwm(ic_ad68_t *ic, uint8_t cell, uint8_t dutyCycle) {
//    const uint8_t dutyCycle = 0b0111;
	cell++;                                 // Change from 0 indexing to 1 indexing

	switch (cell) {
	case 1:
		ic->pwma.pwm1 = dutyCycle;
		break;
	case 2:
		ic->pwma.pwm2 = dutyCycle;
		break;
	case 3:
		ic->pwma.pwm3 = dutyCycle;
		break;
	case 4:
		ic->pwma.pwm4 = dutyCycle;
		break;
	case 5:
		ic->pwma.pwm5 = dutyCycle;
		break;
	case 6:
		ic->pwma.pwm6 = dutyCycle;
		break;
	case 7:
		ic->pwma.pwm7 = dutyCycle;
		break;
	case 8:
		ic->pwma.pwm8 = dutyCycle;
		break;
	case 9:
		ic->pwma.pwm9 = dutyCycle;
		break;
	case 10:
		ic->pwma.pwm10 = dutyCycle;
		break;
	case 11:
		ic->pwma.pwm11 = dutyCycle;
		break;
	case 12:
		ic->pwma.pwm12 = dutyCycle;
		break;
	case 13:
		ic->pwmb.pwm13 = dutyCycle;
		break;
	case 14:
		ic->pwmb.pwm14 = dutyCycle;
		break;
	case 15:
		ic->pwmb.pwm15 = dutyCycle;
		break;
	case 16:
		ic->pwmb.pwm16 = dutyCycle;
		break;
	default:
		// Handle invalid cases
		break;
	}
}

float bms_calculateBalancing(float delta_threshold) {
	float min = 999.0;
	float max = -999.0;

	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		if (ic_ad68[ic].v_avgCell_min < min) {
			min = ic_ad68[ic].v_avgCell_min;
		}
		if (ic_ad68[ic].v_avgCell_max > max) {
			max = ic_ad68[ic].v_avgCell_max;
		}
	}

	if (max - min > delta_threshold) {
		return min + delta_threshold;
	} else {
		return -999;  // No balancing needed
	}
}

/*void bms_startDischarge(float threshold) {
 memset(&ic_ad68[0].pwma, 0, sizeof(ad68_pwma_t));
 memset(&ic_ad68[0].pwmb, 0, sizeof(ad68_pwmb_t));

 //    threshold = 1.5;          // Volts

 for (int ic = 0; ic < TOTAL_AD68; ic++) {
 for (int c = 0; c < TOTAL_CELL; c++) {
 if (ic_ad68[ic].v_avgCell[c] > threshold) {
 bms_setPwm(&ic_ad68[ic], c);
 }
 }
 }

 ic_ad68[0].pwma.pwm1 = 0b0111;  // 4 bit pwm at 937 ms (for testing -> enables discharge for cell 1)

 // The PWM discharge functionality is possible in the standby, REF-UP, extended balancing and in the measure states
 // AND while the discharge timeout has not expired (DCTO ≠ 0)

 ic_ad68[0].cfb_Tx.dcto = 1;     // DC Timer in minutes (DTRNG = 0)
 ic_ad68[0].cfb_Tx.dtmen = 0;    // Disables Discharge Timer Monitor (DTM)
 //    ic_ad68[0].cfb_Tx.dcc = 0b1; // --- High priority discharge (bypasses PWM)

 bms_writeConfigB();             // Send the DCTO Timer config
 bms_writePwmA();                // Send the PWM configs
 bms_writePwmB();                // Send the PWM configs
 }*/

void bms_startDischarge(float threshold) {
	threshold = 5;  // Overwrite the discharge aim voltage (for testing)
	const uint8_t dutyCycle = 0b1111;   // 4 bit pwm at 937 ms

	memset(&ic_ad68[0].pwma, 0, sizeof(ad68_pwma_t));
	memset(&ic_ad68[0].pwmb, 0, sizeof(ad68_pwmb_t));

	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int c = 0; c < TOTAL_CELL; c++) {
			if (ic_ad68[ic].v_avgCell[c] > threshold) {
				printfDma("DISCHARGE: IC %d, CELL %d \n", ic + 1, c + 1);
				bms_setPwm(&ic_ad68[ic], c, dutyCycle);
			} else {
				bms_setPwm(&ic_ad68[ic], c, 0b0000);    // Turn off PWM discharge for that cell
			}
		}
	}

	// for testing -> enables discharge for cell 1
	printfDma("DISCHARGE: IC 1, CELL 1 \n");
	ic_ad68[0].pwma.pwm1 = 0b1111;

	// The PWM discharge functionality is possible in the standby, REF-UP, extended balancing and in the measure states
	// AND while the discharge timeout has not expired (DCTO ≠ 0)

	ic_ad68[0].cfb_Tx.dcto = 1;     // DC Timer in minutes (DTRNG = 0)
	ic_ad68[0].cfb_Tx.dtmen = 0;    // Disables Discharge Timer Monitor (DTM)
//    ic_ad68[0].cfb_Tx.dcc = 0b1; // --- High priority discharge (bypasses PWM)

	bms_writeConfigB();             // Send the DCTO Timer config
	bms_writePwmA();                // Send the PWM configs
	bms_writePwmB();                // Send the PWM configs
}

void bms_stopDischarge(void) {
	bms_wakeupChain();
	bms_transmitCmd(SRST);      // Put all devices to sleep
	printfDma("--- SOFT RESET --- \n");
}

/*void bms29_setGpo(void) {
 ic_ad29.cfa_Tx.gpo1c = 1;      // State control
 ic_ad29.cfa_Tx.gpo1od = 0;      // 1 = Open drain, 0 = push-pull
 ic_ad29.cfa_Tx.gpo2c = 1;      // State control
 ic_ad29.cfa_Tx.gpo2od = 0;      // 1 = Open drain, 0 = push-pull

 bms_writeConfigA();
 }*/

void bms_readVB(void) {
	bms_receiveData(RDVB, rxData, rxPec, rxCc);
	bms_checkRxFault(rxData, rxPec, rxCc);
	bms_printRawData(rxData, rxCc);
	float vb1 = *((int16_t*) (rxData[0] + 2)) * 0.000100 * 396.604395604;
	float vb2 = *((int16_t*) (rxData[0] + 4)) * -0.000085 * 751;
	printfDma("VB: %fV, %fV  \n\n", vb1, vb2);
}

void bms_printRthTempsJson(void) {
	printfDma("{\"rth_temps\":[");
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		printfDma("{\"ic\":%d,\"temps\":[", ic);
		for (int i = 0; i < RTH_PER_MODULE; i++) {
			printfDma("%.3f", ic_ad68[ic].temp_cell[i]);
			if (i < RTH_PER_MODULE - 1) {
				printfDma(",");
			}
		}
		printfDma("]}");
		if (ic < TOTAL_AD68 - 1) {
			printfDma(",");
		}
	}
	printfDma("]}\n");
}

void send_ad68_ui(void) {
	printfDma("[");  // Start of JSON array

	for (int idx = 0; idx < TOTAL_AD68; idx++) {
		ic_ad68_t *d = &ic_ad68[idx];
		printfDma("{\"id\":%d,", idx);  // IC identifier

		// --- cfa_Tx ---
		printfDma("\"cfa_Tx\":{");
		printfDma("\"cth\":%d,\"refon\":%d,\"flag_d\":%d,", d->cfa_Tx.cth, d->cfa_Tx.refon, d->cfa_Tx.flag_d);
		printfDma("\"owa\":%d,\"owrng\":%d,\"soakon\":%d,", d->cfa_Tx.owa, d->cfa_Tx.owrng, d->cfa_Tx.soakon);
		printfDma("\"gpo1to8\":%d,\"gpo9to10\":%d,", d->cfa_Tx.gpo1to8, d->cfa_Tx.gpo9to10);
		printfDma("\"fc\":%d,\"comm_bk\":%d,\"mute_st\":%d,\"snap_st\":%d", d->cfa_Tx.fc, d->cfa_Tx.comm_bk, d->cfa_Tx.mute_st, d->cfa_Tx.snap_st);
		printfDma("},");

		// --- cfa_Rx ---
		printfDma("\"cfa_Rx\":{");
		printfDma("\"cth\":%d,\"refon\":%d,\"flag_d\":%d,", d->cfa_Rx.cth, d->cfa_Rx.refon, d->cfa_Rx.flag_d);
		printfDma("\"owa\":%d,\"owrng\":%d,\"soakon\":%d,", d->cfa_Rx.owa, d->cfa_Rx.owrng, d->cfa_Rx.soakon);
		printfDma("\"gpo1to8\":%d,\"gpo9to10\":%d,", d->cfa_Rx.gpo1to8, d->cfa_Rx.gpo9to10);
		printfDma("\"fc\":%d,\"comm_bk\":%d,\"mute_st\":%d,\"snap_st\":%d", d->cfa_Rx.fc, d->cfa_Rx.comm_bk, d->cfa_Rx.mute_st, d->cfa_Rx.snap_st);
		printfDma("},");

		// --- cfb_Tx ---
		printfDma("\"cfb_Tx\":{");
		printfDma("\"vuv0to7\":%d,\"vuv8to11\":%d,\"vov0to3\":%d,", d->cfb_Tx.vuv0to7, d->cfb_Tx.vuv8to11, d->cfb_Tx.vov0to3);
		printfDma("\"vov4to11\":%d,\"dcto\":%d,\"dtrng\":%d,\"dtmen\":%d,\"dcc\":%d", d->cfb_Tx.vov4to11, d->cfb_Tx.dcto, d->cfb_Tx.dtrng, d->cfb_Tx.dtmen, d->cfb_Tx.dcc);
		printfDma("},");

		// --- cfb_Rx ---
		printfDma("\"cfb_Rx\":{");
		printfDma("\"vuv0to7\":%d,\"vuv8to11\":%d,\"vov0to3\":%d,", d->cfb_Rx.vuv0to7, d->cfb_Rx.vuv8to11, d->cfb_Rx.vov0to3);
		printfDma("\"vov4to11\":%d,\"dcto\":%d,\"dtrng\":%d,\"dtmen\":%d,\"dcc\":%d", d->cfb_Rx.vov4to11, d->cfb_Rx.dcto, d->cfb_Rx.dtrng, d->cfb_Rx.dtmen, d->cfb_Rx.dcc);
		printfDma("},");

		// --- pwma ---
		printfDma("\"pwma\":{");
		printfDma("\"p1\":%d,\"p2\":%d,\"p3\":%d,\"p4\":%d,", d->pwma.pwm1, d->pwma.pwm2, d->pwma.pwm3, d->pwma.pwm4);
		printfDma("\"p5\":%d,\"p6\":%d,\"p7\":%d,\"p8\":%d,", d->pwma.pwm5, d->pwma.pwm6, d->pwma.pwm7, d->pwma.pwm8);
		printfDma("\"p9\":%d,\"p10\":%d,\"p11\":%d,\"p12\":%d", d->pwma.pwm9, d->pwma.pwm10, d->pwma.pwm11, d->pwma.pwm12);
		printfDma("},");

		// --- pwmb ---
		printfDma("\"pwmb\":{");
		printfDma("\"p13\":%d,\"p14\":%d,\"p15\":%d,\"p16\":%d,", d->pwmb.pwm13, d->pwmb.pwm14, d->pwmb.pwm15, d->pwmb.pwm16);
		printfDma("\"rsv\":%u", d->pwmb.rsv);
		printfDma("},");

		// --- v_avgCell ---
		printfDma("\"v_avgCell\":[");
		for (int i = 0; i < TOTAL_CELL; i++) {
			printfDma("%.4f", d->v_avgCell[i]);
			if (i < TOTAL_CELL - 1)
				printfDma(",");
		}
		printfDma("],");

		// --- v_avgCell stats ---
		printfDma("\"v_avgCell_sum\":%.4f,", d->v_avgCell_sum);
		printfDma("\"v_avgCell_avg\":%.4f,", d->v_avgCell_avg);
		printfDma("\"v_avgCell_min\":%.4f,", d->v_avgCell_min);
		printfDma("\"v_avgCell_max\":%.4f,", d->v_avgCell_max);
		printfDma("\"v_avgCell_delta\":%.4f,", d->v_avgCell_delta);

		// --- v_sCell ---
		printfDma("\"v_sCell\":[");
		for (int i = 0; i < TOTAL_CELL; i++) {
			printfDma("%.4f", d->v_sCell[i]);
			if (i < TOTAL_CELL - 1)
				printfDma(",");
		}
		printfDma("],");

		// --- v_tempSens ---
		printfDma("\"v_tempSens\":[");
		for (int i = 0; i < RTH_PER_MODULE; i++) {
			printfDma("%.2f", d->v_tempSens[i]);
			if (i < RTH_PER_MODULE - 1)
				printfDma(",");
		}
		printfDma("],");

		// --- temp_cell ---
		printfDma("\"temp_cell\":[");
		for (int i = 0; i < RTH_PER_MODULE; i++) {
			printfDma("%.2f", d->temp_cell[i]);
			if (i < RTH_PER_MODULE - 1)
				printfDma(",");
		}
		printfDma("],");

		// --- Final floats ---
		printfDma("\"v_segment\":%.2f,", d->v_segment);
		printfDma("\"temp_ic\":%.2f", d->temp_ic);

		printfDma("}");  // End of this IC JSON

		if (idx < TOTAL_AD68 - 1)
			printfDma(",");
	}

	printfDma("]\n");  // End of JSON array
}

void bms_balancingMeasureVoltage(void) {
	// 6830
	// ADSV For triggering single shot S conversion (stops PWM) while C in unaffected
	// So this stops PWM and wait for S to finish
	// Then read the S voltage
	// Potential improvement: S vs C ADC comparison

	ADSV.CONT = 0;      // Continuous
	ADSV.DCP = 0;      // Discharge permitted
	ADSV.OW = 0b00;   // Open wire on C-ADCS and S-ADCs

	bms_transmitCmd((uint8_t*) &ADSV);

	bms_transmitPoll(PLSADC);
	bms_readSVoltage();
}

void bms_startBalancing(float deltaThreshold) {
	float dischargeThreshold = bms_calculateBalancing(deltaThreshold);

	if (dischargeThreshold > 0) {
		bms_startDischarge(dischargeThreshold);
	}
}

void ad68_dump_csv_bt(void) {
	static int header_printed = 0;
	int first;

#define PRINT_COMMA do { if (first) first = 0; else printfDmaBT(","); } while (0)

	// --- header (once) ---
	if (!header_printed) {
		first = 1;
		PRINT_COMMA;
		printfDmaBT("Num. Slave");
		PRINT_COMMA;
		printfDmaBT("Primeira Tensao");
		PRINT_COMMA;
		printfDmaBT("Segunda Tensao");
		PRINT_COMMA;
		printfDmaBT("Terceira Tensao");
		PRINT_COMMA;
		printfDmaBT("Temepratura 1");
		PRINT_COMMA;
		printfDmaBT("Temepratura 2");
		PRINT_COMMA;
		printfDmaBT("Temepratura 3");
		PRINT_COMMA;
		printfDmaBT("Temepratura 4");
		PRINT_COMMA;
		printfDmaBT("Temepratura 5");
		PRINT_COMMA;
		printfDmaBT("Tempo em ms");
		printfDmaBT("\r\n");
		header_printed = 1;
	}

	// --- rows ---
	for (int m = 0; m < TOTAL_AD68; ++m) {
		first = 1;

		// module index
		PRINT_COMMA;
		printfDmaBT("%d", m);

		// v_avgCell[0..2] (guard in case TOTAL_CELL < 3)
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].v_avgCell[0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].v_avgCell[1 < TOTAL_CELL ? 1 : 0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].v_avgCell[2 < TOTAL_CELL ? 2 : 0]);

		// temp_cell[0..4] (guard in case RTH_PER_MODULE < 5)
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].temp_cell[0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].temp_cell[1 < RTH_PER_MODULE ? 1 : 0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].temp_cell[2 < RTH_PER_MODULE ? 2 : 0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].temp_cell[3 < RTH_PER_MODULE ? 3 : 0]);
		PRINT_COMMA;
		printfDmaBT("%g", (double) ic_ad68[m].temp_cell[4 < RTH_PER_MODULE ? 4 : 0]);

		// timestamp in milliseconds (HAL_GetTick already returns ms)
		uint32_t ts = HAL_GetTick();
		PRINT_COMMA;
		printfDmaBT("%lu", (unsigned long) ts);

		printfDmaBT("\r\n");
	}

#undef PRINT_COMMA
}

void bms_openWireCheck(bms_ow_status_t *ow_status[TOTAL_AD68][TOTAL_CELL]) {

	//backup to then skip the OW readings
	ic_ad68_t ic_ad68_backup[TOTAL_AD68];
	memcpy(ic_ad68_backup, ic_ad68, sizeof(ic_ad68_backup));

	float cellReference[TOTAL_AD68][TOTAL_CELL];
	float cellVoltage_OW[TOTAL_AD68][TOTAL_CELL];

	//Inital measurment as reference, no open wire check
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int cell = 0; cell < TOTAL_CELL; cell++) {
			cellReference[ic][cell] = ic_ad68[ic].v_sCell[cell];
		}
	}

	// --- Open Wire EVEN Check ---
	ADSV.CONT = 1;      // Continuous
	ADSV.DCP = 0;      // Discharge permitted
	ADSV.OW = 0b01;   // Open wire on C-ADCS and S-ADCs

	bms_wakeupChain(); //before sending commmands
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		bms_transmitCmd((uint8_t*) &ADSV);
		//printConsole("	g");
	}

	bms_delayMsActive(12);

	// Read EVEN result
	bms_readSVoltage();
	bms_printVoltage(ic_ad68[0].v_sCell);
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int cell = 0; cell < TOTAL_CELL; cell++) {
			cellVoltage_OW[ic][cell] = ic_ad68[ic].v_sCell[cell];
		}
	}

	//COMPARE HERE VOLTAGES
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int cell = 0; cell < TOTAL_CELL; cell++) {

			if (((cell + 1) % 2) != 0)
				continue;  // skip odd cells here, idk if this works as expected

			float Vref = cellReference[ic][cell];
			float Vow = cellVoltage_OW[ic][cell];

			if (Vref < OW_UV_IGNORE_THRESH) {       // too low to decide by ratio
				//*ow_status[ic][cell] = OW_INVALID_LOWV;
				printConsole("cell %d in ic %d is INVALID_LOWV\n", cell + 1, ic + 1);
				continue;
			}

			float cellRatio = Vow / Vref;
			if (cellRatio >= OW_RATIO_MIN && cellRatio <= OW_RATIO_MAX) {
				//*ow_status[ic][cell] = OW_INTACT;
			} else if (cellRatio < OW_OPEN_EDGE) {
				//*ow_status[ic][cell] = OW_OPEN;
				printConsole("cell %d in ic %d is OPEN\n", cell + 1, ic + 1);
			} else {
				//*ow_status[ic][cell] = OW_SUSPECT;
				printConsole("cell %d in ic %d is SUSPECT\n", cell + 1, ic + 1);
			}
		}
	}

	// --- Open Wire ODD Check ---
	ADSV.CONT = 1;
	ADSV.OW = 0b10;
	bms_wakeupChain(); //before sending commmands
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		bms_transmitCmd((uint8_t*) &ADSV);
	}

	bms_delayMsActive(12);

	// Read ODD result
	bms_readSVoltage();
	bms_printVoltage(ic_ad68[0].v_sCell);
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int cell = 0; cell < TOTAL_CELL; cell++) {
			cellVoltage_OW[ic][cell] = ic_ad68[ic].v_sCell[cell];
		}
	}

	//COMPARE VOLTAGES HEre
	for (int ic = 0; ic < TOTAL_AD68; ic++) {
		for (int cell = 0; cell < TOTAL_CELL; cell++) {

			if (((cell + 1) % 2) == 0)
				continue;  // skip even cells here, idk if this works as expected

			float Vref = cellReference[ic][cell];
			float Vow = cellVoltage_OW[ic][cell];

			if (Vref < OW_UV_IGNORE_THRESH) {       // too low to decide by ratio
				//*ow_status[ic][cell] = OW_INVALID_LOWV;
				printConsole("cell %d in ic %d is INVALID_LOWV\n", cell + 1, ic + 1);
				continue;
			}

			float cellRatio = Vow / Vref;
			if (cellRatio >= OW_RATIO_MIN && cellRatio <= OW_RATIO_MAX) {
				//*ow_status[ic][cell] = OW_INTACT;
			} else if (cellRatio < OW_OPEN_EDGE) {
				//*ow_status[ic][cell] = OW_OPEN;
				printConsole("cell %d in ic %d is OPEN\n", cell + 1, ic + 1);
			} else {
				//*ow_status[ic][cell] = OW_SUSPECT;
				printConsole("cell %d in ic %d is SUSPECT\n", cell + 1, ic + 1);
			}
		}
	}

	//skip OW readings by restoring the initial matrix, before executing OW commands
	memcpy(ic_ad68, ic_ad68_backup, sizeof(ic_ad68_backup));

	bms_wakeupChain(); //before sending commmands
	// Turn off Open Wire Check
	ADSV.CONT = 0;
	ADSV.OW = 0b00;
	bms_transmitCmd((uint8_t*) &ADSV);

}

/**
 * @brief Sends a CAN message via the CAN TX FIFO.
 * @param hcan: Pointer to the CAN handle (e.g., &hcan1).
 * @param canID: CAN ID of the message (standard 11-bit).
 * @param dataLength: Length of the message data in bytes (0 to 8 for Classic CAN).
 * @param data: Pointer to the message data array (up to 8 bytes for Classic CAN).
 * @retval HAL_StatusTypeDef: Returns HAL_OK if successful, otherwise HAL_ERROR.
 */
/*HAL_StatusTypeDef Slaves_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData) {
 CAN_TxHeaderTypeDef TxH;
 uint32_t txMailbox;

 if (dataLength > 8)
 return HAL_ERROR;

 TxH.StdId = (uint32_t) canID & 0x7FF;
 TxH.ExtId = 0;
 TxH.IDE = CAN_ID_STD;
 TxH.RTR = CAN_RTR_DATA;
 TxH.DLC = (uint8_t) dataLength;
 TxH.TransmitGlobalTime = DISABLE;

 if (HAL_CAN_AddTxMessage(hcan, &TxH, (uint8_t*) TxData, &txMailbox) != HAL_OK) {
 return HAL_ERROR;
 }
 return HAL_OK;
 }*/
HAL_StatusTypeDef Slaves_CAN_SendMessage(CAN_HandleTypeDef *hcan, uint32_t canID, uint32_t dataLength, const uint8_t *TxData) {
	if (dataLength > 8U) {
		//TODO: Add error handling i guesss
		//return HAL_ERROR;
		return HAL_OK;
	}

	// Just enqueue, do NOT send directly
	return CAN_TX_Add_To_Queue(hcan, canID, (uint8_t) dataLength, TxData);
}

/* Scale helpers:
 *  - Voltage:   3.24879 V -> 3248  (0.001 V resolution, trunc)
 *  - Temp:      21.08 °C  -> 2108  (0.01 °C resolution, trunc)
 */
uint16_t conv_voltage(float v) {
	if (v < 0.0f)
		v = 0.0f;
	uint32_t raw = (uint32_t) (v * 1000.0f);     // truncate
	if (raw > 65535U)
		raw = 65535U;
	return (uint16_t) raw;
}

HAL_StatusTypeDef CAN_Send_AD68_Voltages_Module(CAN_HandleTypeDef *hcan, uint8_t module) {
	if (module >= TOTAL_AD68 || module >= 12)
		return HAL_ERROR;

	ic_ad68_t *d = &ic_ad68[module];
	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;   // module 0 -> slave 1, etc.

	switch (slave) {

	case 1: {
		struct ams_slave_01_voltage_id_1_t v1;
		v1.cell_voltage_1 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_2 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_3 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_4 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_01_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_voltage_id_2_t v2;
		v2.cell_voltage_5 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_6 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_7 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_8 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_01_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_voltage_id_3_t v3;
		v3.cell_voltage_9 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_10 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_11 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_12 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_01_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 2: {
		struct ams_slave_02_voltage_id_1_t v1;
		v1.cell_voltage_13 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_14 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_15 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_16 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_02_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_voltage_id_2_t v2;
		v2.cell_voltage_17 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_18 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_19 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_20 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_02_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_voltage_id_3_t v3;
		v3.cell_voltage_21 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_22 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_23 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_24 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_02_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 3: {
		struct ams_slave_03_voltage_id_1_t v1;
		v1.cell_voltage_25 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_26 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_27 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_28 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_03_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_03_voltage_id_2_t v2;
		v2.cell_voltage_29 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_30 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_31 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_32 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_03_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_03_voltage_id_3_t v3;
		v3.cell_voltage_33 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_34 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_35 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_36 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_03_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 4: {
		struct ams_slave_04_voltage_id_1_t v1;
		v1.cell_voltage_37 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_38 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_39 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_40 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_04_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_04_voltage_id_2_t v2;
		v2.cell_voltage_41 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_42 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_43 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_44 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_04_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_04_voltage_id_3_t v3;
		v3.cell_voltage_45 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_46 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_47 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_48 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_04_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 5: {
		struct ams_slave_05_voltage_id_1_t v1;
		v1.cell_voltage_49 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_50 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_51 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_52 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_05_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_05_voltage_id_2_t v2;
		v2.cell_voltage_53 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_54 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_55 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_56 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_05_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_05_voltage_id_3_t v3;
		v3.cell_voltage_57 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_58 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_59 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_60 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_05_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 6: {
		struct ams_slave_06_voltage_id_1_t v1;
		v1.cell_voltage_61 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_62 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_63 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_64 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_06_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_06_voltage_id_2_t v2;
		v2.cell_voltage_65 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_66 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_67 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_68 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_06_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_06_voltage_id_3_t v3;
		v3.cell_voltage_69 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_70 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_71 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_72 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_06_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 7: {
		struct ams_slave_07_voltage_id_1_t v1;
		v1.cell_voltage_73 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_74 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_75 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_76 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_07_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_07_voltage_id_2_t v2;
		v2.cell_voltage_77 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_78 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_79 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_80 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_07_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_07_voltage_id_3_t v3;
		v3.cell_voltage_81 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_82 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_83 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_84 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_07_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 8: {
		struct ams_slave_08_voltage_id_1_t v1;
		v1.cell_voltage_85 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_86 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_87 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_88 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_08_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_08_voltage_id_2_t v2;
		v2.cell_voltage_89 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_90 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_91 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_92 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_08_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_08_voltage_id_3_t v3;
		v3.cell_voltage_93 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_94 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_95 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_96 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_08_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 9: {
		struct ams_slave_09_voltage_id_1_t v1;
		v1.cell_voltage_97 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_98 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_99 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_100 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_09_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_09_voltage_id_2_t v2;
		v2.cell_voltage_101 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_102 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_103 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_104 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_09_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_09_voltage_id_3_t v3;
		v3.cell_voltage_105 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_106 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_107 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_108 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_09_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 10: {
		struct ams_slave_10_voltage_id_1_t v1;
		v1.cell_voltage_109 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_110 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_111 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_112 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_10_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_10_voltage_id_2_t v2;
		v2.cell_voltage_113 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_114 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_115 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_116 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_10_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_10_voltage_id_3_t v3;
		v3.cell_voltage_117 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_118 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_119 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_120 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_10_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 11: {
		struct ams_slave_11_voltage_id_1_t v1;
		v1.cell_voltage_121 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_122 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_123 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_124 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_11_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_11_voltage_id_2_t v2;
		v2.cell_voltage_125 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_126 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_127 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_128 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_11_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_11_voltage_id_3_t v3;
		v3.cell_voltage_129 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_130 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_131 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_132 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_11_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 12: {
		struct ams_slave_12_voltage_id_1_t v1;
		v1.cell_voltage_133 = conv_voltage(d->v_avgCell[0]);
		v1.cell_voltage_134 = conv_voltage(d->v_avgCell[1]);
		v1.cell_voltage_135 = conv_voltage(d->v_avgCell[2]);
		v1.cell_voltage_136 = conv_voltage(d->v_avgCell[3]);
		len = ams_slave_12_voltage_id_1_pack(data, &v1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_VOLTAGE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_12_voltage_id_2_t v2;
		v2.cell_voltage_137 = conv_voltage(d->v_avgCell[4]);
		v2.cell_voltage_138 = conv_voltage(d->v_avgCell[5]);
		v2.cell_voltage_139 = conv_voltage(d->v_avgCell[6]);
		v2.cell_voltage_140 = conv_voltage(d->v_avgCell[7]);
		len = ams_slave_12_voltage_id_2_pack(data, &v2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_VOLTAGE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_12_voltage_id_3_t v3;
		v3.cell_voltage_141 = conv_voltage(d->v_avgCell[8]);
		v3.cell_voltage_142 = conv_voltage(d->v_avgCell[9]);
		v3.cell_voltage_143 = conv_voltage(d->v_avgCell[10]);
		v3.cell_voltage_144 = conv_voltage(d->v_avgCell[11]);
		len = ams_slave_12_voltage_id_3_pack(data, &v3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_VOLTAGE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	default:
		//return HAL_ERROR;
		return HAL_OK;
	}

	return HAL_OK;
}

uint16_t conv_temp(float t) {
	uint32_t raw = (uint32_t) (t * 100.0f);      // truncate
	if (raw > 65535U)
		raw = 65535U;
	return (uint16_t) raw;
}

HAL_StatusTypeDef CAN_Send_AD68_Temperatures_Module(CAN_HandleTypeDef *hcan, uint8_t module) {
	if (module >= TOTAL_AD68 || module >= 12)
		return HAL_ERROR;

	ic_ad68_t *d = &ic_ad68[module];
	uint8_t data[8];
	int len;
	uint8_t slave = module + 1;

	switch (slave) {

	case 1: {
		struct ams_slave_01_temperature_id_1_t t1;
		t1.temperature_value_1 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_2 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_3 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_4 = conv_temp(d->temp_cell[3]);
		len = ams_slave_01_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_temperature_id_2_t t2;
		t2.temperature_value_5 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_6 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_7 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_8 = conv_temp(d->temp_cell[7]);
		len = ams_slave_01_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_01_temperature_id_3_t t3;
		t3.temperature_value_9 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_10 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_11 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_12 = conv_temp(d->temp_cell[11]);
		len = ams_slave_01_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_01_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 2: {
		struct ams_slave_02_temperature_id_1_t t1;
		t1.temperature_value_13 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_14 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_15 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_16 = conv_temp(d->temp_cell[3]);
		len = ams_slave_02_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_temperature_id_2_t t2;
		t2.temperature_value_17 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_18 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_19 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_20 = conv_temp(d->temp_cell[7]);
		len = ams_slave_02_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_02_temperature_id_3_t t3;
		t3.temperature_value_21 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_22 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_23 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_24 = conv_temp(d->temp_cell[11]);
		len = ams_slave_02_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_02_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 3: {
		struct ams_slave_03_temperature_id_1_t t1;
		t1.temperature_value_25 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_26 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_27 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_28 = conv_temp(d->temp_cell[3]);
		len = ams_slave_03_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_03_temperature_id_2_t t2;
		t2.temperature_value_29 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_30 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_31 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_32 = conv_temp(d->temp_cell[7]);
		len = ams_slave_03_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_03_temperature_id_3_t t3;
		t3.temperature_value_33 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_34 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_35 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_36 = conv_temp(d->temp_cell[11]);
		len = ams_slave_03_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_03_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 4: {
		struct ams_slave_04_temperature_id_1_t t1;
		t1.temperature_value_37 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_38 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_39 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_40 = conv_temp(d->temp_cell[3]);
		len = ams_slave_04_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_04_temperature_id_2_t t2;
		t2.temperature_value_41 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_42 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_43 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_44 = conv_temp(d->temp_cell[7]);
		len = ams_slave_04_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_04_temperature_id_3_t t3;
		t3.temperature_value_45 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_46 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_47 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_48 = conv_temp(d->temp_cell[11]);
		len = ams_slave_04_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_04_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 5: {
		struct ams_slave_05_temperature_id_1_t t1;
		t1.temperature_value_49 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_50 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_51 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_52 = conv_temp(d->temp_cell[3]);
		len = ams_slave_05_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_05_temperature_id_2_t t2;
		t2.temperature_value_53 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_54 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_55 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_56 = conv_temp(d->temp_cell[7]);
		len = ams_slave_05_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_05_temperature_id_3_t t3;
		t3.temperature_value_57 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_58 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_59 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_60 = conv_temp(d->temp_cell[11]);
		len = ams_slave_05_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_05_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 6: {
		struct ams_slave_06_temperature_id_1_t t1;
		t1.temperature_value_61 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_62 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_63 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_64 = conv_temp(d->temp_cell[3]);
		len = ams_slave_06_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_06_temperature_id_2_t t2;
		t2.temperature_value_65 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_66 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_67 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_68 = conv_temp(d->temp_cell[7]);
		len = ams_slave_06_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_06_temperature_id_3_t t3;
		t3.temperature_value_69 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_70 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_71 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_72 = conv_temp(d->temp_cell[11]);
		len = ams_slave_06_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_06_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 7: {
		struct ams_slave_07_temperature_id_1_t t1;
		t1.temperature_value_73 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_74 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_75 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_76 = conv_temp(d->temp_cell[3]);
		len = ams_slave_07_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_07_temperature_id_2_t t2;
		t2.temperature_value_77 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_78 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_79 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_80 = conv_temp(d->temp_cell[7]);
		len = ams_slave_07_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_07_temperature_id_3_t t3;
		t3.temperature_value_81 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_82 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_83 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_84 = conv_temp(d->temp_cell[11]);
		len = ams_slave_07_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_07_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 8: {
		struct ams_slave_08_temperature_id_1_t t1;
		t1.temperature_value_85 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_86 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_87 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_88 = conv_temp(d->temp_cell[3]);
		len = ams_slave_08_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_08_temperature_id_2_t t2;
		t2.temperature_value_89 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_90 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_91 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_92 = conv_temp(d->temp_cell[7]);
		len = ams_slave_08_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_08_temperature_id_3_t t3;
		t3.temperature_value_93 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_94 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_95 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_96 = conv_temp(d->temp_cell[11]);
		len = ams_slave_08_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_08_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 9: {
		struct ams_slave_09_temperature_id_1_t t1;
		t1.temperature_value_97 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_98 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_99 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_100 = conv_temp(d->temp_cell[3]);
		len = ams_slave_09_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_09_temperature_id_2_t t2;
		t2.temperature_value_101 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_102 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_103 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_104 = conv_temp(d->temp_cell[7]);
		len = ams_slave_09_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_09_temperature_id_3_t t3;
		t3.temperature_value_105 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_106 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_107 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_108 = conv_temp(d->temp_cell[11]);
		len = ams_slave_09_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_09_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 10: {
		struct ams_slave_10_temperature_id_1_t t1;
		t1.temperature_value_109 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_110 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_111 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_112 = conv_temp(d->temp_cell[3]);
		len = ams_slave_10_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_10_temperature_id_2_t t2;
		t2.temperature_value_113 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_114 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_115 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_116 = conv_temp(d->temp_cell[7]);
		len = ams_slave_10_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_10_temperature_id_3_t t3;
		t3.temperature_value_117 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_118 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_119 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_120 = conv_temp(d->temp_cell[11]);
		len = ams_slave_10_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_10_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 11: {
		struct ams_slave_11_temperature_id_1_t t1;
		t1.temperature_value_121 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_122 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_123 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_124 = conv_temp(d->temp_cell[3]);
		len = ams_slave_11_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_11_temperature_id_2_t t2;
		t2.temperature_value_125 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_126 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_127 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_128 = conv_temp(d->temp_cell[7]);
		len = ams_slave_11_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_11_temperature_id_3_t t3;
		t3.temperature_value_129 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_130 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_131 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_132 = conv_temp(d->temp_cell[11]);
		len = ams_slave_11_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_11_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	case 12: {
		struct ams_slave_12_temperature_id_1_t t1;
		t1.temperature_value_133 = conv_temp(d->temp_cell[0]);
		t1.temperature_value_134 = conv_temp(d->temp_cell[1]);
		t1.temperature_value_135 = conv_temp(d->temp_cell[2]);
		t1.temperature_value_136 = conv_temp(d->temp_cell[3]);
		len = ams_slave_12_temperature_id_1_pack(data, &t1, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_TEMPERATURE_ID_1_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_12_temperature_id_2_t t2;
		t2.temperature_value_137 = conv_temp(d->temp_cell[4]);
		t2.temperature_value_138 = conv_temp(d->temp_cell[5]);
		t2.temperature_value_139 = conv_temp(d->temp_cell[6]);
		t2.temperature_value_140 = conv_temp(d->temp_cell[7]);
		len = ams_slave_12_temperature_id_2_pack(data, &t2, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_TEMPERATURE_ID_2_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;

		struct ams_slave_12_temperature_id_3_t t3;
		t3.temperature_value_141 = conv_temp(d->temp_cell[8]);
		t3.temperature_value_142 = conv_temp(d->temp_cell[9]);
		t3.temperature_value_143 = conv_temp(d->temp_cell[10]);
		t3.temperature_value_144 = conv_temp(d->temp_cell[11]);
		len = ams_slave_12_temperature_id_3_pack(data, &t3, sizeof(data));
		if (len < 0)
			return HAL_ERROR;
		if (Slaves_CAN_SendMessage(hcan, AMS_SLAVE_12_TEMPERATURE_ID_3_FRAME_ID, len, data) != HAL_OK)
			return HAL_ERROR;
		break;
	}

	default:
		//return HAL_ERROR;
		return HAL_OK;
	}

	return HAL_OK;
}

void CAN_Send_AD68_All(CAN_HandleTypeDef *hcan) {
	for (uint8_t m = 0; m < TOTAL_AD68 && m < 12; m++) {
		CAN_Send_AD68_Voltages_Module(hcan, m);
		CAN_Send_AD68_Temperatures_Module(hcan, m);
	}
}

