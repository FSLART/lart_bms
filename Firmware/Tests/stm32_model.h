/*
 * stm32_model.h - um STM32F412 simulado no PC, para correr o bootloader
 * (Bootloader/Src) tal como esta, incluindo o que mexe nos registos
 * (bl_hw.c, bl_can.c, bl_flash.c, main.c, startup.c).
 *
 * Os registos ficam nos enderecos verdadeiros (perifericos em 0x40000000,
 * flash em 0x08000000, a pagina da RAM partilhada em 0x2003F000, DBGMCU em
 * 0xE0042000). As paginas dos perifericos e da flash estao so de leitura e
 * o TIM2 nem isso: cada escrita (e cada leitura do TIM2) da um page fault, o
 * modelo deixa a instrucao correr um passo (flag TF do x86) e trata o que foi
 * escrito como o RM0402 diz:
 *
 *   RCC     HSE/PLL/troca de relogio, relogios dos perifericos, resets, CSR
 *   GPIO    BSRR/ODR
 *   TIM2    conta com o relogio que o bootloader configurou (o tempo e
 *           simulado: cada leitura do CNT anda us_per_timer_read)
 *   USART1  o texto dos logs, ao baud configurado
 *   bxCAN   sleep/init/normal, bit timing, filtros (com o CAN2SB), caixas
 *           de envio (prioridade, abortar), FIFO 0 com 3 lugares
 *   FLASH   chaves, apagar setores, programar palavras x32, caches
 *   IWDG    chaves, prescaler/reload, reset quando nao e refrescado
 *
 * e regista as violacoes do manual (escrever num periferico sem relogio,
 * filtros fora do modo de init, programar sem apagar, latencia da flash
 * curta para o HCLK, ...). So x86-64 Linux; stm32_init devolve false nos
 * outros.
 *
 * Cada arranque corre num processo filho (stm32_boot): a flash, a RAM e os
 * registos estao em memoria partilhada e passam de um arranque para o outro
 * como no chip; o .data e o .bss do bootloader comecam do zero.
 */

#ifndef STM32_MODEL_H
#define STM32_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define STM32_LOG_MAX      16384u
#define STM32_TX_MAX       8192u
#define STM32_UART_MAX     32768u
#define STM32_REQUEST_MAX  4096u
#define STM32_VIOLATION_MAX 8u

enum {
	STM32_EXIT_NONE = 0,
	STM32_EXIT_RESET,      // NVIC_SystemReset
	STM32_EXIT_JUMP,       // saltou para a aplicacao
	STM32_EXIT_IWDG,       // o IWDG nao foi refrescado
	STM32_EXIT_LIMIT,      // chegou ao limit_ns
	STM32_EXIT_RETURNED,   // a funcao do teste voltou
	STM32_EXIT_CRASH       // o filho morreu (acesso fora do modelo, pendurado)
};

typedef struct {
	uint32_t id;
	uint8_t  dlc;
	uint8_t  data[8];
	bool     extended;
	bool     rtr;
	uint64_t time_ns;
} stm32_frame_t;

typedef struct {
	uint32_t seq;
	uint64_t time_ns;
	uint32_t address;
	uint32_t old_value;
	uint32_t value;        // o que o codigo escreveu (antes da semantica do registo)
} stm32_write_t;

// o uploader: um pedido 0x602 de cada vez, o seguinte depois da resposta 0x580
typedef struct {
	uint8_t  bus;
	uint8_t  data[8];
	uint64_t not_before_ns;
	bool     wait_reply;
} stm32_request_t;

typedef struct {
	/* ---- o hardware (escolhido pelo teste antes de cada arranque) ---- */
	bool     hse_present;            // cristal de 8 MHz
	bool     pll_locks;
	bool     can_init_ack[2];        // o bxCAN responde ao INRQ
	bool     can_bus_idle[2];        // ve 11 bits recessivos (sai do modo de init)
	bool     can_ack[2];             // ha quem de ACK: as tramas saem das caixas
	uint32_t reset_flags;            // RCC_CSR ao arrancar (bits 25..31)
	uint32_t us_per_timer_read;      // quanto anda o tempo em cada leitura do TIM2->CNT
	uint64_t limit_ns;               // acaba a simulacao (0 = sem limite)
	uint64_t warp_at_ns;             // salto no tempo (esperas de minutos sem as correr)
	uint64_t warp_by_ns;
	bool     flash_lock_stuck;       // as chaves nao desbloqueiam o FLASH_CR
	uint32_t flash_erase_error;      // bits do FLASH_SR no proximo apagar
	int32_t  flash_program_error_at; // offset da palavra cujo programar falha (-1 = nenhuma)
	uint32_t flash_program_error;
	int32_t  flash_stuck_offset;     // palavra com bits estragados (-1 = nenhuma)
	uint32_t flash_stuck_zero;       // ...presos a 0 (o apagar nao os poe a 1)
	uint32_t flash_stuck_one;        // ...presos a 1 (o programar nao os poe a 0)
	void   (*hook)(void);            // chamado a cada passo do tempo (no filho)
	uint64_t retry_ns;               // o uploader repete o pedido sem resposta (0 = nunca)

	/* ---- o que aconteceu ---- */
	uint64_t now_ns;
	int      exit_reason;
	int      exit_signal;            // STM32_EXIT_CRASH: o sinal que matou o filho
	uint32_t exit_sp;                // STM32_EXIT_JUMP: o que foi para o MSP e o PC
	uint32_t exit_pc;
	uint32_t exit_vtor;
	uint32_t exit_systick_ctrl;
	bool     exit_irq_disabled;
	uint32_t sysclk_hz;              // os relogios (atualizados a cada escrita no RCC)
	uint32_t hclk_hz;
	uint32_t pclk1_hz;
	uint32_t pclk2_hz;
	uint32_t timclk1_hz;
	uint32_t writes;                 // escritas apanhadas (perifericos + flash)
	uint32_t log_count;
	stm32_write_t log[STM32_LOG_MAX];
	uint32_t violations;
	char     violation[STM32_VIOLATION_MAX][160];
	uint32_t uart_length;
	char     uart[STM32_UART_MAX];
	uint32_t uart_violations;
	uint32_t tx_count[2];
	stm32_frame_t tx[2][STM32_TX_MAX];
	uint32_t rx_dropped[2];          // nao passaram nos filtros (ou o CAN nao estava a receber)
	uint32_t rx_overruns[2];
	uint32_t aborts[2];
	uint32_t erase_count;
	uint8_t  erases[64];             // setores apagados, por ordem
	uint32_t program_words;
	uint32_t overwrites;             // programar 1 por cima de 0 (falta apagar)
	uint32_t cache_flushes;
	uint32_t peripheral_resets;      // impulsos nos RCC_xxxRSTR
	bool     iwdg_started;
	uint32_t iwdg_reloads;
	uint32_t iwdg_timeout_ms;
	uint32_t led_toggles;            // PB10
	uint64_t led_last_ns;
	uint64_t led_period_ns;          // entre as duas ultimas mudancas
	uint32_t barriers;

	/* ---- o uploader ---- */
	uint32_t request_count;
	uint32_t next_request;
	bool     awaiting_reply;
	uint64_t sent_ns;                // quando saiu o pedido a espera de resposta
	uint32_t retries;
	uint32_t seen_tx[2];
	stm32_request_t requests[STM32_REQUEST_MAX];
	uint32_t reply_count;
	uint8_t  replies[STM32_REQUEST_MAX][8];

	/* ---- para a funcao do teste guardar resultados (corre no filho) ---- */
	uint32_t user[64];

	/* ---- interno ---- */
	struct {
		uint8_t  count;
		stm32_frame_t frames[3];
		uint32_t fmi[3];
		bool     overrun;
	} fifo[2];
	struct {
		uint64_t base_ns;
		uint64_t base_count;
		uint32_t psc;
		bool     running;
	} tim2;
	struct {
		uint8_t  key_stage;
		bool     locked_out;
	} flash;
	struct {
		bool     access;
		uint32_t pr;
		uint32_t rlr;
		uint64_t reload_ns;
	} iwdg;
	uint32_t seq;
} stm32_t;

extern stm32_t *stm32;

// mapeia tudo nos enderecos do F412 e instala os handlers (false = este PC nao da)
bool stm32_init(void);

// valores de reset dos registos; a RAM fica (reset) ou fica com lixo (desligar)
void stm32_power_on(bool keep_ram);

// um arranque num processo filho: corre body() ate sair (reset, salto, ...)
int stm32_boot(void (*body)(void));

// a flash e os registos pelo lado do teste (sem passar pelo modelo)
uint8_t *stm32_flash(uint32_t address);
volatile uint32_t *stm32_reg(uint32_t address);

// uma trama no bus (passa pelos filtros como no bxCAN)
void stm32_can_send(uint8_t bus, uint32_t id, const uint8_t *data, uint8_t dlc, bool extended, bool rtr);

// o uploader: acrescenta um pedido 0x602
void stm32_request(uint8_t bus, const uint8_t data[8], uint64_t not_before_ns, bool wait_reply);

// a primeira escrita apanhada neste endereco com estes bits a 1 (-1 = nenhuma)
int32_t stm32_find_write(uint32_t address, uint32_t bits, uint32_t from);

const char* stm32_exit_name(int reason);

#endif
