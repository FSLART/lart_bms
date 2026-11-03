/*
 * can_bus.c
 *
 * Ver can_bus.h. Rings de um produtor e um consumidor: o RX e escrito na
 * interrupcao e lido no main loop; o TX e todo do main loop (nao ha
 * interrupcao de TX), por isso nao ha corridas entre os dois.
 */

#include "can_bus.h"
#include "main.h"
#include "powertrain_t26.h"
#include "handcart_t26.h"
#include <string.h>

#define RX_RING_SIZE        64u    // potencias de 2
#define TX_HIGH_SIZE        32u
#define TX_LOW_SIZE         128u   // 12 slaves x 7 tramas cabem com folga
#define MAILBOX_STUCK_MS    1000u  // mailboxes todas ocupadas este tempo = ninguem da ACK
#define QUEUE_FULL_FAULT_MS 500u
#define CAN2_FILTER_START   14u    // bancos 0..13 para o CAN1, 14..27 para o CAN2

typedef struct {
	can_frame_t *frames;
	uint16_t size;
	volatile uint16_t head;   // so o produtor escreve
	volatile uint16_t tail;   // so o consumidor escreve
} frame_ring_t;

static can_frame_t rx_storage[CAN_BUS_COUNT][RX_RING_SIZE];
static can_frame_t tx_high_storage[CAN_BUS_COUNT][TX_HIGH_SIZE];
static can_frame_t tx_low_storage[CAN_BUS_COUNT][TX_LOW_SIZE];
static frame_ring_t rx_ring[CAN_BUS_COUNT];
static frame_ring_t tx_high[CAN_BUS_COUNT];
static frame_ring_t tx_low[CAN_BUS_COUNT];
static uint32_t last_free_ms[CAN_BUS_COUNT];

/* ================== RINGS ================== */

static void ring_init(frame_ring_t *ring, can_frame_t *storage, uint16_t size) {

	ring->frames = storage;
	ring->size = size;
	ring->head = 0;
	ring->tail = 0;
}

static uint16_t ring_count(const frame_ring_t *ring) {

	return (uint16_t) ((ring->head - ring->tail) & (ring->size - 1u));
}

static bool ring_push(frame_ring_t *ring, const can_frame_t *frame) {

	uint16_t head = ring->head;
	uint16_t next = (uint16_t) ((head + 1u) & (ring->size - 1u));

	if (next == ring->tail) {
		return false;
	}

	ring->frames[head] = *frame;
	__DMB();   // a trama fica escrita antes de o indice a publicar
	ring->head = next;
	return true;
}

static bool ring_peek(const frame_ring_t *ring, can_frame_t *frame) {

	if (ring->tail == ring->head) {
		return false;
	}

	*frame = ring->frames[ring->tail];
	return true;
}

static void ring_drop(frame_ring_t *ring) {

	__DMB();
	ring->tail = (uint16_t) ((ring->tail + 1u) & (ring->size - 1u));
}

/* ================== INIT ================== */

static CAN_HandleTypeDef* handle_of(uint8_t bus) {

	return (bus == CAN_BUS_1) ? &hcan1 : &hcan2;
}

static bms_can_t* status_of(uint8_t bus) {

	return (bus == CAN_BUS_1) ? &bms.can1 : &bms.can2;
}

/* 16 TQ: 1 + BS1 13 + BS2 2 -> amostragem a 87,5 %. PCLK1 = 32 MHz da
 * prescalers inteiros para 125/250/500/1000 kbit/s. Se nao der certo fica a
 * temporizacao do CubeMX */
static void set_bitrate(CAN_HandleTypeDef *hcan, uint16_t kbps) {

	uint32_t pclk1_hz = HAL_RCC_GetPCLK1Freq();
	uint32_t bitrate = (uint32_t) kbps * 1000u;
	uint32_t prescaler = (bitrate > 0u) ? (pclk1_hz / (bitrate * 16u)) : 0u;

	if ((prescaler == 0u) || (prescaler > 1024u) || ((prescaler * bitrate * 16u) != pclk1_hz)) {
		return;
	}

	hcan->Init.Prescaler = prescaler;
	hcan->Init.SyncJumpWidth = CAN_SJW_1TQ;
	hcan->Init.TimeSeg1 = CAN_BS1_13TQ;
	hcan->Init.TimeSeg2 = CAN_BS2_2TQ;
	(void) HAL_CAN_Init(hcan);
}

// bitrate que ficou de facto no hardware (para o JSON)
static uint16_t actual_kbps(const CAN_HandleTypeDef *hcan) {

	uint32_t quanta = 1u + ((hcan->Init.TimeSeg1 >> CAN_BTR_TS1_Pos) + 1u) + ((hcan->Init.TimeSeg2 >> CAN_BTR_TS2_Pos) + 1u);

	return (uint16_t) (HAL_RCC_GetPCLK1Freq() / (hcan->Init.Prescaler * quanta) / 1000u);
}

static uint16_t std_list(uint32_t id) {

	return (uint16_t) ((id & 0x7FFu) << 5);   // STDID[10:0] RTR IDE EXID[17:15]
}

static void filter_std(CAN_HandleTypeDef *hcan, uint32_t bank, uint32_t id1, uint32_t id2, uint32_t id3, uint32_t id4) {

	CAN_FilterTypeDef filter = { 0 };

	filter.FilterBank = bank;
	filter.FilterMode = CAN_FILTERMODE_IDLIST;
	filter.FilterScale = CAN_FILTERSCALE_16BIT;
	filter.FilterIdLow = std_list(id1);
	filter.FilterMaskIdLow = std_list(id2);
	filter.FilterIdHigh = std_list(id3);
	filter.FilterMaskIdHigh = std_list(id4);
	filter.FilterFIFOAssignment = CAN_RX_FIFO0;
	filter.FilterActivation = ENABLE;
	filter.SlaveStartFilterBank = CAN2_FILTER_START;
	(void) HAL_CAN_ConfigFilter(hcan, &filter);
}

static void filter_ext(CAN_HandleTypeDef *hcan, uint32_t bank, uint32_t id1, uint32_t id2) {

	CAN_FilterTypeDef filter = { 0 };
	uint32_t first = (id1 << 3) | CAN_ID_EXT;    // EXID[28:0] IDE RTR 0
	uint32_t second = (id2 << 3) | CAN_ID_EXT;

	filter.FilterBank = bank;
	filter.FilterMode = CAN_FILTERMODE_IDLIST;
	filter.FilterScale = CAN_FILTERSCALE_32BIT;
	filter.FilterIdHigh = first >> 16;
	filter.FilterIdLow = first & 0xFFFFu;
	filter.FilterMaskIdHigh = second >> 16;
	filter.FilterMaskIdLow = second & 0xFFFFu;
	filter.FilterFIFOAssignment = CAN_RX_FIFO0;
	filter.FilterActivation = ENABLE;
	filter.SlaveStartFilterBank = CAN2_FILTER_START;
	(void) HAL_CAN_ConfigFilter(hcan, &filter);
}

static void start(uint8_t bus) {

	CAN_HandleTypeDef *hcan = handle_of(bus);
	bms_can_t *status = status_of(bus);

	status->started = (HAL_CAN_Start(hcan) == HAL_OK);
	if (status->started) {
		(void) HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
	}
}

void CanBus_Init(void) {

	for (uint8_t bus = 0; bus < CAN_BUS_COUNT; bus++) {
		ring_init(&rx_ring[bus], rx_storage[bus], RX_RING_SIZE);
		ring_init(&tx_high[bus], tx_high_storage[bus], TX_HIGH_SIZE);
		ring_init(&tx_low[bus], tx_low_storage[bus], TX_LOW_SIZE);
		memset(status_of(bus), 0, sizeof(bms_can_t));
		last_free_ms[bus] = HAL_GetTick();
	}

	set_bitrate(&hcan1, bms.config.can1_kbps);
	set_bitrate(&hcan2, bms.config.can2_kbps);
	bms.can1.kbps = actual_kbps(&hcan1);
	bms.can2.kbps = actual_kbps(&hcan2);

	// CAN1: VCU, programador e ISA do pack
	filter_std(&hcan1, 0, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID, POWERTRAIN_T26_VCU_STATES_FRAME_ID,
			POWERTRAIN_T26_IVT_MSG_RESPONSE_FRAME_ID);
	filter_std(&hcan1, 1, POWERTRAIN_T26_IVT_MSG_RESULT_I_FRAME_ID, POWERTRAIN_T26_IVT_MSG_RESULT_U1_FRAME_ID, POWERTRAIN_T26_IVT_MSG_RESULT_U2_FRAME_ID,
			POWERTRAIN_T26_IVT_MSG_RESULT_U3_FRAME_ID);
	filter_std(&hcan1, 2, POWERTRAIN_T26_IVT_MSG_RESULT_T_FRAME_ID, POWERTRAIN_T26_IVT_MSG_RESULT_W_FRAME_ID, POWERTRAIN_T26_IVT_MSG_RESULT_AS_FRAME_ID,
			POWERTRAIN_T26_IVT_MSG_RESULT_WH_FRAME_ID);

	// CAN2: handcart, programador, ISA do handcart e o carregador (29 bits)
	filter_std(&hcan2, CAN2_FILTER_START, HANDCART_T26_HANDCART_SWITCH_FEEDBACK_FRAME_ID, POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID,
			HANDCART_T26_IVT_MSG_RESPONSE_FRAME_ID, HANDCART_T26_IVT_MSG_RESULT_I_FRAME_ID);
	filter_std(&hcan2, CAN2_FILTER_START + 1u, HANDCART_T26_IVT_MSG_RESULT_U1_FRAME_ID, HANDCART_T26_IVT_MSG_RESULT_U2_FRAME_ID,
			HANDCART_T26_IVT_MSG_RESULT_U3_FRAME_ID, HANDCART_T26_IVT_MSG_RESULT_T_FRAME_ID);
	filter_std(&hcan2, CAN2_FILTER_START + 2u, HANDCART_T26_IVT_MSG_RESULT_W_FRAME_ID, HANDCART_T26_IVT_MSG_RESULT_AS_FRAME_ID,
			HANDCART_T26_IVT_MSG_RESULT_WH_FRAME_ID, HANDCART_T26_IVT_MSG_RESULT_WH_FRAME_ID);
	filter_ext(&hcan2, CAN2_FILTER_START + 3u, HANDCART_T26_CHARGER_STATUS_P1000_FRAME_ID, HANDCART_T26_CHARGER_STATUS_P1000_FRAME_ID);

	start(CAN_BUS_1);
	start(CAN_BUS_2);
}

/* ================== RX (interrupcao) ================== */

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {

	uint8_t bus = (hcan->Instance == CAN1) ? CAN_BUS_1 : CAN_BUS_2;
	bms_can_t *status = status_of(bus);
	CAN_RxHeaderTypeDef header;
	can_frame_t frame;

	while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0u) {

		if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, frame.data) != HAL_OK) {
			break;
		}

		frame.extended = (header.IDE == CAN_ID_EXT);
		frame.id = frame.extended ? header.ExtId : header.StdId;
		frame.dlc = (uint8_t) ((header.DLC > 8u) ? 8u : header.DLC);

		if (ring_push(&rx_ring[bus], &frame)) {
			status->rx_frames++;
		} else {
			status->rx_dropped++;
		}
	}
}

bool CanBus_Receive(uint8_t bus, can_frame_t *frame) {

	if ((bus >= CAN_BUS_COUNT) || !ring_peek(&rx_ring[bus], frame)) {
		return false;
	}

	ring_drop(&rx_ring[bus]);
	return true;
}

/* ================== TX (main loop) ================== */

uint16_t CanBus_FreeSlots(uint8_t bus, bool high_priority) {

	const frame_ring_t *ring = high_priority ? &tx_high[bus] : &tx_low[bus];

	return (bus < CAN_BUS_COUNT) ? (uint16_t) (ring->size - 1u - ring_count(ring)) : 0u;
}

bool CanBus_Send(uint8_t bus, const can_frame_t *frame, bool high_priority) {

	if ((bus >= CAN_BUS_COUNT) || !status_of(bus)->started) {
		return false;
	}

	if (!ring_push(high_priority ? &tx_high[bus] : &tx_low[bus], frame)) {
		status_of(bus)->tx_dropped++;
		return false;
	}

	return true;
}

static void fill_mailboxes(uint8_t bus, uint32_t now_ms) {

	CAN_HandleTypeDef *hcan = handle_of(bus);
	bms_can_t *status = status_of(bus);

	while (HAL_CAN_GetTxMailboxesFreeLevel(hcan) > 0u) {

		frame_ring_t *queue = (ring_count(&tx_high[bus]) > 0u) ? &tx_high[bus] : &tx_low[bus];
		can_frame_t frame;
		CAN_TxHeaderTypeDef header = { 0 };
		uint32_t mailbox;

		if (!ring_peek(queue, &frame)) {
			break;
		}

		header.IDE = frame.extended ? CAN_ID_EXT : CAN_ID_STD;
		header.StdId = frame.extended ? 0u : (frame.id & 0x7FFu);
		header.ExtId = frame.extended ? (frame.id & 0x1FFFFFFFu) : 0u;
		header.RTR = CAN_RTR_DATA;
		header.DLC = frame.dlc;
		header.TransmitGlobalTime = DISABLE;

		if (HAL_CAN_AddTxMessage(hcan, &header, frame.data, &mailbox) != HAL_OK) {
			status->hal_errors++;
			break;
		}

		ring_drop(queue);
		status->tx_frames++;
	}

	// mailboxes todas presas (sem ACK, cabo solto): abortar e seguir
	if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) > 0u) {
		last_free_ms[bus] = now_ms;
	} else if ((now_ms - last_free_ms[bus]) > MAILBOX_STUCK_MS) {
		(void) HAL_CAN_AbortTxRequest(hcan, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
		status->tx_dropped += 3u;
		last_free_ms[bus] = now_ms;
	}
}

static void read_errors(uint8_t bus, uint32_t now_ms) {

	CAN_HandleTypeDef *hcan = handle_of(bus);
	bms_can_t *status = status_of(bus);
	uint32_t esr = hcan->Instance->ESR;
	bool full = (ring_count(&tx_high[bus]) >= (TX_HIGH_SIZE - 1u)) || (ring_count(&tx_low[bus]) >= (TX_LOW_SIZE - 1u));

	status->tx_errors = (uint8_t) ((esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
	status->rx_errors = (uint8_t) ((esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos);
	status->last_error_code = (uint8_t) ((esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos);
	status->bus_off = (esr & CAN_ESR_BOFF) != 0u;
	status->error_passive = (esr & CAN_ESR_EPVF) != 0u;
	status->tx_queue_depth = (uint16_t) (ring_count(&tx_high[bus]) + ring_count(&tx_low[bus]));
	if (status->tx_queue_depth > status->tx_queue_peak) {
		status->tx_queue_peak = status->tx_queue_depth;
	}

	if (!full) {
		status->full_since_ms = 0;
	} else if (status->full_since_ms == 0u) {
		status->full_since_ms = (now_ms != 0u) ? now_ms : 1u;
	}
}

static bool full_too_long(const bms_can_t *status, uint32_t now_ms) {

	return (status->full_since_ms != 0u) && ((now_ms - status->full_since_ms) > QUEUE_FULL_FAULT_MS);
}

void CanBus_Service(uint32_t now_ms, bool can2_required) {

	for (uint8_t bus = 0; bus < CAN_BUS_COUNT; bus++) {
		if (status_of(bus)->started) {
			fill_mailboxes(bus, now_ms);
			read_errors(bus, now_ms);
		}
	}

	// o CAN2 no carro nao tem ninguem: so conta enquanto se carrega
	bms.faults.can1_bus_off = bms.can1.bus_off;
	bms.faults.can2_bus_off = can2_required && bms.can2.bus_off;
	bms.faults.can_tx_queue_full = full_too_long(&bms.can1, now_ms) || (can2_required && full_too_long(&bms.can2, now_ms));
}
