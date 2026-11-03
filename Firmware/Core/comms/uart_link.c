/*
 * uart_link.c
 *
 * Ver uart_link.h. Rings de um produtor e um consumidor:
 *   TX: o main loop escreve a cabeca, o callback do DMA avanca a cauda
 *   RX: o callback do UART escreve a cabeca, o main loop avanca a cauda
 * O arranque do DMA a partir do main loop e feito com as interrupcoes
 * desligadas (umas dezenas de ciclos), porque o callback tambem o arranca.
 */

#include "uart_link.h"
#include "main.h"
#include <string.h>

#define TX_SIZE          8192u   // potencias de 2; o JSON ronda os 3,5 kB
#define RX_SIZE          512u
#define RX_CHUNK         64u
#define TX_STUCK_MS      2000u

typedef struct {
	UART_HandleTypeDef *huart;
	bms_link_t *status;
	uint8_t tx_buffer[TX_SIZE];
	volatile uint16_t tx_head;
	volatile uint16_t tx_tail;
	volatile uint16_t tx_chunk;
	volatile bool tx_busy;
	volatile uint32_t tx_start_ms;
	uint8_t rx_buffer[RX_SIZE];
	volatile uint16_t rx_head;
	volatile uint16_t rx_tail;
	uint8_t rx_chunk[RX_CHUNK];
} uart_link_t;

static uart_link_t links[UART_LINK_COUNT];

static uart_link_t* link_of(UART_HandleTypeDef *huart) {

	for (uint8_t index = 0; index < UART_LINK_COUNT; index++) {
		if (links[index].huart == huart) {
			return &links[index];
		}
	}
	return NULL;
}

static uint16_t tx_count(const uart_link_t *link) {

	return (uint16_t) ((link->tx_head - link->tx_tail) & (TX_SIZE - 1u));
}

static void arm_rx(uart_link_t *link) {

	(void) HAL_UARTEx_ReceiveToIdle_IT(link->huart, link->rx_chunk, RX_CHUNK);
}

void UartLink_Init(void) {

	memset(links, 0, sizeof(links));
	links[UART_LINK_1].huart = &huart1;
	links[UART_LINK_1].status = &bms.uart1;
	links[UART_LINK_2].huart = &huart2;
	links[UART_LINK_2].status = &bms.uart2;

	for (uint8_t index = 0; index < UART_LINK_COUNT; index++) {
		memset(links[index].status, 0, sizeof(bms_link_t));
		arm_rx(&links[index]);
	}
}

/* ================== TX ================== */

// chamado com as interrupcoes desligadas ou de dentro do callback
static void start_tx(uart_link_t *link) {

	uint16_t head = link->tx_head;
	uint16_t tail = link->tx_tail;

	if (link->tx_busy || (head == tail)) {
		return;
	}

	// o DMA quer um bloco contiguo: ate a cabeca, ou ate ao fim do buffer
	uint16_t chunk = (head > tail) ? (uint16_t) (head - tail) : (uint16_t) (TX_SIZE - tail);

	link->tx_chunk = chunk;
	link->tx_busy = true;
	link->tx_start_ms = HAL_GetTick();

	if (HAL_UART_Transmit_DMA(link->huart, &link->tx_buffer[tail], chunk) != HAL_OK) {
		link->tx_busy = false;
		link->status->dma_errors++;
	}
}

bool UartLink_Write(uint8_t link_index, const void *data, uint16_t length) {

	if ((link_index >= UART_LINK_COUNT) || (length == 0u)) {
		return false;
	}

	uart_link_t *link = &links[link_index];
	const uint8_t *bytes = (const uint8_t*) data;

	if (length > UartLink_Free(link_index)) {
		link->status->frames_dropped++;
		return false;
	}

	uint16_t head = link->tx_head;
	uint16_t first = (uint16_t) (TX_SIZE - head);

	if (first > length) {
		first = length;
	}
	memcpy(&link->tx_buffer[head], bytes, first);
	memcpy(&link->tx_buffer[0], &bytes[first], length - first);

	__DMB();
	link->tx_head = (uint16_t) ((head + length) & (TX_SIZE - 1u));
	link->status->frames_sent++;

	uint16_t used = tx_count(link);
	if (used > link->status->ring_peak_bytes) {
		link->status->ring_peak_bytes = used;
	}

	return true;
}

uint16_t UartLink_Free(uint8_t link_index) {

	return (link_index < UART_LINK_COUNT) ? (uint16_t) (TX_SIZE - 1u - tx_count(&links[link_index])) : 0u;
}

bool UartLink_TxIdle(uint8_t link_index) {

	return (link_index < UART_LINK_COUNT) && !links[link_index].tx_busy && (tx_count(&links[link_index]) == 0u);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {

	uart_link_t *link = link_of(huart);

	if (link == NULL) {
		return;
	}

	link->tx_tail = (uint16_t) ((link->tx_tail + link->tx_chunk) & (TX_SIZE - 1u));
	link->status->tx_bytes += link->tx_chunk;
	link->tx_busy = false;
	start_tx(link);
}

/* ================== RX ================== */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {

	uart_link_t *link = link_of(huart);

	if (link == NULL) {
		return;
	}

	for (uint16_t index = 0; (index < size) && (index < RX_CHUNK); index++) {

		uint16_t next = (uint16_t) ((link->rx_head + 1u) & (RX_SIZE - 1u));

		if (next == link->rx_tail) {
			link->status->rx_overruns++;
			break;
		}
		link->rx_buffer[link->rx_head] = link->rx_chunk[index];
		link->rx_head = next;
	}
	link->status->rx_bytes += size;

	// no modo IT a rececao para a cada evento: rearmar ja
	if (huart->RxState == HAL_UART_STATE_READY) {
		arm_rx(link);
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {

	uart_link_t *link = link_of(huart);

	if (link == NULL) {
		return;
	}

	// erro de TX (DMA): o bloco nao saiu, volta a tentar o mesmo
	if (link->tx_busy && (huart->gState == HAL_UART_STATE_READY)) {
		link->status->dma_errors++;
		link->tx_busy = false;
		start_tx(link);
	}

	// erro de RX (ruido, overrun): o HAL parou a rececao
	if (huart->RxState == HAL_UART_STATE_READY) {
		arm_rx(link);
	}
}

uint16_t UartLink_Read(uint8_t link_index, uint8_t *out, uint16_t max_length) {

	if (link_index >= UART_LINK_COUNT) {
		return 0;
	}

	uart_link_t *link = &links[link_index];
	uint16_t count = 0;

	while ((count < max_length) && (link->rx_tail != link->rx_head)) {
		out[count++] = link->rx_buffer[link->rx_tail];
		link->rx_tail = (uint16_t) ((link->rx_tail + 1u) & (RX_SIZE - 1u));
	}

	return count;
}

/* ================== MAIN LOOP ================== */

void UartLink_Service(uint32_t now_ms) {

	for (uint8_t index = 0; index < UART_LINK_COUNT; index++) {

		uart_link_t *link = &links[index];
		uint32_t primask = __get_PRIMASK();

		__disable_irq();
		start_tx(link);
		__set_PRIMASK(primask);

		// DMA encravado: abort e recomecar o mesmo bloco
		if (link->tx_busy && ((now_ms - link->tx_start_ms) > TX_STUCK_MS)) {
			(void) HAL_UART_AbortTransmit(link->huart);
			link->status->dma_restarts++;
			__disable_irq();
			link->tx_busy = false;
			start_tx(link);
			__set_PRIMASK(primask);
		}

		// rececao parada por algum motivo (erro sem callback): rearmar
		if (link->huart->RxState == HAL_UART_STATE_READY) {
			arm_rx(link);
		}
	}
}
