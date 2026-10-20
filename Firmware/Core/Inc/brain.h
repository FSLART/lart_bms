/*
 * brain.h
 *
 *  Created on: Oct 20, 2025
 *      Author: jpser
 */

#ifndef INC_BRAIN_H_
#define INC_BRAIN_H_


#include <stdbool.h>
#include <stdint.h>


// ================== SNAPSHOT DO BMS ==================

//Defines parAA SO LIMITAR NA RAM
#define BMS_MAX_SLAVES 16
#define BMS_MAX_CELLS  16
#define BMS_MAX_NTCS   10

//tABELAS DE CONFIG
#define CFG_SOC_POINTS   12   // curva tensao da celula x SOC
#define CFG_TEMPCOMP_POINTS 10   // compensacao por temperatura, -20..70 C

typedef enum {
	AMS_STARTUP = 0,
	AMS_IDLE
} ams_state_t;

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

// BIG FAULT STRUCT
typedef struct {
	// celulas
	bool cell_overvoltage;
	bool cell_undervoltage;
	bool cell_open_wire;          // open-wire do ADBMS ou celula < 2,3 V
	// temperaturas
	bool overtemperature;
	bool undertemperature;
	bool ntc_open_wire;
	bool die_overtemperature;     // ADBMS6830 acima de 85 C
	// cadeia ADBMS
	bool slave_missing;           // slaves encontrados != esperados
	bool pec_error;               // demasiados erros de PEC
	bool adbms_timeout;
	// accu overall
	bool overcurrent;
	bool pack_voltage_mismatch;   // ISA vs soma das celulas
	bool soc_low;
	// comunicacoes
	bool can1_bus_off;
	bool can2_bus_off;
	bool can_tx_queue_full;
	bool isa_pack_timeout;        // ISA do pack calada > 600 ms (CAN1)
	bool vcu_timeout;
	bool handcart_timeout;        // 0x084 calado > 1 s (so a carregar)
	bool charger_timeout;         // status do carregador calado > 3 s
	// contactores / precarga
	bool contactor_mismatch;      // comando != feedback
	bool precharge_timeout;
	bool precharge_failure;
	bool sdc_open;
	// carregador
	bool charger_fault;           // flags de falha do proprio carregador
	// sistema
	bool config_invalid;          // EEPROM vazia ou corrompida -> defaults
	bool watchdog_reset;
	bool mcu_adc_error;         //////adcs fo f412 na current sense
} bms_faults_t;

typedef struct {
	ams_state_t state;
	ams_state_t previous_state;
	uint32_t    uptime_ms;
	uint32_t    loop_us;              // duracao do ultimo brain_loop
	uint32_t    loop_max_us;          // pior caso; o WWDG corta a ~65 ms
	bool        wwdg_reset;           // o ultimo arranque foi reset do WWDG
} bms_system_t;

// rampa de uma ventoinha: 0 abaixo de start, de pwm_min a pwm_max ate full
typedef struct {
	int16_t  start_dC;
	int16_t  full_dC;
	uint8_t  pwm_min_pct;
	uint8_t  pwm_max_pct;
} bms_fan_cfg_t;

// eeprom configs saved hopefully
typedef struct {
	bool     loaded;                  // lida da EEPROM e validada (false = defaults)
	uint8_t  version;                 // sobe quando o layout muda
	// topologia: validar contra BMS_MAX_* ao carregar
	uint8_t  slaves;                  // ADBMS6830 na cadeia
	uint8_t  cells_per_slave;
	uint8_t  ntcs_per_slave;
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
	int16_t  temp_max_dC;             // falha OT, falha de temperatura
	// carregamento
	uint16_t charge_voltage_dV;       // setpoint enviado ao carregador
	uint16_t charge_current_dA;       // (60)
	uint16_t charge_open_mA;          // abre os contactores abaixo disto, max 5 s
	// balanceamento
	uint16_t bal_min_cell_mV;         // abaixo disto a celula e ignorad
	uint16_t bal_max_cell_mV;         // acima disto a celula e ignorada
	uint16_t bal_delta_mV;            // delta maximo admitido
	int16_t  bal_die_stop_dC;         // temperatura dos chips, nao das celulas
	uint16_t bal_pulse_ms;            // tem de ser < 1800, tSLEEP do ADBMS6830
	// ventoinhas
	bms_fan_cfg_t fan_accu_cells;          	// celulas (300, 550, 20, 100)
	bms_fan_cfg_t fan_accu_lv;          	// eletronica LV dentro da bateria
	// pre-carga
	uint16_t precharge_timeout_ms;          // tempo maximo da sequencia (10000)
	uint8_t  precharge_bus_percentage;             // tensao do bus para concluir, % do accu
	uint16_t precharge_air_wait_ms;         // espera antes de ler o feedback de cada AIR
	// SOC
	uint16_t capacity_mAh;            		// capacidade nominal
	uint8_t  soc_fit_degree;        		// grau do ajuste mostrado na UI
	uint8_t  soc_curve_pct[CFG_SOC_POINTS]; // 0, 5, 10, 20 ... 100
	uint16_t soc_curve_mV[CFG_SOC_POINTS];  // tensao em repouso nesse SOC
	// compensacao por temperatura
	int8_t   temp_compensation_temp_C[CFG_TEMPCOMP_POINTS];   			// -20, -10 ... 70
	int16_t  temp_compensation_adjust_mV[CFG_TEMPCOMP_POINTS];  		// correcao da tensao lida, 0,1 mV
	uint16_t temp_compensation_resistance_mOhm[CFG_TEMPCOMP_POINTS];    // resistencia interna, 0,1 mOhm
	// quais faults disparam a linha AMS_ERROR
	bms_faults_t ams_error_enable;
} bms_config_t;

typedef struct {
	bool         active;              // linha AMS_ERROR em erro
	bms_faults_t sources;             // faults que estao a segurar a linha
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
	uint32_t updated_ms;              // HAL_GetTick() DO LAST VALUE OBTIDFO
} bms_cells_t;

typedef struct {
	uint32_t total;                   // erros de PEC desde o arranque
	uint16_t per_slave[BMS_MAX_SLAVES];
	bool     last_read[BMS_MAX_SLAVES];   // true = a ultima leitura deste slave falhou o PEC
} bms_pec_t;

typedef struct {
	int32_t  current_mA;              // ISA do accu, can1
	int32_t  voltage_mV;              // ISA do accu, U1
	uint32_t voltage_sum_mV;          // soma de todas as celulas
	int32_t  power_W;
	int32_t  charge_As;               // contador As da ISA
	int32_t  energy_Wh;
	int16_t  isa_temp_dC;
	bool     isa_alive;
	uint32_t isa_age_ms;              // ms desde a ultima trama de corrente
} bms_accu_t;

typedef struct {
	bool     ready;                   // ja inicializado pela tensao das celulas
	uint16_t percent_x100;            // 0..10000
	uint16_t initial_percent_x100;    // tabela OCV da celula minima no arranque
	int32_t  used_As;                 // carga gasta desde o arranque (coulomb counting)
} bms_soc_t;

typedef struct {
	precharge_state_t state;
	uint32_t state_ms;                // tempo no estado atual
	//request contactor state
	bool     trigger_air_pos_cmd;
	bool     trigger_air_neg_cmd;
	bool     trigger_precharge_cmd;
	bool     trigger_discharge_cmd;
	// contactor feedback, actual contactor state
	bool     air_pos_fb;
	bool     air_neg_fb;
	bool     precharge_fb;
	bool     discharge_fb;
	bool     sdc_closed;
	int8_t   vcu_request;             // -1 = nunca recebido
	uint32_t vcu_request_age_ms;
	uint8_t  mismatch_count;          //mismatch counter of contactors feedbacks
} bms_precharge_t;

typedef struct {
	balancing_stage_t stage;
	uint16_t target_mV;               // minimo congelado no inicio do balanceamento
	uint16_t dcc_mask[BMS_MAX_SLAVES];    // bit i = celula i+1 a descarregar (= CAN 0x706)
	uint8_t  cells_discharging;
	uint16_t worst_delta_mV;          // maior (celula - alvo), 0 = convergiu
	uint8_t  worst_slave;             // 1-based, 0 = nenhum
	uint8_t  worst_cell;
	bool     die_ot_hold;             // bloqueado 60 s apos OT do die
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
	bool     handcart_connected;      //can id exclusive to handcart information
	bool     charger_connected;      //can id exclusive to charger protocol
	bool     switch_on;               // pedido de carga do switch do handcart
	bool     charger_alive;           // status do carregador nos ultimos 3 s
	uint16_t out_voltage_dV;          // medido pelo carregador
	uint16_t out_current_dA;
	uint16_t req_voltage_dV;          // setpoints enviados
	uint16_t req_current_dA;
	int16_t  temp_C;
	// flags do carregador (Charger_Status_P1000)
	bool     hw_failure;
	bool     over_temp;
	bool     input_voltage_fault;
	bool     starting_state_fault;    // bateria desligada ou invertida
	bool     comm_timeout;
	uint32_t updated_ms;
} bms_charger_t;

// uma ventoinha; ha duas em bms_t (celulas e eletronica LV)
typedef struct {
	bool     bypass_on;               //fan fully on if bypass required
	uint8_t  pwm;                     // 0..255
	int16_t  control_temp_dC;         // temperatura que comanda a rampa
} bms_fan_t;

typedef struct {
	uint16_t current_mA;              //consumo do AMS master MCS1802gs-05-z
	int16_t  mcu_temp_dC;
	uint16_t vdda_mV;
} bms_board_t;

typedef struct {
	bool     started;
	bool     bus_off;
	bool     error_passive;
	uint8_t  tx_errors;
	uint8_t  rx_errors;
	uint8_t  last_error_code;
	uint16_t tx_queue_depth;
	uint32_t tx_dropped;
	uint32_t rx_frames;
} bms_can_t;

typedef struct {
	bool     rn4871_alive;            // precisa do RX do UART2 (antes nunca foi armado)
	uint32_t rn4871_reboots;
	uint32_t frames_sent;             // tramas entregues ao ring buffer
	uint32_t frames_dropped;          // nao couberam no ring (tudo-ou-nada)
	uint32_t dma_errors;              // HAL_UART_ErrorCallback
	uint32_t dma_restarts;            // TX encravado > 2 s -> abort + recomeco
	uint16_t ring_peak_bytes;         // maior ocupacao do ring buffer
} bms_uart_t;

typedef struct {
	bms_system_t    system;
	bms_config_t    config;
	bms_faults_t    faults;           // todos os faults ativos
	bms_ams_error_t ams_error;
	bms_cells_t     cells;
	bms_pec_t       pec;
	bms_adbms_t     adbms;            // maquinas de estado da cadeia ADBMS
	bms_accu_t      accu;
	bms_soc_t       soc;
	bms_precharge_t precharge;
	bms_balancing_t balancing;
	bms_charger_t   charger;
	bms_fan_t       fan_cells;        // arrefecimento das celulas, TIM12 CH1 (polaridade invertida)
	bms_fan_t       fan_lv;           // eletronica LV dentro da bateria, saida por definir;
	bms_board_t     board;
	bms_can_t       can1;             // powertrain
	bms_can_t       can2;             // carregamento ou data
	bms_uart_t      uart;
} bms_t;

extern bms_t bms;


// Watchdog reset flag raw RCC->CSR captured before peripherals initilise
extern uint32_t watchdog_flag;

void brain_start(void);

void brain_loop(void);

void heartbeat(void);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
