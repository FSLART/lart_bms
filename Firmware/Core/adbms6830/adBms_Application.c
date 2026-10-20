/*
 * adBms_Application.c
 */

#include "adBms_Application.h"
#include "adBms6830CmdList.h"   //os comandos estao definidos no header tho
#include "adbms_cell_read.h"
#include "adbms_temp_read.h"
#include "adbms_open_wire.h"
#include "adbms_balancing.h"

#define WAKE_SLEEP_EDGE_US 600u		// tWAKE (500 us max) e < tIDLE (4,3 ms min)
#define READ_RETRIES       2u		// o número de vezes que se repete a leitura de um grupo de registos quando chega com o PEC error
#define DEAD_CYCLES        2u		// number de detectoes sem slaves detactados pra ser fault
#define MISSING_RETRIES    2u		// vezes que se volta a acordar a cadeia antes de aceitar slaves != config
#define FAULT_RETRY_US     1000u	// em FAULT, volta a acordar a cadeia ao fim deste valor
#define MIN_NTC_PERCENT    30u		// FS 2027 EV 5.8.6

cell_asic IC[BMS_MAX_SLAVES];

extern TIM_HandleTypeDef htim6;        // tick de 200 ms
static volatile uint32_t tick_count;   // ticks do timer, basicamente conta o numero de resets de overflow do timer6

// TICK DE 200 ms (TIM6)
void ADBMS_Tick(void) {

	tick_count++;
}

// compara o timer6 ao ADBMS_CYCLE_MS pra ver se o timer esta incorretmante configurado
static bool tick_timer_ok(void) {

	uint32_t tick_hz = adBmsTimerClockHz() / (htim6.Instance->PSC + 1u);

	return (tick_hz == 1000u) && ((htim6.Instance->ARR + 1u) == ADBMS_CYCLE_MS);
}

// varios tipos de pec error
static uint8_t* pec_field(TYPE type, uint8_t slave_index) {

	switch (type) {
	case Cell:
		return &IC[slave_index].cccrc.cell_pec;
	case S_volt:
		return &IC[slave_index].cccrc.scell_pec;
	case Aux:
		return &IC[slave_index].cccrc.aux_pec;
	case RAux:
		return &IC[slave_index].cccrc.raux_pec;
	case Status:
		return &IC[slave_index].cccrc.stat_pec;
	case Config:
		return &IC[slave_index].cccrc.cfgr_pec;
	default:
		return &IC[slave_index].cccrc.sid_pec;
	}
}

//Lê os varios grupos, checka os pec errors e dá retry caso seja necessario
static bool read_groups(uint8_t *const commands[], uint8_t group_count, TYPE type, GRP first_group) {

	uint8_t slave_count = bms.cells.slaves_found;
	uint8_t pec_failed[BMS_MAX_SLAVES] = { 0 };
	bool all_ok = true;

	for (uint8_t group_index = 0; group_index < group_count; group_index++) {

		uint8_t retry_counter = 0;
		bool any_pec_error;

		do {
			adBmsReadData(slave_count, IC, commands[group_index], type, (GRP) (first_group + group_index));

			any_pec_error = false;
			for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {
				if (*pec_field(type, slave_index) != 0u) {
					any_pec_error = true;
					bms.pec.per_slave[slave_index]++;
					bms.pec.total++;
				}
			}
		} while (any_pec_error && (++retry_counter <= READ_RETRIES));

		for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {
			pec_failed[slave_index] |= *pec_field(type, slave_index);
		}
	}

	for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {
		*pec_field(type, slave_index) = pec_failed[slave_index];

		if (pec_failed[slave_index] != 0u) {
			bms.pec.last_read[slave_index] = true;
			all_ok = false;
		}
	}

	return all_ok;
}

//start cell readings
void ADBMS_StartCells(void) {

	adBmsWakeupIc(bms.cells.slaves_found);

	/* RD=1: o S-ADC mede em paralelo e o ADBMS compara (CSxFLT). Os DCC ja
	 * estao a 0 desde o CONFIG: o DCP=0 sozinho nao os desliga */
	adBms6830_Adcv(RD_ON, SINGLE, DCP_OFF, RSTF_OFF, OW_OFF_ALL_CH);
}

//start aux readings
void ADBMS_StartAux(void) {

	adBmsWakeupIc(bms.cells.slaves_found);
	adBms6830_Adax(AUX_OW_OFF, PUP_DOWN, AUX_ALL);   // GPIOs, VREF2, ITMP, VA, VD
	adBms6830_Adax2(AUX_ALL);                        // GPIOs pelo caminho redundante
}

//read cell readings
bool ADBMS_ReadCells(void) {

	static uint8_t *const rdcv[] = { RDCVA, RDCVB, RDCVC, RDCVD, RDCVE, RDCVF };
	static uint8_t *const rdstatc[] = { RDSTATC };

	bool cells_ok = read_groups(rdcv, 6u, Cell, A);
	bool status_ok = read_groups(rdstatc, 1u, Status, C);   // CSxFLT da comparacao C vs S

	return cells_ok && status_ok;
}

//read aux readings
bool ADBMS_ReadAuxAndStatus(void) {

	static uint8_t *const rdaux[] = { RDAUXA, RDAUXB, RDAUXC, RDAUXD };
	static uint8_t *const rdrax[] = { RDRAXA, RDRAXB, RDRAXC, RDRAXD };
	static uint8_t *const rdstat[] = { RDSTATA, RDSTATB, RDSTATC, RDSTATD, RDSTATE };

	bool aux_ok = read_groups(rdaux, 4u, Aux, A);
	bool raux_ok = read_groups(rdrax, 4u, RAux, A);
	bool status_ok = read_groups(rdstat, 5u, Status, A);

	return aux_ok && raux_ok && status_ok;
}

//start ow readings
void ADBMS_StartOpenWire(OW_C_S channels) {

	adBmsWakeupIc(bms.cells.slaves_found);
	adBms6830_Adsv(SINGLE, DCP_OFF, channels);
}

//read ow cell results
bool ADBMS_ReadSCells(void) {

	static uint8_t *const rdsv[] = { RDSVA, RDSVB, RDSVC, RDSVD, RDSVE, RDSVF };

	return read_groups(rdsv, 6u, S_volt, A);
}

//write discharge mask
bool ADBMS_WriteDcc(void) {

	static uint8_t *const rdcfgb[] = { RDCFGB };
	uint8_t slave_count = bms.cells.slaves_found;
	bool all_ok = true;

	for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {
		IC[slave_index].tx_cfgb.dcc = bms.balancing.dcc_mask[slave_index];
	}

	adBmsWriteData(slave_count, IC, WRCFGB, Config, B);
	(void) read_groups(rdcfgb, 1u, Config, B);

	for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {

		const cell_asic *chip = &IC[slave_index];

		if ((chip->cccrc.cfgr_pec != 0u) || (chip->rx_cfgb.dcc != chip->tx_cfgb.dcc)) {
			all_ok = false;
		}
	}

	return all_ok;
}

/* Config de todos os slaves, reescrita em cada ciclo (um ADBMS que reiniciou
 * volta a ficar certo):
 *  - REFON: referencia sempre ligada, sem os ate 4,4 ms de tREFUP por conversao
 *  - CTH 8,1 mV: limite da comparacao C-ADC vs S-ADC (o default do datasheet)
 *  - GPO todos a 1: GPIOs sem pull-down, sao as entradas analogicas dos NTC
 *  - VOV/VUV: comparadores do proprio ADBMS com os limites da config
 *  - DCC a 0: o ADCV e o open-wire medem sem descarga. O DCP=0 so corta a
 *    descarga por PWM, os DCC tem prioridade (datasheet Tabela 27); com um
 *    switch ligado o S-ADC nao bate com o C-ADC (CSxFLT) e o open-wire falha.
 *    O Balancing_Step volta a liga-los depois do open-wire
 * Depois limpa as flags de diagnostico da STATC (CSxFLT, SPIFLT, SLEEP...) para
 * a leitura deste ciclo so mostrar falhas novas, e le a config de volta */
static void write_config(void) {

	static uint8_t *const rdcfg[] = { RDCFGA, RDCFGB };
	uint8_t slave_count = bms.cells.slaves_found;
	uint16_t vov_code = SetOverVoltageThreshold((float) bms.config.cell_max_mV / 1000.0f);
	uint16_t vuv_code = SetUnderVoltageThreshold((float) bms.config.cell_min_mV / 1000.0f);

	for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {

		cell_asic *chip = &IC[slave_index];

		chip->tx_cfga.refon = PWR_UP;
		chip->tx_cfga.cth = CVT_8_1mV;
		chip->tx_cfga.gpo = 0x3FFu;
		chip->tx_cfgb.vov = vov_code;
		chip->tx_cfgb.vuv = vuv_code;
		chip->tx_cfgb.dcc = 0u;
		memset(&chip->clflag, 0xFF, sizeof(chip->clflag));
	}

	adBmsWriteData(slave_count, IC, WRCFGA, Config, A);
	adBmsWriteData(slave_count, IC, WRCFGB, Config, B);
	adBmsWriteData(slave_count, IC, CLRFLAG, Clrflag, NONE);
	(void) read_groups(rdcfg, 2u, Config, A);

	for (uint8_t slave_index = 0; slave_index < slave_count; slave_index++) {

		const cell_asic *chip = &IC[slave_index];
		bool config_ok = (chip->cccrc.cfgr_pec == 0u) && (chip->rx_cfga.refon == chip->tx_cfga.refon) && (chip->rx_cfga.cth == chip->tx_cfga.cth)
				&& (chip->rx_cfga.gpo == chip->tx_cfga.gpo) && (chip->rx_cfgb.vov == (chip->tx_cfgb.vov & 0x0FFFu))
				&& (chip->rx_cfgb.vuv == (chip->tx_cfgb.vuv & 0x0FFFu)) && (chip->rx_cfgb.dcc == chip->tx_cfgb.dcc);

		if (!config_ok) {
			bms.adbms.config_errors++;
		}
		bms.adbms.config_ok[slave_index] = config_ok;
	}
}

//verificar se o sid é válido
static bool sid_valid(const uint8_t *sid) {

	bool all_00 = true;
	bool all_ff = true;

	for (uint8_t byte_index = 0; byte_index < RSID; byte_index++) {
		if (sid[byte_index] != 0x00u) {
			all_00 = false;
		}
		if (sid[byte_index] != 0xFFu) {
			all_ff = false;
		}
	}

	return !all_00 && !all_ff;
}

// encontrar o numero de dispositivos na isospi daisychain
static uint8_t count_slaves(void) {

	uint8_t detected_slaves = 0;

	adBmsReadData(BMS_MAX_SLAVES, IC, RDSID, Sid, NONE);

	while ((detected_slaves < BMS_MAX_SLAVES) && (IC[detected_slaves].cccrc.sid_pec == 0u) && sid_valid(IC[detected_slaves].sid.sid)) {
		detected_slaves++;
	}

	return detected_slaves;
}

// sem slaves nao ha medicoes e sem comandos o ADBMS entra em sleep ao fim de tSLEEP
static void enter_fault(void) {

	adBmsCsHigh();
	bms.cells.slaves_found = 0u;
	bms.faults.slave_missing = true;
	Balancing_Stop();
	bms.adbms.step_us = adBmsMicros();
	bms.adbms.state = ADBMS_FAULT;
}

// serve como watchdog, verifica o quão recente são os dados
static void check_timeouts(void) {

	uint32_t now_ms = HAL_GetTick();
	bool any_late = false;

	for (uint8_t slave_index = 0; slave_index < bms.config.slaves; slave_index++) {
		if (!ADBMS_Fresh(bms.adbms.cells_valid_ms[slave_index], now_ms) || !ADBMS_Fresh(bms.adbms.temps_valid_ms[slave_index], now_ms)) {
			any_late = true;
		}
	}

	bms.faults.adbms_timeout = any_late;
}

// inicialização, valida os vaslores nas configs, reset á maquina de estados e start timers
bool ADBMS_Init(void) {

	bms_config_t *config = &bms.config;
	bool config_ok = true;

	// topologia da arquitetura
	if ((config->slaves == 0u) || (config->slaves > BMS_MAX_SLAVES)) {
		config->slaves = BMS_MAX_SLAVES;
		config_ok = false;
	}
	if ((config->cells_per_slave == 0u) || (config->cells_per_slave > BMS_MAX_CELLS)) {
		config->cells_per_slave = BMS_MAX_CELLS;
		config_ok = false;
	}
	if (config->ntcs_per_slave > BMS_MAX_NTCS) {
		config->ntcs_per_slave = BMS_MAX_NTCS;
		config_ok = false;
	}

	// FS 2027 EV 5.8.6: NTC 30 % das celulas
	for (uint8_t slave_index = 0; slave_index < config->slaves; slave_index++) {

		uint16_t disabled_ntcs = config->ntc_disabled_mask[slave_index] & (uint16_t) ((1u << config->ntcs_per_slave) - 1u);
		uint32_t enabled_ntcs = (uint32_t) config->ntcs_per_slave - (uint32_t) __builtin_popcount(disabled_ntcs);

		if ((enabled_ntcs * 100u) < ((uint32_t) config->cells_per_slave * MIN_NTC_PERCENT)) {
			config_ok = false;
		}
	}

	// tick com outro periodo = medicoes fora do tempo que a FS 2027 pede
	if (!tick_timer_ok()) {
		config_ok = false;
	}

	memset(&bms.adbms, 0, sizeof(bms.adbms));   // state = ADBMS_INIT
	bms.adbms.tick_seen = tick_count;           // os ticks antes do init nao contam
	adBmsTimerInit();
	HAL_TIM_Base_Start_IT(&htim6);

	return config_ok;
}

void ADBMS_Run(void) {

	uint32_t now_us = adBmsMicros();
	uint32_t waited_us = now_us - bms.adbms.step_us;

	switch (bms.adbms.state) {

	// arcordar a daisychain com o chipselect
	case ADBMS_INIT:
		bms.adbms.wake_pulses = BMS_MAX_SLAVES;
		adBmsCsLow();
		bms.adbms.step_us = now_us;
		bms.adbms.state = ADBMS_WAKE_LOW;
		break;

	case ADBMS_WAKE_LOW:
		if (waited_us >= WAKE_SLEEP_EDGE_US) {
			adBmsCsHigh();
			bms.adbms.step_us = now_us;
			bms.adbms.state = ADBMS_WAKE_HIGH;
		}
		break;

	case ADBMS_WAKE_HIGH:
		if (waited_us >= WAKE_SLEEP_EDGE_US) {

			bms.adbms.wake_pulses--;

			if (bms.adbms.wake_pulses > 0u) {
				adBmsCsLow();
				bms.adbms.step_us = now_us;
				bms.adbms.state = ADBMS_WAKE_LOW;
			} else {
				bms.adbms.state = ADBMS_DETECT;   // muda pro detect pra avalaiar o numero de devices da chain
			}
		}
		break;

	case ADBMS_WAIT_TICK: {
		uint32_t tick_now = tick_count;

		if (tick_now != bms.adbms.tick_seen) {

			// mais que um tick desde o ultimo ciclo means o ciclo passou dos 200 ms
			if ((bms.adbms.cycles > 0u) && ((tick_now - bms.adbms.tick_seen) > 1u)) {
				bms.adbms.overruns += (tick_now - bms.adbms.tick_seen) - 1u;
			}

			bms.adbms.tick_seen = tick_now;
			bms.adbms.state = ADBMS_DETECT;
		}
		break;
	}

	case ADBMS_DETECT: {
		bms.adbms.cycle_start_us = now_us;
		memset(bms.pec.last_read, 0, sizeof(bms.pec.last_read));

		uint8_t detected_slaves = count_slaves();
		bms.cells.slaves_found = detected_slaves;

		if (detected_slaves == 0u) {
			if (++bms.adbms.dead_count >= DEAD_CYCLES) {
				enter_fault();
			} else {
				bms.adbms.state = ADBMS_INIT;   //chain pode tar em sleep, wakepu just in case
			}
			break;
		}
		bms.adbms.dead_count = 0u;

		if (detected_slaves != bms.config.slaves) {

			// um slave ainda sleep, pd n ser necessariamnte slave em falta
			if (bms.adbms.missing_count < MISSING_RETRIES) {
				bms.adbms.missing_count++;
				bms.adbms.state = ADBMS_INIT;
				break;
			}

			// continua a medir os que respondem; o fault fica ate a cadeia bater certo
			bms.faults.slave_missing = true;
		} else {
			bms.adbms.missing_count = 0u;
			bms.faults.slave_missing = false;
		}

		bms.adbms.state = ADBMS_CONFIG;
		break;
	}

	case ADBMS_CONFIG:
		write_config();
		CellRead_Start();
		bms.adbms.state = ADBMS_CELL_READ;
		break;

	case ADBMS_CELL_READ:
		if (CellRead_Step()) {
			OpenWire_Start();
			bms.adbms.state = ADBMS_OPEN_WIRE;
		}
		break;

	case ADBMS_OPEN_WIRE:
		if (OpenWire_Step()) {
			bms.adbms.state = ADBMS_BALANCING;
		}
		break;

	// depois das medicoes que precisam dos DCC a 0, vamos experimentar fazer discharge durante a leitura das ntcs que é AUX e nao Cell
	case ADBMS_BALANCING:
		Balancing_Step();
		TempRead_Start();
		bms.adbms.state = ADBMS_TEMP_READ;
		break;

	case ADBMS_TEMP_READ: {
		if (!TempRead_Step()) {
			break;
		}

		bool pec_failed = false;
		for (uint8_t slave_index = 0; slave_index < bms.cells.slaves_found; slave_index++) {
			pec_failed = pec_failed || bms.pec.last_read[slave_index];
		}
		bms.faults.pec_error = pec_failed;

		bms.adbms.spi_errors = adBmsSpiErrors();
		bms.adbms.cycles++;
		bms.adbms.cycle_us = adBmsMicros() - bms.adbms.cycle_start_us;
		bms.adbms.state = ADBMS_WAIT_TICK;
		break;
	}

	case ADBMS_FAULT:
		if (waited_us >= FAULT_RETRY_US) {
			bms.adbms.missing_count = 0u;
			bms.adbms.dead_count = 0u;
			bms.adbms.state = ADBMS_INIT;
		}
		break;

	default:
		//se esta aqui, ta tudo fdd
		enter_fault();
		break;
	}

	// verificar o quão old sao os dados devido a mas leituras, n dependendo de um eestado especifico da state machine
	check_timeouts();
}
