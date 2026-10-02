#include "uartDMA.h"
#include "stdio.h"
#include "stdarg.h"
#include "stdbool.h"
#include "stdint.h"
#include "string.h"
#include "main.h"
#include "brain.h"
#include "fault_manager.h"

#define uartHandle huart1
#define uart2Handle huart2
#define BUFFER_SIZE 10000

static void jsonSendEscaped(const char *s);

char buffer[BUFFER_SIZE] = { 0 };
volatile int head = 0;
volatile int tail = 0;

char buffer2[BUFFER_SIZE] = { 0 };
volatile int head2 = 0;
volatile int tail2 = 0;

bool isFull = false;
volatile bool isWrapped = false;

bool isFull2 = false;
volatile bool isWrapped2 = false;   // limpo no ISR de TX completo

int tailDma = 0; // Stores the tail index of the buffer being sent
volatile int tailDma2 = 0; // Stores the tail index of the buffer being sent

/* Vigia do DMA do UART2: o maior bloco possivel (BUFFER_SIZE) demora 0,87 s
 * a 115200 baud, logo um TX ocupado ha' mais de 2 s esta' encravado */
#define UART2_TX_STALL_MS 2000
volatile uint32_t uart2TxStartMs = 0;
volatile uint32_t uart2DmaErrors = 0;    // HAL_UART_ErrorCallback no UART2
volatile uint32_t uart2DmaRestarts = 0;  // aborts por TX encravado

int getDataLen(void) {
	int tempTail = tail;

	if (head >= tempTail) {
		return head - tempTail; // Data is in a single contiguous block
	} else {
		return BUFFER_SIZE - tempTail + head; // Data wraps around the buffer
	}
}

int getDataLen2(void) {
	int tempTail = tail2;

	if (head2 >= tempTail) {
		return head2 - tempTail; // Data is in a single contiguous block
	} else {
		return BUFFER_SIZE - tempTail + head2; // Data wraps around the buffer
	}
}

void startUartDmaTx(void) {
	// Get the data len up to the buffer size only
	int dataLen = isWrapped ? (BUFFER_SIZE - tail) : (head - tail);
	if (dataLen <= 0)
		return;          // guard against empty / race

	// get pointer to the tail data
	uint8_t *dataPtr = (uint8_t*) (buffer + tail);

	tailDma = tail + dataLen;

	HAL_UART_Transmit_DMA(&uartHandle, dataPtr, dataLen);
	//TODO: gpio expander
	//HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
}

void startUart2DmaTx(void) {
	// Get the data len up to the buffer size only
	int dataLen = isWrapped2 ? (BUFFER_SIZE - tail2) : (head2 - tail2);
	if (dataLen <= 0)
		return;          // guard against empty / race

	// get pointer to the tail data
	uint8_t *dataPtr = (uint8_t*) (buffer2 + tail2);

	tailDma2 = tail2 + dataLen;

	uart2TxStartMs = HAL_GetTick();
	HAL_UART_Transmit_DMA(&uart2Handle, dataPtr, dataLen);
	//HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
}

/* Erro no DMA/UART de TX. O HAL ja' repos o gState em READY (UART_DMAError);
 * o tail2 nao avancou, logo reenvia-se o mesmo bloco. So' o UART2 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart == &uart2Handle) {
		uart2DmaErrors++;

		if ((head2 != tail2) && (huart->gState == HAL_UART_STATE_READY)) {
			startUart2DmaTx();
		}
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart == &uartHandle) {
		tail = tailDma;

		if (tail == BUFFER_SIZE) {
			tail = 0;
			isWrapped = false;   // we've just flushed the end segment
		}

		// If there is ANY data pending, start next DMA
		if (head != tail) {        // instead of (head > tail)

			startUartDmaTx();
		}
	}

	if (huart == &uart2Handle) {
		tail2 = tailDma2;

		if (tail2 == BUFFER_SIZE) {
			tail2 = 0;
			isWrapped2 = false;   // we've just flushed the end segment
		}

		// If there is ANY data pending, start next DMA
		if (head2 != tail2) {        // <— instead of (head > tail)

			startUart2DmaTx();
		}
	}
}

// Function to append formatted data to the ring buffer
int printfUI(const char *format, ...) {
	const int TEMP_BUFF_SIZE = 256;

	char temp_buffer[TEMP_BUFF_SIZE];
	va_list args;
	va_start(args, format);
	int written = vsnprintf(temp_buffer, TEMP_BUFF_SIZE, format, args);
	va_end(args);

	if (written < 0) {
		return -1; // Error in formatting
	} else if (written > TEMP_BUFF_SIZE) {
		// Over limit of temp buffer
		RAISE_ERROR(FAULT_UART1_TX);
		return 0;
	} else if (getDataLen() + written > BUFFER_SIZE) {
		// Buffer full
		RAISE_ERROR(FAULT_UART1_TX);
		return 0;
	}

	for (int i = 0; i < written; i++) {
		buffer[head++] = temp_buffer[i];

		if (head == BUFFER_SIZE) {
			isWrapped = true;
			head = 0;
		}

//        // Overflow handling (overwrite oldest data)
//        if (is_full)
//        {
//            tail = (tail + 1) % BUFFER_SIZE;
//        }

		if (head == tail) {
			RAISE_ERROR(FAULT_UART1_TX);
			return 0;
		}
	}

	if (HAL_UART_GetState(&uartHandle) == HAL_UART_STATE_READY) {
		startUartDmaTx();
		//TODO: gpio expander
		//HAL_GPIO_TogglePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
	}

	return written;
}

// Function to append formatted data to the ring buffer for BT
int printfDebugRaw(const char *format, ...) {
	const int TEMP_BUFF_SIZE = 256;

	char temp_buffer[TEMP_BUFF_SIZE];
	va_list args;
	va_start(args, format);
	int written = vsnprintf(temp_buffer, TEMP_BUFF_SIZE, format, args);
	va_end(args);

	if (written < 0) {
		return -1; // Error in formatting
	} else if (written >= TEMP_BUFF_SIZE) {
		// Over limit of temp buffer
		RAISE_ERROR(FAULT_UART2_TX);
		return 0;
	}

	return uart2Write(temp_buffer, written);
}

/* Bytes crus para o BT (UART2). Tudo ou nada: se nao couber inteiro no ring
 * buffer nao escreve nada e devolve 0 (uma trama JSON nunca sai a meio) */
int uart2Write(const char *data, int len) {

	// vigia ANTES do teste de cheio: com o DMA encravado o ring enche e,
	// se isto viesse depois, nunca mais se chegava aqui
	if ((uart2Handle.gState != HAL_UART_STATE_READY) && ((HAL_GetTick() - uart2TxStartMs) > UART2_TX_STALL_MS)) {
		// TX ocupado ha' demasiado tempo: o DMA encravou. Abortar e
		// recomecar do tail2 (pode repetir parte de uma linha, nunca perde)
		HAL_UART_AbortTransmit(&uart2Handle);
		uart2DmaRestarts++;
		startUart2DmaTx();
	}

	// >= e nao >: head2 == tail2 significa vazio, nunca pode encher ate ao fim
	if ((len <= 0) || (getDataLen2() + len >= BUFFER_SIZE)) {
		RAISE_ERROR(FAULT_UART2_TX);
		return 0;
	}

	for (int i = 0; i < len; i++) {
		buffer2[head2++] = data[i];

		if (head2 == BUFFER_SIZE) {
			isWrapped2 = true;
			head2 = 0;
		}
	}

	// so o estado de TX: o RX nunca e' armado, mas o GetState() mistura os dois
	if (uart2Handle.gState == HAL_UART_STATE_READY) {
		startUart2DmaTx();
	}

	return len;
}

/* One-size function:
 * - printConsole("hello");
 * - printConsole("cell %d = %.3f V", i, v);
 * Emits: [{"console":"..."}]\n
 * (change "console" to "cossole" if you need that exact key)
 */
void printfConsole(const char *format, ...) {
	if (!format)
		format = "";

	enum {
		TEMP_SZ = 256
	};
	// tune if you need longer messages
	char tmp[TEMP_SZ];

	va_list args;
	va_start(args, format);
	int n = vsnprintf(tmp, TEMP_SZ, format, args);
	va_end(args);

	if (n < 0) {
		tmp[0] = '\0';            // formatting error -> empty
	} else if (n >= TEMP_SZ) {
		tmp[TEMP_SZ - 1] = '\0';  // truncated but valid
	}

	printfUI("[");
	printfUI("{\"console\":\"");
	jsonSendEscaped(tmp);
	printfUI("\"}");
	printfUI("]\n\r");
}

/* Stream a JSON-escaped string through printfDma
 Prevenir que a mensagem termine por emissão de caracteres especiais */
static void jsonSendEscaped(const char *s) {
	while (*s) {
		unsigned char c = (unsigned char) *s++;
		switch (c) {
		case '\"':
			printfUI("\\\"");
			break;
		case '\\':
			printfUI("\\\\");
			break;
		case '\b':
			printfUI("\\b");
			break;
		case '\f':
			printfUI("\\f");
			break;
		case '\n':
			printfUI("\\n");
			break;
		case '\r':
			printfUI("\\r");
			break;
		case '\t':
			printfUI("\\t");
			break;
		default:
			if (c < 0x20) {
				printfUI("\\u%04X", (unsigned) c);
			} else {
				printfUI("%c", c);
			}
		}
	}
}

/* ---------------------------------------------------------------------------
 * Reboot periodico do RN4871 (as cegas, decisao do utilizador)
 *
 * O modulo as vezes deixa de passar o UART para o BLE e so' tem TX/RX ligados
 * ao MCU (sem RST_N), logo a unica cura e' por comando: "$$$" (modo de
 * comandos) + "R,1\r" (reboot). Derruba a ligacao BLE: o telemovel tem de
 * voltar a ligar. Nao bloqueante (o brain_loop tem ~65 ms de WWDG):
 *
 *   IDLE --periodo--> WAIT_SILENCE (JSON em pausa, espera ring vazio + 200 ms)
 *        --"$$$"--> WAIT_CMD (200 ms) --"R,1\r"--> WAIT_BOOT (2 s) --> IDLE
 * --------------------------------------------------------------------------- */
#define RN4871_REBOOT_PERIOD_MS   30000UL   // 30 s
#define RN4871_SILENCE_MS         200    // linha calada antes do "$$$"
#define RN4871_CMD_WAIT_MS        200    // tempo para entrar em modo de comandos
#define RN4871_BOOT_MS            2000   // reboot do modulo (bytes aqui perdiam-se)

typedef enum {
	RN_IDLE = 0, RN_WAIT_SILENCE, RN_WAIT_CMD, RN_WAIT_BOOT
} rn4871_state_t;

static rn4871_state_t rnState = RN_IDLE;
static uint32_t rnT0 = 0;
static uint32_t rnLastReboot = 0;
volatile uint32_t rn4871Reboots = 0;

/* 1 = sequencia de reboot em curso: nao mandar JSON (estragava os comandos) */
uint8_t RN4871_IsBusy(void) {
	return (rnState != RN_IDLE) ? 1U : 0U;
}

/* Chamar em cada passagem do brain_loop */
void RN4871_Service(void) {
	uint32_t now = HAL_GetTick();

	switch (rnState) {

	case RN_IDLE:
		if ((now - rnLastReboot) >= RN4871_REBOOT_PERIOD_MS) {
			rnT0 = now;
			rnState = RN_WAIT_SILENCE;
		}
		break;

	case RN_WAIT_SILENCE:
		// so' conta silencio com o ring vazio e o DMA parado
		if ((head2 != tail2) || (uart2Handle.gState != HAL_UART_STATE_READY)) {
			rnT0 = now;
		} else if ((now - rnT0) >= RN4871_SILENCE_MS) {
			uart2Write("$$$", 3);
			rnT0 = now;
			rnState = RN_WAIT_CMD;
		}
		break;

	case RN_WAIT_CMD:
		if ((now - rnT0) >= RN4871_CMD_WAIT_MS) {
			uart2Write("R,1\r", 4);
			rn4871Reboots++;
			rnT0 = now;
			rnState = RN_WAIT_BOOT;
		}
		break;

	case RN_WAIT_BOOT:
	default:
		if ((now - rnT0) >= RN4871_BOOT_MS) {
			rnLastReboot = now;
			rnState = RN_IDLE;
		}
		break;
	}
}

void RN4871_SetName(void) {
	printfDebugRaw("Changing RN4871 name...\r\n");

	// no \r\n after $$$
	HAL_Delay(150);
	printfDebugRaw("$$$");
	HAL_Delay(300);

	//set namre
	printfDebugRaw("SN,LART Accumulator\r");
	HAL_Delay(300);

	//reboot
	printfDebugRaw("R,1\r");
	HAL_Delay(1000);

	printfDebugRaw("RN4871 name command sent\r\n");
}
