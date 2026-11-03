/*
 * brain.h
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 *
 * Snapshot unico do BMS: tudo o que se mede, decide e configura vive no
 * struct global bms. Cada bloco tem um so modulo que o escreve (o dono esta
 * no comentario); os outros so leem. Cada modulo avanca a sua maquina de
 * estados um passo por brain_loop e nunca espera dentro de uma funcao.
 */

#ifndef INC_BRAIN_H_
#define INC_BRAIN_H_


#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"   // tipos do HAL (TIM_HandleTypeDef) e HAL_GetTick


// ================== SNAPSHOT DO BMS ==================

// versao no CAN (0x701 master_firmware_version, VAL_ do DBC: 13 = "alpha-v3")
#define BMS_FIRMWARE_VERSION 13u

//Defines parAA SO LIMITAR NA RAM
#define BMS_MAX_SLAVES 16
#define BMS_MAX_CELLS  16
#define BMS_MAX_NTCS   10

//tABELAS DE CONFIG
#define CFG_SOC_POINTS      12   // curva tensao da celula x SOC
#define CFG_TEMPCOMP_POINTS 10   // compensacao por temperatura, -20..70 C
#define CFG_NAME_LEN        21   // nome BLE do RN4871 (max 20 caracteres) + terminador

// valores = VAL_ do master_state no DBC (0x701): a VCU e a UI leem estes numeros
typedef enum {
	AMS_BALANCING = 0,      // HV desligada, a descarregar celulas
	AMS_CHARGING = 1,       // pedido de carga do handcart (precarga + carregador)
	AMS_IDLE = 2,           // HV desligada, so a medir
	AMS_ONMISSION = 3,      // HV pedida pela VCU (precarga ou HV_ON)
	AMS_STARTUP = 4,        // ainda sem uma leitura completa e valida da cadeia
	AMS_DISCHARGE_TEST = 5, // existe no DBC, nao usado
	AMS_RESET_ISA = 6,      // existe no DBC, nao usado
	AMS_FAULT = 7           // linha AMS_ERROR ativa
} ams_state_t;

// valores = VAL_ do precharge_state no DBC (0x702), nao mudar a ordem
typedef enum {
	START = 0,
	OPEN_ALL,
	SWITCH_HVNEG,
	WAIT_FOR_AIR_NEG_TO_CLOSE,
	CHECKING_AIR_NEG_IS_CLOSED,
	SWITCH_PRECHARGE,
	WAIT_FOR_PRECHARGE_TO_CLOSE,
	CHECKING_PRECHARGE_IS_CLOSED,
	VERIFY_CURRENT,
	VERIFY_BUS_VOLT,
	SWITCH_HVPOS,
	WAIT_FOR_AIR_POS_TO_CLOSE,
	CHECKING_AIR_POS_IS_CLOSED,
	TURN_OFF_PRECHARGE,
	WAIT_FOR_PRECHARGE_TO_OPEN,
	CHECKING_PRECHARGE_IS_OPEN,
	HV_ON,
	WRONG,
	KILL,
	RX_CAN
} precharge_state_t;

typedef enum {
	CHARGER_WAIT_HV = 0,    // espera pelo HV_ON
	CHARGER_PRESTART_STOP,  // 5 s com Control=1 antes de pedir carga
	CHARGER_CHARGING,
	CHARGER_STOPPING,       // stop enviado, espera CORRENTE CAIR
	CHARGER_DONE            // latch: so reinicia com o switch OFF->ON
} charger_state_t;

// porque e que a carga acabou (bms.charger.end_reason)
typedef enum {
	CHARGE_END_NONE = 0,
	CHARGE_END_COMPLETE,      // celula maxima chegou ao cell_high_warn_mV
	CHARGE_END_SWITCH_OFF,    // switch do handcart desligado
	CHARGE_END_SDC_OPEN,
	CHARGE_END_HV_LOST,
	CHARGE_END_ISA_TIMEOUT,   // ISA do handcart calada: o corte de corrente ficava cego
	CHARGE_END_OVERTEMP,
	CHARGE_END_OVERCURRENT,
	CHARGE_END_CHARGER_TIMEOUT,
	CHARGE_END_CHARGER_FAULT,
	CHARGE_END_BUS_MISMATCH,  // ISA do handcart vs soma das celulas no arranque
	CHARGE_END_AMS_ERROR,
	CHARGE_END_HANDCART_TIMEOUT
} charge_end_t;

typedef enum {
	BALANCING_OFF = 0,
	BALANCING_ROUGH,
	BALANCING_FINE,
	BALANCING_END
} balancing_stage_t;

// maquina principal da cadeia ADBMS6822 -> ADBMS6830B (adBms_Application.c)
typedef enum {
	ADBMS_INIT = 0,        // prepara o acordar da cadeia
	ADBMS_WAKE_LOW,        // CS em baixo: pulso -1, um por slave (acorda do SLEEP)
	ADBMS_WAKE_HIGH,       // CS em cima: pulso +1
	ADBMS_WAIT_TICK,       // espera pelo tick de 200 ms do TIM6
	ADBMS_DETECT,          // conta os slaves com RDSID
	ADBMS_CONFIG,          // escreve e verifica CFGA/CFGB (DCC a 0), limpa as flags de diagnostico
	ADBMS_CELL_READ,       // maquina de leitura das celulas (adbms_cell_read.c)
	ADBMS_OPEN_WIRE,       // maquina de open-wire (adbms_open_wire.c)
	ADBMS_BALANCING,       // maquina de balanceamento (adbms_balancing.c), volta a ligar os DCC
	ADBMS_TEMP_READ,       // maquina de leitura das temperaturas (adbms_temp_read.c)
	ADBMS_FAULT            // cadeia muda: espera e volta a acordar
} adbms_state_t;

typedef enum {
	CELL_READ_START = 0,   // ADCV com redundancia (C-ADC + S-ADC)
	CELL_READ_WAIT,        // espera a conversao (8 ms)
	CELL_READ_DONE
} cell_read_state_t;

typedef enum {
	TEMP_READ_START = 0,   // ADAX + ADAX2: NTC pelos dois caminhos, VREF2, die
	TEMP_READ_WAIT,        // espera o AUX2 (ate 91 ms)
	TEMP_READ_DONE
} temp_read_state_t;

typedef enum {
	OPEN_WIRE_START = 0,   // ADSV com a corrente de OW nos canais pares
	OPEN_WIRE_WAIT_EVEN,
	OPEN_WIRE_WAIT_ODD,
	OPEN_WIRE_DONE
} open_wire_state_t;

// maquina do RN4871 (rn4871.c)
typedef enum {
	BT_BOOT = 0,           // espera o modulo arrancar
	BT_ENTER_CMD,          // "$$$" -> "CMD>"
	BT_GET_NAME,           // "GN" -> nome atual
	BT_SET_NAME,           // "SN,<nome>" -> "AOK" (so se for diferente)
	BT_EXIT_CMD,           // "---" -> "END"
	BT_DATA,               // UART transparente: JSON e comandos
	BT_PROBE_ENTER,        // vigia: "$$$" com o modo de dados em pausa
	BT_PROBE_EXIT,
	BT_REBOOT              // "R,1": espera o %REBOOT%
} bt_state_t;

// gravacao da config na EEPROM (config_store.c)
typedef enum {
	EEPROM_IDLE = 0,
	EEPROM_WRITING,        // uma pagina de 16 B por passo, 5 ms de escrita cada
	EEPROM_VERIFY,         // le o banco de volta e confere o CRC
	EEPROM_DONE,
	EEPROM_FAILED
} eeprom_state_t;

// BIG FAULT STRUCT. A ordem e o numero do fault (faults.c, CAN 0x703, JSON):
// acrescentar so no fim. Cada flag tem um so dono, o modulo que mede
typedef struct {
	// celulas
	bool cell_overvoltage;        // adbms_cell_read
	bool cell_undervoltage;       // adbms_cell_read
	bool cell_open_wire;          // adbms_open_wire: open-wire do ADBMS ou celula < cell_open_wire_mV
	// temperaturas
	bool overtemperature;         // adbms_temp_read
	bool undertemperature;        // adbms_temp_read
	bool ntc_open_wire;           // adbms_temp_read
	bool die_overtemperature;     // adbms_temp_read: ADBMS6830 acima de bal_die_stop_dC
	// cadeia ADBMS
	bool slave_missing;           // adBms_Application: slaves encontrados != esperados
	bool pec_error;               // adBms_Application: PEC errado depois das repeticoes
	bool adbms_timeout;           // adBms_Application: dados mais velhos que adbms_data_timeout_ms
	// accu overall
	bool overcurrent;             // isa
	bool pack_voltage_mismatch;   // isa: bus da ISA vs soma das celulas com HV_ON
	bool soc_low;                 // soc
	// comunicacoes
	bool can1_bus_off;            // can_bus
	bool can2_bus_off;            // can_bus
	bool can_tx_queue_full;       // can_bus
	bool isa_pack_timeout;        // isa: ISA do pack calada (CAN1)
	bool vcu_timeout;             // precharge: VCU calada com HV pedida por ela
	bool handcart_timeout;        // charger: 0x084 calado (so a carregar)
	bool charger_timeout;         // charger: status do carregador calado
	// contactores / precarga
	bool contactor_mismatch;      // precharge: comando != feedback
	bool precharge_timeout;       // precharge: o bus nao subiu a tempo
	bool precharge_failure;       // precharge: a sequencia acabou em WRONG
	bool sdc_open;                // contactors: shutdown circuit aberto
	// carregador
	bool charger_fault;           // charger: flags de falha do proprio carregador
	// sistema
	bool config_invalid;          // config_store: EEPROM vazia ou corrompida -> defaults
	bool watchdog_reset;          // brain: o ultimo arranque foi um reset do WWDG
	bool mcu_adc_error;           // board: o ADC/DMA do F412 parou
	bool eeprom_error;            // config_store: I2C da EEPROM falhou
	bool isa_pack_error;          // isa: a ISA do pack reporta erro na trama da corrente
	bool cell_voltage_warning;    // adbms_cell_read: fora dos avisos (low/high warn)
	bool temperature_warning;     // adbms_temp_read: fora dos avisos
} bms_faults_t;

typedef struct {
	ams_state_t state;                // dono: brain
	ams_state_t previous_state;
	uint32_t    uptime_ms;
	uint32_t    loop_us;              // duracao do ultimo brain_loop
	uint32_t    loop_max_us;          // pior caso; o WWDG corta a ~65 ms
	uint32_t    loops;
	bool        wwdg_reset;           // o ultimo arranque foi reset do WWDG
	uint32_t    reset_flags;          // RCC->CSR no arranque
	bool        hv_safe;              // contactores abertos, KILL e feedbacks frescos
	uint32_t    state_since_ms;
} bms_system_t;

// rampa de uma ventoinha: 0 abaixo de start, de pwm_min a pwm_max ate full
typedef struct {
	int16_t  start_dC;
	int16_t  full_dC;
	uint8_t  pwm_min_pct;
	uint8_t  pwm_max_pct;
} bms_fan_cfg_t;

/* Configuracao: tudo o que se pode afinar sem recompilar. Vive na EEPROM
 * (config_store.c), a tabela de parametros (bms_config.c) diz o nome, a
 * unidade, a gama e a ordem na EEPROM de cada campo. Dono: bms_config */
typedef struct {
	bool     loaded;                  // lida da EEPROM e validada (false = defaults)
	uint8_t  version;                 // layout da EEPROM (CONFIG_SCHEMA_VERSION)
	uint32_t sequence;                // numero da escrita na EEPROM que esta em uso
	// topologia: validar contra BMS_MAX_* ao carregar
	uint8_t  slaves;                  // ADBMS6830 na cadeia
	uint8_t  cells_per_slave;
	uint8_t  ntcs_per_slave;
	uint8_t  cells_parallel;          // celulas em paralelo (3p): corrente por celula no SOC
	uint16_t ntc_disabled_mask[BMS_MAX_SLAVES];  // bit n = NTC n+1 desligado (harness partido)
	// limites de celula, por paralelo
	uint16_t cell_min_mV;             // falha UV
	uint16_t cell_low_warn_mV;        // (2800)
	uint16_t cell_high_warn_mV;       // tambem termina a carga
	uint16_t cell_max_mV;             // falha OV (4200)
	uint16_t cell_open_wire_mV;       // abaixo disto = fio de sense partido
	// temperatura das celulas (NTC), mesma logica das tensoes
	int16_t  temp_min_dC;             // falha UT
	int16_t  temp_low_warn_dC;        // aviso UT
	int16_t  temp_high_warn_dC;       // aviso de temperatura
	int16_t  temp_max_dC;             // falha OT, falha de temperatura (FS 2027 EV 5.8.4: max 60 C)
	// cadeia ADBMS
	uint16_t adbms_data_timeout_ms;   // dados mais velhos = perda de medicao (FS: abrir em 500 ms)
	uint8_t  adbms_read_retries;      // repeticoes de um grupo com PEC errado
	uint8_t  adbms_missing_retries;   // vezes que se volta a acordar a cadeia antes de aceitar slaves != config
	uint8_t  adbms_dead_cycles;       // detecoes seguidas sem slaves antes do FAULT
	uint8_t  adbms_cth;               // limite C-ADC vs S-ADC, enum CTH (5 = 8,1 mV)
	uint8_t  ow_min_gain_pct;         // S-ADC com corrente de OW abaixo disto = fio partido
	uint8_t  ow_conv_ms;              // espera de cada ADSV (menos de 20 ms dava OW falso)
	uint16_t ntc_beta;
	uint16_t ntc_r25_ohm;
	uint16_t ntc_r_fixed_ohm;         // resistencia para o VREF2
	uint16_t ntc_aux_match_mV;        // AUX vs AUX2 do mesmo GPIO
	// corrente e tensao do pack (ISA do CAN1)
	uint16_t overcurrent_discharge_A;
	uint16_t overcurrent_charge_A;
	uint16_t overcurrent_ms;          // FS 2027 EV 5.8.10: 500 ms para corrente
	uint8_t  pack_voltage_tolerance_pct;
	uint16_t pack_voltage_mismatch_ms;
	uint16_t isa_pack_timeout_ms;     // ISA do pack calada -> isa_pack_timeout (600)
	uint16_t isa_handcart_timeout_ms; // ISA do handcart calada a carregar -> corte (1000)
	uint8_t  isa_bus_channel;         // 1..3: canal U da ISA do pack que mede o bus (depois dos AIR)
	bool     isa_pack_invert;         // troca o sinal: no bms, corrente > 0 = a entrar no pack
	bool     isa_handcart_invert;
	// carregamento
	uint16_t charge_voltage_dV;       // setpoint enviado ao carregador
	uint16_t charge_current_dA;       // (60)
	uint16_t charge_open_mA;          // abre os contactores abaixo disto, max charge_settle_ms
	uint16_t charge_cut_current_mA;   // corrente da ISA do handcart acima disto = corte
	int16_t  charge_temp_max_dC;      // temperatura maxima a carregar
	uint16_t charge_settle_ms;        // espera maxima pela corrente depois do stop
	uint16_t charge_prestart_ms;      // Control=1 antes de pedir carga
	uint16_t charge_command_ms;       // periodo do BMS_ChargingRequest_P1000
	uint16_t charge_status_timeout_ms;
	uint16_t charge_fault_debounce_ms;// flags do carregador tem de durar isto (uma trama nao corta)
	uint8_t  charge_bus_tolerance_pct;// ISA do handcart vs soma das celulas antes de arrancar
	uint16_t handcart_timeout_ms;     // 0x084 calado a carregar -> corte
	// balanceamento
	bool     bal_enabled;             // arranque automatico com a HV desligada
	uint16_t bal_min_cell_mV;         // abaixo disto a celula e ignorad
	uint16_t bal_max_cell_mV;         // acima disto a celula e ignorada
	uint16_t bal_delta_mV;            // delta maximo admitido
	uint16_t bal_rough_mV;            // ROUGH: so as celulas acima de alvo + isto
	int16_t  bal_die_stop_dC;         // temperatura dos chips, nao das celulas
	uint16_t bal_die_hold_s;          // depois de um OT do die, sem balancear
	// ventoinhas
	bms_fan_cfg_t fan_accu_cells;          	// celulas (300, 550, 20, 100)
	bms_fan_cfg_t fan_accu_lv;          	// eletronica LV dentro da bateria
	uint16_t fan_pwm_hz;              // frequencia do TIM12
	int16_t  fan_hysteresis_dC;
	// pre-carga
	uint8_t  precharge_mode;          // 0 = so tempo (precharge_min_ms), 1 = tensao do bus pela ISA
	uint16_t precharge_timeout_ms;          // tempo maximo da sequencia (10000)
	uint8_t  precharge_bus_percentage;             // tensao do bus para concluir, % do accu
	uint16_t precharge_air_wait_ms;         // espera antes de ler o feedback de cada AIR
	uint16_t precharge_min_ms;        // tempo minimo com o rele de precarga fechado
	uint16_t precharge_end_current_mA;// |corrente| maxima para fechar o AIR+, 0 = nao verifica
	uint16_t precharge_lockout_ms;    // depois de um arranque, novos pedidos sao ignorados
	uint16_t contactor_debounce_ms;   // feedback estavel este tempo
	uint8_t  contactor_mismatch_count;// checks seguidos errados em HV_ON antes de abrir
	uint8_t  contactor_check_mask;    // feedbacks verificados: bit0 AIR-, bit1 PRE, bit2 AIR+, bit3 descarga
	uint16_t vcu_timeout_ms;          // VCU calada com HV pedida por ela, 0 = nao vigia
	// SOC
	uint16_t capacity_mAh;            		// capacidade nominal
	uint8_t  soc_fit_degree;        		// grau do ajuste mostrado na UI
	uint8_t  soc_curve_pct[CFG_SOC_POINTS]; // 0, 5, 10, 20 ... 100
	uint16_t soc_curve_mV[CFG_SOC_POINTS];  // tensao em repouso nesse SOC
	uint16_t soc_rest_current_mA;     // abaixo disto o pack esta em repouso
	uint16_t soc_rest_s;              // repouso este tempo -> volta a ancorar na curva
	uint8_t  soc_low_pct;             // abaixo disto -> soc_low
	// compensacao por temperatura
	int8_t   temp_compensation_temp_C[CFG_TEMPCOMP_POINTS];   			// -20, -10 ... 70
	int16_t  temp_compensation_adjust_mV[CFG_TEMPCOMP_POINTS];  		// correcao da tensao lida, 0,1 mV
	uint16_t temp_compensation_resistance_mOhm[CFG_TEMPCOMP_POINTS];    // resistencia interna, 0,1 mOhm
	// placa master
	uint16_t board_current_mV_per_A;  // sensibilidade do MCS1802 (264)
	int16_t  board_current_offset_mA; // calibracao do zero
	// CAN
	uint16_t can1_kbps;               // powertrain
	uint16_t can2_kbps;               // carregamento
	uint16_t can_master_ms;           // 0x701..0x709, 0 = desligado
	uint16_t can_slaves_ms;           // 0x600..0x653, 0 = desligado
	bool     can2_mirror_charging;    // telemetria tambem no CAN2 enquanto carrega
	uint8_t  bootloader_can_mask;     // bit0 CAN1, bit1 CAN2: onde se aceita o 0x0DC
	// comunicacao externa: RN4871 no UART2, consola por cabo no UART1
	char     bt_name[CFG_NAME_LEN];
	uint16_t live_period_ms;          // JSON no BLE, 0 = desligado
	uint16_t live_uart1_ms;           // JSON no UART1, 0 = desligado
	uint16_t bt_probe_s;              // vigia do RN4871 ($$$ / ---), 0 = desligado
	uint16_t bt_reboot_s;             // reboot periodico as cegas, 0 = desligado
	uint16_t bt_pin;                  // PIN para alterar a config pelo BLE ("unlock"), 0 = sem PIN
	// quais faults disparam a linha AMS_ERROR (os da FS 2027 EV 5.8 estao sempre ligados)
	bms_faults_t ams_error_enable;
	uint16_t ams_error_min_on_ms;     // a linha fica em erro pelo menos isto
} bms_config_t;

typedef struct {
	bool         active;              // linha AMS_ERROR em erro
	bms_faults_t sources;             // faults que estao a segurar a linha
	uint64_t     sources_mask;        // os mesmos, bit = numero do fault
	uint64_t     faults_mask;         // todos os faults ativos (com e sem AMS_ERROR)
	uint8_t      fault_count;
	bool         boot_hold;           // em erro desde o arranque ate a cadeia estar lida
	uint32_t     since_ms;
	uint32_t     transitions;
} bms_ams_error_t;

typedef struct {
	uint16_t cell_mV[BMS_MAX_SLAVES][BMS_MAX_CELLS];
	int16_t  ntc_dC[BMS_MAX_SLAVES][BMS_MAX_NTCS];
	int16_t  die_dC[BMS_MAX_SLAVES];     					 // temperatura interna de cada ADBMS6830
	bool     cell_open_wire[BMS_MAX_SLAVES][BMS_MAX_CELLS];
	bool     ntc_open_wire[BMS_MAX_SLAVES][BMS_MAX_NTCS];
	// resumo do pack (posicoes 1-based, 0 = nenhuma)
	uint8_t  slaves_found;
	uint16_t cell_max_mV;
	uint16_t cell_min_mV;
	uint16_t cell_avg_mV;
	uint16_t cell_delta_mV;
	uint8_t  cell_max_slave;
	uint8_t  cell_max_cell;
	uint8_t  cell_min_slave;
	uint8_t  cell_min_cell;
	int16_t  temp_max_dC;
	int16_t  temp_min_dC;
	uint8_t  temp_max_slave;
	uint8_t  temp_max_ntc;
	uint8_t  temp_min_slave;
	uint8_t  temp_min_ntc;
	int16_t  die_max_dC;
	bool     cells_valid;             // todos os slaves com celulas frescas
	bool     temps_valid;             // todos os slaves com NTC frescos
	uint32_t updated_ms;              // HAL_GetTick() DO LAST VALUE OBTIDFO
} bms_cells_t;

// bits de bms.pec.flags (iguais aos pec_flags do JSON antigo)
#define PEC_FLAG_CELL   0x01u
#define PEC_FLAG_SCELL  0x02u
#define PEC_FLAG_AUX    0x04u
#define PEC_FLAG_RAUX   0x08u
#define PEC_FLAG_STATUS 0x10u
#define PEC_FLAG_CONFIG 0x20u

typedef struct {
	uint32_t total;                   // erros de PEC desde o arranque
	uint16_t per_slave[BMS_MAX_SLAVES];
	bool     last_read[BMS_MAX_SLAVES];   // true = a ultima leitura deste slave falhou o PEC
	uint8_t  flags[BMS_MAX_SLAVES];       // PEC_FLAG_* das leituras deste ciclo
} bms_pec_t;

// uma ISA IVT-S; ha duas: pack no CAN1, handcart no CAN2 (mesmos IDs). Dono: isa
typedef struct {
	int32_t  current_mA;              // ja com o sinal da config: + = a entrar no pack
	int32_t  voltage_mV[3];           // U1, U2, U3
	int32_t  power_W;
	int32_t  charge_As;
	int32_t  energy_Wh;
	int16_t  temp_dC;
	uint8_t  error_flags;             // bits 4..7 do byte 1 da trama da corrente
	bool     alive;                   // corrente recebida dentro do timeout
	uint32_t current_rx_ms;           // ultima trama de corrente (0 = nunca)
	uint32_t any_rx_ms;               // ultima trama de qualquer canal
	uint32_t age_ms;                  // desde a ultima trama de corrente
	uint32_t frames;
} bms_isa_t;

typedef struct {
	int32_t  current_mA;              // ISA do accu, can1 (+ = a carregar)
	int32_t  voltage_mV;              // ISA do accu, canal do bus (isa_bus_channel)
	uint32_t voltage_sum_mV;          // soma de todas as celulas
	int32_t  power_W;
	int32_t  charge_As;               // contador As da ISA
	int32_t  energy_Wh;
	int16_t  isa_temp_dC;
	bool     isa_alive;
	uint32_t isa_age_ms;              // ms desde a ultima trama de corrente
	uint32_t overcurrent_since_ms;    // 0 = dentro do limite
	uint32_t mismatch_since_ms;
} bms_accu_t;

typedef struct {
	bool     ready;                   // ja inicializado pela tensao das celulas
	uint16_t percent_x100;            // 0..10000
	uint16_t initial_percent_x100;    // tabela OCV da celula minima no arranque
	int32_t  used_As;                 // carga gasta desde o arranque (coulomb counting)
	int64_t  used_mAms;               // acumulador fino (mA x ms)
	uint16_t ocv_mV;                  // tensao em vazio estimada da celula minima
	uint32_t rest_since_ms;           // 0 = com corrente
	uint32_t last_sample_ms;
	uint32_t anchors;                 // vezes que ancorou na curva
} bms_soc_t;

typedef struct {
	precharge_state_t state;
	uint32_t state_ms;                // tempo no estado atual
	uint32_t entered_ms;              // HAL_GetTick() da entrada no estado
	uint32_t sequence_start_ms;
	uint32_t lockout_until_ms;
	uint8_t  source;                  // 0 nenhum, 1 VCU, 2 handcart
	//request contactor state (o contactors.c decide a saida fisica)
	bool     trigger_air_pos_cmd;
	bool     trigger_air_neg_cmd;
	bool     trigger_precharge_cmd;
	bool     trigger_discharge_cmd;   // sem saida na placa: a descarga segue o SDC
	// contactor feedback, actual contactor state (com debounce, contactors.c)
	bool     air_pos_fb;
	bool     air_neg_fb;
	bool     precharge_fb;
	bool     discharge_fb;
	bool     sdc_closed;
	bool     inputs_valid;            // ja houve debounce completo de todas as entradas
	uint16_t fb_edges[5];             // flancos vistos pelo EXTI: AIR+, AIR-, PRE, descarga, SDC
	int8_t   vcu_request;             // -1 = nunca recebido
	uint32_t vcu_request_rx_ms;
	uint32_t vcu_request_age_ms;
	uint32_t vcu_alive_rx_ms;         // ultimo 0x083 ou 0x750
	bool     handcart_start;          // pedido do charger (flanco do switch)
	bool     kill_request;            // pedido do charger para abrir tudo
	uint8_t  mismatch_count;          //mismatch counter of contactors feedbacks
	uint32_t mismatch_check_ms;
	uint8_t  expected_mask;           // ultimo check: bit0 AIR-, bit1 PRE, bit2 AIR+, bit3 descarga
	uint8_t  actual_mask;
	int32_t  bus_voltage_mV;          // tensao do bus vista no VERIFY_BUS_VOLT
	uint32_t hv_on_count;             // sequencias completas desde o arranque
} bms_precharge_t;

typedef struct {
	balancing_stage_t stage;
	uint16_t target_mV;               // minimo congelado no inicio do balanceamento
	uint16_t dcc_mask[BMS_MAX_SLAVES];    // bit i = celula i+1 a descarregar (= CAN 0x706)
	uint8_t  cells_discharging;
	uint16_t worst_delta_mV;          // maior (celula - alvo), 0 = convergiu
	uint8_t  worst_slave;             // 1-based, 0 = nenhum
	uint8_t  worst_cell;
	bool     die_ot_hold;             // bloqueado bal_die_hold_s apos OT do die
	uint32_t hold_since_ms;
	uint32_t started_ms;
} bms_balancing_t;

// estado das maquinas ADBMS e diagnostico da cadeia
typedef struct {
	adbms_state_t     state;
	cell_read_state_t cell_read;
	temp_read_state_t temp_read;
	open_wire_state_t open_wire;
	uint32_t step_us;                         // TIM5 (us) no inicio da espera atual
	uint8_t  wake_pulses;                     // pulsos de CS que faltam
	uint8_t  missing_count;                   // ciclos seguidos com slaves != config
	uint8_t  dead_count;                      // ciclos seguidos sem nenhum slave
	uint32_t tick_seen;                       // ultimo tick de 200 ms tratado
	uint32_t cycles;                          // ciclos completos
	uint32_t overruns;                        // ticks perdidos (ciclo > 200 ms)
	uint32_t cycle_start_us;
	uint32_t cycle_us;                        // duracao do ultimo ciclo
	uint32_t config_errors;                   // CFGA/CFGB lidos != escritos
	uint32_t spi_errors;                      // timeouts/erros HAL no SPI
	bool     config_ok[BMS_MAX_SLAVES];       // config verificada neste ciclo
	uint32_t cells_valid_ms[BMS_MAX_SLAVES];  // ultima leitura de celulas valida
	uint32_t temps_valid_ms[BMS_MAX_SLAVES];  // ultima leitura de NTC valida
} bms_adbms_t;

typedef struct {
	charger_state_t state;
	charge_end_t end_reason;
	bool     handcart_connected;      //can id exclusive to handcart information
	bool     charger_connected;      //can id exclusive to charger protocol
	bool     switch_on;               // pedido de carga do switch do handcart
	bool     switch_seen;             // ja chegou algum 0x084
	bool     charger_alive;           // status do carregador nos ultimos charge_status_timeout_ms
	bool     status_seen;
	uint16_t out_voltage_dV;          // medido pelo carregador
	uint16_t out_current_dA;
	uint16_t req_voltage_dV;          // setpoints enviados
	uint16_t req_current_dA;
	bool     req_enable;              // Control = 0 (carregar) no ultimo pedido
	int16_t  temp_C;
	// flags do carregador (Charger_Status_P1000)
	bool     hw_failure;
	bool     over_temp;
	bool     input_voltage_fault;
	bool     starting_state_fault;    // bateria desligada ou invertida
	bool     comm_timeout;
	uint32_t updated_ms;
	uint32_t switch_rx_ms;
	uint32_t state_ms;                // entrada no estado atual
	uint32_t fault_since_ms;          // 0 = sem flags de falha
	uint32_t sessions;
} bms_charger_t;

// uma ventoinha; ha duas em bms_t (celulas e eletronica LV)
typedef struct {
	bool     bypass_on;               //fan fully on if bypass required
	bool     running;                 // histerese: ligada
	uint8_t  pwm;                     // 0..255
	int16_t  control_temp_dC;         // temperatura que comanda a rampa
} bms_fan_t;

typedef struct {
	uint16_t current_mA;              //consumo do AMS master MCS1802gs-05-z
	int16_t  mcu_temp_dC;
	uint16_t vdda_mV;
	uint32_t adc_conversions;         // contador do callback do DMA
	uint32_t adc_seen;                // valor do contador na ultima verificacao
	uint32_t adc_checked_ms;
	uint32_t updated_ms;
} bms_board_t;

typedef struct {
	bool     started;
	bool     bus_off;
	bool     error_passive;
	uint8_t  tx_errors;
	uint8_t  rx_errors;
	uint8_t  last_error_code;
	uint16_t kbps;
	uint16_t tx_queue_depth;
	uint16_t tx_queue_peak;
	uint32_t tx_frames;
	uint32_t tx_dropped;
	uint32_t rx_frames;
	uint32_t rx_dropped;              // ring de RX cheio
	uint32_t hal_errors;
	uint32_t full_since_ms;           // fila de TX cheia desde (0 = nao)
} bms_can_t;

// um UART com ring de TX por DMA e RX por interrupcao (uart_link.c)
typedef struct {
	uint32_t tx_bytes;
	uint32_t rx_bytes;
	uint32_t frames_sent;             // tramas entregues ao ring buffer
	uint32_t frames_dropped;          // nao couberam no ring (tudo-ou-nada)
	uint32_t dma_errors;              // HAL_UART_ErrorCallback
	uint32_t dma_restarts;            // TX encravado > 2 s -> abort + recomeco
	uint32_t rx_overruns;             // ring de RX cheio
	uint16_t ring_peak_bytes;         // maior ocupacao do ring buffer
	uint16_t last_json_bytes;
	uint32_t json_sent;               // linhas JSON entregues (a BMS_UI usa como numero de sequencia)
	uint32_t json_dropped;            // linhas JSON que nao couberam
} bms_link_t;

typedef struct {
	bt_state_t state;
	bool     rn4871_alive;            // respondeu ao ultimo comando ou mandou status
	bool     connected;               // entre %CONNECT% e %DISCONNECT%
	uint32_t rn4871_reboots;
	uint32_t probes;
	uint32_t probe_failures;
	uint32_t state_ms;
	uint32_t last_ok_ms;              // ultima resposta valida do modulo
	uint32_t last_probe_ms;
	uint32_t last_reboot_ms;
	char     name[CFG_NAME_LEN];      // nome lido do modulo
} bms_bt_t;

typedef struct {
	eeprom_state_t state;
	bool     present;                 // respondeu ao I2C no arranque
	uint8_t  bank;                    // banco em uso: 0 = A, 1 = B, 0xFF = nenhum
	uint32_t sequence;                // numero da ultima escrita valida
	uint16_t progress;                // bytes escritos na gravacao atual
	uint32_t writes;                  // gravacoes completas
	uint32_t failures;
	uint32_t i2c_errors;
	uint32_t step_ms;
	bool     dirty;                   // ha alteracoes por gravar
	bool     reboot_pending;          // ha parametros que so mudam depois de reiniciar
} bms_eeprom_t;

typedef struct {
	bms_system_t    system;
	bms_config_t    config;
	bms_faults_t    faults;           // todos os faults ativos
	bms_ams_error_t ams_error;
	bms_cells_t     cells;
	bms_pec_t       pec;
	bms_adbms_t     adbms;            // maquinas de estado da cadeia ADBMS
	bms_accu_t      accu;
	bms_isa_t       isa_pack;         // ISA do pack, CAN1
	bms_isa_t       isa_handcart;     // ISA do handcart, CAN2
	bms_soc_t       soc;
	bms_precharge_t precharge;
	bms_balancing_t balancing;
	bms_charger_t   charger;
	bms_fan_t       fan_cells;        // arrefecimento das celulas, TIM12 CH1 (polaridade invertida)
	bms_fan_t       fan_lv;           // eletronica LV dentro da bateria, saida por definir;
	bms_board_t     board;
	bms_can_t       can1;             // powertrain
	bms_can_t       can2;             // carregamento ou data
	bms_link_t      uart1;            // consola por cabo
	bms_link_t      uart2;            // RN4871
	bms_bt_t        bt;
	bms_eeprom_t    eeprom;
} bms_t;

extern bms_t bms;


// Watchdog reset flag raw RCC->CSR captured before peripherals initilise
extern uint32_t watchdog_flag;

void brain_start(void);

void brain_loop(void);

void heartbeat(void);

const char* brain_state_name(ams_state_t state);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
