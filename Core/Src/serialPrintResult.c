/*******************************************************************************
 Copyright (c) 2020 - Analog Devices Inc. All Rights Reserved.
 This software is proprietary & confidential to Analog Devices, Inc.
 and its licensor.
 ******************************************************************************
 * @file:    serialPrintResult.c
 * @brief:   Print IO terminal functions
 * @version: $Revision$
 * @date:    $Date$
 * Developed by: ADIBMS Software team, Bangalore, India
 *****************************************************************************/
/*! \addtogroup PRINT RESULT
 *  @{
 */

/*! @addtogroup RESULT PRINT
 *  @{
 */
#include <math.h>
#include "common.h"
#include "serialPrintResult.h"
#include "uartDMA.h"

/**
 *******************************************************************************
 * Function: printWriteConfig
 * @brief Print write config A/B result.
 *
 * @details This function Print write config result into terminal.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @param [in]  grp      Enum type of resistor group
 *
 * @return None
 *
 *******************************************************************************
 */
void printWriteConfig(uint8_t tIC, cell_asic *IC, TYPE type, GRP grp) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfUI("Write Config A:\r\n");
				printfUI("0x%X, ", IC[ic].configa.tx_data[0]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[1]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[2]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[3]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[4]);
				printfUI("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);
			} else if (grp == B) {
				printfUI("Write Config B:\r\n");
				printfUI("0x%X, ", IC[ic].configb.tx_data[0]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[1]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[2]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[3]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[4]);
				printfUI("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else if (grp == ALL_GRP) {
				printfUI("Write Config A:\r\n");
				printfUI("0x%X, ", IC[ic].configa.tx_data[0]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[1]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[2]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[3]);
				printfUI("0x%X, ", IC[ic].configa.tx_data[4]);
				printfUI("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);

				printfUI("Write Config B:\r\n");
				printfUI("0x%X, ", IC[ic].configb.tx_data[0]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[1]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[2]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[3]);
				printfUI("0x%X, ", IC[ic].configb.tx_data[4]);
				printfUI("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else {
				printfUI("Wrong Register Group Select\r\n");
			}
		}
	}
}

/**
 *******************************************************************************
 * Function: printReadConfig
 * @brief Print read config result.
 *
 * @details This function Print read config result into terminal.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @param [in]  grp      Enum type of resistor group
 *
 * @return None
 *
 *******************************************************************************
 */
void printReadConfig(uint8_t tIC, cell_asic *IC, TYPE type, GRP grp) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfUI("Read Config A:\r\n");
				printfUI("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfUI("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfUI("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfUI("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfUI("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfUI("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfUI("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfUI("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfUI("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfUI("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfUI("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfUI("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfUI("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfUI("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfUI("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfUI("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfUI("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfUI("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));
				printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == B) {
				printfUI("Read Config B:\r\n");
				printfUI("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfUI("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfUI("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfUI("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfUI("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfUI("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == ALL_GRP) {
				printfUI("Read Config A:\r\n");
				printfUI("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfUI("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfUI("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfUI("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfUI("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfUI("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfUI("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfUI("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfUI("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfUI("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfUI("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfUI("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfUI("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfUI("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfUI("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfUI("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfUI("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfUI("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));

				printfUI("Read Config B:\r\n");
				printfUI("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfUI("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfUI("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfUI("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfUI("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfUI("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else {
				printfUI("Wrong Register Group Select\r\n");
			}
		}
	}
}

/**
 *******************************************************************************
 * Function: printVoltages
 * @brief Print Voltages.
 *
 * @details This function Print Voltages into IAR I/O terminal.
 *
 * Parameters:
 * @param [in]	tIC    Total IC
 *
 * @param [in]  *IC    cell_asic stucture pointer
 *
 * @param [in]  type    Enum type of resistor group
 *
 * @return None
 *
 *******************************************************************************
 */
void printVoltages(uint8_t tIC, cell_asic *IC, TYPE type) {
	float voltage;
	int16_t temp;
	uint8_t channel;
	if ((type == Cell) || (type == AvgCell) || (type == F_volt) || (type == S_volt)) {
		channel = CELL;
	} else if (type == Aux) {
		channel = AUX;
	} else if (type == RAux) {
		channel = RAUX;
	}
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:", (ic + 1));
		for (uint8_t index = 0; index < channel; index++) {
			if (type == Cell) {
				temp = IC[ic].cell.c_codes[index];
			} else if (type == AvgCell) {
				temp = IC[ic].acell.ac_codes[index];
			} else if (type == F_volt) {
				temp = IC[ic].fcell.fc_codes[index];
			} else if (type == S_volt) {
				temp = IC[ic].scell.sc_codes[index];
			} else if (type == Aux) {
				temp = IC[ic].aux.a_codes[index];
			} else if (type == RAux) {
				temp = IC[ic].raux.ra_codes[index];
			}
			voltage = getVoltage(temp);
			if (type == Cell) {
				printfUI("C%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfUI("PECError:%d", IC[ic].cccrc.cell_pec);
				}
			} else if (type == AvgCell) {
				printfUI("AC%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfUI("PECError:%d", IC[ic].cccrc.acell_pec);
				}
			} else if (type == F_volt) {
				printfUI("FC%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfUI("PECError:%d", IC[ic].cccrc.fcell_pec);
				}
			} else if (type == S_volt) {
				printfUI("S%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfUI("PECError:%d", IC[ic].cccrc.scell_pec);
				}
			} else if (type == Aux) {
			    if (index <= 9) {
			        printfUI("AUX%d=%.4fV,", (index + 1), voltage);
			    } else if (index == 10) {
			        printfUI("VMV:%.4fV,", (20 * voltage));
			    } else if (index == 11) {
			        printfUI("V+:%.4fV,", (20 * voltage));
			        printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			        printfUI("PECError:%d", IC[ic].cccrc.aux_pec);
			    }
			} else if (type == RAux) {
				//NOTE: Added printing temperatures along aux voltage readings
			    float temperature = getTemperature(temp);
			    printfUI("RAUX%d=%.4fV,", (index + 1), voltage);
			    printfUI("RT%d=%.2fC,", (index + 1), temperature);

			    if (index == (channel - 1)) {
			        printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			        printfUI("PECError:%d", IC[ic].cccrc.raux_pec);
			    }
			} else {
				printfUI("Wrong Register Group Select\r\n");
			}
		}
		printfUI("\r\n\r\n");
	}
}

/**
 *******************************************************************************
 * Function: PrintStatus
 * @brief Print status reg. result.
 *
 * @details This function Print status result into IAR I/O terminal.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @param [in]  grp      Enum type of resistor group
 *
 * @return None
 *
 *******************************************************************************
 */
void printStatus(uint8_t tIC, cell_asic *IC, TYPE type, GRP grp) {
	float voltage;
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Status) {
			if (grp == A) {
				printfUI("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfUI("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfUI("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfUI("ITMP:%f�C\r\n", (voltage / 0.0075) - 273);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == B) {
				printfUI("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfUI("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfUI("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfUI("VR4K:%fV\r\n", voltage);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == C) {
				printfUI("Status C:\r\n");
				printfUI("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfUI("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfUI("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfUI("OTP1_MED:0x%X ", IC[ic].statc.otp1_med);
				printfUI("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfUI("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfUI("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfUI("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfUI("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfUI("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfUI("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfUI("THSD:0x%X, ", IC[ic].statc.thsd);
				printfUI("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfUI("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfUI("COMP:0x%X, ", IC[ic].statc.comp);
				printfUI("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfUI("VDE:0x%X\r\n", IC[ic].statc.vde);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == D) {
				printfUI("Status D:\r\n");
				printfUI("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfUI("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfUI("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfUI("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfUI("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfUI("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfUI("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfUI("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfUI("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfUI("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfUI("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfUI("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfUI("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfUI("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfUI("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfUI("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfUI("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfUI("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfUI("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfUI("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfUI("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfUI("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfUI("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfUI("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfUI("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfUI("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfUI("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfUI("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfUI("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfUI("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfUI("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfUI("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfUI("CTS:0x%X, ", IC[ic].statd.cts);
				printfUI("CT:0x%X, ", IC[ic].statd.ct);
				printfUI("OC_CNTR:0x%X\r\n", IC[ic].statd.oc_cntr);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == E) {
				printfUI("Status E:\r\n");
				printfUI("GPI:0x%X, ", IC[ic].state.gpi);
				printfUI("REV_ID:0x%X\r\n", IC[ic].state.rev);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == ALL_GRP) {
				printfUI("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfUI("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfUI("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfUI("ITMP:%f�C\r\n\r\n", (voltage / 0.0075) - 273);

				printfUI("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfUI("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfUI("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfUI("VR4K:%fV\r\n\r\n", voltage);

				printfUI("Status C:\r\n");
				printfUI("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfUI("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfUI("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfUI("OTP1_MED:0x%X, ", IC[ic].statc.otp1_med);
				printfUI("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfUI("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfUI("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfUI("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfUI("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfUI("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfUI("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfUI("THSD:0x%X, ", IC[ic].statc.thsd);
				printfUI("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfUI("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfUI("COMP:0x%X, ", IC[ic].statc.comp);
				printfUI("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfUI("VDE:0x%X\r\n\r\n", IC[ic].statc.vde);

				printfUI("Status D:\r\n");
				printfUI("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfUI("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfUI("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfUI("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfUI("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfUI("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfUI("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfUI("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfUI("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfUI("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfUI("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfUI("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfUI("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfUI("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfUI("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfUI("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfUI("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfUI("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfUI("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfUI("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfUI("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfUI("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfUI("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfUI("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfUI("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfUI("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfUI("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfUI("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfUI("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfUI("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfUI("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfUI("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfUI("CTS:0x%X, ", IC[ic].statd.cts);
				printfUI("CT:0x%X\r\n\r\n", IC[ic].statd.ct);

				printfUI("Status E:\r\n");
				printfUI("GPI:0x%X, ", IC[ic].state.gpi);
				printfUI("REV_ID:0x%X\r\n\r\n", IC[ic].state.rev);

				printfUI("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else {
				printfUI("Wrong Register Group Select\r\n");
			}
		}
	}
}

/**
 *******************************************************************************
 * Function: PrintDeviceSID
 * @brief Print Device SID.
 *
 * @details This function Print Device SID into IAR I/O terminal.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printDeviceSID(uint8_t tIC, cell_asic *IC, TYPE type) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Sid) {
			printfUI("Read Device SID:\r\n");
			printfUI("0x%X, ", IC[ic].sid.sid[0]);
			printfUI("0x%X, ", IC[ic].sid.sid[1]);
			printfUI("0x%X, ", IC[ic].sid.sid[2]);
			printfUI("0x%X, ", IC[ic].sid.sid[3]);
			printfUI("0x%X, ", IC[ic].sid.sid[4]);
			printfUI("0x%X, ", IC[ic].sid.sid[5]);
			printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.sid_pec);
		} else {
			printfUI("Wrong Register Type Select\r\n");
		}
	}
}

/**
 *******************************************************************************
 * Function: printWritePwmDutyCycle
 * @brief Print Write Pwm Duty Cycle.
 *
 * @details This function Print write pwm duty cycle value.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @param [in]  grp      Enum group of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printWritePwmDutyCycle(uint8_t tIC, cell_asic *IC, TYPE type, GRP grp) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfUI("Write Pwma Duty Cycle:\r\n");
			printfUI("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfUI("0x%X\r\n\r\n", IC[ic].pwma.tx_data[5]);
		} else if (grp == B) {
			printfUI("Write Pwmb Duty Cycle:\r\n");
			printfUI("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfUI("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else if (grp == ALL_GRP) {
			printfUI("Write Pwma Duty Cycle:\r\n");
			printfUI("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfUI("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfUI("0x%X\r\n", IC[ic].pwma.tx_data[5]);

			printfUI("Write Pwmb Duty Cycle:\r\n");
			printfUI("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfUI("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else {
			printfUI("Wrong Register Group Select\r\n");
		}
	}
}

/**
 *******************************************************************************
 * Function: printReadPwmDutyCycle
 * @brief Print Read Pwm Duty Cycle.
 *
 * @details This function print read pwm duty cycle value.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @param [in]  grp      Enum group of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printReadPwmDutyCycle(uint8_t tIC, cell_asic *IC, TYPE type, GRP grp) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfUI("Read PWMA Duty Cycle:\r\n");
			printfUI("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfUI("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfUI("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfUI("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfUI("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfUI("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfUI("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfUI("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfUI("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfUI("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfUI("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfUI("PWM12:0x%X, ", IC[ic].PwmA.pwma[11]);
			printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == B) {
			printfUI("Read PWMB Duty Cycle:\r\n");
			printfUI("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfUI("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfUI("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfUI("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == ALL_GRP) {
			printfUI("Read PWMA Duty Cycle:\r\n");
			printfUI("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfUI("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfUI("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfUI("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfUI("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfUI("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfUI("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfUI("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfUI("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfUI("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfUI("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfUI("PWM12:0x%X\r\n", IC[ic].PwmA.pwma[11]);

			printfUI("Read PWMB Duty Cycle:\r\n");
			printfUI("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfUI("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfUI("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfUI("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else {
			printfUI("Wrong Register Type Select\r\n");
		}
	}
}

/**
 *******************************************************************************
 * Function: printWriteCommData
 * @brief Print Write Comm data.
 *
 * @details This function Print write comm data.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printWriteCommData(uint8_t tIC, cell_asic *IC, TYPE type) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfUI("Write Comm Data:\r\n");
			printfUI("0x%X, ", IC[ic].com.tx_data[0]);
			printfUI("0x%X, ", IC[ic].com.tx_data[1]);
			printfUI("0x%X, ", IC[ic].com.tx_data[2]);
			printfUI("0x%X, ", IC[ic].com.tx_data[3]);
			printfUI("0x%X, ", IC[ic].com.tx_data[4]);
			printfUI("0x%X\r\n\r\n", IC[ic].com.tx_data[5]);
		} else {
			printfUI("Wrong Register Group Select\r\n");
		}
	}
}

/**
 *******************************************************************************
 * Function: printReadCommData
 * @brief Print Read Comm Data.
 *
 * @details This function print read comm data.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printReadCommData(uint8_t tIC, cell_asic *IC, TYPE type) {
	for (uint8_t ic = 0; ic < tIC; ic++) {
		printfUI("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfUI("Read Comm Data:\r\n");
			printfUI("ICOM0:0x%X, ", IC[ic].comm.icomm[0]);
			printfUI("ICOM1:0x%X, ", IC[ic].comm.icomm[1]);
			printfUI("ICOM2:0x%X\r\n", IC[ic].comm.icomm[2]);
			printfUI("FCOM0:0x%X, ", IC[ic].comm.fcomm[0]);
			printfUI("FCOM1:0x%X, ", IC[ic].comm.fcomm[1]);
			printfUI("FCOM2:0x%X\r\n", IC[ic].comm.fcomm[2]);
			printfUI("DATA0:0x%X, ", IC[ic].comm.data[0]);
			printfUI("DATA1:0x%X, ", IC[ic].comm.data[1]);
			printfUI("DATA2:0x%X\r\n", IC[ic].comm.data[2]);
			printfUI("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfUI("PECError:%d\r\n\r\n", IC[ic].cccrc.comm_pec);
		} else {
			printfUI("Wrong Register Type Select\r\n");
		}
	}
}

/**
 *******************************************************************************
 * Function: printDiagnosticTestResult
 * @brief Print diagnostic test result.
 *
 * @details This function Print diagnostic test result (PASS,FAIL) into console terminal.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  TEST     Enum type diagnostic test
 *
 * @return None
 *
 *******************************************************************************
 */
void printDiagnosticTestResult(uint8_t tIC, cell_asic *IC, DIAGNOSTIC_TYPE type) {
	if (type == OSC_MISMATCH) {
		printfUI("OSC Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.osc_mismatch);
		}
		printfUI("\r\n\r\n");
	}

	else if (type == SUPPLY_ERROR) {
		printfUI("Force Supply Error Detection Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.supply_error);
		}
		printfUI("\r\n\r\n");
	}

	else if (type == THSD) {
		printfUI("Thsd Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.thsd);
		}
		printfUI("\r\n\r\n");
	}

	else if (type == FUSE_ED) {
		printfUI("Fuse_ed Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_ed);
		}
		printfUI("\r\n\r\n");
	}

	else if (type == FUSE_MED) {
		printfUI("Fuse_med Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_med);
		}
		printfUI("\r\n\r\n");
	}

	else if (type == TMODCHK) {
		printfUI("TMODCHK Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.tmodchk);
		}
		printfUI("\r\n\r\n");
	} else {
		printfUI("Wrong Diagnostic Selected\r\n");
	}
}

/**
 *******************************************************************************
 * Function: diagnosticResultPrint
 * @brief Print diagnostic (PASS/FAIL) result.
 *
 * @details This function print diagnostic (PASS/FAIL) result into console.
 *
 * Parameters:
 * @param [in]	result   Result byte
 *
 * @return None
 *
 *******************************************************************************
 */
void diagnosticTestResultPrint(uint8_t result) {
	if (result == 1) {
		printfUI("PASS\r\n");
	} else {
		printfUI("FAIL\r\n");
	}
}

/**
 *******************************************************************************
 * Function: printOpenWireTestResult
 * @brief Print open wire test result.
 *
 * @details This function print open wire test result.
 *
 * Parameters:
 * @param [in]	tIC      Total IC
 *
 * @param [in]  *IC      cell_asic stucture pointer
 *
 * @param [in]  type     Enum type of resistor
 *
 * @return None
 *
 *******************************************************************************
 */
void printOpenWireTestResult(uint8_t tIC, cell_asic *IC, TYPE type) {
	if (type == Cell) {
		printfUI("Cell Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfUI("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cell_ow[cell]);
			}
			printfUI("\r\n\r\n");
		}
	} else if (type == S_volt) {
		printfUI("Cell redundant Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfUI("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cellred_ow[cell]);
			}
			printfUI("\r\n\r\n");
		}
	} else if (type == Aux) {
		printfUI("Aux Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfUI("IC%d:\r\n", (ic + 1));
			for (uint8_t gpio = 0; gpio < (AUX - 2); gpio++) {
				printfUI("GPIO%d:", (gpio + 1));
				openWireResultPrint(IC[ic].diag_result.aux_ow[gpio]);
			}
			printfUI("\r\n\r\n");
		}
	} else {
		printfUI("Wrong Resistor Type Selected\r\n");
	}
}

/**
 *******************************************************************************
 * Function: openWireResultPrint
 * @brief Print open wire (OPEN/CLOSE) result.
 *
 * @details This function print open wire result into console.
 *
 * Parameters:
 * @param [in]	result   Result byte
 *
 * @return None
 *
 *******************************************************************************
 */
void openWireResultPrint(uint8_t result) {
	if (result == 1) {
		printfUI(" OPEN\r\n");
	} else {
		printfUI(" CLOSE\r\n");
	}
}

/**
 *******************************************************************************
 * Function: printPollAdcConvTime
 * @brief Print Poll adc conversion Time.
 *
 * @details This function print poll adc conversion Time.
 *
 * @return None
 *
 *******************************************************************************
 */
void printPollAdcConvTime(int count) {
	printfUI("Adc Conversion Time = %fms\r\n", (float) (count / 64000.0));
}

/**
 *******************************************************************************
 * Function: printMenu
 * @brief Print Command Menu.
 *
 * @details This function print all command menu.
 *
 * @return None
 *
 *******************************************************************************
 */
void printMenu() {
	printfUI("List of ADBMS6830 Command:\r\n");
	printfUI("Write and Read Configuration: 1 \r\n");
	printfUI("Read Configuration: 2 \r\n");
	printfUI("Start Cell Voltage Conversion: 3 \r\n");
	printfUI("Read Cell Voltages: 4 \r\n");
	printfUI("Start S-Voltage Conversion: 5 \r\n");
	printfUI("Read S-Voltages: 6 \r\n");
	printfUI("Start Avg Cell Voltage Conversion: 7 \r\n");
	printfUI("Read Avg Cell Voltages: 8 \r\n");
	printfUI("Start F-Cell Voltage Conversion: 9 \r\n");
	printfUI("Read F-Cell Voltages: 10 \r\n");
	printfUI("Start Aux Voltage Conversion: 11 \r\n");
	printfUI("Read Aux Voltages: 12 \r\n");
	printfUI("Start RAux Voltage Conversion: 13 \r\n");
	printfUI("Read RAux Voltages: 14 \r\n");
	printfUI("Read Status Registers: 15 \r\n");
	printfUI("Loop Measurements: 16 \r\n");
	printfUI("Clear Cell registers: 17 \r\n");
	printfUI("Clear Aux registers: 18 \r\n");
	printfUI("Clear Spin registers: 19 \r\n");
	printfUI("Clear Fcell registers: 20 \r\n");
	printfUI("Write Configuration: 21 \r\n");

	printfUI("\r\n");
	printfUI("Print '0' for menu\r\n");
	printfUI("Please enter command: \r\n");
	printfUI("\r\n");
}

/**
 *******************************************************************************
 * Function: getVoltage
 * @brief Get voltages with multiplication factor.
 *
 * @details This function calculate the voltage.
 *
 * Parameters:
 * @param [in]	data    voltages(uint16_t)
 *
 * @return voltage(float)
 *
 *******************************************************************************
 */
float getVoltage(int data) {
	float voltage_float; //voltage in Volts
	voltage_float = ((data + 10000) * 0.000150);
	return voltage_float;
}

/**
 *******************************************************************************
 * Function: getTemperature
 * @brief Convert raw ADC thermistor data to temperature in ºC.
 *
 * @details Uses getVoltage() to obtain the divider voltage and then applies
 * the Beta equation for a 10k NTC thermistor.
 *
 * Parameters:
 * @param [in] data    Raw measurement (uint16_t / int)
 *
 * @return temperature (float) in ºC
 *******************************************************************************
 */
float getTemperature(int data) {
	//Thermistor: Amphenol NKA502C1*1C
	float VREF2 = 3.0f;      // Reference voltage
	float R1 = 10000.0f;     // Fixed resistor (10k)
	float R0 = 5000.0f;     // Thermistor nominal resistance at 25°C
	float BETA = 3977.0f;    // Beta constant
	float T0_K = 298.15f;    // 25°C in Kelvin

	float voltage = getVoltage(data);

	// Calculate thermistor resistance from divider
	float Rt = R1 * voltage / (VREF2 - voltage);

	// Beta equation
	float tempK = 1.0f / ((1.0f / T0_K) + (1.0f / BETA) * logf(Rt / R0));

	// Convert Kelvin to Celsius
	return tempK - 273.15f;
}

/** @}*/
/** @}*/
