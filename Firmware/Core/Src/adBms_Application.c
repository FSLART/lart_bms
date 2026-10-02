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
#include "gpio_expander.h"
#include "adbms_to_CAN.h"   // g_pack_tmax_cC (cross-check ITMP no balanceamento)
#include "ams_error.h"
#include "precharge.h"

/* OW AMS_ERROR (clearable): definido junto aos evaluate, chamado na state machine */
static void adBms6830_OpenWire_UpdateAmsError(void);

/* Fases de open-wire partilhadas por IDLE, CHARGING e BALANCING: chamar a
 * cada passagem, devolve true quando a avaliacao acabou */
static bool OpenWire_Step(void);
static void OpenWire_Restart(void);

uint8_t slaves_found = 0;

/**
 *******************************************************************************
 * @brief Setup Variables
 * The following variables can be modified to configure the software.
 *******************************************************************************
 */

typedef enum {
	ADBMS_IDLE_READ_PREV = 0,
	ADBMS_IDLE_READ_AVG_START_AUX,
	ADBMS_IDLE_READ_AUX_START_RAUX,
	ADBMS_IDLE_READ_RAUX_STATUS,
	ADBMS_IDLE_OW
} adbms_idle_phase_t;

/* Mesmo ciclo de leitura do IDLE mas sem as fases de open-wire: durante o
 * carregamento só interessa tensao/temperatura frescas para o charger */
typedef enum {
	ADBMS_CHARGING_READ_PREV = 0,
	ADBMS_CHARGING_READ_AVG_START_AUX,
	ADBMS_CHARGING_READ_AUX_START_RAUX,
	ADBMS_CHARGING_READ_RAUX_SNAPSHOT,
	ADBMS_CHARGING_OW
} adbms_charging_phase_t;

typedef enum {
	BAL_CYCLE_INIT = 0,
	BAL_CYCLE_APPLY,
	BAL_CYCLE_ON_TIME,
	BAL_CYCLE_STOP_DISCHARGE,
	BAL_CYCLE_SETTLE,
	BAL_CYCLE_START_AVG,
	BAL_CYCLE_WAIT_AVG,
	BAL_CYCLE_READ_AVG,
	BAL_CYCLE_COMPUTE,
	BAL_CYCLE_OW          /* no fim: live_debug indexa nomes por esta ordem */
} adbms_balancing_phase_t;

typedef enum {
	STARTUP_START_AVG = 0, STARTUP_READ_AVG_START_AUX, STARTUP_READ_AUX_START_RAUX, STARTUP_READ_RAUX_STATUS, STARTUP_END
} adbms_startup_phase_t;

cell_asic IC[ADBMS_MAX_DEVICES];

cell_asic SLAVE[ADBMS_MAX_DEVICES];

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
const float UV_THRESHOLD = 2.7; /* Volt */
const int OWC_Threshold = 2500; /* Cell Open wire threshold(mili volt) */
const int OWC_Threshold_Delta = 350; /* Cell Open wire threshold(mili volt) */
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

adbms_startup_phase_t startupPhase = STARTUP_START_AVG;
uint32_t startupPhaseStart = 0;

adbms_idle_phase_t adbmsPhase = ADBMS_IDLE_READ_PREV;
uint32_t adbmsPhaseStart = 0;

adbms_charging_phase_t chargingPhase = ADBMS_CHARGING_READ_PREV;
uint32_t chargingPhaseStart = 0;
static balance_config_t g_balance_cfg;
static bool g_balance_cfg_initialized = false;

uint16_t global_min_mV = 0; //tem de ser global a puta, fdss
uint16_t balance_start_min_mV = 0xFFFF;
balance_stage_t balanceStage = BALANCE_STAGE_ROUGH;

static adbms_balancing_phase_t balPhase = BAL_CYCLE_INIT;
static uint32_t balPhaseStart = 0;
/* Tem de ficar abaixo do tSLEEP mínimo do ADBMS6830 (1.8s): sem comandos
 * válidos durante o pulso, o watchdog limpa os bits DCC e o IC adormece */
static const uint32_t BALANCE_ON_TIME_MS = 1500;
/* Relaxação da célula após cortar a descarga; 15ms só limpava a queda
 * ohmica, com deadband de 8mV convém deixar assentar mais */
static const uint32_t BALANCE_SETTLE_MS = 100;
static const uint32_t AVG_CONV_WAIT_MS = 20;
/* Guarda térmica dos FETs internos de descarga (datasheet: monitorizar
 * die temp com descarga interna) */
static const float BALANCE_DIE_TEMP_LIMIT_C = 85.0f;

/* Latest state returned by adbms_main(), mirrored for live debug snapshot */
volatile adbms_result_state adbms_current_state = ADBMS_END;

/* Depois de um fim por die OT, o auto-arranque espera isto para arrefecer */
static const uint32_t BALANCE_DIE_OT_HOLD_MS = 60000;
static uint32_t bal_hold_start_ms = 0;
static bool bal_hold_active = false;

/* Ultima mascara DCC calculada por slave (0 fora do balanceamento), para a 0x706 */
static uint16_t bal_mask[12] = { 0 };

/* balPhase é static; expor só o valor para o live debug */
uint8_t Balancing_GetPhase(void) {
	return (uint8_t) balPhase;
}

uint16_t Balancing_GetMask(uint8_t module) {
	return (module < 12) ? bal_mask[module] : 0;
}

/* Grupos A..F de celulas (Cell = RDCV, AvgCell = RDAC) com PEC acumulado.
 * O vendor grava cell_pec/acell_pec com o PEC do ULTIMO grupo lido (=, nao
 * |=): o grupo F e' a celula 16, que nao existe com 12s, logo o flag nunca
 * refletia nenhuma celula real. Aqui fica 1 se qualquer grupo do slave
 * falhou nesta leitura -> SafetyCheck e balanceamento ignoram esse slave
 * neste ciclo e voltam a usa-lo quando ler limpo */
static void ReadCellGroups(TYPE type) {
	uint8_t *rdcv[6] = { RDCVA, RDCVB, RDCVC, RDCVD, RDCVE, RDCVF };
	uint8_t *rdac[6] = { RDACA, RDACB, RDACC, RDACD, RDACE, RDACF };
	uint8_t pec_acc[ADBMS_MAX_DEVICES] = { 0 };

	for (uint8_t g = 0; g < 6; g++) {
		adBmsReadData(slaves_found, &IC[0], (type == Cell) ? rdcv[g] : rdac[g], type, (GRP) (A + g));

		for (uint8_t m = 0; m < slaves_found; m++) {
			pec_acc[m] |= (type == Cell) ? IC[m].cccrc.cell_pec : IC[m].cccrc.acell_pec;
		}
	}

	for (uint8_t m = 0; m < slaves_found; m++) {
		if (type == Cell) {
			IC[m].cccrc.cell_pec = pec_acc[m];
		} else {
			IC[m].cccrc.acell_pec = pec_acc[m];
		}
	}
}

/* Fim de sessao (convergiu, die OT, precarga a arrancar, AMS_ERROR): DCC a
 * zero ja' escrita nos ICs, estado limpo e volta a IDLE */
static void Balancing_Stop(void) {
	for (uint8_t module = 0; module < slaves_found; module++) {
		IC[module].tx_cfgb.dcc = 0;
	}
	memset(bal_mask, 0, sizeof(bal_mask));

	g_balance_cfg_initialized = false;
	balPhase = BAL_CYCLE_INIT;
	AMS_State = IDLE;
	adBms6830_init_config(slaves_found, &IC[0]);
}

static adbms_result_state adbms_main_impl(AMSStates_t ams_state);

adbms_result_state adbms_main(AMSStates_t ams_state) {
	adbms_result_state result = adbms_main_impl(ams_state);
	adbms_current_state = result;
	return result;
}

static adbms_result_state adbms_main_impl(AMSStates_t ams_state) {

	switch (ams_state) {

	case BALANCING:
		/* So' se balanceia com a HV totalmente desligada e sem AMS_ERROR:
		 * precarga a arrancar corta a DCC ja', sem esperar pelo fim do ciclo */
		if ((Precharge_GetState() != KILL) || AMS_Error_IsActive()) {
			printfDebug("BAL STOP: precharge=%d ams_error=%u\r\n", (int) Precharge_GetState(), AMS_Error_IsActive());
			Balancing_Stop();
			break;
		}

		if (!g_balance_cfg_initialized) {
			Balance_InitDefaultConfig(&g_balance_cfg);

			balanceStage = BALANCE_STAGE_ROUGH;
			balance_start_min_mV = BatteryPack_FindMinVoltageGlobally(&IC[0], slaves_found, &g_balance_cfg);

			global_min_mV = balance_start_min_mV;   // freeze target for stage 1
			balPhase = BAL_CYCLE_INIT;

			g_balance_cfg_initialized = true;
		}

		switch (balPhase) {

		case BAL_CYCLE_INIT:

			/* Recalcular o estágio ANTES de decidir sair: senão o END só é
			 * detetado um ciclo (~1.6s) mais tarde. BALANCE_END vindo de fora
			 * (paragem via CAN) é sticky - não recalcular por cima dele */
			if (balanceStage != BALANCE_END) {
				balanceStage = BatteryPack_DetermineBalanceStage(&IC[0], slaves_found, &g_balance_cfg, global_min_mV);

				if (balanceStage == BALANCE_END) {
					printfDebug("BAL END: converged/no work (target=%umV)\r\n", global_min_mV);
				}
			} else {
				printfDebug("BAL END: external stop (CAN)\r\n");
			}

			/* Guarda térmica: FETs de descarga internos, die temp via STATA.
			 * Endurecida contra lixo de leitura (último IC da chain já provou
			 * entregar registos podres): PEC tem de estar OK, valor tem de ser
			 * plausível, e só aborta com 2 ciclos consecutivos acima do limite */
			{
				static uint8_t die_ot_count[12] = { 0 };

				for (uint8_t module = 0; module < slaves_found && module < 12; module++) {

					/* STATA deste ciclo com PEC errado -> registo não fiável */
					if (IC[module].cccrc.stat_pec != 0) {
						continue;
					}

					float itmp_v = ((IC[module].stata.itmp + 10000) * 0.000150f);
					float die_c = (itmp_v / 0.0075f) - 273.0f;

					/* Fora de -40..150ºC = leitura impossível (0x8000/lixo),
					 * ignorar sem mexer no contador */
					if ((die_c < -40.0f) || (die_c > 150.0f)) {
						printfDebug("BAL ITMP GARBAGE: S%u raw=0x%04X (%.1fC)\r\n", module + 1, (uint16_t) IC[module].stata.itmp, die_c);
						continue;
					}

					/* Cross-check físico: die não pode estar >60ºC acima do NTC
					 * mais quente do pack (S12 já entregou 125.6ºC de lixo que
					 * passava na janela absoluta). Backstop real: thermal
					 * shutdown interno do chip a ~150ºC */
					float ntc_max_c = ((float) g_pack_tmax_cC) / 100.0f;
					if (die_c > (ntc_max_c + 60.0f)) {
						printfDebug("BAL ITMP IMPLAUSIVEL: S%u raw=0x%04X (%.1fC, NTCmax=%.1fC)\r\n", module + 1, (uint16_t) IC[module].stata.itmp, die_c, ntc_max_c);
						continue;
					}

					if (die_c >= BALANCE_DIE_TEMP_LIMIT_C) {
						die_ot_count[module]++;

						if (die_ot_count[module] >= 2) {
							printfDebug("BAL DIE OT: S%u raw=0x%04X %.1fC\r\n", module + 1, (uint16_t) IC[module].stata.itmp, die_c);
							RAISE_ERROR(FAULT_BALANCING_OVERTEMP, .slave_idx = module + 1, .measured_value = die_c, .threshold_value = BALANCE_DIE_TEMP_LIMIT_C);
							balanceStage = BALANCE_END;

							// sem isto o auto-arranque reabria a sessao no ciclo seguinte
							bal_hold_start_ms = getRuntimeMs();
							bal_hold_active = true;
						}
					} else {
						die_ot_count[module] = 0;
					}
				}

				/* Sessão terminou: limpar contadores para a próxima */
				if (balanceStage == BALANCE_END) {
					memset(die_ot_count, 0, sizeof(die_ot_count));
				}
			}

			if (balanceStage == BALANCE_END) {
				Balancing_Stop();
			} else {

				balPhase = BAL_CYCLE_COMPUTE;
			}

			break;

		case BAL_CYCLE_COMPUTE:
			for (uint8_t module = 0; module < slaves_found; module++) {
				balance_result_t result;

				Balance_ComputeModule(&IC[module], &g_balance_cfg, &result, global_min_mV, balanceStage);

				Balance_ApplyToIc(&IC[module], &result, global_min_mV, module);
			}

			if (global_min_mV == 0xFFFF) {
				for (uint8_t module = 0; module < slaves_found; module++) {
					IC[module].tx_cfgb.dcc = 0;
				}
			}

			/* mascara que vai mesmo ser aplicada neste ciclo -> 0x706 */
			for (uint8_t module = 0; module < slaves_found && module < 12; module++) {
				bal_mask[module] = IC[module].tx_cfgb.dcc;
			}

			balPhase = BAL_CYCLE_APPLY;
			break;

		case BAL_CYCLE_APPLY:
			adBmsWakeupIc(slaves_found);
			adBmsWriteData(slaves_found, &IC[0], WRCFGB, Config, B);
			//adBmsWriteData(slaves_found, &IC[0], WRPWM1, Pwm, A);   // only 12 populated cells

			balPhaseStart = getRuntimeMs();
			balPhase = BAL_CYCLE_ON_TIME;

			break;

		case BAL_CYCLE_ON_TIME:
			if (getRuntimeMsDiff(balPhaseStart) >= BALANCE_ON_TIME_MS) {
				balPhase = BAL_CYCLE_STOP_DISCHARGE;
			}

			break;

		case BAL_CYCLE_STOP_DISCHARGE:
			for (uint8_t module = 0; module < slaves_found; module++) {
				IC[module].tx_cfgb.dcc = 0;
			}

			adBmsWakeupIc(slaves_found);
			adBmsWriteData(slaves_found, &IC[0], WRCFGA, Config, A);
			adBmsWriteData(slaves_found, &IC[0], WRCFGB, Config, B);
			//adBmsWriteData(slaves_found, &IC[0], WRPWM1, Pwm, A);

			balPhaseStart = getRuntimeMs();
			balPhase = BAL_CYCLE_SETTLE;

			break;

		case BAL_CYCLE_SETTLE:
			if (getRuntimeMsDiff(balPhaseStart) >= BALANCE_SETTLE_MS) {
				balPhase = BAL_CYCLE_START_AVG;
			}

			break;

		case BAL_CYCLE_START_AVG:
			adBmsWakeupIc(slaves_found);
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
			adBmsWakeupIc(slaves_found);
			/* ORDEM IMPORTA: as arrays do cell_asic (CELL=12, AUX=6, RAUX=6) sao
			 * menores do que o que o parser do vendor escreve (16/12/10), e cada
			 * leitura transborda para a struct seguinte:
			 *   RDCV  -> ac_codes[0..3]   (logo RDAC depois de RDCV)
			 *   RDAUX -> ra_codes[0..5]   (logo RDRAX depois de RDAUX)
			 *   RDRAX -> stata.itmp       (logo RDSTAT depois de RDRAX)
			 * Com RDAC antes de RDCV o open-wire via ac_codes[0..3] estragados e
			 * marcava as celulas 1-4 de todos os slaves. */
			ReadCellGroups(Cell);
			ReadCellGroups(AvgCell);

			adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
			adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
			adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
			adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

			adBmsReadData(slaves_found, &IC[0], RDRAXA, RAux, A);
			adBmsReadData(slaves_found, &IC[0], RDRAXB, RAux, B);
			adBmsReadData(slaves_found, &IC[0], RDRAXC, RAux, C);
			adBmsReadData(slaves_found, &IC[0], RDRAXD, RAux, D);

			adBmsReadData(slaves_found, &IC[0], RDSTATA, Status, A);
			adBmsReadData(slaves_found, &IC[0], RDSTATB, Status, B);
			adBmsReadData(slaves_found, &IC[0], RDSTATC, Status, C);
			adBmsReadData(slaves_found, &IC[0], RDSTATD, Status, D);
			adBmsReadData(slaves_found, &IC[0], RDSTATE, Status, E);

			memcpy(SLAVE, IC, sizeof(SLAVE));

			OpenWire_Restart();
			balPhase = BAL_CYCLE_OW;

			break;

		case BAL_CYCLE_OW:
			/* DCC ja' esta a 0 desde o STOP_DISCHARGE: OW a cada nova mascara */
			if (OpenWire_Step()) {
				balPhase = BAL_CYCLE_INIT;
			}

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

			adBmsWakeupIc(slaves_found);
			ReadCellGroups(Cell);

			adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			adbmsPhaseStart = getRuntimeMs();
			adbmsPhase = ADBMS_IDLE_READ_AVG_START_AUX;

			return ADBMS_START;
			break;

		case ADBMS_IDLE_READ_AVG_START_AUX:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				ReadCellGroups(AvgCell);

				//Read AUX
				adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_AUX_START_RAUX;
			}
			break;

		case ADBMS_IDLE_READ_AUX_START_RAUX:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

				//Read GPIOS
				adBms6830_Adax2(AUX_CH_TO_CONVERT);
				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_RAUX_STATUS;
			}
			break;

		case ADBMS_IDLE_READ_RAUX_STATUS:
			if (getRuntimeMsDiff(adbmsPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDRAXA, RAux, A);
				adBmsReadData(slaves_found, &IC[0], RDRAXB, RAux, B);
				adBmsReadData(slaves_found, &IC[0], RDRAXC, RAux, C);
				adBmsReadData(slaves_found, &IC[0], RDRAXD, RAux, D);
				/* RDSTAT depois do RDRAX: o RDRAX transborda para stata.itmp */
				adBmsReadData(slaves_found, &IC[0], RDSTATA, Status, A);
				adBmsReadData(slaves_found, &IC[0], RDSTATB, Status, B);
				adBmsReadData(slaves_found, &IC[0], RDSTATC, Status, C);
				adBmsReadData(slaves_found, &IC[0], RDSTATD, Status, D);
				adBmsReadData(slaves_found, &IC[0], RDSTATE, Status, E);
				//printVoltages(slaves_found, &IC[0], Aux);

				/*  SNAPSHOT  antes das conversoes OW mexerem nos registos */
				memcpy(SLAVE, IC, sizeof(SLAVE));
				OpenWire_Restart();

				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_OW;
			}
			break;

		case ADBMS_IDLE_OW:
			if ((getRuntimeMsDiff(adbmsPhaseStart) >= 5) && OpenWire_Step()) {

				/* auto-arranque do balanceamento: dados do ciclo frescos (ac_codes
				 * lidos neste ciclo, OW acabado). Pack parado (precarga em KILL),
				 * sem AMS_ERROR e fora da pausa de arrefecimento do die OT */
				Balance_InitDefaultConfig(&g_balance_cfg);

				if (bal_hold_active && (getRuntimeMsDiff(bal_hold_start_ms) >= BALANCE_DIE_OT_HOLD_MS)) {
					bal_hold_active = false;
				}

				if ((Precharge_GetState() == KILL) && !AMS_Error_IsActive() && !bal_hold_active && BatteryPack_NeedsBalancing(&IC[0], slaves_found, &g_balance_cfg)) {
					printfDebug("BAL START: auto (delta > %umV)\r\n", g_balance_cfg.start_delta_mV);
					AMS_State = BALANCING;
				}

				adbmsPhaseStart = getRuntimeMs();
				adbmsPhase = ADBMS_IDLE_READ_PREV;
			}
			break;
		}
		break;

		//break;

	case CHARGING:

		g_balance_cfg_initialized = false; // making sure it is always false

		switch (chargingPhase) {

		case ADBMS_CHARGING_READ_PREV:

			adBmsWakeupIc(slaves_found);
			ReadCellGroups(Cell);

			adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);
			chargingPhaseStart = getRuntimeMs();
			chargingPhase = ADBMS_CHARGING_READ_AVG_START_AUX;

			return ADBMS_START;
			break;

		case ADBMS_CHARGING_READ_AVG_START_AUX:
			if (getRuntimeMsDiff(chargingPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				ReadCellGroups(AvgCell);

				//Read AUX
				adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);
				chargingPhaseStart = getRuntimeMs();
				chargingPhase = ADBMS_CHARGING_READ_AUX_START_RAUX;
			}
			break;

		case ADBMS_CHARGING_READ_AUX_START_RAUX:
			if (getRuntimeMsDiff(chargingPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

				//Read GPIOS
				adBms6830_Adax2(AUX_CH_TO_CONVERT);
				chargingPhaseStart = getRuntimeMs();
				chargingPhase = ADBMS_CHARGING_READ_RAUX_SNAPSHOT;
			}
			break;

		case ADBMS_CHARGING_READ_RAUX_SNAPSHOT:
			if (getRuntimeMsDiff(chargingPhaseStart) >= 10) {
				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDRAXA, RAux, A);
				adBmsReadData(slaves_found, &IC[0], RDRAXB, RAux, B);
				adBmsReadData(slaves_found, &IC[0], RDRAXC, RAux, C);
				adBmsReadData(slaves_found, &IC[0], RDRAXD, RAux, D);
				/* RDSTAT depois do RDRAX: o RDRAX transborda para stata.itmp */
				adBmsReadData(slaves_found, &IC[0], RDSTATA, Status, A);
				adBmsReadData(slaves_found, &IC[0], RDSTATB, Status, B);
				adBmsReadData(slaves_found, &IC[0], RDSTATC, Status, C);
				adBmsReadData(slaves_found, &IC[0], RDSTATD, Status, D);
				adBmsReadData(slaves_found, &IC[0], RDSTATE, Status, E);

				/*  SNAPSHOT  - o charger decide (4.2V / 60C) com base nos agregados
				 * calculados a partir do SLAVE[], tirado antes das conversoes OW
				 * mexerem nos registos */
				memcpy(SLAVE, IC, sizeof(SLAVE));
				OpenWire_Restart();

				chargingPhaseStart = getRuntimeMs();
				chargingPhase = ADBMS_CHARGING_OW;
			}
			break;

		case ADBMS_CHARGING_OW:
			if ((getRuntimeMsDiff(chargingPhaseStart) >= 5) && OpenWire_Step()) {
				chargingPhaseStart = getRuntimeMs();
				chargingPhase = ADBMS_CHARGING_READ_PREV;
			}
			break;
		}
		break;

	case STARTUP:

		switch (startupPhase) {

		case STARTUP_START_AVG:

			//find initial slaves in the chain
			slaves_found = adBms6830_daisychain_device_counter();

			g_balance_cfg_initialized = false;
			adBms6830_init_config(slaves_found, &IC[0]);

			adBmsWakeupIc(slaves_found);
			ReadCellGroups(Cell);

			adBms6830_Adcv(RD_ON, CONTINUOUS_MEASUREMENT, DISCHARGE_PERMITTED, RESET_FILTER, CELL_OPEN_WIRE_DETECTION);

			startupPhaseStart = getRuntimeMs();
			startupPhase = STARTUP_READ_AVG_START_AUX;

			break;

		case STARTUP_READ_AVG_START_AUX:
			if (getRuntimeMsDiff(startupPhaseStart) >= 10) {

				adBmsWakeupIc(slaves_found);
				ReadCellGroups(AvgCell);

				//Read AUX
				adBms6830_Adax(AUX_OPEN_WIRE_DETECTION, OPEN_WIRE_CURRENT_SOURCE, AUX_CH_TO_CONVERT);

				startupPhaseStart = getRuntimeMs();
				startupPhase = STARTUP_READ_AUX_START_RAUX;
			}
			break;

		case STARTUP_READ_AUX_START_RAUX:
			if (getRuntimeMsDiff(startupPhaseStart) >= 10) {

				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
				adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
				adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
				adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

				//Read GPIOS
				adBms6830_Adax2(AUX_CH_TO_CONVERT);

				startupPhaseStart = getRuntimeMs();
				startupPhase = STARTUP_READ_RAUX_STATUS;
			}
			break;
		case STARTUP_READ_RAUX_STATUS:
			if (getRuntimeMsDiff(startupPhaseStart) >= 10) {

				adBmsWakeupIc(slaves_found);
				adBmsReadData(slaves_found, &IC[0], RDRAXA, RAux, A);
				adBmsReadData(slaves_found, &IC[0], RDRAXB, RAux, B);
				adBmsReadData(slaves_found, &IC[0], RDRAXC, RAux, C);
				adBmsReadData(slaves_found, &IC[0], RDRAXD, RAux, D);
				/* RDSTAT depois do RDRAX: o RDRAX transborda para stata.itmp */
				adBmsReadData(slaves_found, &IC[0], RDSTATA, Status, A);
				adBmsReadData(slaves_found, &IC[0], RDSTATB, Status, B);
				adBmsReadData(slaves_found, &IC[0], RDSTATC, Status, C);
				adBmsReadData(slaves_found, &IC[0], RDSTATD, Status, D);
				adBmsReadData(slaves_found, &IC[0], RDSTATE, Status, E);

				memcpy(SLAVE, IC, sizeof(SLAVE));

				startupPhaseStart = getRuntimeMs();
				startupPhase = STARTUP_END;
			}
			break;

		case STARTUP_END:
			startupPhase = STARTUP_START_AVG;
			return ADBMS_END;
			break;

			return ADBMS_ONGOING;
		}

		break;

	}

	return ADBMS_ONGOING;
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

/* estado de open-wire da ultima avaliacao (cell + aux). O AMS_ERROR de OW
 * e clearable: liga quando ha OW, desliga quando deixa de haver (recupera
 * em runtime se o fio for reparado com o sistema a correr) */
static uint8_t cell_ow_present = 0;
static uint8_t aux_ow_present = 0;

void adBms6830_evaluate_cell_open_wire(uint8_t tIC, cell_asic *ic) {

	KILL_ERROR(FAULT_OW_DETECTED_CELL); //Garantir novos erros caso detectados

	uint8_t ow_found = 0;

	for (uint8_t slave = 0; slave < tIC; slave++) {

		/* leitura com PEC mau = codes sao lixo do parse do vendor -> julgar
		 * isto dava OW falso e latch permanente sem fio nenhum partido */
		if ((ic[slave].cccrc.scell_pec != 0) || (ic[slave].cccrc.acell_pec != 0)) {
			printfDebug("OW CHECK: IC%u skipped (PEC error)\r\n", slave + 1);
			continue;
		}

		for (uint8_t cell = 0; cell < CELL; cell++) {

			int32_t raw_code;
			int32_t avg_raw_code;
			//uint8_t cell_offset = cell + 1;

			if ((cell % 2) == 0) {
				raw_code = ic[slave].owcell.cell_ow_even[cell];
			} else {
				raw_code = ic[slave].owcell.cell_ow_odd[cell];
			}

			avg_raw_code = ic[slave].acell.ac_codes[cell];

			//int32_t voltage_mV = raw_code * 150 / 1000;
			int32_t voltage_mV = (int32_t) ((raw_code + 10000) * 0.150);

			int32_t avg_voltage_mV = (int32_t) ((avg_raw_code + 10000) * 0.150);

			int32_t voltage_delta = (avg_voltage_mV - voltage_mV);

			if (voltage_delta < 0) {

				voltage_delta = voltage_delta * (-1);
			}

			//printfUI("OW CHECK: IC%u Cell%u (%ldmV)\r\n", slave + 1, cell + 1, voltage_mV);

			if (voltage_mV < OWC_Threshold) {

				ic[slave].diag_result.cell_ow[cell] = 1;
				// seguranca primeiro, log depois: o printfDebug formata 256
				// bytes por chamada e pode ser chamado 144x por ciclo
				RAISE_ERROR(FAULT_OW_DETECTED_CELL, .slave_idx = slave + 1, .cell_idx = cell + 1, .measured_value = (float )voltage_mV);
				if (AMS_ERR_SRC_OPENWIRE) {
					AMS_Error_Trigger();
				}
				printfDebug("OW FAULT: IC%u Cell%u (%ldmV)\r\n", slave + 1, cell + 1, voltage_mV);

				// fio de sense partido -> marca OW presente (AMS_ERROR
				// clearable, limpa quando deixar de haver OW)
				ow_found = 1;

			} else if (voltage_delta > OWC_Threshold_Delta) {

				ic[slave].diag_result.cell_ow[cell] = 1;
				RAISE_ERROR(FAULT_OW_DETECTED_CELL, .slave_idx = slave + 1, .cell_idx = cell + 1, .measured_value = (float )voltage_delta);
				if (AMS_ERR_SRC_OPENWIRE) {
					AMS_Error_Trigger();
				}
				printfDebug("OW FAULT: IC%u Cell%u DELTA(%ldmV)\r\n", slave + 1, cell + 1, voltage_delta);

				ow_found = 1;

			} else {
				ic[slave].diag_result.cell_ow[cell] = 0;
			}
		}
	}

	cell_ow_present = ow_found;
}

/*void adBms6830_evaluate_cell_open_wire(uint8_t tIC, cell_asic *ic) {

 KILL_ERROR(FAULT_OW_DETECTED_CELL);

 for (uint8_t slave = 0; slave < tIC; slave++) {
 for (uint8_t cell = 0; cell < CELL; cell++) {

 // cell is 0-based, but OWC grouping uses 1-based cell numbers.
 // Even 1-based cells (2,4,6...) have index 1,3,5... (odd 0-based).
 // So: odd 0-based index → read from cell_ow_even (even channel pass)
 //     even 0-based index → read from cell_ow_odd (odd channel pass)
 int32_t raw_code;
 uint8_t cell_number = cell + 1;  // convert to 1-based

 if ((cell_number % 2) == 0) {
 // even cell (1-based): OWC switch was active during the EVEN pass
 raw_code = ic[slave].owcell.cell_ow_even[cell];
 } else {
 // odd cell (1-based): OWC switch was active during the ODD pass
 raw_code = ic[slave].owcell.cell_ow_odd[cell];
 }

 // sc_codes have no offset — 150 µV/LSB directly
 int32_t voltage_mV = raw_code * 150 / 1000;

 printfUI("OW CHECK: IC%u Cell%u (%ldmV)\r\n", slave + 1, cell + 1, voltage_mV);

 if (voltage_mV < OWC_Threshold) {

 ic[slave].diag_result.cell_ow[cell] = 1;

 printfDebug("OW FAULT: IC%u Cell%u (%ldmV)\r\n", slave + 1, cell + 1, voltage_mV);

 RAISE_ERROR(FAULT_OW_DETECTED_CELL, .slave_idx = slave + 1, .cell_idx = cell + 1, .measured_value = (float )voltage_mV);

 } else {
 ic[slave].diag_result.cell_ow[cell] = 0;
 }
 }
 }
 }*/

/* NTCs desativados por hardware (harness partido / sensor removido). Um
 * canal aqui listado e ignorado em todo o lado: sem OT, sem contaminar o
 * min/max do pack e sem latchar AMS_ERROR por open-wire. Fonte unica de
 * verdade - editar so esta tabela para ligar/desligar NTCs.
 *   slave_1b, ntc_1b sao 1-based (slave 1..12, NTC 1..6) */
uint8_t NTC_IsBypassed(uint8_t slave_1b, uint8_t ntc_1b) {

	// slave 3: NTC3 e NTC4 desativados (harness partido; era o slave 10
	// antes da nova disposicao dos slaves)
	if ((slave_1b == 3) && ((ntc_1b == 3) || (ntc_1b == 4))) {
		return 1;
	}

	return 0;
}

/* Qual o NTC a ler no lugar deste. Um NTC desativado herda o valor do
 * anterior, em cascata: NTC3 -> NTC2, NTC4 -> NTC3 -> NTC2. Assim os
 * canais mortos mostram a temperatura do vizinho em vez de lixo/zero.
 * Devolve indice 1-based (1..6) */
uint8_t NTC_ResolveSource(uint8_t slave_1b, uint8_t ntc_1b) {

	if (!NTC_IsBypassed(slave_1b, ntc_1b)) {
		return ntc_1b;
	}

	// primeiro NTC valido para tras
	for (uint8_t n = ntc_1b; n > 1; n--) {
		if (!NTC_IsBypassed(slave_1b, (uint8_t) (n - 1))) {
			return (uint8_t) (n - 1);
		}
	}

	// nao ha nenhum antes (ex: NTC1 desativado) -> procurar para a frente
	for (uint8_t n = (uint8_t) (ntc_1b + 1); n <= 6; n++) {
		if (!NTC_IsBypassed(slave_1b, n)) {
			return n;
		}
	}

	// slave inteiro desativado: nao ha substituto possivel
	return ntc_1b;
}

void adBms6830_evaluate_aux_open_wire(uint8_t tIC, cell_asic *ic) {

	KILL_ERROR(FAULT_OW_DETECTED_RTH); //Garantir novos erros caso detectados

	uint8_t ow_found = 0;

	for (uint8_t slave = 0; slave < tIC; slave++) {

		/* mesmo racional do OW de celula: PEC mau = dados invalidos, skip */
		if (ic[slave].cccrc.aux_pec != 0) {
			printfDebug("AUX OW CHECK: IC%u skipped (PEC error)\r\n", slave + 1);
			continue;
		}

		for (uint8_t gpio = 0; gpio < AUX; gpio++) {

			// NTC desativado por hardware: nao medir de todo
			if (NTC_IsBypassed((uint8_t) (slave + 1), (uint8_t) (gpio + 1))) {
				ic[slave].diag_result.aux_ow[gpio] = 0;
				continue;
			}

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
				// seguranca primeiro, log depois
				RAISE_ERROR(FAULT_OW_DETECTED_RTH, .slave_idx = slave + 1, .channel_idx = gpio + 1, .channel_mask = (uint16_t)(1U << (gpio + 1)), .measured_value = (float )pdown);
				if (AMS_ERR_SRC_OPENWIRE) {
					AMS_Error_Trigger();
				}
				printfDebug("AUX OW FAULT: IC%u GPIO%u diff (%ldmV) \r\n", slave + 1, gpio + 1, pdown);

				// NTC sem fio -> marca OW presente (canais desativados ja
				// sairam no continue acima); AMS_ERROR clearable
				ow_found = 1;
			} else {
				ic[slave].diag_result.aux_ow[gpio] = 0;
			}
		}
	}

	aux_ow_present = ow_found;
}

/* Junta o OW de cell e aux e liga/desliga o AMS_ERROR. Clearable: se todos
 * os open-wire desaparecerem (fio reparado em runtime), limpa sozinho. Clear
 * so na transicao ow->sem-ow (edge), para nao apagar a cada ciclo um erro
 * clearable posto por outra fonte. Chamado no fim da fase OW da state machine */
static void adBms6830_OpenWire_UpdateAmsError(void) {

	static uint8_t ow_active = 0;

	/* com AMS_ERR_SRC_OPENWIRE=0 fica sempre 0: nem o Trigger nem o Clear de
	 * borda correm, para nao apagarem um AMS_ERROR posto por outra fonte */
	uint8_t ow_now = (AMS_ERR_SRC_OPENWIRE && (cell_ow_present || aux_ow_present)) ? 1 : 0;

	if (ow_now != 0) {
		AMS_Error_Trigger();
	} else if (ow_active != 0) {
		AMS_Error_Clear();
	}

	ow_active = ow_now;
}

typedef enum {
	OW_START_EVEN = 0, OW_READ_EVEN_START_ODD, OW_READ_ODD_EVALUATE
} ow_step_t;

static ow_step_t owStep = OW_START_EVEN;
static uint32_t owStepStart = 0;

/* Espera minima depois de cada ADSV com OW antes de ler o resultado.
 * NAO baixar de ~20 ms (conversao incompleta = OW falso). 25 ms = o gate do
 * CHARGING; o IDLE ja' tinha >= 50 ms pelo gate do brain. (O OW falso nas
 * celulas 1-4 em BALANCING NAO era isto: era a ordem RDAC/RDCV, ver
 * BAL_CYCLE_READ_AVG) */
#define OW_CONV_WAIT_MS 25

/* Chamado quem entra na fase OW: se o estado mudou a meio de um OW (ex.
 * IDLE -> CHARGING), nao continuar uma conversao de outro contexto */
static void OpenWire_Restart(void) {
	owStep = OW_START_EVEN;
}

/* Open-wire de celula (S-ADC com corrente par/impar) e de NTC (ADAX com
 * pull-up/pull-down), avaliado no fim. Nao bloqueante: uma fase por chamada.
 * Devolve true quando acabou a avaliacao */
static bool OpenWire_Step(void) {

	switch (owStep) {

	case OW_START_EVEN:
		adBmsWakeupIc(slaves_found);
		// Start even-channel OW check
		adBms6830_Adsv(SINGLE, DCP_OFF, OW_ON_EVEN_CH);
		// Send ADAX with pull-up current and OW enabled
		adBms6830_Adax(AUX_OW_ON, PUP_UP, AUX_CH_TO_CONVERT);

		owStepStart = getRuntimeMs();
		owStep = OW_READ_EVEN_START_ODD;
		break;

	case OW_READ_EVEN_START_ODD:
		if (getRuntimeMsDiff(owStepStart) >= OW_CONV_WAIT_MS) {

			// Read S-volt results with even pull active
			adBmsWakeupIc(slaves_found);
			adBmsReadData(slaves_found, &IC[0], RDSVA, S_volt, A);
			adBmsReadData(slaves_found, &IC[0], RDSVB, S_volt, B);
			adBmsReadData(slaves_found, &IC[0], RDSVC, S_volt, C);
			adBmsReadData(slaves_found, &IC[0], RDSVD, S_volt, D);
			adBmsReadData(slaves_found, &IC[0], RDSVE, S_volt, E);
			adBmsReadData(slaves_found, &IC[0], RDSVF, S_volt, F);

			adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
			adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
			adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
			adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

			// Save pull-up readings
			for (uint8_t slave = 0; slave < slaves_found; slave++) {
				for (uint8_t g = 0; g < AUX; g++) {
					IC[slave].gpio.aux_pup_up[g] = IC[slave].aux.a_codes[g];
				}
			}

			// Start odd-channel OW check
			adBms6830_Adsv(SINGLE, DCP_OFF, OW_ON_ODD_CH);
			// Now send ADAX with pull-down current
			adBms6830_Adax(AUX_OW_ON, PUP_DOWN, AUX_CH_TO_CONVERT);

			owStepStart = getRuntimeMs();
			owStep = OW_READ_ODD_EVALUATE;
		}
		break;

	case OW_READ_ODD_EVALUATE:
		if (getRuntimeMsDiff(owStepStart) >= OW_CONV_WAIT_MS) {

			// Save even-pull readings for even-numbered cells
			for (uint8_t slave = 0; slave < slaves_found; slave++) {
				for (uint8_t cell = 0; cell < CELL; cell++) {
					IC[slave].owcell.cell_ow_even[cell] = IC[slave].scell.sc_codes[cell];
				}
			}

			adBmsWakeupIc(slaves_found);
			// Read S-volt results with odd pull active
			adBmsReadData(slaves_found, &IC[0], RDSVA, S_volt, A);
			adBmsReadData(slaves_found, &IC[0], RDSVB, S_volt, B);
			adBmsReadData(slaves_found, &IC[0], RDSVC, S_volt, C);
			adBmsReadData(slaves_found, &IC[0], RDSVD, S_volt, D);
			adBmsReadData(slaves_found, &IC[0], RDSVE, S_volt, E);
			adBmsReadData(slaves_found, &IC[0], RDSVF, S_volt, F);

			adBmsReadData(slaves_found, &IC[0], RDAUXA, Aux, A);
			adBmsReadData(slaves_found, &IC[0], RDAUXB, Aux, B);
			adBmsReadData(slaves_found, &IC[0], RDAUXC, Aux, C);
			adBmsReadData(slaves_found, &IC[0], RDAUXD, Aux, D);

			// Save odd-pull readings and pull-down readings
			for (uint8_t slave = 0; slave < slaves_found; slave++) {
				for (uint8_t cell = 0; cell < CELL; cell++) {
					IC[slave].owcell.cell_ow_odd[cell] = IC[slave].scell.sc_codes[cell];
				}
				for (uint8_t g = 0; g < AUX; g++) {
					IC[slave].gpio.aux_pup_down[g] = IC[slave].aux.a_codes[g];
				}
			}

			adBms6830_evaluate_cell_open_wire(slaves_found, IC);
			adBms6830_evaluate_aux_open_wire(slaves_found, IC);

			// liga/desliga o AMS_ERROR conforme haja OW (clearable)
			adBms6830_OpenWire_UpdateAmsError();

			owStep = OW_START_EVEN;
			return true;
		}
		break;

	default:
		owStep = OW_START_EVEN;
		break;
	}

	return false;
}

uint16_t adBms6830_FindMinVoltageGlobally(void) {
	//encontrar a celula com menor tensão
	uint16_t min_cell_mV = 65535;

	for (uint8_t slave = 0; slave < slaves_found; slave++) {
		printfDebug("c_codes[slave][0]=%d  ac_codes[slave][0]=%d\r\n", SLAVE[slave].cell.c_codes[0], SLAVE[slave].acell.ac_codes[0]);
		for (uint8_t cell = 0; cell < CELL; cell++) {

			int16_t raw_value = SLAVE[slave].acell.ac_codes[cell];

			float volts = volts = 1.5 + ((float) raw_value * 0.00015);

			uint16_t mV = (uint16_t) (volts * 1000 + 0.5);

			if (mV < min_cell_mV) {
				min_cell_mV = mV;
			}
		}
	}

	return min_cell_mV;

}

/**
 *******************************************************************************
 * @brief Count how many 6830 are in the daisy chain
 *******************************************************************************
 */
uint8_t adBms6830_daisychain_device_counter(void) {

	uint8_t device_counter = 0;

	//TODO: EEPROM: masterCfg.total_ic
	uint8_t expected_devices = 12;

	cell_asic TEMP_SLAVE[ADBMS_MAX_DEVICES] = { 0 };

	// a puta do {0} crashava o  processador fds
	//memset(TEMP_SLAVE, 0, sizeof(TEMP_SLAVE));

	adBmsWakeupIc(ADBMS_MAX_DEVICES);
	adBmsReadData(ADBMS_MAX_DEVICES, &TEMP_SLAVE[0], RDSID, Sid, NONE);

	for (uint8_t device = 0; device < ADBMS_MAX_DEVICES; device++) {

		uint8_t all_00 = 1;
		uint8_t all_ff = 1;

		for (uint8_t i = 0; i < 6; i++) {

			if (TEMP_SLAVE[device].sid.sid[i] != 0x00) {
				all_00 = 0;
			}

			if (TEMP_SLAVE[device].sid.sid[i] != 0xFF) {
				all_ff = 0;
			}
		}

		// If the response is all 0x00 or all 0xFF, assume no real device answered
		uint8_t sid_valid = 1;

		if (all_00 || all_ff) {
			sid_valid = 0;
		}

		// PEC from the SID read
		uint8_t pec_ok = 0;

		if (TEMP_SLAVE[device].cccrc.sid_pec == 0) {
			pec_ok = 1;
		}

		// Good device found
		if (sid_valid && pec_ok) {
			device_counter++;
		} else {
			// first bad/missing device means end of chain
			break;
		}

	}

	// Now compare detected devices with expected devices
	if (device_counter == expected_devices) {

		printfDebug("Chain OK: expected %d, found %d device(s).\r\n", expected_devices, device_counter);

		KILL_ERROR(FAULT_SLAVE_NOT_DETECTED);
		KILL_ERROR(FAULT_PEC_ERROR);

		// chain completa = prova de vida -> limpa o ERRO fail-safe do boot
		AMS_Error_Clear();

		MCP23017_LED(LED_ISOSPI, OFF);

	} else {

		printfDebug("Chain BAD: expected %d, found %d device(s).\r\n", expected_devices, device_counter);

		RAISE_ERROR(FAULT_SLAVE_NOT_DETECTED, .slave_idx = device_counter, .measured_value = device_counter, .threshold_value = expected_devices);

		// menos slaves que o esperado = medicoes em falta -> linha AMS_ERROR
		// vai a erro e fica latched (nada no loop volta a por OK sozinho)
		if (AMS_ERR_SRC_SLAVE_COUNT) {
			AMS_Error_Trigger();
		}

		MCP23017_LED(LED_ISOSPI, ON);
	}

	return device_counter;

}
