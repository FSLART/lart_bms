/*
 * test_drivers.c - os drivers que falam com o HAL, no PC.
 *
 * O can_bus.c, o uart_link.c e o bsp.c compilam tal como estao, com o main.h
 * verdadeiro (pinos do CubeMX) e um HAL falso (Tests/hal, fake_hal.c) que
 * grava o que lhe pedem e faz de hardware: mailboxes e FIFO do bxCAN, DMA do
 * UART, a EEPROM 24FC08, o ADC, o TIM12 e a flash (mapeada no endereco
 * verdadeiro, 0x08000000, como as calibracoes de fabrica em 0x1FFF7A2A).
 * Correr com Tests/run_tests.sh.
 */

#include "fake_hal.h"
#include "main.h"            // o verdadeiro: pinos e handles do CubeMX
#include "brain.h"
#include "bsp.h"
#include "can_bus.h"
#include "uart_link.h"
#include "boot_layout.h"
#include "powertrain_t26.h"
#include "handcart_t26.h"
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

bms_t bms;

uint32_t adBmsTimerClockHz(void) {

	return 64000000u;   // TIM12 no APB1 x2, como no mcuWrapper
}

/* ================== CAN ================== */

static void can_start(uint16_t can1_kbps, uint16_t can2_kbps) {

	fake_hal_reset();
	memset(&bms, 0, sizeof(bms));
	bms.config.can1_kbps = can1_kbps;
	bms.config.can2_kbps = can2_kbps;
	hcan1.Init.Prescaler = 7;    // o que o CubeMX tinha
	hcan1.Init.TimeSeg1 = 5u << CAN_BTR_TS1_Pos;
	hcan1.Init.TimeSeg2 = 3u << CAN_BTR_TS2_Pos;
	hcan2.Init = hcan1.Init;
	CanBus_Init();
}

static can_frame_t frame(uint32_t id, bool extended, uint8_t dlc, uint8_t first) {

	can_frame_t out = { .id = id, .extended = extended, .dlc = dlc };

	for (uint8_t index = 0; index < 8u; index++) {
		out.data[index] = (uint8_t) (first + index);
	}
	return out;
}

static uint16_t list_id(uint32_t id) {

	return (uint16_t) (id << 5);
}

static void test_can_bitrates(void) {

	static const uint16_t rates[] = { 125, 250, 500, 1000 };
	static const uint32_t prescalers[] = { 16, 8, 4, 2 };

	for (uint8_t index = 0; index < 4u; index++) {
		can_start(rates[index], 500);
		CHECK(fake_can[0].init_calls == 1u);
		CHECK(fake_can[0].init.Prescaler == prescalers[index]);
		CHECK(fake_can[0].init.SyncJumpWidth == CAN_SJW_1TQ);
		CHECK(fake_can[0].init.TimeSeg1 == CAN_BS1_13TQ);
		CHECK(fake_can[0].init.TimeSeg2 == CAN_BS2_2TQ);
		CHECK(bms.can1.kbps == rates[index]);
		CHECK(bms.can2.kbps == 500u);
	}

	// um bitrate que nao da um prescaler inteiro fica com o do CubeMX
	can_start(300, 500);
	CHECK(fake_can[0].init_calls == 0u);
	CHECK(hcan1.Init.Prescaler == 7u);
	CHECK(bms.can1.kbps == (32000000u / (7u * (1u + 6u + 4u)) / 1000u));

	// PCLK1 diferente: 1000 kbit/s a 8 MHz nao da
	fake_hal_reset();
	memset(&bms, 0, sizeof(bms));
	fake_hal_pclk1_hz = 8000000u;
	bms.config.can1_kbps = 1000;
	bms.config.can2_kbps = 250;
	CanBus_Init();
	CHECK(fake_can[0].init_calls == 0u);
	CHECK((fake_can[1].init_calls == 1u) && (fake_can[1].init.Prescaler == 2u));
}

static void test_can_filters_and_start(void) {

	can_start(500, 500);

	// CAN1: 3 bancos em lista de 16 bits (0..2)
	CHECK(fake_can[0].filter_count == 3u);
	for (uint8_t index = 0; index < 3u; index++) {
		const CAN_FilterTypeDef *filter = &fake_can[0].filters[index];
		CHECK(filter->FilterBank == index);
		CHECK((filter->FilterMode == CAN_FILTERMODE_IDLIST) && (filter->FilterScale == CAN_FILTERSCALE_16BIT));
		CHECK((filter->FilterFIFOAssignment == CAN_RX_FIFO0) && (filter->FilterActivation == ENABLE));
		CHECK(filter->SlaveStartFilterBank == 14u);
	}
	CHECK(fake_can[0].filters[0].FilterIdLow == list_id(POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID));
	CHECK(fake_can[0].filters[0].FilterMaskIdLow == list_id(POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID));
	CHECK(fake_can[0].filters[0].FilterIdHigh == list_id(POWERTRAIN_T26_VCU_STATES_FRAME_ID));
	CHECK(fake_can[0].filters[0].FilterMaskIdHigh == list_id(POWERTRAIN_T26_IVT_MSG_RESPONSE_FRAME_ID));
	CHECK(fake_can[0].filters[1].FilterIdLow == list_id(0x521u));
	CHECK(fake_can[0].filters[2].FilterMaskIdHigh == list_id(0x528u));

	// CAN2: bancos 14..17, o ultimo em lista de 32 bits para o carregador (29 bits)
	CHECK(fake_can[1].filter_count == 4u);
	CHECK(fake_can[1].filters[0].FilterBank == 14u);
	CHECK(fake_can[1].filters[0].FilterIdLow == list_id(HANDCART_T26_HANDCART_SWITCH_FEEDBACK_FRAME_ID));
	CHECK(fake_can[1].filters[0].FilterMaskIdLow == list_id(0x0DCu));
	CHECK(fake_can[1].filters[2].FilterMaskIdHigh == list_id(0x601u));   // ENTER_BOOT do bootloader

	const CAN_FilterTypeDef *charger = &fake_can[1].filters[3];
	uint32_t expected = (HANDCART_T26_CHARGER_STATUS_P1000_FRAME_ID << 3) | CAN_ID_EXT;

	CHECK((charger->FilterBank == 17u) && (charger->FilterScale == CAN_FILTERSCALE_32BIT));
	CHECK((charger->FilterIdHigh == (expected >> 16)) && (charger->FilterIdLow == (expected & 0xFFFFu)));

	// arranque: interrupcao da FIFO 0
	CHECK(bms.can1.started && bms.can2.started);
	CHECK((fake_can[0].notifications & CAN_IT_RX_FIFO0_MSG_PENDING) != 0u);

	// um bus que nao arranca nao aceita tramas
	fake_hal_reset();
	memset(&bms, 0, sizeof(bms));
	bms.config.can1_kbps = bms.config.can2_kbps = 500;
	fake_can[1].start_result = HAL_ERROR;
	CanBus_Init();
	CHECK(bms.can1.started && !bms.can2.started);
	CHECK(fake_can[1].notifications == 0u);

	can_frame_t out = frame(0x123, false, 8, 0);

	CHECK(!CanBus_Send(CAN_BUS_2, &out, true));
	CHECK(CanBus_Send(CAN_BUS_1, &out, true));
	CHECK(!CanBus_Send(2, &out, true));
}

static void test_can_receive(void) {

	uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	can_frame_t in;

	can_start(500, 500);
	CHECK(!CanBus_Receive(CAN_BUS_1, &in));

	fake_can_rx_push(0, 0x083, false, data, 1);
	fake_can_rx_push(0, 0x18FF50E5u, true, data, 8);
	fake_can_rx_push(0, 0x521, false, data, 15);   // DLC > 8 (CAN FD no bus?) fica em 8
	HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);

	CHECK(bms.can1.rx_frames == 3u);
	CHECK(CanBus_Receive(CAN_BUS_1, &in) && (in.id == 0x083u) && !in.extended && (in.dlc == 1u) && (in.data[0] == 1u));
	CHECK(CanBus_Receive(CAN_BUS_1, &in) && (in.id == 0x18FF50E5u) && in.extended && (in.dlc == 8u) && (in.data[7] == 8u));
	CHECK(CanBus_Receive(CAN_BUS_1, &in) && (in.dlc == 8u));
	CHECK(!CanBus_Receive(CAN_BUS_1, &in));
	CHECK(!CanBus_Receive(CAN_BUS_2, &in));
	CHECK(!CanBus_Receive(5, &in));

	// a interrupcao do CAN2 vai para o ring do CAN2
	fake_can_rx_push(1, 0x084, false, data, 1);
	HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);
	CHECK(CanBus_Receive(CAN_BUS_2, &in) && (in.id == 0x084u));
	CHECK(bms.can2.rx_frames == 1u);

	// ring de 64: cabem 63, o resto conta como perdido
	for (uint8_t index = 0; index < 70u; index++) {
		data[0] = index;
		fake_can_rx_push(0, 0x700u + index, false, data, 8);
	}
	HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
	CHECK(bms.can1.rx_dropped == 7u);
	for (uint8_t index = 0; index < 63u; index++) {
		if (!CanBus_Receive(CAN_BUS_1, &in) || (in.id != (0x700u + index))) {
			CHECK(false);
			break;
		}
	}
	CHECK(!CanBus_Receive(CAN_BUS_1, &in));

	// erro do HAL a ler: para (sem ficar preso)
	fake_can_rx_push(0, 0x083, false, data, 1);
	fake_can[0].get_result = HAL_ERROR;
	HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
	CHECK(!CanBus_Receive(CAN_BUS_1, &in));
}

static void test_can_transmit_priorities(void) {

	can_start(500, 500);
	fake_can[0].auto_complete = false;   // as mailboxes ficam ocupadas ate o teste as libertar

	can_frame_t low = frame(0x600, false, 8, 0x10);
	can_frame_t high = frame(0x701, false, 8, 0x20);
	can_frame_t extended = frame(0x1806E5F4u, true, 8, 0x30);

	CHECK(CanBus_Send(CAN_BUS_1, &low, false));
	CHECK(CanBus_Send(CAN_BUS_1, &low, false));
	CHECK(CanBus_Send(CAN_BUS_1, &high, true));
	CHECK(CanBus_Send(CAN_BUS_1, &extended, true));
	CHECK(CanBus_FreeSlots(CAN_BUS_1, true) == 29u);
	CHECK(CanBus_FreeSlots(CAN_BUS_1, false) == 125u);
	CHECK(CanBus_FreeSlots(7, false) == 0u);

	CanBus_Service(10, false);
	// 3 mailboxes: as duas de alta prioridade primeiro, depois uma de baixa
	CHECK(fake_can[0].tx_count == 3u);
	CHECK((fake_can[0].tx_log[0].header.StdId == 0x701u) && (fake_can[0].tx_log[0].header.IDE == CAN_ID_STD));
	CHECK((fake_can[0].tx_log[1].header.ExtId == 0x1806E5F4u) && (fake_can[0].tx_log[1].header.IDE == CAN_ID_EXT) && (fake_can[0].tx_log[1].header.StdId == 0u));
	CHECK(fake_can[0].tx_log[2].header.StdId == 0x600u);
	CHECK((fake_can[0].tx_log[0].header.RTR == CAN_RTR_DATA) && (fake_can[0].tx_log[0].header.DLC == 8u) && (fake_can[0].tx_log[0].header.TransmitGlobalTime == DISABLE));
	CHECK(fake_can[0].tx_log[0].data[0] == 0x20u);
	CHECK(bms.can1.tx_frames == 3u);
	CHECK(bms.can1.tx_queue_depth == 1u);

	fake_can_complete(0);
	CanBus_Service(20, false);
	CHECK((fake_can[0].tx_count == 4u) && (fake_can[0].tx_log[3].header.StdId == 0x600u));
	CHECK(bms.can1.tx_queue_depth == 0u);
	CHECK(bms.can1.tx_queue_peak == 1u);

	// o HAL recusa: a trama fica na fila para a volta seguinte
	fake_can_complete(0);
	CHECK(CanBus_Send(CAN_BUS_1, &high, true));
	fake_can[0].add_result = HAL_ERROR;
	CanBus_Service(30, false);
	CHECK((bms.can1.hal_errors == 1u) && (fake_can[0].tx_count == 4u));
	fake_can[0].add_result = HAL_OK;
	CanBus_Service(40, false);
	CHECK(fake_can[0].tx_count == 5u);

	// o ID e mascarado a 11 / 29 bits
	fake_can_complete(0);
	can_frame_t wide = frame(0xFFFFu, false, 2, 0);

	CHECK(CanBus_Send(CAN_BUS_1, &wide, false));
	CanBus_Service(50, false);
	CHECK(fake_can[0].tx_log[5].header.StdId == 0x7FFu);
}

static void test_can_queue_full_and_stuck(void) {

	can_start(500, 500);
	fake_can[0].auto_complete = false;
	fake_can[1].auto_complete = false;

	can_frame_t out = frame(0x600, false, 8, 0);

	// a fila de baixa prioridade leva 127: a seguinte e perdida
	for (uint16_t index = 0; index < 127u; index++) {
		CHECK(CanBus_Send(CAN_BUS_1, &out, false));
	}
	CHECK(!CanBus_Send(CAN_BUS_1, &out, false));
	CHECK(bms.can1.tx_dropped == 1u);

	// mailboxes presas (ninguem da ACK): a fila enche e o fault so aparece depois de 500 ms
	CanBus_Service(1000, false);          // 3 vao para as mailboxes e la ficam
	CHECK(!bms.faults.can_tx_queue_full);
	for (uint8_t index = 0; index < 3u; index++) {
		(void) CanBus_Send(CAN_BUS_1, &out, false);
	}
	CanBus_Service(1100, false);
	CHECK(!bms.faults.can_tx_queue_full);
	CanBus_Service(1700, false);
	CHECK(bms.faults.can_tx_queue_full);

	// mais de 1 s sem uma mailbox livre: aborta as tres e enche-as outra vez
	CHECK(fake_can[0].abort_calls == 0u);
	CanBus_Service(2000, false);
	CHECK(fake_can[0].abort_calls == 0u);
	CanBus_Service(2001, false);
	CHECK(fake_can[0].abort_calls == 1u);
	CHECK(fake_can[0].abort_mask == (CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2));
	CHECK(bms.can1.tx_dropped == 4u);
	CHECK(fake_can[0].tx_count == 6u);

	// o bus volta: esvazia e o fault sai
	fake_can[0].auto_complete = true;
	fake_can_complete(0);
	for (uint8_t round = 0; round < 10u; round++) {
		CanBus_Service(2100u + round, false);
	}
	CHECK(bms.can1.tx_queue_depth == 0u);
	CHECK(!bms.faults.can_tx_queue_full);

	// CAN2 cheio so conta a carregar (a telemetria continua a chegar: a fila e reposta)
	for (uint16_t index = 0; index < 130u; index++) {
		(void) CanBus_Send(CAN_BUS_2, &out, false);
	}
	CanBus_Service(3000, false);
	while (CanBus_Send(CAN_BUS_2, &out, false)) {
	}
	CanBus_Service(3010, false);
	CanBus_Service(3600, false);
	CHECK(!bms.faults.can_tx_queue_full);
	CanBus_Service(3700, true);
	CHECK(bms.faults.can_tx_queue_full);
}

/* Bus saudavel mas sempre com fila (carga alta): as mailboxes saem e voltam a
 * encher a cada volta. Nao pode abortar nada (o driver antigo abortava 3
 * tramas por segundo neste caso) */
static void test_can_busy_bus_is_not_stuck(void) {

	can_start(500, 500);
	fake_can[0].auto_complete = false;

	can_frame_t out = frame(0x600, false, 8, 0);

	for (uint32_t now = 0; now < 5000u; now += 10u) {
		for (uint8_t index = 0; index < 6u; index++) {
			(void) CanBus_Send(CAN_BUS_1, &out, false);
		}
		CanBus_Service(now, false);
		fake_can_complete(0);   // as 3 sairam ate a volta seguinte
	}
	CHECK(fake_can[0].abort_calls == 0u);
	CHECK(bms.can1.tx_frames == (500u * 3u));
}

static void test_can_error_registers(void) {

	can_start(500, 500);
	fake_can1_regs.ESR = (200u << CAN_ESR_TEC_Pos) | (17u << CAN_ESR_REC_Pos) | (3u << CAN_ESR_LEC_Pos) | CAN_ESR_EPVF;
	fake_can2_regs.ESR = CAN_ESR_BOFF | (255u << CAN_ESR_TEC_Pos);
	CanBus_Service(10, false);

	CHECK((bms.can1.tx_errors == 200u) && (bms.can1.rx_errors == 17u) && (bms.can1.last_error_code == 3u));
	CHECK(bms.can1.error_passive && !bms.can1.bus_off);
	CHECK(bms.can2.bus_off);
	CHECK(!bms.faults.can1_bus_off);
	CHECK(!bms.faults.can2_bus_off);   // o CAN2 so importa a carregar
	CanBus_Service(20, true);
	CHECK(bms.faults.can2_bus_off);

	fake_can1_regs.ESR = CAN_ESR_BOFF;
	CanBus_Service(30, false);
	CHECK(bms.faults.can1_bus_off);
	fake_can1_regs.ESR = 0;
	CanBus_Service(40, false);
	CHECK(!bms.faults.can1_bus_off && !bms.can1.error_passive);
}

/* ================== UART ================== */

static void uart_start(void) {

	fake_hal_reset();
	memset(&bms, 0, sizeof(bms));
	UartLink_Init();
}

static void test_uart_tx_ring_and_dma(void) {

	uart_start();
	CHECK((fake_uart[0].rx_arms == 1u) && (fake_uart[1].rx_arms == 1u));
	CHECK(UartLink_TxIdle(UART_LINK_1));
	CHECK(UartLink_Free(UART_LINK_1) == 8191u);
	CHECK(UartLink_Free(9) == 0u);
	CHECK(!UartLink_Write(UART_LINK_1, "x", 0));
	CHECK(!UartLink_Write(4, "x", 1));
	CHECK(!UartLink_TxIdle(4));

	CHECK(UartLink_Write(UART_LINK_1, "hello ", 6));
	CHECK(UartLink_Write(UART_LINK_1, "world\r\n", 7));
	CHECK(!UartLink_TxIdle(UART_LINK_1));
	CHECK(bms.uart1.frames_sent == 2u);
	CHECK(bms.uart1.ring_peak_bytes == 13u);

	// o main loop arranca o DMA com tudo o que for contiguo
	UartLink_Service(0);
	CHECK((fake_uart[0].tx_calls == 1u) && (fake_uart[0].tx_size == 13u));
	CHECK(fake_hal_primask == 0);              // as interrupcoes voltam como estavam
	CHECK(fake_hal_irq_disables >= 1u);
	UartLink_Service(1);
	CHECK(fake_uart[0].tx_calls == 1u);        // ocupado: nao arranca outro

	// o que chega entretanto vai no bloco seguinte, arrancado pelo callback
	CHECK(UartLink_Write(UART_LINK_1, "next", 4));
	fake_uart_dma_done(0);
	CHECK((fake_uart[0].tx_calls == 2u) && (fake_uart[0].tx_size == 4u));
	fake_uart_dma_done(0);
	CHECK((fake_uart[0].sent_length == 17u) && (memcmp(fake_uart[0].sent, "hello world\r\nnext", 17) == 0));
	CHECK(bms.uart1.tx_bytes == 17u);
	CHECK(UartLink_TxIdle(UART_LINK_1));
	CHECK(fake_uart[1].tx_calls == 0u);        // o UART2 nao foi tocado
}

static void test_uart_wraparound_and_full(void) {

	static uint8_t block[4000];

	uart_start();
	for (uint32_t index = 0; index < sizeof(block); index++) {
		block[index] = (uint8_t) index;
	}

	// 4000 + 4000 B e saem: a cabeca fica em 8000, perto do fim do ring de 8192
	CHECK(UartLink_Write(UART_LINK_2, block, 4000));
	CHECK(UartLink_Write(UART_LINK_2, block, 4000));
	UartLink_Service(0);
	fake_uart_dma_done(1);
	CHECK(fake_uart[1].sent_length == 8000u);

	// 500 B dao a volta: dois blocos de DMA (ate ao fim, depois do inicio)
	CHECK(UartLink_Write(UART_LINK_2, block, 500));
	UartLink_Service(10);
	CHECK(fake_uart[1].tx_size == 192u);
	fake_uart_dma_done(1);
	CHECK(fake_uart[1].tx_size == 308u);
	fake_uart_dma_done(1);
	CHECK(fake_uart[1].sent_length == 8500u);
	CHECK(memcmp(&fake_uart[1].sent[8000], block, 500) == 0);

	// tudo ou nada: o que nao cabe e descartado inteiro
	CHECK(UartLink_Write(UART_LINK_2, block, 4000));
	CHECK(UartLink_Write(UART_LINK_2, block, 4000));
	CHECK(UartLink_Free(UART_LINK_2) == 191u);
	CHECK(!UartLink_Write(UART_LINK_2, block, 192));
	CHECK(bms.uart2.frames_dropped == 1u);
	CHECK(UartLink_Write(UART_LINK_2, block, 191));
	CHECK(UartLink_Free(UART_LINK_2) == 0u);
}

static void test_uart_dma_errors(void) {

	uart_start();

	// o HAL recusa o DMA: conta e tenta outra vez na volta seguinte
	fake_uart[0].tx_result = HAL_BUSY;
	CHECK(UartLink_Write(UART_LINK_1, "abc", 3));
	UartLink_Service(0);
	CHECK(bms.uart1.dma_errors == 1u);
	fake_uart[0].tx_result = HAL_OK;
	UartLink_Service(1);
	CHECK((fake_uart[0].tx_calls == 2u) && (fake_uart[0].tx_size == 3u));

	// DMA preso mais de 2 s: abort e o mesmo bloco outra vez
	UartLink_Service(2000);
	CHECK(fake_uart[0].abort_calls == 0u);
	UartLink_Service(2002);
	CHECK((fake_uart[0].abort_calls == 1u) && (bms.uart1.dma_restarts == 1u));
	CHECK((fake_uart[0].tx_calls == 3u) && (fake_uart[0].tx_size == 3u));

	// erro de TX do DMA (gState pronto): repete o bloco
	huart1.gState = HAL_UART_STATE_READY;
	HAL_UART_ErrorCallback(&huart1);
	CHECK((bms.uart1.dma_errors == 2u) && (fake_uart[0].tx_calls == 4u));
	fake_uart_dma_done(0);
	CHECK((fake_uart[0].sent_length == 3u) && (memcmp(fake_uart[0].sent, "abc", 3) == 0));

	// erro so de RX: rearma a rececao
	uint32_t arms = fake_uart[0].rx_arms;

	huart1.RxState = HAL_UART_STATE_READY;
	HAL_UART_ErrorCallback(&huart1);
	CHECK(fake_uart[0].rx_arms == (arms + 1u));
	CHECK(bms.uart1.dma_errors == 2u);   // sem TX a meio nao conta

	// callbacks de um UART que nao e nosso
	UART_HandleTypeDef other = { .id = 3, .gState = HAL_UART_STATE_READY, .RxState = HAL_UART_STATE_READY };

	HAL_UART_TxCpltCallback(&other);
	HAL_UARTEx_RxEventCallback(&other, 1);
	HAL_UART_ErrorCallback(&other);
	CHECK(bms.uart1.tx_bytes == 3u);
}

static void test_uart_receive(void) {

	uint8_t out[600];
	char text[64];

	uart_start();
	CHECK(UartLink_Read(UART_LINK_1, out, sizeof(out)) == 0u);
	CHECK(UartLink_Read(3, out, sizeof(out)) == 0u);

	fake_uart_receive(0, "status\r\n", 8);
	CHECK(fake_uart[0].rx_arms == 2u);   // rearmado logo
	CHECK(bms.uart1.rx_bytes == 8u);
	CHECK((UartLink_Read(UART_LINK_1, out, 3) == 3u) && (memcmp(out, "sta", 3) == 0));
	CHECK((UartLink_Read(UART_LINK_1, out, sizeof(out)) == 5u) && (memcmp(out, "tus\r\n", 5) == 0));

	// o ring de 512 enche: cada rececao que perde bytes conta um overrun
	memset(text, 'a', sizeof(text));
	for (uint8_t index = 0; index < 9u; index++) {
		fake_uart_receive(1, text, 64);
	}
	CHECK(bms.uart2.rx_overruns == 2u);
	CHECK(bms.uart2.rx_bytes == (9u * 64u));
	CHECK(UartLink_Read(UART_LINK_2, out, sizeof(out)) == 511u);

	// o HAL parou a rececao sem callback: o Service rearma
	uint32_t arms = fake_uart[0].rx_arms;

	huart1.RxState = HAL_UART_STATE_READY;
	UartLink_Service(5);
	CHECK(fake_uart[0].rx_arms == (arms + 1u));
	UartLink_Service(6);
	CHECK(fake_uart[0].rx_arms == (arms + 1u));   // ja armado
}

/* ================== BSP ================== */

static bool contactor_closed(const fake_gpio_write_t *entry) {

	return entry->state == GPIO_PIN_RESET;   // SET = aberto
}

static void test_bsp_inputs(void) {

	fake_hal_reset();
	CHECK(Bsp_ReadInputs() == 0u);
	fake_gpioc.IDR = MCU_AIR_positivo_FB_Pin | MCU_SDC_FB_Pin;
	CHECK(Bsp_ReadInputs() == (BSP_IN_AIR_POS | BSP_IN_SDC));
	fake_gpioc.IDR = MCU_AIR_negativo_FB_Pin | MCU_DISCH_FB_Pin;
	fake_gpioa.IDR = MCU_PRE_FB_Pin;
	CHECK(Bsp_ReadInputs() == (BSP_IN_AIR_NEG | BSP_IN_DISCH | BSP_IN_PRE));

	CHECK(Bsp_InputFromPin(MCU_AIR_positivo_FB_Pin) == BSP_IN_AIR_POS);
	CHECK(Bsp_InputFromPin(MCU_AIR_negativo_FB_Pin) == BSP_IN_AIR_NEG);
	CHECK(Bsp_InputFromPin(MCU_PRE_FB_Pin) == BSP_IN_PRE);
	CHECK(Bsp_InputFromPin(MCU_DISCH_FB_Pin) == BSP_IN_DISCH);
	CHECK(Bsp_InputFromPin(MCU_SDC_FB_Pin) == BSP_IN_SDC);
	CHECK(Bsp_InputFromPin(GPIO_PIN_0) == 0u);
}

static void test_bsp_contactors_open_before_close(void) {

	for (uint8_t request = 0; request < 8u; request++) {

		bool air_pos = (request & 1u) != 0u;
		bool air_neg = (request & 2u) != 0u;
		bool precharge = (request & 4u) != 0u;

		fake_hal_reset();
		Bsp_SetContactors(air_pos, air_neg, precharge);

		// cada pino uma vez, e todos os "abrir" antes de qualquer "fechar"
		CHECK(fake_gpio_log_count == 3u);
		bool closing = false;
		for (uint32_t index = 0; index < fake_gpio_log_count; index++) {
			if (contactor_closed(&fake_gpio_log[index])) {
				closing = true;
			} else if (closing) {
				CHECK(false);
			}
		}
		CHECK(((CONTACT_AIR_positivo_GPIO_Port->ODR & CONTACT_AIR_positivo_Pin) == 0u) == air_pos);
		CHECK(((CONTACT_AIR_negativo_GPIO_Port->ODR & CONTACT_AIR_negativo_Pin) == 0u) == air_neg);
		CHECK(((CONTACT_PRE_GPIO_Port->ODR & CONTACT_PRE_Pin) == 0u) == precharge);
	}

	// a fechar tudo: AIR- primeiro, depois pre-carga, AIR+ por ultimo
	fake_hal_reset();
	Bsp_SetContactors(true, true, true);
	CHECK((fake_gpio_log[0].pin == CONTACT_AIR_negativo_Pin) && (fake_gpio_log[1].pin == CONTACT_PRE_Pin) && (fake_gpio_log[2].pin == CONTACT_AIR_positivo_Pin));
}

static void test_bsp_ams_error_heartbeat_reset(void) {

	fake_hal_reset();
	Bsp_SetAmsError(true);
	CHECK((AMS_ERROR_GPIO_Port->ODR & AMS_ERROR_Pin) != 0u);   // SET = erro
	Bsp_SetAmsError(false);
	CHECK((AMS_ERROR_GPIO_Port->ODR & AMS_ERROR_Pin) == 0u);

	Bsp_ToggleHeartbeat();
	CHECK((MCU_HEARTBEAT_GPIO_Port->ODR & MCU_HEARTBEAT_Pin) != 0u);
	Bsp_ToggleHeartbeat();
	CHECK((MCU_HEARTBEAT_GPIO_Port->ODR & MCU_HEARTBEAT_Pin) == 0u);

	// o reset so vem depois de tudo aberto e da linha em erro
	fake_hal_reset();
	Bsp_SetContactors(true, true, false);
	Bsp_SetAmsError(false);
	Bsp_SystemReset();
	CHECK(fake_hal_resets == 1u);
	CHECK((AMS_ERROR_GPIO_Port->ODR & AMS_ERROR_Pin) != 0u);
	CHECK((CONTACT_AIR_positivo_GPIO_Port->ODR & (CONTACT_AIR_positivo_Pin | CONTACT_AIR_negativo_Pin)) == (CONTACT_AIR_positivo_Pin | CONTACT_AIR_negativo_Pin));
	CHECK((CONTACT_PRE_GPIO_Port->ODR & CONTACT_PRE_Pin) != 0u);
	CHECK(fake_gpio_log[fake_gpio_log_count - 1u].sequence < fake_reset_sequence);

	fake_iwdg_regs.KR = 0;
	Bsp_IwdgRefresh();
	CHECK(fake_iwdg_regs.KR == 0xAAAAu);
}

static void test_bsp_fan(void) {

	fake_hal_reset();
	Bsp_FanInit(25000);   // 64 MHz / 25 kHz = 2560 ticks
	CHECK((fake_pwm_stops == 1u) && (fake_pwm_starts == 1u));
	CHECK((htim12.Instance->PSC == 0u) && (htim12.Instance->ARR == 2559u) && (htim12.Init.Period == 2559u));
	CHECK((htim12.Instance->CCR1 == 0u) && (htim12.Instance->EGR == TIM_EGR_UG));

	Bsp_SetFanPwm(255);
	CHECK(htim12.Instance->CCR1 == 2560u);
	Bsp_SetFanPwm(128);
	CHECK(htim12.Instance->CCR1 == (2560u * 128u) / 255u);
	Bsp_SetFanPwm(0);
	CHECK(htim12.Instance->CCR1 == 0u);

	// frequencia baixa: o prescaler sobe para o ARR caber em 16 bits
	Bsp_FanInit(25);
	CHECK(htim12.Instance->PSC == 39u);
	CHECK(htim12.Instance->ARR == ((2560000u / 40u) - 1u));
	CHECK(htim12.Instance->ARR <= 0xFFFFu);

	// 0 Hz na config: 1 kHz
	Bsp_FanInit(0);
	CHECK((htim12.Instance->PSC == 0u) && (htim12.Instance->ARR == 63999u));
}

static void test_bsp_adc(void) {

	uint16_t temp;
	uint16_t vref;
	uint16_t current;

	fake_hal_reset();
	Bsp_AdcStart();
	CHECK((fake_tim_base_starts == 1u) && (fake_adc_dma_buffer != NULL) && (fake_adc_dma_length == 96u));

	// buffer a zeros: sem VREFINT nao ha medida
	CHECK(!Bsp_AdcAverage(&temp, &vref, &current));

	// canais intercalados TEMP, VREFINT, IN14
	for (uint32_t sample = 0; sample < 32u; sample++) {
		fake_adc_dma_buffer[(sample * 3u) + 0u] = (uint16_t) (940u + (sample & 1u) * 2u);
		fake_adc_dma_buffer[(sample * 3u) + 1u] = 1500u;
		fake_adc_dma_buffer[(sample * 3u) + 2u] = (uint16_t) (2000u + sample);
	}
	CHECK(Bsp_AdcAverage(&temp, &vref, &current));
	CHECK((temp == 941u) && (vref == 1500u) && (current == 2015u));

	// o contador so conta o ADC1
	ADC_HandleTypeDef other = { &fake_adc2_regs };
	uint32_t before = Bsp_AdcConversions();

	HAL_ADC_ConvCpltCallback(&hadc1);
	HAL_ADC_ConvCpltCallback(&other);
	CHECK(Bsp_AdcConversions() == (before + 1u));
}

static void test_bsp_factory_calibration(void) {

	// RM0402: VREFINT_CAL em 0x1FFF7A2A, TS_CAL1 em 0x1FFF7A2C, TS_CAL2 em 0x1FFF7A2E
	*(volatile uint16_t*) (uintptr_t) 0x1FFF7A2Au = 1510u;
	*(volatile uint16_t*) (uintptr_t) 0x1FFF7A2Cu = 945u;
	*(volatile uint16_t*) (uintptr_t) 0x1FFF7A2Eu = 1220u;
	CHECK(Bsp_VrefintCal() == 1510u);
	CHECK(Bsp_TsCal1() == 945u);
	CHECK(Bsp_TsCal2() == 1220u);
}

static void test_bsp_eeprom(void) {

	uint8_t data[1024];
	uint8_t page[16];

	fake_hal_reset();
	for (uint32_t index = 0; index < 1024u; index++) {
		fake_eeprom.memory[index] = (uint8_t) (index * 7u);
	}

	CHECK(Bsp_EepromPresent());
	CHECK(fake_eeprom.last_device == 0xA0u);

	// cada bloco de 256 B tem o seu endereco I2C: uma leitura de 1 KB sao 4 leituras
	CHECK(Bsp_EepromRead(0, data, 1024));
	CHECK(fake_eeprom.reads == 4u);
	CHECK(fake_eeprom.last_device == 0xA6u);
	for (uint32_t index = 0; index < 1024u; index++) {
		if (data[index] != (uint8_t) (index * 7u)) {
			CHECK(false);
			break;
		}
	}

	// uma leitura que atravessa um bloco parte-se na fronteira
	fake_eeprom.reads = 0;
	CHECK(Bsp_EepromRead(250, data, 20));
	CHECK(fake_eeprom.reads == 2u);
	CHECK((fake_eeprom.last_device == 0xA2u) && (fake_eeprom.last_address == 0u) && (fake_eeprom.last_size == 14u));
	CHECK((data[0] == (uint8_t) (250u * 7u)) && (data[19] == (uint8_t) (269u * 7u)));

	fake_eeprom.fail_reads = true;
	CHECK(!Bsp_EepromRead(0, data, 4));
	fake_eeprom.fail_reads = false;

	// paginas de 16 B: nada de atravessar pagina, nem escrita vazia
	memset(page, 0x5A, sizeof(page));
	CHECK(Bsp_EepromWritePage(0x210, page, 16));
	CHECK((fake_eeprom.last_device == 0xA4u) && (fake_eeprom.last_address == 0x10u));
	CHECK((fake_eeprom.memory[0x210] == 0x5Au) && (fake_eeprom.memory[0x21F] == 0x5Au) && (fake_eeprom.memory[0x220] != 0x5Au));
	CHECK(Bsp_EepromWritePage(0x3F8, page, 8));
	uint32_t writes = fake_eeprom.writes;
	CHECK(!Bsp_EepromWritePage(0x3F8, page, 9));
	CHECK(!Bsp_EepromWritePage(0x101, page, 16));
	CHECK(!Bsp_EepromWritePage(0x100, page, 0));
	CHECK(fake_eeprom.writes == writes);
	fake_eeprom.fail_writes = true;
	CHECK(!Bsp_EepromWritePage(0x000, page, 4));

	// pronta e recuperacao do I2C
	CHECK(Bsp_EepromReady());
	fake_eeprom.present = false;
	CHECK(!Bsp_EepromReady());
	CHECK(!Bsp_EepromPresent());
	Bsp_EepromRecover();
	CHECK((fake_eeprom.deinit_calls == 1u) && (fake_eeprom.init_calls == 1u));
}

static void test_bsp_journal_flash(void) {

	uint8_t record[48];
	uint32_t address = BOOT_JOURNAL_A_ADDRESS + 96u;

	fake_hal_reset();
	memset((void*) (uintptr_t) BOOT_JOURNAL_A_ADDRESS, 0xFF, 2u * BOOT_JOURNAL_SIZE);
	for (uint32_t index = 0; index < sizeof(record); index++) {
		record[index] = (uint8_t) (index + 1u);
	}

	// a aplicacao nunca apaga
	CHECK(!BootFlash_EraseSector(BOOT_JOURNAL_A_ADDRESS));
	CHECK(!BootFlash_EraseSector(BOOT_JOURNAL_B_ADDRESS));

	CHECK(BootFlash_Program(address, record, sizeof(record)));
	CHECK(memcmp((const void*) (uintptr_t) address, record, sizeof(record)) == 0);
	CHECK((fake_flash.programs == 12u) && (fake_flash.unlocks == 1u) && (fake_flash.locks == 1u) && fake_flash.locked);
	CHECK((fake_flash.cleared_flags & (FLASH_FLAG_PGSERR | FLASH_FLAG_PGAERR | FLASH_FLAG_WRPERR)) == (FLASH_FLAG_PGSERR | FLASH_FLAG_PGAERR | FLASH_FLAG_WRPERR));
	CHECK((fake_flash.cache_event_count == 3u) && (memcmp(fake_flash.cache_events, "dre", 3) == 0));

	// so dentro do journal, palavras alinhadas
	uint32_t programs = fake_flash.programs;

	CHECK(!BootFlash_Program(BOOT_EXEC_ADDRESS, record, 4));
	CHECK(!BootFlash_Program(BOOT_JOURNAL_A_ADDRESS - 4u, record, 8));
	CHECK(!BootFlash_Program(BOOT_JOURNAL_B_ADDRESS + BOOT_JOURNAL_SIZE - 4u, record, 8));
	CHECK(!BootFlash_Program(address + 2u, record, 4));
	CHECK(!BootFlash_Program(address + 200u, record, 6));
	CHECK(fake_flash.programs == programs);

	// sem desbloquear nao escreve
	fake_flash.fail_unlock = true;
	CHECK(!BootFlash_Program(address + 200u, record, 4));
	fake_flash.fail_unlock = false;

	// uma palavra que falha para o resto e devolve false
	fake_flash.fail_program_at = (int32_t) fake_flash.programs + 1;
	CHECK(!BootFlash_Program(address + 200u, record, 12));
	CHECK(fake_flash.locked);

	// um bit que nao passa a 0: a verificacao apanha
	fake_flash.fail_program_at = -1;
	fake_flash.stuck_mask = 0x00000100u;
	CHECK(!BootFlash_Program(address + 400u, record, 4));
}

int main(void) {

	if (!fake_hal_map_memory()) {
		printf("sem memoria em 0x08000000 / 0x1FFF7000 (precisa de Linux): testes dos drivers saltados\n");
		return 0;
	}

	test_can_bitrates();
	test_can_filters_and_start();
	test_can_receive();
	test_can_transmit_priorities();
	test_can_queue_full_and_stuck();
	test_can_error_registers();
	test_can_busy_bus_is_not_stuck();
	test_uart_tx_ring_and_dma();
	test_uart_wraparound_and_full();
	test_uart_dma_errors();
	test_uart_receive();
	test_bsp_inputs();
	test_bsp_contactors_open_before_close();
	test_bsp_ams_error_heartbeat_reset();
	test_bsp_fan();
	test_bsp_adc();
	test_bsp_factory_calibration();
	test_bsp_eeprom();
	test_bsp_journal_flash();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
