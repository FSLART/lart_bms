#include "uartDMA.h"
#include "stdio.h"
#include "stdarg.h"
#include "stdbool.h"
#include "stdint.h"
#include "string.h"
#include "main.h"
#include "brain.h"

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
bool isWrapped2 = false;

int tailDma = 0; // Stores the tail index of the buffer being sent
volatile int tailDma2 = 0; // Stores the tail index of the buffer being sent

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
	HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
}

void startUart2DmaTx(void) {
	// Get the data len up to the buffer size only
	int dataLen = isWrapped2 ? (BUFFER_SIZE - tail2) : (head2 - tail2);
	if (dataLen <= 0)
		return;          // guard against empty / race

	// get pointer to the tail data
	uint8_t *dataPtr = (uint8_t*) (buffer2 + tail2);

	tailDma2 = tail2 + dataLen;

	HAL_UART_Transmit_DMA(&uart2Handle, dataPtr, dataLen);
	//HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart == &uartHandle) {
		tail = tailDma;

		if (tail == BUFFER_SIZE) {
			tail = 0;
			isWrapped = false;   // we've just flushed the end segment
		}

		// If there is ANY data pending, start next DMA
		if (head != tail){        // instead of (head > tail)

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
		if (head2 != tail2){        // <— instead of (head > tail)

			startUart2DmaTx();
		}
	}
}

// Function to append formatted data to the ring buffer
int printfDma(const char *format, ...) {
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
		Error_Handler();
	} else if (getDataLen() + written > BUFFER_SIZE) {
		// Buffer full
		Error_Handler();
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
			Error_Handler();
		}
	}

	if (HAL_UART_GetState(&uartHandle) == HAL_UART_STATE_READY) {
		startUartDmaTx();
		HAL_GPIO_TogglePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
	}

	return written;
}

// Function to append formatted data to the ring buffer for BT
int printfDmaBT(const char *format, ...) {
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
		Error_Handler();
	} else if (getDataLen2() + written > BUFFER_SIZE) {
		// Buffer full
		Error_Handler();
	}

	for (int i = 0; i < written; i++) {
		buffer2[head2++] = temp_buffer[i];

		if (head2 == BUFFER_SIZE) {
			isWrapped2 = true;
			head2 = 0;
		}

//        // Overflow handling (overwrite oldest data)
//        if (is_full)
//        {
//            tail = (tail + 1) % BUFFER_SIZE;
//        }

		if (head2 == tail2) {
			Error_Handler();
		}
	}

	if (HAL_UART_GetState(&uart2Handle) == HAL_UART_STATE_READY) {
		startUart2DmaTx();
		///HAL_GPIO_TogglePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
	}

	return written;
}

/* One-size function:
 * - printConsole("hello");
 * - printConsole("cell %d = %.3f V", i, v);
 * Emits: [{"console":"..."}]\n
 * (change "console" to "cossole" if you need that exact key)
 */
void printConsole(const char *format, ...) {
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

	printfDma("[");
	printfDma("{\"console\":\"");
	jsonSendEscaped(tmp);
	printfDma("\"}");
	printfDma("]\n\r");
}

/* Stream a JSON-escaped string through printfDma
 Prevenir que a mensagem termine por emissão de caracteres especiais */
static void jsonSendEscaped(const char *s) {
	while (*s) {
		unsigned char c = (unsigned char) *s++;
		switch (c) {
		case '\"':
			printfDma("\\\"");
			break;
		case '\\':
			printfDma("\\\\");
			break;
		case '\b':
			printfDma("\\b");
			break;
		case '\f':
			printfDma("\\f");
			break;
		case '\n':
			printfDma("\\n");
			break;
		case '\r':
			printfDma("\\r");
			break;
		case '\t':
			printfDma("\\t");
			break;
		default:
			if (c < 0x20) {
				printfDma("\\u%04X", (unsigned) c);
			} else {
				printfDma("%c", c);
			}
		}
	}
}
