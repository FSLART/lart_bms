/*
 * test_adi.c - o driver da ADI (adBms6830GenericType.c e ParseCreate.c)
 * contra uma cadeia de ADBMS6830 ao nivel do SPI (fake_spi_chain.c): PEC15
 * dos comandos e PEC10 dos dados conferidos com uma implementacao propria a
 * partir do datasheet, ordem dos slaves na cadeia, bits de cada registo
 * (CFGA, CFGB, celulas, AUX, estado, SID) e a codificacao dos comandos ADC.
 * Os outros testes trocam este driver pelo fake_chain.c; este e o unico que
 * corre o codigo que fala com o isoSPI.
 * Correr com Tests/run_tests.sh.
 */

#include "fake_spi_chain.h"
#include "adbms_main.h"
#include "brain.h"
#include "adBms6830CmdList.h"   // as tabelas de comandos (so se incluem num ficheiro)
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// nao esta no .h da ADI
uint16_t adi_pec10_calc_table(bool bIsRxCmd, uint8_t nLength, uint8_t *pDataBuf);

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static cell_asic chips[SPI_CHAIN_MAX];

static uint16_t code_of(const uint8_t command[2]) {

	return (uint16_t) (((command[0] & 0x07u) << 8) | command[1]);
}

static void put_codes(uint8_t *reg, const int16_t *codes, uint8_t count) {

	for (uint8_t index = 0; index < count; index++) {
		reg[index * 2u] = (uint8_t) codes[index];
		reg[(index * 2u) + 1u] = (uint8_t) ((uint16_t) codes[index] >> 8);
	}
}

/* ================== PEC ================== */

static void test_pec15(void) {

	uint8_t wrcfg[2] = { 0x00, 0x01 };
	uint8_t rdcva[2] = { 0x00, 0x04 };
	int mismatches = 0;

	// exemplos do datasheet: WRCFGA -> 0x3D6E, RDCVA -> 0x07C2
	CHECK(ref_pec15(wrcfg, 2) == 0x3D6Eu);
	CHECK(Pec15_Calc(2, wrcfg) == 0x3D6Eu);
	CHECK(ref_pec15(rdcva, 2) == 0x07C2u);
	CHECK(Pec15_Calc(2, rdcva) == 0x07C2u);

	// o STCOMM traz o PEC escrito a mao na tabela de comandos
	CHECK(ref_pec15(STCOMM, 2) == (uint16_t) ((STCOMM[2] << 8) | STCOMM[3]));

	// a tabela da ADI bate com a implementacao bit a bit em dados ao calhas
	srand(1234);
	for (int round = 0; round < 2000; round++) {
		uint8_t data[32];
		uint8_t length = (uint8_t) (1 + (rand() % 32));
		for (uint8_t index = 0; index < length; index++) {
			data[index] = (uint8_t) rand();
		}
		if (Pec15_Calc(length, data) != ref_pec15(data, length)) {
			mismatches++;
		}
	}
	CHECK(mismatches == 0);
}

static void test_pec10(void) {

	int mismatches = 0;

	srand(99);
	for (int round = 0; round < 3000; round++) {

		uint8_t data[71];
		uint8_t sizes[4] = { 6, 32, 64, 68 };   // registo, RD*ALL de 34, de 66 e de 70 bytes
		uint8_t length = sizes[round % 4];
		uint8_t counter = (uint8_t) (rand() & 0x3F);

		for (uint8_t index = 0; index < length; index++) {
			data[index] = (uint8_t) rand();
		}
		data[length] = (uint8_t) ((counter << 2) | (rand() & 0x03));   // o byte do contador, com os 2 bits altos do PEC

		// escrita (sem contador) e leitura (com os 6 bits do contador), as duas versoes da ADI
		if ((pec10_calc(false, length, data) != ref_pec10(data, length, 0)) || (pec10_calc(true, length, data) != ref_pec10(data, length, counter))
				|| (adi_pec10_calc_table(false, length, data) != ref_pec10(data, length, 0))
				|| (adi_pec10_calc_table(true, length, data) != ref_pec10(data, length, counter))) {
			mismatches++;
		}
	}
	CHECK(mismatches == 0);
}

static void test_command_table(void) {

	// os comandos tem 11 bits: o primeiro byte nunca passa de 0x07
	const uint8_t *commands[] = { WRCFGA, WRCFGB, RDCFGA, RDCFGB, RDCVA, RDCVB, RDCVC, RDCVD, RDCVE, RDCVF, RDCVALL, RDAUXA, RDAUXB, RDAUXC,
			RDAUXD, RDRAXA, RDRAXB, RDRAXC, RDRAXD, RDSTATA, RDSTATB, RDSTATC, RDSTATD, RDSTATE, RDASALL, WRPWM1, WRPWM2, RDPWM1, RDPWM2, CLRCELL,
			CLRAUX, CLRSPIN, CLRFLAG, CLRFC, CLOVUV, PLADC, PLAUT, PLCADC, PLSADC, PLAUX1, PLAUX2, WRCOMM, RDCOMM, STCOMM, MUTE, UNMUTE, RSTCC, SNAP,
			UNSNAP, SRST, RDSID };
	int bad = 0;

	for (size_t index = 0; index < (sizeof(commands) / sizeof(commands[0])); index++) {
		if ((commands[index][0] & 0xF8u) != 0u) {
			bad++;
		}
	}
	CHECK(bad == 0);
	CHECK((code_of(RDCVA) == 0x004u) && (code_of(RDAUXD) == 0x01Fu) && (code_of(RDASALL) == 0x035u) && (code_of(CLRFLAG) == 0x717u));
}

/* ================== ESCRITAS ================== */

static void test_write_config_a(void) {

	spi_chain_reset(3);
	memset(chips, 0, sizeof(chips));
	for (uint8_t index = 0; index < 3u; index++) {
		chips[index].tx_cfga.refon = 1;
		chips[index].tx_cfga.cth = (uint8_t) (index + 3u);
		chips[index].tx_cfga.flag_d = (uint8_t) (0x10u << index);
		chips[index].tx_cfga.soakon = index & 1u;
		chips[index].tx_cfga.owrng = 1;
		chips[index].tx_cfga.owa = 5;
		chips[index].tx_cfga.gpo = (uint16_t) (0x3FFu >> index);
		chips[index].tx_cfga.snap = 1;
		chips[index].tx_cfga.mute_st = 0;
		chips[index].tx_cfga.comm_bk = 1;
		chips[index].tx_cfga.fc = 6;
	}
	adBmsWriteData(3, chips, WRCFGA, Config, A);

	CHECK((spi_chain.bad_command_pec == 0u) && (spi_chain.bad_data_pec == 0u));
	CHECK(spi_chain.last_command == 0x001u);
	CHECK((spi_chain.wakeups == 1u) && (spi_chain.transactions == 1u) && !spi_chain.cs_low);

	// cada chip recebe o seu (o chips[0] e o mais perto do master), bit a bit como no datasheet
	for (uint8_t index = 0; index < 3u; index++) {
		const uint8_t *reg = spi_chain.devices[index].regs[code_of(RDCFGA)];
		const cfa_ *cfg = &chips[index].tx_cfga;
		uint8_t expected[6] = {
			(uint8_t) ((cfg->refon << 7) | cfg->cth),
			cfg->flag_d,
			(uint8_t) ((cfg->soakon << 7) | (cfg->owrng << 6) | (cfg->owa << 3)),
			(uint8_t) (cfg->gpo & 0xFFu),
			(uint8_t) ((cfg->gpo >> 8) & 0x03u),
			(uint8_t) ((cfg->snap << 5) | (cfg->mute_st << 4) | (cfg->comm_bk << 3) | cfg->fc)
		};
		CHECK(memcmp(reg, expected, 6) == 0);
	}

	// e le-se de volta igual, sem erros de PEC
	adBmsReadData(3, chips, RDCFGA, Config, A);
	for (uint8_t index = 0; index < 3u; index++) {
		CHECK(chips[index].cccrc.cfgr_pec == 0u);
		CHECK(memcmp(&chips[index].rx_cfga, &chips[index].tx_cfga, sizeof(cfa_)) == 0);
	}
}

static void test_write_config_b(void) {

	spi_chain_reset(BMS_MAX_SLAVES);
	memset(chips, 0, sizeof(chips));
	for (uint8_t index = 0; index < BMS_MAX_SLAVES; index++) {
		chips[index].tx_cfgb.vuv = (uint16_t) (0x123u + index);
		chips[index].tx_cfgb.vov = (uint16_t) (0xABCu - index);
		chips[index].tx_cfgb.dtmen = 1;
		chips[index].tx_cfgb.dtrng = index & 1u;
		chips[index].tx_cfgb.dcto = (uint8_t) (index + 1u);
		chips[index].tx_cfgb.dcc = (uint16_t) (1u << index);
	}
	adBmsWriteData(BMS_MAX_SLAVES, chips, WRCFGB, Config, B);
	CHECK((spi_chain.bad_data_pec == 0u) && (spi_chain.last_command == 0x024u));

	const uint8_t *reg = spi_chain.devices[5].regs[code_of(RDCFGB)];

	// CFGBR0 = VUV[7:0], CFGBR1 = VOV[3:0]|VUV[11:8], CFGBR2 = VOV[11:4], CFGBR3 = DTMEN|DTRNG|DCTO, CFGBR4/5 = DCC
	CHECK(reg[0] == (uint8_t) (0x123u + 5u));
	CHECK(reg[1] == (uint8_t) ((((0xABCu - 5u) & 0x0Fu) << 4) | ((0x123u + 5u) >> 8)));
	CHECK(reg[2] == (uint8_t) ((0xABCu - 5u) >> 4));
	CHECK(reg[3] == (uint8_t) (0x80u | (1u << 6) | 6u));
	CHECK((reg[4] == 0x20u) && (reg[5] == 0x00u));
	CHECK(spi_chain.devices[11].regs[code_of(RDCFGB)][5] == 0x08u);   // DCC12 no byte alto
	CHECK(spi_chain.devices[15].regs[code_of(RDCFGB)][5] == 0x80u);   // DCC16 no ultimo slave

	adBmsReadData(BMS_MAX_SLAVES, chips, RDCFGB, Config, B);
	int differ = 0;
	for (uint8_t index = 0; index < BMS_MAX_SLAVES; index++) {
		if ((memcmp(&chips[index].rx_cfgb, &chips[index].tx_cfgb, sizeof(cfb_)) != 0) || (chips[index].cccrc.cfgr_pec != 0u)) {
			differ++;
		}
	}
	CHECK(differ == 0);

	// um tipo sem dados de escrita manda zeros (com PEC certo), nao o que ficou da escrita anterior
	adBmsWriteData(BMS_MAX_SLAVES, chips, WRCFGB, Cell, NONE);
	CHECK(spi_chain.bad_data_pec == 0u);
	CHECK(memcmp(spi_chain.devices[5].regs[code_of(RDCFGB)], "\0\0\0\0\0\0", 6) == 0);
	spi_chain_reset(2);
	adBmsWriteData(2, chips, WRCFGB, Config, B);
	CHECK(spi_chain.devices[0].regs[code_of(RDCFGB)][0] == 0x23u);
	adBmsWriteData(2, chips, WRCFGB, Config, C);   // grupo que nao existe: zeros tambem
	CHECK(memcmp(spi_chain.devices[0].regs[code_of(RDCFGB)], "\0\0\0\0\0\0", 6) == 0);
}

static void test_write_comm_pwm_clrflag(void) {

	spi_chain_reset(2);
	memset(chips, 0, sizeof(chips));

	// COMM (I2C/SPI pelos GPIO): ICOMM|FCOMM e o byte de dados
	chips[1].comm.icomm[0] = 0x6;
	chips[1].comm.fcomm[0] = 0x8;
	chips[1].comm.data[0] = 0xA5;
	chips[1].comm.icomm[2] = 0x1;
	chips[1].comm.fcomm[2] = 0x9;
	chips[1].comm.data[2] = 0x3C;
	adBmsWriteData(2, chips, WRCOMM, Comm, NONE);
	CHECK((spi_chain.bad_data_pec == 0u) && (spi_chain.last_command == 0x721u));
	CHECK(memcmp(spi_chain.devices[1].regs[code_of(RDCOMM)], "\x68\xA5\x00\x00\x19\x3C", 6) == 0);
	memset(&chips[1].comm, 0, sizeof(chips[1].comm));
	adBmsReadData(2, chips, RDCOMM, Comm, NONE);
	CHECK((chips[1].comm.icomm[0] == 0x6u) && (chips[1].comm.fcomm[0] == 0x8u) && (chips[1].comm.data[0] == 0xA5u));
	CHECK((chips[1].comm.icomm[2] == 0x1u) && (chips[1].comm.fcomm[2] == 0x9u) && (chips[1].comm.data[2] == 0x3Cu));
	CHECK(chips[1].cccrc.comm_pec == 0u);

	// PWM: 4 bits por celula, a celula par no nibble de baixo
	SetPwmDutyCycle(2, chips, PWM_39_6_PCT);
	chips[0].PwmA.pwma[1] = PWM_100_0_PCT;
	adBmsWriteData(2, chips, WRPWM1, Pwm, A);
	CHECK(memcmp(spi_chain.devices[0].regs[code_of(RDPWM1)], "\xF6\x66\x66\x66\x66\x66", 6) == 0);
	adBmsWriteData(2, chips, WRPWM2, Pwm, B);
	CHECK(memcmp(spi_chain.devices[1].regs[code_of(RDPWM2)], "\x66\x66\x00\x00\x00\x00", 6) == 0);
	memset(&chips[0].PwmA, 0, sizeof(chips[0].PwmA));
	memset(&chips[1].PwmB, 0, sizeof(chips[1].PwmB));
	adBmsReadData(2, chips, RDPWM1, Pwm, A);
	CHECK((chips[0].PwmA.pwma[0] == PWM_39_6_PCT) && (chips[0].PwmA.pwma[1] == PWM_100_0_PCT) && (chips[0].PwmA.pwma[11] == PWM_39_6_PCT));
	CHECK(chips[0].cccrc.pwm_pec == 0u);
	adBmsReadData(2, chips, RDPWM2, Pwm, B);
	CHECK(chips[1].PwmB.pwmb[3] == PWM_39_6_PCT);

	// CLRFLAG: CS_FLT nos dois primeiros bytes, as flags de alimentacao e de falha nos dois ultimos
	chips[0].clflag.cl_csflt = 0xFFFF;
	chips[0].clflag.cl_vaov = 1;
	chips[1].clflag.cl_spiflt = 1;
	chips[1].clflag.cl_oscchk = 1;
	adBmsWriteData(2, chips, CLRFLAG, Clrflag, NONE);
	CHECK(spi_chain.last_command == 0x717u);
	CHECK(memcmp(spi_chain.devices[0].regs[0x717], "\xFF\xFF\x00\x00\x80\x00", 6) == 0);
	CHECK(memcmp(spi_chain.devices[1].regs[0x717], "\x00\x00\x00\x00\x00\x11", 6) == 0);
	CHECK(spi_chain.bad_data_pec == 0u);
}

/* ================== LEITURAS ================== */

static void test_read_cells_and_pec_errors(void) {

	int16_t codes[16];
	int16_t extremes[3] = { -1, -32768, 32767 };

	spi_chain_reset(4);
	memset(chips, 0, sizeof(chips));
	for (uint8_t device = 0; device < 4u; device++) {
		for (uint8_t cell = 0; cell < 16u; cell++) {
			codes[cell] = (int16_t) (14000 + (device * 100) + cell);
		}
		put_codes(spi_chain.devices[device].regs[code_of(RDCVA)], &codes[0], 3);
		put_codes(spi_chain.devices[device].regs[code_of(RDCVD)], &codes[9], 3);
		put_codes(spi_chain.devices[device].regs[code_of(RDCVALL)], codes, 16);
		spi_chain.devices[device].cc = (uint8_t) (device + 7u);
	}

	adBmsReadData(4, chips, RDCVA, Cell, A);
	CHECK((chips[0].cell.c_codes[0] == 14000) && (chips[0].cell.c_codes[2] == 14002));
	CHECK(chips[3].cell.c_codes[1] == 14301);
	CHECK((chips[2].cccrc.cell_pec == 0u) && (chips[2].cccrc.cmd_cntr == 9u));
	CHECK((spi_chain.wakeups == 1u) && !spi_chain.cs_low);

	adBmsReadData(4, chips, RDCVD, Cell, D);
	CHECK((chips[1].cell.c_codes[9] == 14109) && (chips[1].cell.c_codes[11] == 14111));

	// os codigos tem sinal (complemento para 2)
	put_codes(spi_chain.devices[0].regs[code_of(RDCVB)], extremes, 3);
	adBmsReadData(4, chips, RDCVB, Cell, B);
	CHECK((chips[0].cell.c_codes[3] == -1) && (chips[0].cell.c_codes[4] == -32768) && (chips[0].cell.c_codes[5] == 32767));

	// um bit trocado no slave 2: so ele fica com o PEC errado
	spi_chain.devices[2].corrupt_next_read = true;
	adBmsReadData(4, chips, RDCVA, Cell, A);
	CHECK((chips[2].cccrc.cell_pec == 1u) && (chips[1].cccrc.cell_pec == 0u) && (chips[3].cccrc.cell_pec == 0u));

	// menos slaves a responder do que se pediu: os que faltam leem 0xFF e falham o PEC
	spi_chain.count = 2;
	adBmsReadData(4, chips, RDCVA, Cell, A);
	CHECK((chips[1].cccrc.cell_pec == 0u) && (chips[2].cccrc.cell_pec == 1u) && (chips[3].cccrc.cell_pec == 1u));
	spi_chain.count = 4;

	// o SPI nao recebe nada: zeros e PEC errado em todos, nunca a leitura anterior com PEC bom
	spi_chain.rx_fail = true;
	adBmsReadData(4, chips, RDCVA, Cell, A);
	CHECK((chips[0].cccrc.cell_pec == 1u) && (chips[3].cccrc.cell_pec == 1u));
	CHECK(chips[0].cell.c_codes[0] == 0);
	spi_chain.rx_fail = false;

	// as 16 celulas de uma vez (34 B por slave): o buffer do driver leva 3 slaves
	memset(chips, 0, sizeof(chips));
	adBmsReadData(3, chips, RDCVALL, Rdcvall, ALL_GRP);
	CHECK((chips[0].cell.c_codes[15] == 14015) && (chips[2].cell.c_codes[15] == 14215) && (chips[2].cell.c_codes[0] == 14200));
	CHECK((chips[2].cccrc.cell_pec == 0u) && (chips[2].cccrc.cmd_cntr == 9u));

	// 4 slaves ja nao cabem (136 B > 128 B): o driver nao faz nada (a aplicacao le grupo a grupo)
	uint32_t transactions = spi_chain.transactions;

	adBmsReadData(4, chips, RDCVALL, Rdcvall, ALL_GRP);
	CHECK(spi_chain.transactions == transactions);
}

static void test_read_aux_status_sid(void) {

	int16_t aux[12];

	spi_chain_reset(2);
	memset(chips, 0, sizeof(chips));
	for (uint8_t index = 0; index < 12u; index++) {
		aux[index] = (int16_t) (1000 * (index + 1));
	}
	put_codes(spi_chain.devices[1].regs[code_of(RDAUXA)], &aux[0], 3);
	put_codes(spi_chain.devices[1].regs[code_of(RDAUXD)], &aux[9], 3);
	put_codes(spi_chain.devices[1].regs[code_of(RDRAXB)], &aux[3], 3);

	adBmsReadData(2, chips, RDAUXA, Aux, A);
	CHECK((chips[1].aux.a_codes[0] == 1000) && (chips[1].aux.a_codes[2] == 3000) && (chips[1].cccrc.aux_pec == 0u));
	adBmsReadData(2, chips, RDAUXD, Aux, D);
	CHECK(chips[1].aux.a_codes[11] == 12000);
	adBmsReadData(2, chips, RDRAXB, RAux, B);
	CHECK((chips[1].raux.ra_codes[3] == 4000) && (chips[1].raux.ra_codes[5] == 6000) && (chips[1].cccrc.raux_pec == 0u));

	// estado A (VREF2, temperatura interna), C (flags de falha), D (OV/UV, CT/CTS), E (GPI e revisao)
	uint8_t *stata = spi_chain.devices[0].regs[code_of(RDSTATA)];
	uint8_t *statc = spi_chain.devices[0].regs[code_of(RDSTATC)];
	uint8_t *statd = spi_chain.devices[0].regs[code_of(RDSTATD)];
	uint8_t *state = spi_chain.devices[0].regs[code_of(RDSTATE)];

	memcpy(stata, "\x34\x12\x78\x56\xBC\x9A", 6);
	memcpy(statc, "\x05\x80\x00\x00\x81\x10", 6);
	memcpy(statd, "\x02\x01\x00\x80\x00\x09", 6);
	statd[4] = (uint8_t) ((17u << 2) | 2u);
	memcpy(state, "\x00\x00\x00\x00\x55\x32", 6);

	adBmsReadData(2, chips, RDSTATA, Status, A);
	CHECK((chips[0].stata.vref2 == 0x1234u) && (chips[0].stata.itmp == 0x5678u) && (chips[0].stata.vref3 == 0x9ABCu));
	CHECK(chips[0].cccrc.stat_pec == 0u);
	adBmsReadData(2, chips, RDSTATC, Status, C);
	CHECK(chips[0].statc.cs_flt == 0x8005u);
	CHECK(chips[0].statc.otp2_med && chips[0].statc.va_ov && !chips[0].statc.va_uv && chips[0].statc.spiflt && !chips[0].statc.thsd);
	adBmsReadData(2, chips, RDSTATD, Status, D);
	CHECK((chips[0].statd.ct == 17u) && (chips[0].statd.cts == 2u) && (chips[0].statd.oc_cntr == 9u));
	CHECK(chips[0].statd.c_ov[0] && !chips[0].statd.c_uv[0] && chips[0].statd.c_uv[4] && chips[0].statd.c_ov[15] && !chips[0].statd.c_uv[15]);
	adBmsReadData(2, chips, RDSTATE, Status, E);
	CHECK((chips[0].state.gpi == 0x255u) && (chips[0].state.rev == 3u));

	// numero de serie
	memcpy(spi_chain.devices[1].regs[code_of(RDSID)], "\x01\x02\x03\x04\x05\x06", 6);
	adBmsReadData(2, chips, RDSID, Sid, NONE);
	CHECK((chips[1].sid.sid[0] == 1u) && (chips[1].sid.sid[5] == 6u) && (chips[1].cccrc.sid_pec == 0u));
}

static void test_read_all_aux_status(void) {

	int16_t aux[12];
	int16_t raux[10];

	// RDASALL: AUX (24 B) + AUX redundante (20 B) + estado A..E (24 B) de um slave (70 B: o buffer do driver so leva um)
	spi_chain_reset(1);
	memset(chips, 0, sizeof(chips));
	for (uint8_t index = 0; index < 12u; index++) {
		aux[index] = (int16_t) (1000 * (index + 1));
	}
	for (uint8_t index = 0; index < 10u; index++) {
		raux[index] = (int16_t) (10000 + index);
	}

	uint8_t *all = spi_chain.devices[0].regs[code_of(RDASALL)];

	put_codes(&all[0], aux, 12);
	put_codes(&all[24], raux, 10);
	all[44] = 0x22; all[45] = 0x11;                  // STATA: VREF2
	all[56] = 0x34; all[57] = 0x12;                  // STATC: CS_FLT
	all[58] = 0x80;                                  // STATC: VA_OV
	all[59] = 0x10;                                  // STATC: SPIFLT
	all[64] = (uint8_t) ((5u << 2) | 1u);            // STATD: CT, CTS
	all[65] = 7;                                     // STATD: OC_CNTR
	all[66] = 0xAA; all[67] = 0x21;                  // STATE: GPI, REV
	adBmsReadData(1, chips, RDASALL, Rdasall, ALL_GRP);

	CHECK((chips[0].aux.a_codes[0] == 1000) && (chips[0].aux.a_codes[11] == 12000));
	CHECK((chips[0].raux.ra_codes[0] == 10000) && (chips[0].raux.ra_codes[9] == 10009));
	CHECK(chips[0].stata.vref2 == 0x1122u);
	CHECK((chips[0].statc.cs_flt == 0x1234u) && chips[0].statc.va_ov && chips[0].statc.spiflt);
	CHECK((chips[0].statd.ct == 5u) && (chips[0].statd.cts == 1u) && (chips[0].statd.oc_cntr == 7u));
	CHECK((chips[0].state.gpi == 0x1AAu) && (chips[0].state.rev == 2u));
	CHECK((chips[0].cccrc.aux_pec == 0u) && (chips[0].cccrc.raux_pec == 0u) && (chips[0].cccrc.stat_pec == 0u));

	// dois slaves ja nao cabem: nada
	uint32_t transactions = spi_chain.transactions;

	adBmsReadData(2, chips, RDASALL, Rdasall, ALL_GRP);
	CHECK(spi_chain.transactions == transactions);
}

static void test_out_of_range_requests(void) {

	// mais slaves que os buffers: o driver nao faz nada
	spi_chain_reset(2);
	uint32_t transactions = spi_chain.transactions;

	adBmsReadData(BMS_MAX_SLAVES + 1u, chips, RDCVA, Cell, A);
	adBmsWriteData(BMS_MAX_SLAVES + 1u, chips, WRCFGA, Config, A);
	CHECK(spi_chain.transactions == transactions);
	CHECK(spi_chain.wakeups == 0u);

	// ALL_GRP com um tipo que nao tem leitura de grupo: nada
	adBmsReadData(2, chips, RDCVA, Cell, ALL_GRP);
	CHECK(spi_chain.transactions == transactions);
}

/* ================== todos os grupos de leitura ================== */

typedef struct {
	const uint8_t *command;
	TYPE    type;
	GRP     group;
	size_t  array;        // offsetof do array de codigos em cell_asic
	uint8_t first;        // primeiro indice que o grupo enche
	uint8_t count;
	size_t  pec;          // offsetof da flag de PEC em cell_asic
} read_case_t;

#define CODES(field)  offsetof(cell_asic, field)
#define PEC(field)    offsetof(cell_asic, cccrc.field)

static const read_case_t read_cases[] = {
	{ RDCVA, Cell, A, CODES(cell.c_codes), 0, 3, PEC(cell_pec) },
	{ RDCVB, Cell, B, CODES(cell.c_codes), 3, 3, PEC(cell_pec) },
	{ RDCVC, Cell, C, CODES(cell.c_codes), 6, 3, PEC(cell_pec) },
	{ RDCVD, Cell, D, CODES(cell.c_codes), 9, 3, PEC(cell_pec) },
	{ RDCVE, Cell, E, CODES(cell.c_codes), 12, 3, PEC(cell_pec) },
	{ RDCVF, Cell, F, CODES(cell.c_codes), 15, 1, PEC(cell_pec) },
	{ RDACA, AvgCell, A, CODES(acell.ac_codes), 0, 3, PEC(acell_pec) },
	{ RDACB, AvgCell, B, CODES(acell.ac_codes), 3, 3, PEC(acell_pec) },
	{ RDACC, AvgCell, C, CODES(acell.ac_codes), 6, 3, PEC(acell_pec) },
	{ RDACD, AvgCell, D, CODES(acell.ac_codes), 9, 3, PEC(acell_pec) },
	{ RDACE, AvgCell, E, CODES(acell.ac_codes), 12, 3, PEC(acell_pec) },
	{ RDACF, AvgCell, F, CODES(acell.ac_codes), 15, 1, PEC(acell_pec) },
	{ RDSVA, S_volt, A, CODES(scell.sc_codes), 0, 3, PEC(scell_pec) },
	{ RDSVB, S_volt, B, CODES(scell.sc_codes), 3, 3, PEC(scell_pec) },
	{ RDSVC, S_volt, C, CODES(scell.sc_codes), 6, 3, PEC(scell_pec) },
	{ RDSVD, S_volt, D, CODES(scell.sc_codes), 9, 3, PEC(scell_pec) },
	{ RDSVE, S_volt, E, CODES(scell.sc_codes), 12, 3, PEC(scell_pec) },
	{ RDSVF, S_volt, F, CODES(scell.sc_codes), 15, 1, PEC(scell_pec) },
	{ RDFCA, F_volt, A, CODES(fcell.fc_codes), 0, 3, PEC(fcell_pec) },
	{ RDFCB, F_volt, B, CODES(fcell.fc_codes), 3, 3, PEC(fcell_pec) },
	{ RDFCC, F_volt, C, CODES(fcell.fc_codes), 6, 3, PEC(fcell_pec) },
	{ RDFCD, F_volt, D, CODES(fcell.fc_codes), 9, 3, PEC(fcell_pec) },
	{ RDFCE, F_volt, E, CODES(fcell.fc_codes), 12, 3, PEC(fcell_pec) },
	{ RDFCF, F_volt, F, CODES(fcell.fc_codes), 15, 1, PEC(fcell_pec) },
	{ RDAUXA, Aux, A, CODES(aux.a_codes), 0, 3, PEC(aux_pec) },
	{ RDAUXB, Aux, B, CODES(aux.a_codes), 3, 3, PEC(aux_pec) },
	{ RDAUXC, Aux, C, CODES(aux.a_codes), 6, 3, PEC(aux_pec) },
	{ RDAUXD, Aux, D, CODES(aux.a_codes), 9, 3, PEC(aux_pec) },
	{ RDRAXA, RAux, A, CODES(raux.ra_codes), 0, 3, PEC(raux_pec) },
	{ RDRAXB, RAux, B, CODES(raux.ra_codes), 3, 3, PEC(raux_pec) },
	{ RDRAXC, RAux, C, CODES(raux.ra_codes), 6, 3, PEC(raux_pec) },
	{ RDRAXD, RAux, D, CODES(raux.ra_codes), 9, 1, PEC(raux_pec) },
	{ RDCVALL, Rdcvall, ALL_GRP, CODES(cell.c_codes), 0, 16, PEC(cell_pec) },
	{ RDACALL, Rdacall, ALL_GRP, CODES(acell.ac_codes), 0, 16, PEC(acell_pec) },
	{ RDSALL, Rdsall, ALL_GRP, CODES(scell.sc_codes), 0, 16, PEC(scell_pec) },
	{ RDFCALL, Rdfcall, ALL_GRP, CODES(fcell.fc_codes), 0, 16, PEC(fcell_pec) },
};

static int16_t code_at(const cell_asic *chip, size_t array, uint8_t index) {

	int16_t value;

	memcpy(&value, (const uint8_t*) chip + array + (index * sizeof(int16_t)), sizeof(value));
	return value;
}

// cada grupo vai para os indices certos do array certo, nos dois slaves, sem mexer nos outros
static void test_every_read_group(void) {

	int wrong = 0;

	for (size_t number = 0; number < (sizeof(read_cases) / sizeof(read_cases[0])); number++) {

		const read_case_t *test = &read_cases[number];
		int16_t codes[16];

		spi_chain_reset(2);
		memset(chips, 0x55, sizeof(chips));   // o que nao for escrito fica com 0x5555
		for (uint8_t device = 0; device < 2u; device++) {
			for (uint8_t slot = 0; slot < test->count; slot++) {
				codes[slot] = (int16_t) ((number * 500u) + (device * 100u) + slot + 1u);
			}
			put_codes(spi_chain.devices[device].regs[code_of(test->command)], codes, test->count);
		}
		adBmsReadData(2, chips, (uint8_t*) test->command, test->type, test->group);

		for (uint8_t device = 0; device < 2u; device++) {
			for (uint8_t index = 0; index < 16u; index++) {

				bool inside = (index >= test->first) && (index < (test->first + test->count));
				int16_t expected = inside ? (int16_t) ((number * 500u) + (device * 100u) + (index - test->first) + 1u) : (int16_t) 0x5555;

				if ((index < ((test->array == CODES(raux.ra_codes)) ? 10u : ((test->array == CODES(aux.a_codes)) ? 12u : 16u)))
						&& (code_at(&chips[device], test->array, index) != expected)) {
					wrong++;
				}
			}
			if (*((const uint8_t*) &chips[device] + test->pec) != 0u) {
				wrong++;
			}
		}
	}
	CHECK(wrong == 0);

	// celulas + S de uma vez (66 B por slave: o buffer leva um)
	const uint8_t *both[2] = { RDCSALL, RDACSALL };
	TYPE types[2] = { Rdcsall, Rdacsall };
	size_t first_array[2] = { CODES(cell.c_codes), CODES(acell.ac_codes) };

	for (uint8_t round = 0; round < 2u; round++) {

		int16_t codes[32];

		spi_chain_reset(1);
		memset(chips, 0, sizeof(chips));
		for (uint8_t index = 0; index < 32u; index++) {
			codes[index] = (int16_t) (20000 + (round * 100) + index);
		}
		put_codes(spi_chain.devices[0].regs[code_of(both[round])], codes, 32);
		adBmsReadData(1, chips, (uint8_t*) both[round], types[round], ALL_GRP);
		CHECK((code_at(&chips[0], first_array[round], 0) == codes[0]) && (code_at(&chips[0], first_array[round], 15) == codes[15]));
		CHECK((chips[0].scell.sc_codes[0] == codes[16]) && (chips[0].scell.sc_codes[15] == codes[31]));
		CHECK(chips[0].cccrc.scell_pec == 0u);
	}

	// estado B (VD, VA, VR4K)
	spi_chain_reset(1);
	memcpy(spi_chain.devices[0].regs[code_of(RDSTATB)], "\x10\x27\x20\x4E\x30\x75", 6);
	adBmsReadData(1, chips, RDSTATB, Status, B);
	CHECK((chips[0].statb.vd == 10000u) && (chips[0].statb.va == 20000u) && (chips[0].statb.vr4k == 30000u));
}

// grupos e tipos que nao existem: o driver le (ou escreve zeros) e nao mexe em nada
static void test_unknown_groups(void) {

	cell_asic before;

	spi_chain_reset(2);
	memset(chips, 0x33, sizeof(chips));
	memcpy(&before, &chips[0], sizeof(before));
	adBmsReadData(2, chips, RDCFGA, Config, C);
	adBmsReadData(2, chips, RDCVA, Cell, NONE);
	adBmsReadData(2, chips, RDACA, AvgCell, NONE);
	adBmsReadData(2, chips, RDSVA, S_volt, NONE);
	adBmsReadData(2, chips, RDFCA, F_volt, NONE);
	adBmsReadData(2, chips, RDAUXA, Aux, E);
	adBmsReadData(2, chips, RDRAXA, RAux, E);
	adBmsReadData(2, chips, RDSTATA, Status, F);
	adBmsReadData(2, chips, RDPWM1, Pwm, C);
	adBmsReadData(2, chips, RDCVA, Clrflag, A);   // tipo sem leitura
	CHECK(memcmp(&before.cell, &chips[0].cell, sizeof(before.cell)) == 0);
	CHECK(memcmp(&before.aux, &chips[0].aux, sizeof(before.aux)) == 0);
	CHECK(memcmp(&before.stata, &chips[0].stata, sizeof(before.stata)) == 0);
	CHECK(memcmp(&before.PwmA, &chips[0].PwmA, sizeof(before.PwmA)) == 0);
	CHECK(spi_chain.bad_command_pec == 0u);

	// PWM num grupo que nao existe: escreve zeros
	memset(spi_chain.devices[0].regs[code_of(RDPWM1)], 0x77, 6);
	adBmsWriteData(2, chips, WRPWM1, Pwm, C);
	CHECK(memcmp(spi_chain.devices[0].regs[code_of(RDPWM1)], "\0\0\0\0\0\0", 6) == 0);

	// as funcoes de baixo com mais slaves do que os buffers: nada no SPI
	uint8_t buffer[8];
	uint8_t pec[BMS_MAX_SLAVES + 1u];
	uint8_t counters[BMS_MAX_SLAVES + 1u];
	uint8_t data[6 * (BMS_MAX_SLAVES + 1u)];
	uint32_t transactions = spi_chain.transactions;

	memset(data, 0, sizeof(data));
	spiReadData(BMS_MAX_SLAVES, RDCVALL, buffer, pec, counters, RDCVALL_SIZE);
	spiWriteData(BMS_MAX_SLAVES + 1u, WRCFGA, data);
	CHECK(spi_chain.transactions == transactions);
}

/* ================== COMANDOS ADC ================== */

static void test_adc_command_encoding(void) {

	int wrong = 0;

	spi_chain_reset(1);

	// ADCV: 0 1 RD CONT 1 1 DCP 0 RSTF OW[1:0]
	for (int rd = 0; rd <= 1; rd++) {
		for (int cont = 0; cont <= 1; cont++) {
			for (int dcp = 0; dcp <= 1; dcp++) {
				for (int rstf = 0; rstf <= 1; rstf++) {
					for (int ow = 0; ow <= 3; ow++) {
						adBms6830_Adcv((RD) rd, (CONT) cont, (DCP) dcp, (RSTF) rstf, (OW_C_S) ow);
						uint16_t expected = (uint16_t) (0x260u | (rd << 8) | (cont << 7) | (dcp << 4) | (rstf << 2) | ow);
						if (spi_chain.last_command != expected) {
							wrong++;
						}
					}
				}
			}
		}
	}
	CHECK(wrong == 0);
	CHECK(spi_chain.commands == 64u);

	// ADSV: 0 0 1 CONT 1 1 DCP 1 0 OW[1:0]
	adBms6830_Adsv(CONTINUOUS, DCP_ON, OW_ON_ODD_CH);
	CHECK(spi_chain.last_command == (0x168u | 0x80u | 0x10u | 0x2u));
	adBms6830_Adsv(SINGLE, DCP_OFF, OW_OFF_ALL_CH);
	CHECK(spi_chain.last_command == 0x168u);

	// ADAX: 1 0 OW PUP CH4 0 1 CH[3:0]
	adBms6830_Adax(AUX_OW_ON, PUP_UP, (CH) 0x13);
	CHECK(spi_chain.last_command == (0x410u | 0x100u | 0x80u | 0x40u | 0x3u));
	adBms6830_Adax(AUX_OW_OFF, PUP_DOWN, AUX_ALL);
	CHECK(spi_chain.last_command == 0x410u);

	// ADAX2: 1 0 0 0 0 0 0 CH[3:0]
	adBms6830_Adax2((CH) 5);
	CHECK(spi_chain.last_command == 0x405u);
	CHECK(spi_chain.bad_command_pec == 0u);

	// PLADC: devolve o tempo da conversao (o SDO vai a 1 no fim)
	spi_chain.timer_count = 1234;
	CHECK(adBmsPollAdc(PLADC) == 1234u);
	CHECK(spi_chain.last_command == code_of(PLADC));

	// comandos simples
	spiSendCmd(CLRCELL);
	CHECK(spi_chain.last_command == code_of(CLRCELL));
	spiSendCmd(SRST);
	CHECK(spi_chain.last_command == 0x027u);

	// cada comando aceite conta no contador dos slaves (6 bits, da a volta)
	CHECK(spi_chain.devices[0].cc == (uint8_t) (spi_chain.commands & 0x3Fu));
}

static void test_helpers(void) {

	// VOV/VUV: (V - 1,5 V) / (16 x 150 uV), em 12 bits com sinal
	CHECK(abs((int) SetOverVoltageThreshold(4.2f) - 1125) <= 1);
	CHECK(abs((int) SetUnderVoltageThreshold(2.5f) - 417) <= 1);
	CHECK(SetUnderVoltageThreshold(1.5f) == 0u);
	CHECK(abs((int) SetUnderVoltageThreshold(1.26f) - (4096 - 100)) <= 1);   // abaixo de 1,5 V: complemento para 2
	CHECK(SetOverVoltageThreshold(10.0f) <= 0xFFFu);

	memset(chips, 0, sizeof(chips));
	SetConfigB_DischargeTimeOutValue(2, chips, RANG_0_TO_16_8_HR, TIME_2MIN_OR_0_53HR);
	CHECK((chips[1].tx_cfgb.dtrng == 1u) && (chips[1].tx_cfgb.dcto == TIME_2MIN_OR_0_53HR));
	SetConfigB_DischargeTimeOutValue(2, chips, RANG_0_TO_63_MIN, TIME_5MIN_OR_1_33HR);
	CHECK((chips[0].tx_cfgb.dtrng == 0u) && (chips[0].tx_cfgb.dcto == TIME_5MIN_OR_1_33HR));
	CHECK(chips[2].tx_cfgb.dcto == 0u);   // so os tIC pedidos

	CHECK(ConfigB_DccBit(DCC1, DCC_BIT_SET) == 0x0001u);
	CHECK(ConfigB_DccBit(DCC16, DCC_BIT_SET) == 0x8000u);
	CHECK(ConfigB_DccBit(DCC3, DCC_BIT_CLR) == 0u);
	CHECK(ConfigA_Flag(FLAG_D0, FLAG_SET) == 0x01u);
	CHECK(ConfigA_Flag(FLAG_D7, FLAG_SET) == 0x80u);
	CHECK(ConfigA_Flag(FLAG_D7, FLAG_CLR) == 0u);
	CHECK(ConfigA_Gpo(GPO10, GPO_SET) == 0x0200u);
	CHECK(ConfigA_Gpo(GPO1, GPO_CLR) == 0u);
}

int main(void) {

	test_pec15();
	test_pec10();
	test_command_table();
	test_write_config_a();
	test_write_config_b();
	test_write_comm_pwm_clrflag();
	test_read_cells_and_pec_errors();
	test_read_aux_status_sid();
	test_read_all_aux_status();
	test_out_of_range_requests();
	test_every_read_group();
	test_unknown_groups();
	test_adc_command_encoding();
	test_helpers();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
