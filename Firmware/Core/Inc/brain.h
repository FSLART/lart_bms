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


/* ================== SNAPSHOT DO BMS ================== */

//Defines parAA SO LIMITAR NA RAM
#define BMS_MAX_SLAVES 16
#define BMS_MAX_CELLS  16
#define BMS_MAX_NTCS   10

//tABELAS DE CONFIG
#define CFG_SOC_POINTS   12   /* curva tensao da celula x SOC */
#define CFG_TCOMP_POINTS 10   /* compensacao por temperatura, -20..70 C */

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
	CHARGER_WAIT_HV_PRESTART_STOP,  // 5 s com Control=1 antes de pedir carga
	CHARGER_WAIT_HV,
	CHARGER_WAIT_HV_STOPPING,       // stop enviado, espera CORRENTE CAIR
	CHARGER_WAIT_HVG_DONE            // latch: so reinicia com o switch OFF->ON
} charger_state_t;

typedef enum {
	BALANCING_OFF = 0,
	BALANCING,
	BALANCING_FINE,
	BALANCING_END
} balancing_stage_t;

// BIG FAULT STRUCT
typedef struct {
	/* celulas */
	bool cell_overvoltage;
	bool cell_undervoltage;
	bool cell_open_wire;          /* open-wire do ADBMS ou celula < 2,3 V */
	/* temperaturas */
	bool overtemperature;
	bool undertemperature;
	bool ntc_open_wire;
	bool die_overtemperature;     /* ADBMS6830 acima de 85 C */
	/* cadeia ADBMS */
	bool slave_missing;           /* slaves encontrados != esperados */
	bool pec_error;               /* demasiados erros de PEC */
	bool adbms_timeout;
	/* accu overall */
	bool overcurrent;
	bool pack_voltage_mismatch;   /* ISA vs soma das celulas */
	bool soc_low;
	/* comunicacoes */
	bool can1_bus_off;
	bool can2_bus_off;
	bool can_tx_queue_full;
	bool isa_pack_timeout;        /* ISA do pack calada > 600 ms (CAN1) */
	bool vcu_timeout;
	bool handcart_timeout;        /* 0x084 calado > 1 s (so a carregar) */
	bool charger_timeout;         /* status do carregador calado > 3 s */
	/* contactores / precarga */
	bool contactor_mismatch;      /* comando != feedback */
	bool precharge_timeout;
	bool precharge_failure;
	bool sdc_open;
	/* carregador */
	bool charger_fault;           /* flags de falha do proprio carregador */
	/* sistema */
	bool config_invalid;          /* EEPROM vazia ou corrompida -> defaults */
	bool watchdog_reset;
	bool mcu_adc_error;         //////adcs fo f412 na current sense
} bms_faults_t;

typedef struct {
	ams_state_t state;
	ams_state_t previous_state;
	uint32_t    uptime_ms;
	uint32_t    loop_us;              /* duracao do ultimo brain_loop */
	uint32_t    loop_max_us;          /* pior caso; o WWDG corta a ~65 ms */
	bool        wwdg_reset;           /* o ultimo arranque foi reset do WWDG */
} bms_system_t;

/* Tudo o que a pagina Configuracao da UI edita, mais a topologia.
 * E isto que vai para a EEPROM. Valores entre parenteses = os da UI. */
typedef struct {
	bool     loaded;                  /* lida da EEPROM e validada (false = defaults) */
	uint8_t  version;                 /* sobe quando o layout muda */
	/* topologia: validar contra BMS_MAX_* ao carregar */
	uint8_t  slaves;                  /* ADBMS6830 na cadeia */
	uint8_t  cells_per_slave;
	uint8_t  ntcs_per_slave;
	/* limites de celula, por paralelo */
	uint16_t cell_min_mV;             /* falha UV (2500) */
	uint16_t cell_low_warn_mV;        /* (2800) */
	uint16_t cell_high_warn_mV;       /* tambem termina a carga (4150) */
	uint16_t cell_max_mV;             /* falha OV (4200) */
	uint16_t cell_open_wire_mV;       /* abaixo disto = fio de sense partido (2300) */
	/* temperatura das celulas (NTC), mesma logica das tensoes */
	int16_t  temp_min_dC;             /* falha UT (falta na UI) */
	int16_t  temp_low_warn_dC;        /* (falta na UI) */
	int16_t  temp_high_warn_dC;       /* aviso de temperatura (500) */
	int16_t  temp_max_dC;             /* falha OT, falha de temperatura (600) */
	/* carregamento */
	uint16_t charge_voltage_dV;       /* setpoint enviado ao carregador (6000) */
	uint16_t charge_current_dA;       /* (60) */
	uint16_t charge_open_mA;          /* abre os contactores abaixo disto, max 5 s (500) */
	/* balanceamento */
	uint16_t bal_min_cell_mV;         /* abaixo disto a celula e ignorada (3000) */
	uint16_t bal_max_cell_mV;         /* acima disto a celula e ignorada (4250) */
	uint16_t bal_delta_mV;            /* delta maximo admitido (20) */
	int16_t  bal_die_stop_dC;         /* temperatura dos chips, nao das celulas (850) */
	uint16_t bal_pulse_ms;            /* tem de ser < 1800, tSLEEP do ADBMS6830 (1500) */
	/* ventoinhas: 0 abaixo de start, rampa de pwm_min a pwm_max ate full */
	int16_t  fan_start_dC;            /* (300) */
	int16_t  fan_full_dC;             /* (550) */
	uint8_t  fan_pwm_min_pct;         /* (20) */
	uint8_t  fan_pwm_max_pct;         /* (100) */
	/* pre-carga */
	uint16_t pre_timeout_ms;          /* tempo maximo da sequencia (10000) */
	uint8_t  pre_bus_pct;             /* tensao do bus para concluir, % do pack (95) */
	uint16_t pre_air_wait_ms;         /* espera antes de ler o feedback de cada AIR (500) */
	/* SOC */
	uint16_t capacity_mAh;            /* capacidade nominal (13500) */
	uint8_t  soc_fit_degree;          /* grau do ajuste mostrado na UI (4) */
	uint8_t  soc_curve_pct[CFG_SOC_POINTS];    /* 0, 5, 10, 20 ... 100 */
	uint16_t soc_curve_mV[CFG_SOC_POINTS];     /* tensao em repouso nesse SOC */
	/* compensacao por temperatura */
	int8_t   tcomp_temp_C[CFG_TCOMP_POINTS];   /* -20, -10 ... 70 */
	int16_t  tcomp_adjust_dmV[CFG_TCOMP_POINTS];  /* correcao da tensao lida, 0,1 mV */
	uint16_t tcomp_r_dmOhm[CFG_TCOMP_POINTS];     /* resistencia interna, 0,1 mOhm */
	/* quais faults disparam a linha AMS_ERROR */
	bms_faults_t ams_error_enable;
} bms_config_t;

typedef struct {
	bool         active;              /* linha AMS_ERROR em erro */
	bms_faults_t sources;             /* faults que estao a segurar a linha */
} bms_ams_error_t;

typedef struct {
	uint16_t cell_mV[BMS_MAX_SLAVES][BMS_MAX_CELLS];
	int16_t  ntc_dC[BMS_MAX_SLAVES][BMS_MAX_NTCS];
	int16_t  die_dC[BMS_MAX_SLAVES];      /* temperatura interna de cada ADBMS6830 */
	bool     cell_open_wire[BMS_MAX_SLAVES][BMS_MAX_CELLS];
	bool     ntc_open_wire[BMS_MAX_SLAVES][BMS_MAX_NTCS];
	/* resumo do pack (posicoes 1-based, 0 = nenhuma) */
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
	uint32_t updated_ms;              /* HAL_GetTick() da ultima copia valida */
} bms_cells_t;

typedef struct {
	uint32_t total;                   /* erros de PEC desde o arranque */
	uint16_t per_slave[BMS_MAX_SLAVES];
	bool     last_read[BMS_MAX_SLAVES];   /* true = a ultima leitura deste slave falhou o PEC */
} bms_pec_t;

typedef struct {
	int32_t  current_mA;              /* ISA do pack (CAN1), sinal da ISA */
	int32_t  voltage_mV;              /* ISA do pack, U1 */
	uint32_t voltage_sum_mV;          /* soma de todas as celulas */
	int32_t  power_W;
	int32_t  charge_As;               /* contador As da ISA */
	int32_t  energy_Wh;
	int16_t  isa_temp_dC;
	bool     isa_alive;
	uint32_t isa_age_ms;              /* ms desde a ultima trama de corrente */
} bms_accu_t;

typedef struct {
	bool     ready;                   /* ja inicializado pela tensao das celulas */
	uint16_t percent_x100;            /* 0..10000 */
	uint16_t initial_percent_x100;    /* tabela OCV da celula minima no arranque */
	int32_t  used_As;                 /* carga gasta desde o arranque (coulomb counting) */} bms_soc_t;

typedef struct {
	precharge_state_t state;
	uint32_t state_ms;                /* tempo no estado atual */
	/* comando (o que o MCU pede) */
	bool     air_pos_cmd;
	bool     air_neg_cmd;
	bool     precharge_cmd;
	bool     discharge_cmd;
	/* feedback (o que o contactor diz) */
	bool     air_pos_fb;
	bool     air_neg_fb;
	bool     precharge_fb;
	bool     discharge_fb;
	bool     sdc_closed;
	int8_t   vcu_request;             /* -1 = nunca recebido */
	uint32_t vcu_request_age_ms;
	uint8_t  mismatch_count;          /* checks de feedback falhados seguidos */
} bms_precharge_t;

typedef struct {
	balancing_stage_t stage;
	uint16_t target_mV;               /* minimo congelado no inicio do balanceamento */
	uint16_t dcc_mask[BMS_MAX_SLAVES];    /* bit i = celula i+1 a descarregar (= CAN 0x706) */
	uint8_t  cells_discharging;
	uint16_t worst_delta_mV;          /* maior (celula - alvo), 0 = convergiu */
	uint8_t  worst_slave;             /* 1-based, 0 = nenhum */
	uint8_t  worst_cell;
	bool     die_ot_hold;             /* bloqueado 60 s apos OT do die */
	uint32_t started_ms;
} bms_balancing_t;

typedef struct {
	charger_state_t state;
	bool     handcart_connected;      /* 0x084 a chegar */
	bool     switch_on;               /* pedido de carga do switch do handcart */
	bool     charger_alive;           /* status do carregador nos ultimos 3 s */
	uint16_t out_voltage_dV;          /* medido pelo carregador */
	uint16_t out_current_dA;
	uint16_t req_voltage_dV;          /* setpoints enviados */
	uint16_t req_current_dA;
	int16_t  temp_C;
	/* flags do carregador (Charger_Status_P1000) */
	bool     hw_failure;
	bool     over_temp;
	bool     input_voltage_fault;
	bool     starting_state_fault;    /* bateria desligada ou invertida */
	bool     comm_timeout;
	uint32_t updated_ms;
} bms_charger_t;

typedef struct {
	bool     on;                      /* com histerese */
	uint8_t  pwm;                     /* 0..255, TIM12 CH1 com polaridade invertida */
	int16_t  control_temp_dC;         /* temperatura que comanda a rampa */
} bms_fans_t;

typedef struct {
	uint16_t current_mA;              /* consumo do AMS master (ADC, CURRENT_SENSOR) */
	int16_t  mcu_temp_dC;
	uint16_t vdda_mV;
} bms_board_t;

typedef struct {
	bool     started;
	bool     bus_off;
	bool     error_passive;
	uint8_t  tx_errors;               /* TEC: sobe com TX sem ACK */
	uint8_t  rx_errors;               /* REC */
	uint8_t  last_error_code;         /* 0 ok, 1 stuff, 2 form, 3 ACK, 4 bit rec, 5 bit dom, 6 CRC */
	uint16_t tx_queue_depth;
	uint32_t tx_dropped;
	uint32_t rx_frames;
} bms_can_t;

typedef struct {
	bool     rn4871_alive;            /* precisa do RX do UART2 (antes nunca foi armado) */
	uint32_t rn4871_reboots;
	uint32_t frames_sent;             /* tramas entregues ao ring buffer */
	uint32_t frames_dropped;          /* nao couberam no ring (tudo-ou-nada) */
	uint32_t dma_errors;              /* HAL_UART_ErrorCallback */
	uint32_t dma_restarts;            /* TX encravado > 2 s -> abort + recomeco */
	uint16_t ring_peak_bytes;         /* maior ocupacao do ring buffer */
} bms_uart_t;

typedef struct {
	bms_system_t    system;
	bms_config_t    config;
	bms_faults_t    faults;           /* todos os faults ativos */
	bms_ams_error_t ams_error;
	bms_cells_t     cells;
	bms_pec_t       pec;
	bms_accu_t      accu;
	bms_soc_t       soc;
	bms_precharge_t precharge;
	bms_balancing_t balancing;
	bms_charger_t   charger;
	bms_fans_t      fans;
	bms_board_t     board;
	bms_can_t       can1;             /* powertrain */
	bms_can_t       can2;             /* carregamento */
	bms_uart_t      uart;
} bms_t;

extern bms_t bms;


/* Watchdog reset flag raw RCC->CSR captured before peripherals initilise */
extern uint32_t watchdog_flag;

void brain_start(void);

void brain_loop(void);

void heartbeat(void);


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void HAL_SYSTICK_Callback(void);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);


#endif /* INC_BRAIN_H_ */
