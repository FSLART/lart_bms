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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfDebug("Write Config A:\r\n");
				printfDebug("0x%X, ", IC[ic].configa.tx_data[0]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[1]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[2]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[3]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[4]);
				printfDebug("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);
			} else if (grp == B) {
				printfDebug("Write Config B:\r\n");
				printfDebug("0x%X, ", IC[ic].configb.tx_data[0]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[1]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[2]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[3]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[4]);
				printfDebug("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else if (grp == ALL_GRP) {
				printfDebug("Write Config A:\r\n");
				printfDebug("0x%X, ", IC[ic].configa.tx_data[0]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[1]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[2]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[3]);
				printfDebug("0x%X, ", IC[ic].configa.tx_data[4]);
				printfDebug("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);

				printfDebug("Write Config B:\r\n");
				printfDebug("0x%X, ", IC[ic].configb.tx_data[0]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[1]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[2]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[3]);
				printfDebug("0x%X, ", IC[ic].configb.tx_data[4]);
				printfDebug("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else {
				printfDebug("Wrong Register Group Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfDebug("Read Config A:\r\n");
				printfDebug("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfDebug("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfDebug("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfDebug("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfDebug("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfDebug("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfDebug("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfDebug("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfDebug("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfDebug("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfDebug("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfDebug("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfDebug("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfDebug("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfDebug("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfDebug("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfDebug("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfDebug("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));
				printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == B) {
				printfDebug("Read Config B:\r\n");
				printfDebug("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfDebug("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfDebug("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfDebug("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfDebug("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfDebug("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == ALL_GRP) {
				printfDebug("Read Config A:\r\n");
				printfDebug("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfDebug("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfDebug("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfDebug("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfDebug("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfDebug("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfDebug("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfDebug("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfDebug("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfDebug("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfDebug("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfDebug("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfDebug("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfDebug("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfDebug("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfDebug("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfDebug("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfDebug("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));

				printfDebug("Read Config B:\r\n");
				printfDebug("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfDebug("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfDebug("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfDebug("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfDebug("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfDebug("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else {
				printfDebug("Wrong Register Group Select\r\n");
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
		printfDebug("IC%d:", (ic + 1));
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
				printfDebug("C%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDebug("PECError:%d", IC[ic].cccrc.cell_pec);
				}
			} else if (type == AvgCell) {
				printfDebug("AC%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDebug("PECError:%d", IC[ic].cccrc.acell_pec);
				}
			} else if (type == F_volt) {
				printfDebug("FC%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDebug("PECError:%d", IC[ic].cccrc.fcell_pec);
				}
			} else if (type == S_volt) {
				printfDebug("S%d=%.4fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDebug("PECError:%d", IC[ic].cccrc.scell_pec);
				}
			} else if (type == Aux) {
			    if (index <= 9) {
			        printfDebug("AUX%d=%.4fV,", (index + 1), voltage);
			    } else if (index == 10) {
			        printfDebug("VMV:%.4fV,", (20 * voltage));
			    } else if (index == 11) {
			        printfDebug("V+:%.4fV,", (20 * voltage));
			        printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			        printfDebug("PECError:%d", IC[ic].cccrc.aux_pec);
			    }
			} else if (type == RAux) {
				//NOTE: Added printing temperatures along aux voltage readings
			    float temperature = getTemperature(temp);
			    printfDebug("RAUX%d=%.4fV,", (index + 1), voltage);
			    printfDebug("RT%d=%.2fC,", (index + 1), temperature);

			    if (index == (channel - 1)) {
			        printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			        printfDebug("PECError:%d", IC[ic].cccrc.raux_pec);
			    }
			} else {
				printfDebug("Wrong Register Group Select\r\n");
			}
		}
		printfDebug("\r\n\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Status) {
			if (grp == A) {
				printfDebug("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfDebug("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfDebug("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfDebug("ITMP:%f�C\r\n", (voltage / 0.0075) - 273);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == B) {
				printfDebug("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfDebug("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfDebug("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfDebug("VR4K:%fV\r\n", voltage);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == C) {
				printfDebug("Status C:\r\n");
				printfDebug("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfDebug("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfDebug("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfDebug("OTP1_MED:0x%X ", IC[ic].statc.otp1_med);
				printfDebug("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfDebug("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfDebug("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfDebug("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfDebug("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfDebug("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfDebug("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfDebug("THSD:0x%X, ", IC[ic].statc.thsd);
				printfDebug("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfDebug("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfDebug("COMP:0x%X, ", IC[ic].statc.comp);
				printfDebug("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfDebug("VDE:0x%X\r\n", IC[ic].statc.vde);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == D) {
				printfDebug("Status D:\r\n");
				printfDebug("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfDebug("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfDebug("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfDebug("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfDebug("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfDebug("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfDebug("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfDebug("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfDebug("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfDebug("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfDebug("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfDebug("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfDebug("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfDebug("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfDebug("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfDebug("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfDebug("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfDebug("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfDebug("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfDebug("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfDebug("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfDebug("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfDebug("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfDebug("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfDebug("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfDebug("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfDebug("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfDebug("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfDebug("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfDebug("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfDebug("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfDebug("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfDebug("CTS:0x%X, ", IC[ic].statd.cts);
				printfDebug("CT:0x%X, ", IC[ic].statd.ct);
				printfDebug("OC_CNTR:0x%X\r\n", IC[ic].statd.oc_cntr);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == E) {
				printfDebug("Status E:\r\n");
				printfDebug("GPI:0x%X, ", IC[ic].state.gpi);
				printfDebug("REV_ID:0x%X\r\n", IC[ic].state.rev);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == ALL_GRP) {
				printfDebug("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfDebug("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfDebug("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfDebug("ITMP:%f�C\r\n\r\n", (voltage / 0.0075) - 273);

				printfDebug("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfDebug("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfDebug("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfDebug("VR4K:%fV\r\n\r\n", voltage);

				printfDebug("Status C:\r\n");
				printfDebug("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfDebug("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfDebug("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfDebug("OTP1_MED:0x%X, ", IC[ic].statc.otp1_med);
				printfDebug("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfDebug("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfDebug("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfDebug("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfDebug("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfDebug("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfDebug("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfDebug("THSD:0x%X, ", IC[ic].statc.thsd);
				printfDebug("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfDebug("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfDebug("COMP:0x%X, ", IC[ic].statc.comp);
				printfDebug("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfDebug("VDE:0x%X\r\n\r\n", IC[ic].statc.vde);

				printfDebug("Status D:\r\n");
				printfDebug("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfDebug("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfDebug("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfDebug("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfDebug("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfDebug("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfDebug("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfDebug("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfDebug("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfDebug("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfDebug("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfDebug("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfDebug("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfDebug("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfDebug("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfDebug("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfDebug("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfDebug("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfDebug("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfDebug("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfDebug("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfDebug("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfDebug("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfDebug("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfDebug("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfDebug("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfDebug("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfDebug("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfDebug("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfDebug("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfDebug("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfDebug("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfDebug("CTS:0x%X, ", IC[ic].statd.cts);
				printfDebug("CT:0x%X\r\n\r\n", IC[ic].statd.ct);

				printfDebug("Status E:\r\n");
				printfDebug("GPI:0x%X, ", IC[ic].state.gpi);
				printfDebug("REV_ID:0x%X\r\n\r\n", IC[ic].state.rev);

				printfDebug("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else {
				printfDebug("Wrong Register Group Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Sid) {
			printfDebug("Read Device SID:\r\n");
			printfDebug("0x%X, ", IC[ic].sid.sid[0]);
			printfDebug("0x%X, ", IC[ic].sid.sid[1]);
			printfDebug("0x%X, ", IC[ic].sid.sid[2]);
			printfDebug("0x%X, ", IC[ic].sid.sid[3]);
			printfDebug("0x%X, ", IC[ic].sid.sid[4]);
			printfDebug("0x%X, ", IC[ic].sid.sid[5]);
			printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.sid_pec);
		} else {
			printfDebug("Wrong Register Type Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfDebug("Write Pwma Duty Cycle:\r\n");
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfDebug("0x%X\r\n\r\n", IC[ic].pwma.tx_data[5]);
		} else if (grp == B) {
			printfDebug("Write Pwmb Duty Cycle:\r\n");
			printfDebug("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfDebug("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else if (grp == ALL_GRP) {
			printfDebug("Write Pwma Duty Cycle:\r\n");
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfDebug("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfDebug("0x%X\r\n", IC[ic].pwma.tx_data[5]);

			printfDebug("Write Pwmb Duty Cycle:\r\n");
			printfDebug("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfDebug("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else {
			printfDebug("Wrong Register Group Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfDebug("Read PWMA Duty Cycle:\r\n");
			printfDebug("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfDebug("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfDebug("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfDebug("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfDebug("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfDebug("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfDebug("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfDebug("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfDebug("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfDebug("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfDebug("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfDebug("PWM12:0x%X, ", IC[ic].PwmA.pwma[11]);
			printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == B) {
			printfDebug("Read PWMB Duty Cycle:\r\n");
			printfDebug("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfDebug("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfDebug("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfDebug("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == ALL_GRP) {
			printfDebug("Read PWMA Duty Cycle:\r\n");
			printfDebug("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfDebug("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfDebug("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfDebug("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfDebug("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfDebug("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfDebug("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfDebug("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfDebug("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfDebug("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfDebug("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfDebug("PWM12:0x%X\r\n", IC[ic].PwmA.pwma[11]);

			printfDebug("Read PWMB Duty Cycle:\r\n");
			printfDebug("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfDebug("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfDebug("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfDebug("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else {
			printfDebug("Wrong Register Type Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfDebug("Write Comm Data:\r\n");
			printfDebug("0x%X, ", IC[ic].com.tx_data[0]);
			printfDebug("0x%X, ", IC[ic].com.tx_data[1]);
			printfDebug("0x%X, ", IC[ic].com.tx_data[2]);
			printfDebug("0x%X, ", IC[ic].com.tx_data[3]);
			printfDebug("0x%X, ", IC[ic].com.tx_data[4]);
			printfDebug("0x%X\r\n\r\n", IC[ic].com.tx_data[5]);
		} else {
			printfDebug("Wrong Register Group Select\r\n");
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
		printfDebug("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfDebug("Read Comm Data:\r\n");
			printfDebug("ICOM0:0x%X, ", IC[ic].comm.icomm[0]);
			printfDebug("ICOM1:0x%X, ", IC[ic].comm.icomm[1]);
			printfDebug("ICOM2:0x%X\r\n", IC[ic].comm.icomm[2]);
			printfDebug("FCOM0:0x%X, ", IC[ic].comm.fcomm[0]);
			printfDebug("FCOM1:0x%X, ", IC[ic].comm.fcomm[1]);
			printfDebug("FCOM2:0x%X\r\n", IC[ic].comm.fcomm[2]);
			printfDebug("DATA0:0x%X, ", IC[ic].comm.data[0]);
			printfDebug("DATA1:0x%X, ", IC[ic].comm.data[1]);
			printfDebug("DATA2:0x%X\r\n", IC[ic].comm.data[2]);
			printfDebug("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDebug("PECError:%d\r\n\r\n", IC[ic].cccrc.comm_pec);
		} else {
			printfDebug("Wrong Register Type Select\r\n");
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
		printfDebug("OSC Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.osc_mismatch);
		}
		printfDebug("\r\n\r\n");
	}

	else if (type == SUPPLY_ERROR) {
		printfDebug("Force Supply Error Detection Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.supply_error);
		}
		printfDebug("\r\n\r\n");
	}

	else if (type == THSD) {
		printfDebug("Thsd Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.thsd);
		}
		printfDebug("\r\n\r\n");
	}

	else if (type == FUSE_ED) {
		printfDebug("Fuse_ed Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_ed);
		}
		printfDebug("\r\n\r\n");
	}

	else if (type == FUSE_MED) {
		printfDebug("Fuse_med Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_med);
		}
		printfDebug("\r\n\r\n");
	}

	else if (type == TMODCHK) {
		printfDebug("TMODCHK Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.tmodchk);
		}
		printfDebug("\r\n\r\n");
	} else {
		printfDebug("Wrong Diagnostic Selected\r\n");
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
		printfDebug("PASS\r\n");
	} else {
		printfDebug("FAIL\r\n");
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
		printfDebug("Cell Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfDebug("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cell_ow[cell]);
			}
			printfDebug("\r\n\r\n");
		}
	} else if (type == S_volt) {
		printfDebug("Cell redundant Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfDebug("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cellred_ow[cell]);
			}
			printfDebug("\r\n\r\n");
		}
	} else if (type == Aux) {
		printfDebug("Aux Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDebug("IC%d:\r\n", (ic + 1));
			for (uint8_t gpio = 0; gpio < (AUX - 2); gpio++) {
				printfDebug("GPIO%d:", (gpio + 1));
				openWireResultPrint(IC[ic].diag_result.aux_ow[gpio]);
			}
			printfDebug("\r\n\r\n");
		}
	} else {
		printfDebug("Wrong Resistor Type Selected\r\n");
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
		printfDebug(" OPEN\r\n");
	} else {
		printfDebug(" CLOSE\r\n");
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
	printfDebug("Adc Conversion Time = %fms\r\n", (float) (count / 64000.0));
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
	printfDebug("List of ADBMS6830 Command:\r\n");
	printfDebug("Write and Read Configuration: 1 \r\n");
	printfDebug("Read Configuration: 2 \r\n");
	printfDebug("Start Cell Voltage Conversion: 3 \r\n");
	printfDebug("Read Cell Voltages: 4 \r\n");
	printfDebug("Start S-Voltage Conversion: 5 \r\n");
	printfDebug("Read S-Voltages: 6 \r\n");
	printfDebug("Start Avg Cell Voltage Conversion: 7 \r\n");
	printfDebug("Read Avg Cell Voltages: 8 \r\n");
	printfDebug("Start F-Cell Voltage Conversion: 9 \r\n");
	printfDebug("Read F-Cell Voltages: 10 \r\n");
	printfDebug("Start Aux Voltage Conversion: 11 \r\n");
	printfDebug("Read Aux Voltages: 12 \r\n");
	printfDebug("Start RAux Voltage Conversion: 13 \r\n");
	printfDebug("Read RAux Voltages: 14 \r\n");
	printfDebug("Read Status Registers: 15 \r\n");
	printfDebug("Loop Measurements: 16 \r\n");
	printfDebug("Clear Cell registers: 17 \r\n");
	printfDebug("Clear Aux registers: 18 \r\n");
	printfDebug("Clear Spin registers: 19 \r\n");
	printfDebug("Clear Fcell registers: 20 \r\n");
	printfDebug("Write Configuration: 21 \r\n");

	printfDebug("\r\n");
	printfDebug("Print '0' for menu\r\n");
	printfDebug("Please enter command: \r\n");
	printfDebug("\r\n");
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
