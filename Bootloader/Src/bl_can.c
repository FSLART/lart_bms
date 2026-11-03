/*
 * bl_can.c
 *
 * Ver bl_can.h. Sequencia do RM0402 (bxCAN): sair do sleep, INRQ, bit
 * timing, filtros (os do CAN2 ficam nos registos do CAN1, a partir do banco
 * 14), largar o INRQ e esperar 11 bits recessivos.
 */

#include "stm32f4xx.h"
#include "bl_can.h"

#define MODE_TIMEOUT 400000u   // ~10 ms a 64 MHz
#define CAN2_FIRST_BANK 14u

static CAN_TypeDef* regs(uint8_t bus) {

	return (bus == BL_CAN1) ? CAN1 : CAN2;
}

static void pin_alternate(GPIO_TypeDef *port, uint32_t pin) {

	uint32_t shift = (pin & 7u) * 4u;

	port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << shift)) | (9u << shift);   // AF9 = CAN
	port->OSPEEDR |= (3u << (pin * 2u));
	port->PUPDR &= ~(3u << (pin * 2u));
	port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (2u << (pin * 2u));
}

static bool wait_ack(CAN_TypeDef *can, bool initialising) {

	uint32_t timeout = MODE_TIMEOUT;

	while ((((can->MSR & CAN_MSR_INAK) != 0u) != initialising) && (--timeout > 0u)) {
	}
	return ((can->MSR & CAN_MSR_INAK) != 0u) == initialising;
}

// banco de filtro: so tramas standard de dados com este ID, para a FIFO 0
static void set_filter(uint32_t bank, uint32_t id) {

	uint32_t bit = 1u << bank;

	CAN1->FA1R &= ~bit;
	CAN1->FM1R &= ~bit;       // mascara
	CAN1->FS1R |= bit;        // 32 bits
	CAN1->FFA1R &= ~bit;      // FIFO 0
	CAN1->sFilterRegister[bank].FR1 = id << 21;
	CAN1->sFilterRegister[bank].FR2 = (0x7FFu << 21) | CAN_RI0R_IDE | CAN_RI0R_RTR;
	CAN1->FA1R |= bit;
}

bool BlCan_Init(uint8_t bus, uint16_t kbps, uint32_t pclk1_hz, uint32_t request_id) {

	CAN_TypeDef *can = regs(bus);

	// o CAN1 tem os filtros dos dois: liga-se sempre
	RCC->APB1ENR |= RCC_APB1ENR_CAN1EN | ((bus == BL_CAN2) ? RCC_APB1ENR_CAN2EN : 0u);
	(void) RCC->APB1ENR;

	if (bus == BL_CAN1) {
		pin_alternate(GPIOA, 11u);
		pin_alternate(GPIOA, 12u);
	} else {
		pin_alternate(GPIOB, 5u);
		pin_alternate(GPIOB, 13u);
	}

	can->MCR &= ~CAN_MCR_SLEEP;
	can->MCR |= CAN_MCR_INRQ;
	if (!wait_ack(can, true)) {
		return false;
	}

	// bus-off sai sozinho; prioridade de envio pelo ID (as respostas 0x580 primeiro)
	can->MCR = CAN_MCR_INRQ | CAN_MCR_ABOM;

	uint32_t prescaler = pclk1_hz / ((uint32_t) kbps * 1000u * 16u);

	if (prescaler == 0u) {
		prescaler = 1u;
	}
	can->BTR = (prescaler - 1u) | (12u << CAN_BTR_TS1_Pos) | (1u << CAN_BTR_TS2_Pos);   // 1 + 13 + 2 TQ, SJW 1

	CAN1->FMR |= CAN_FMR_FINIT;
	CAN1->FMR = (CAN1->FMR & ~CAN_FMR_CAN2SB) | (CAN2_FIRST_BANK << CAN_FMR_CAN2SB_Pos) | CAN_FMR_FINIT;
	set_filter((bus == BL_CAN1) ? 0u : CAN2_FIRST_BANK, request_id);
	CAN1->FMR &= ~CAN_FMR_FINIT;

	// sai do modo de init sozinho quando vir 11 bits recessivos: um bus ainda desligado entra mais tarde
	can->MCR &= ~CAN_MCR_INRQ;
	(void) wait_ack(can, false);
	return true;
}

bool BlCan_Synced(uint8_t bus) {

	return (regs(bus)->MSR & CAN_MSR_INAK) == 0u;
}

bool BlCan_Receive(uint8_t bus, uint32_t *id, uint8_t data[8], uint8_t *dlc) {

	CAN_TypeDef *can = regs(bus);

	if ((can->RF0R & CAN_RF0R_FOVR0) != 0u) {
		can->RF0R = CAN_RF0R_FOVR0;
	}
	if ((can->RF0R & CAN_RF0R_FMP0) == 0u) {
		return false;
	}

	uint32_t identifier = can->sFIFOMailBox[0].RIR;
	uint32_t low = can->sFIFOMailBox[0].RDLR;
	uint32_t high = can->sFIFOMailBox[0].RDHR;

	*dlc = (uint8_t) (can->sFIFOMailBox[0].RDTR & CAN_RDT0R_DLC);
	can->RF0R = CAN_RF0R_RFOM0;

	for (uint32_t index = 0; index < 4u; index++) {
		data[index] = (uint8_t) (low >> (8u * index));
		data[index + 4u] = (uint8_t) (high >> (8u * index));
	}
	if ((identifier & (CAN_RI0R_IDE | CAN_RI0R_RTR)) != 0u) {
		return false;
	}
	*id = identifier >> 21;
	return true;
}

uint8_t BlCan_FreeMailboxes(uint8_t bus) {

	uint32_t status = regs(bus)->TSR;

	return (uint8_t) (((status & CAN_TSR_TME0) ? 1u : 0u) + ((status & CAN_TSR_TME1) ? 1u : 0u) + ((status & CAN_TSR_TME2) ? 1u : 0u));
}

bool BlCan_Send(uint8_t bus, uint32_t id, const uint8_t data[8], uint8_t keep_free, uint32_t *mailbox) {

	CAN_TypeDef *can = regs(bus);

	if (BlCan_FreeMailboxes(bus) <= keep_free) {
		return false;
	}

	uint32_t box = (can->TSR & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos;

	can->sTxMailBox[box].TIR = id << 21;
	can->sTxMailBox[box].TDTR = 8u;
	can->sTxMailBox[box].TDLR = (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
	can->sTxMailBox[box].TDHR = (uint32_t) data[4] | ((uint32_t) data[5] << 8) | ((uint32_t) data[6] << 16) | ((uint32_t) data[7] << 24);
	can->sTxMailBox[box].TIR |= CAN_TI0R_TXRQ;

	if (mailbox != 0) {
		*mailbox = box;
	}
	return true;
}

bool BlCan_TxPending(uint8_t bus, uint32_t mailbox) {

	return (regs(bus)->TSR & (CAN_TSR_TME0 << mailbox)) == 0u;
}

void BlCan_Abort(uint8_t bus, uint32_t mailbox) {

	regs(bus)->TSR = CAN_TSR_ABRQ0 << (mailbox * 8u);
}

uint32_t BlCan_ErrorStatus(uint8_t bus) {

	return regs(bus)->ESR;
}
