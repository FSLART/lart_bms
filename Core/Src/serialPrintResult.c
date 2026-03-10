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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfDma("Write Config A:\r\n");
				printfDma("0x%X, ", IC[ic].configa.tx_data[0]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[1]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[2]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[3]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[4]);
				printfDma("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);
			} else if (grp == B) {
				printfDma("Write Config B:\r\n");
				printfDma("0x%X, ", IC[ic].configb.tx_data[0]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[1]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[2]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[3]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[4]);
				printfDma("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else if (grp == ALL_GRP) {
				printfDma("Write Config A:\r\n");
				printfDma("0x%X, ", IC[ic].configa.tx_data[0]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[1]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[2]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[3]);
				printfDma("0x%X, ", IC[ic].configa.tx_data[4]);
				printfDma("0x%X\r\n\r\n", IC[ic].configa.tx_data[5]);

				printfDma("Write Config B:\r\n");
				printfDma("0x%X, ", IC[ic].configb.tx_data[0]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[1]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[2]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[3]);
				printfDma("0x%X, ", IC[ic].configb.tx_data[4]);
				printfDma("0x%X\r\n\r\n", IC[ic].configb.tx_data[5]);
			} else {
				printfDma("Wrong Register Group Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Config) {
			if (grp == A) {
				printfDma("Read Config A:\r\n");
				printfDma("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfDma("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfDma("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfDma("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfDma("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfDma("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfDma("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfDma("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfDma("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfDma("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfDma("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfDma("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfDma("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfDma("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfDma("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfDma("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfDma("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfDma("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));
				printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == B) {
				printfDma("Read Config B:\r\n");
				printfDma("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfDma("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfDma("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfDma("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfDma("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfDma("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else if (grp == ALL_GRP) {
				printfDma("Read Config A:\r\n");
				printfDma("REFON:0x%X, ", IC[ic].rx_cfga.refon);
				printfDma("CTH:0x%X\r\n", IC[ic].rx_cfga.cth & 0x07);
				printfDma("FLAG_D[0]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x01));
				printfDma("FLAG_D[1]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x02) >> 1);
				printfDma("FLAG_D[2]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x04) >> 2);
				printfDma("FLAG_D[3]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x08) >> 3);
				printfDma("FLAG_D[4]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x10) >> 4);
				printfDma("FLAG_D[5]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x20) >> 5);
				printfDma("FLAG_D[6]:0x%X, ", (IC[ic].rx_cfga.flag_d & 0x40) >> 6);
				printfDma("FLAG_D[7]:0x%X\r\n", (IC[ic].rx_cfga.flag_d & 0x80) >> 7);
				printfDma("OWA[2:0]:0x%X, ", (IC[ic].rx_cfga.owa));
				printfDma("OWRNG:0x%X, ", (IC[ic].rx_cfga.owrng));
				printfDma("SOAKON:0x%X, ", (IC[ic].rx_cfga.soakon));
				printfDma("GPO:0x%X, ", (IC[ic].rx_cfga.gpo));
				printfDma("FC:0x%X, ", (IC[ic].rx_cfga.fc));
				printfDma("COMM_BK:0x%X, ", (IC[ic].rx_cfga.comm_bk));
				printfDma("MUTE_ST:0x%X, ", (IC[ic].rx_cfga.mute_st));
				printfDma("SNAP:0x%X\r\n\r\n", (IC[ic].rx_cfga.snap));

				printfDma("Read Config B:\r\n");
				printfDma("VUV:0x%X, ", IC[ic].rx_cfgb.vuv);
				printfDma("VOV:0x%X, ", IC[ic].rx_cfgb.vov);
				printfDma("DCTO:0x%X, ", IC[ic].rx_cfgb.dcto);
				printfDma("DTRNG:0x%X, ", IC[ic].rx_cfgb.dtrng);
				printfDma("DTMEN:0x%X, ", IC[ic].rx_cfgb.dtmen);
				printfDma("DCC:0x%X\r\n\r\n", IC[ic].rx_cfgb.dcc);
				printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.cfgr_pec);
			} else {
				printfDma("Wrong Register Group Select\r\n");
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
		printfDma("IC%d:", (ic + 1));
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
				printfDma("C%d=%fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.cell_pec);
				}
			} else if (type == AvgCell) {
				printfDma("AC%d=%fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.acell_pec);
				}
			} else if (type == F_volt) {
				printfDma("FC%d=%fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.fcell_pec);
				}
			} else if (type == S_volt) {
				printfDma("S%d=%fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.scell_pec);
				}
			} else if (type == Aux) {
				if (index <= 9) {
					printfDma("AUX%d=%fV,", (index + 1), voltage);
				} else if (index == 10) {
					printfDma("VMV:%fV,", (20 * voltage));
				} else if (index == 11) {
					printfDma("V+:%fV,", (20 * voltage));
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.aux_pec);
				}
			} else if (type == RAux) {
				printfDma("RAUX%d=%fV,", (index + 1), voltage);
				if (index == (channel - 1)) {
					printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
					printfDma("PECError:%d", IC[ic].cccrc.raux_pec);
				}
			} else {
				printfDma("Wrong Register Group Select\r\n");
			}
		}
		printfDma("\r\n\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Status) {
			if (grp == A) {
				printfDma("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfDma("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfDma("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfDma("ITMP:%f�C\r\n", (voltage / 0.0075) - 273);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == B) {
				printfDma("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfDma("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfDma("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfDma("VR4K:%fV\r\n", voltage);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == C) {
				printfDma("Status C:\r\n");
				printfDma("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfDma("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfDma("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfDma("OTP1_MED:0x%X ", IC[ic].statc.otp1_med);
				printfDma("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfDma("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfDma("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfDma("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfDma("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfDma("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfDma("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfDma("THSD:0x%X, ", IC[ic].statc.thsd);
				printfDma("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfDma("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfDma("COMP:0x%X, ", IC[ic].statc.comp);
				printfDma("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfDma("VDE:0x%X\r\n", IC[ic].statc.vde);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == D) {
				printfDma("Status D:\r\n");
				printfDma("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfDma("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfDma("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfDma("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfDma("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfDma("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfDma("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfDma("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfDma("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfDma("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfDma("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfDma("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfDma("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfDma("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfDma("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfDma("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfDma("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfDma("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfDma("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfDma("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfDma("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfDma("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfDma("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfDma("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfDma("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfDma("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfDma("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfDma("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfDma("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfDma("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfDma("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfDma("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfDma("CTS:0x%X, ", IC[ic].statd.cts);
				printfDma("CT:0x%X, ", IC[ic].statd.ct);
				printfDma("OC_CNTR:0x%X\r\n", IC[ic].statd.oc_cntr);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == E) {
				printfDma("Status E:\r\n");
				printfDma("GPI:0x%X, ", IC[ic].state.gpi);
				printfDma("REV_ID:0x%X\r\n", IC[ic].state.rev);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else if (grp == ALL_GRP) {
				printfDma("Status A:\r\n");
				voltage = getVoltage(IC[ic].stata.vref2);
				printfDma("VREF2:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.vref3);
				printfDma("VREF3:%fV, ", voltage);
				voltage = getVoltage(IC[ic].stata.itmp);
				printfDma("ITMP:%f�C\r\n\r\n", (voltage / 0.0075) - 273);

				printfDma("Status B:\r\n");
				voltage = getVoltage(IC[ic].statb.va);
				printfDma("VA:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vd);
				printfDma("VD:%fV, ", voltage);
				voltage = getVoltage(IC[ic].statb.vr4k);
				printfDma("VR4K:%fV\r\n\r\n", voltage);

				printfDma("Status C:\r\n");
				printfDma("CSFLT:0x%X, ", IC[ic].statc.cs_flt);

				printfDma("OTP2_MED:0x%X, ", IC[ic].statc.otp2_med);
				printfDma("OTP2_ED:0x%X, ", IC[ic].statc.otp2_ed);
				printfDma("OTP1_MED:0x%X, ", IC[ic].statc.otp1_med);
				printfDma("OTP1_ED:0x%X, ", IC[ic].statc.otp1_ed);
				printfDma("VD_UV:0x%X, ", IC[ic].statc.vd_uv);
				printfDma("VD_OV:0x%X, ", IC[ic].statc.vd_ov);
				printfDma("VA_UV:0x%X, ", IC[ic].statc.va_uv);
				printfDma("VA_OV:0x%X\r\n", IC[ic].statc.va_ov);

				printfDma("OSCCHK:0x%X, ", IC[ic].statc.oscchk);
				printfDma("TMODCHK:0x%X, ", IC[ic].statc.tmodchk);
				printfDma("THSD:0x%X, ", IC[ic].statc.thsd);
				printfDma("SLEEP:0x%X, ", IC[ic].statc.sleep);
				printfDma("SPIFLT:0x%X, ", IC[ic].statc.spiflt);
				printfDma("COMP:0x%X, ", IC[ic].statc.comp);
				printfDma("VDEL:0x%X, ", IC[ic].statc.vdel);
				printfDma("VDE:0x%X\r\n\r\n", IC[ic].statc.vde);

				printfDma("Status D:\r\n");
				printfDma("C1UV:0x%X, ", IC[ic].statd.c_uv[0]);
				printfDma("C2UV:0x%X, ", IC[ic].statd.c_uv[1]);
				printfDma("C3UV:0x%X, ", IC[ic].statd.c_uv[2]);
				printfDma("C4UV:0x%X, ", IC[ic].statd.c_uv[3]);
				printfDma("C5UV:0x%X, ", IC[ic].statd.c_uv[4]);
				printfDma("C6UV:0x%X, ", IC[ic].statd.c_uv[5]);
				printfDma("C7UV:0x%X, ", IC[ic].statd.c_uv[6]);
				printfDma("C8UV:0x%X, ", IC[ic].statd.c_uv[7]);
				printfDma("C9UV:0x%X, ", IC[ic].statd.c_uv[8]);
				printfDma("C10UV:0x%X, ", IC[ic].statd.c_uv[9]);
				printfDma("C11UV:0x%X, ", IC[ic].statd.c_uv[10]);
				printfDma("C12UV:0x%X, ", IC[ic].statd.c_uv[11]);
				printfDma("C13UV:0x%X, ", IC[ic].statd.c_uv[12]);
				printfDma("C14UV:0x%X, ", IC[ic].statd.c_uv[13]);
				printfDma("C15UV:0x%X, ", IC[ic].statd.c_uv[14]);
				printfDma("C16UV:0x%X\r\n", IC[ic].statd.c_uv[15]);

				printfDma("C1OV:0x%X, ", IC[ic].statd.c_ov[0]);
				printfDma("C2OV:0x%X, ", IC[ic].statd.c_ov[1]);
				printfDma("C3OV:0x%X, ", IC[ic].statd.c_ov[2]);
				printfDma("C4OV:0x%X, ", IC[ic].statd.c_ov[3]);
				printfDma("C5OV:0x%X, ", IC[ic].statd.c_ov[4]);
				printfDma("C6OV:0x%X, ", IC[ic].statd.c_ov[5]);
				printfDma("C7OV:0x%X, ", IC[ic].statd.c_ov[6]);
				printfDma("C8OV:0x%X, ", IC[ic].statd.c_ov[7]);
				printfDma("C9OV:0x%X, ", IC[ic].statd.c_ov[8]);
				printfDma("C10OV:0x%X, ", IC[ic].statd.c_ov[9]);
				printfDma("C11OV:0x%X, ", IC[ic].statd.c_ov[10]);
				printfDma("C12OV:0x%X, ", IC[ic].statd.c_ov[11]);
				printfDma("C13OV:0x%X, ", IC[ic].statd.c_ov[12]);
				printfDma("C14OV:0x%X, ", IC[ic].statd.c_ov[13]);
				printfDma("C15OV:0x%X, ", IC[ic].statd.c_ov[14]);
				printfDma("C16OV:0x%X\r\n", IC[ic].statd.c_ov[15]);

				printfDma("CTS:0x%X, ", IC[ic].statd.cts);
				printfDma("CT:0x%X\r\n\r\n", IC[ic].statd.ct);

				printfDma("Status E:\r\n");
				printfDma("GPI:0x%X, ", IC[ic].state.gpi);
				printfDma("REV_ID:0x%X\r\n\r\n", IC[ic].state.rev);

				printfDma("CCount:%d, ", IC[ic].cccrc.cmd_cntr);
				printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.stat_pec);
			} else {
				printfDma("Wrong Register Group Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Sid) {
			printfDma("Read Device SID:\r\n");
			printfDma("0x%X, ", IC[ic].sid.sid[0]);
			printfDma("0x%X, ", IC[ic].sid.sid[1]);
			printfDma("0x%X, ", IC[ic].sid.sid[2]);
			printfDma("0x%X, ", IC[ic].sid.sid[3]);
			printfDma("0x%X, ", IC[ic].sid.sid[4]);
			printfDma("0x%X, ", IC[ic].sid.sid[5]);
			printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.sid_pec);
		} else {
			printfDma("Wrong Register Type Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfDma("Write Pwma Duty Cycle:\r\n");
			printfDma("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfDma("0x%X\r\n\r\n", IC[ic].pwma.tx_data[5]);
		} else if (grp == B) {
			printfDma("Write Pwmb Duty Cycle:\r\n");
			printfDma("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfDma("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else if (grp == ALL_GRP) {
			printfDma("Write Pwma Duty Cycle:\r\n");
			printfDma("0x%X, ", IC[ic].pwma.tx_data[0]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[1]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[2]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[3]);
			printfDma("0x%X, ", IC[ic].pwma.tx_data[4]);
			printfDma("0x%X\r\n", IC[ic].pwma.tx_data[5]);

			printfDma("Write Pwmb Duty Cycle:\r\n");
			printfDma("0x%X, ", IC[ic].pwmb.tx_data[0]);
			printfDma("0x%X\r\n\r\n", IC[ic].pwmb.tx_data[1]);
		} else {
			printfDma("Wrong Register Group Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (grp == A) {
			printfDma("Read PWMA Duty Cycle:\r\n");
			printfDma("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfDma("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfDma("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfDma("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfDma("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfDma("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfDma("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfDma("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfDma("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfDma("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfDma("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfDma("PWM12:0x%X, ", IC[ic].PwmA.pwma[11]);
			printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == B) {
			printfDma("Read PWMB Duty Cycle:\r\n");
			printfDma("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfDma("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfDma("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfDma("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else if (grp == ALL_GRP) {
			printfDma("Read PWMA Duty Cycle:\r\n");
			printfDma("PWM1:0x%X, ", IC[ic].PwmA.pwma[0]);
			printfDma("PWM2:0x%X, ", IC[ic].PwmA.pwma[1]);
			printfDma("PWM3:0x%X, ", IC[ic].PwmA.pwma[2]);
			printfDma("PWM4:0x%X, ", IC[ic].PwmA.pwma[3]);
			printfDma("PWM5:0x%X, ", IC[ic].PwmA.pwma[4]);
			printfDma("PWM6:0x%X, ", IC[ic].PwmA.pwma[5]);
			printfDma("PWM7:0x%X, ", IC[ic].PwmA.pwma[6]);
			printfDma("PWM8:0x%X, ", IC[ic].PwmA.pwma[7]);
			printfDma("PWM9:0x%X, ", IC[ic].PwmA.pwma[8]);
			printfDma("PWM10:0x%X, ", IC[ic].PwmA.pwma[9]);
			printfDma("PWM11:0x%X, ", IC[ic].PwmA.pwma[10]);
			printfDma("PWM12:0x%X\r\n", IC[ic].PwmA.pwma[11]);

			printfDma("Read PWMB Duty Cycle:\r\n");
			printfDma("PWM13:0x%X, ", IC[ic].PwmB.pwmb[0]);
			printfDma("PWM14:0x%X, ", IC[ic].PwmB.pwmb[1]);
			printfDma("PWM15:0x%X, ", IC[ic].PwmB.pwmb[2]);
			printfDma("PWM16:0x%X, ", IC[ic].PwmB.pwmb[3]);
			printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.pwm_pec);
		} else {
			printfDma("Wrong Register Type Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfDma("Write Comm Data:\r\n");
			printfDma("0x%X, ", IC[ic].com.tx_data[0]);
			printfDma("0x%X, ", IC[ic].com.tx_data[1]);
			printfDma("0x%X, ", IC[ic].com.tx_data[2]);
			printfDma("0x%X, ", IC[ic].com.tx_data[3]);
			printfDma("0x%X, ", IC[ic].com.tx_data[4]);
			printfDma("0x%X\r\n\r\n", IC[ic].com.tx_data[5]);
		} else {
			printfDma("Wrong Register Group Select\r\n");
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
		printfDma("IC%d:\r\n", (ic + 1));
		if (type == Comm) {
			printfDma("Read Comm Data:\r\n");
			printfDma("ICOM0:0x%X, ", IC[ic].comm.icomm[0]);
			printfDma("ICOM1:0x%X, ", IC[ic].comm.icomm[1]);
			printfDma("ICOM2:0x%X\r\n", IC[ic].comm.icomm[2]);
			printfDma("FCOM0:0x%X, ", IC[ic].comm.fcomm[0]);
			printfDma("FCOM1:0x%X, ", IC[ic].comm.fcomm[1]);
			printfDma("FCOM2:0x%X\r\n", IC[ic].comm.fcomm[2]);
			printfDma("DATA0:0x%X, ", IC[ic].comm.data[0]);
			printfDma("DATA1:0x%X, ", IC[ic].comm.data[1]);
			printfDma("DATA2:0x%X\r\n", IC[ic].comm.data[2]);
			printfDma("CCount:%d,", IC[ic].cccrc.cmd_cntr);
			printfDma("PECError:%d\r\n\r\n", IC[ic].cccrc.comm_pec);
		} else {
			printfDma("Wrong Register Type Select\r\n");
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
		printfDma("OSC Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.osc_mismatch);
		}
		printfDma("\r\n\r\n");
	}

	else if (type == SUPPLY_ERROR) {
		printfDma("Force Supply Error Detection Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.supply_error);
		}
		printfDma("\r\n\r\n");
	}

	else if (type == THSD) {
		printfDma("Thsd Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.thsd);
		}
		printfDma("\r\n\r\n");
	}

	else if (type == FUSE_ED) {
		printfDma("Fuse_ed Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_ed);
		}
		printfDma("\r\n\r\n");
	}

	else if (type == FUSE_MED) {
		printfDma("Fuse_med Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.fuse_med);
		}
		printfDma("\r\n\r\n");
	}

	else if (type == TMODCHK) {
		printfDma("TMODCHK Diagnostic Test:\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:", (ic + 1));
			diagnosticTestResultPrint(IC[ic].diag_result.tmodchk);
		}
		printfDma("\r\n\r\n");
	} else {
		printfDma("Wrong Diagnostic Selected\r\n");
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
		printfDma("PASS\r\n");
	} else {
		printfDma("FAIL\r\n");
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
		printfDma("Cell Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfDma("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cell_ow[cell]);
			}
			printfDma("\r\n\r\n");
		}
	} else if (type == S_volt) {
		printfDma("Cell redundant Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:\r\n", (ic + 1));
			for (uint8_t cell = 0; cell < CELL; cell++) {
				printfDma("CELL%d:", (cell + 1));
				openWireResultPrint(IC[ic].diag_result.cellred_ow[cell]);
			}
			printfDma("\r\n\r\n");
		}
	} else if (type == Aux) {
		printfDma("Aux Open Wire Test\r\n");
		for (uint8_t ic = 0; ic < tIC; ic++) {
			printfDma("IC%d:\r\n", (ic + 1));
			for (uint8_t gpio = 0; gpio < (AUX - 2); gpio++) {
				printfDma("GPIO%d:", (gpio + 1));
				openWireResultPrint(IC[ic].diag_result.aux_ow[gpio]);
			}
			printfDma("\r\n\r\n");
		}
	} else {
		printfDma("Wrong Resistor Type Selected\r\n");
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
		printfDma(" OPEN\r\n");
	} else {
		printfDma(" CLOSE\r\n");
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
	printfDma("Adc Conversion Time = %fms\r\n", (float) (count / 64000.0));
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
	printfDma("List of ADBMS6830 Command:\r\n");
	printfDma("Write and Read Configuration: 1 \r\n");
	printfDma("Read Configuration: 2 \r\n");
	printfDma("Start Cell Voltage Conversion: 3 \r\n");
	printfDma("Read Cell Voltages: 4 \r\n");
	printfDma("Start S-Voltage Conversion: 5 \r\n");
	printfDma("Read S-Voltages: 6 \r\n");
	printfDma("Start Avg Cell Voltage Conversion: 7 \r\n");
	printfDma("Read Avg Cell Voltages: 8 \r\n");
	printfDma("Start F-Cell Voltage Conversion: 9 \r\n");
	printfDma("Read F-Cell Voltages: 10 \r\n");
	printfDma("Start Aux Voltage Conversion: 11 \r\n");
	printfDma("Read Aux Voltages: 12 \r\n");
	printfDma("Start RAux Voltage Conversion: 13 \r\n");
	printfDma("Read RAux Voltages: 14 \r\n");
	printfDma("Read Status Registers: 15 \r\n");
	printfDma("Loop Measurements: 16 \r\n");
	printfDma("Clear Cell registers: 17 \r\n");
	printfDma("Clear Aux registers: 18 \r\n");
	printfDma("Clear Spin registers: 19 \r\n");
	printfDma("Clear Fcell registers: 20 \r\n");
	printfDma("Write Configuration: 21 \r\n");

	printfDma("\r\n");
	printfDma("Print '0' for menu\r\n");
	printfDma("Please enter command: \r\n");
	printfDma("\r\n");
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

/** @}*/
/** @}*/
