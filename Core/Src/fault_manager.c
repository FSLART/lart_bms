/*
 * fault_manager.c
 *
 *  Created on: Apr 14, 2026
 *      Author: jpser
 */

#include "fault_manager.h"
#include "uartDMA.h"
#include "can.h"

#include <string.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════════
 *  CAN frame IDs for the fault bitmask frames.
 *  Add these two signals to powertrain_t26.dbc:
 *
 *  BO_ 432 master_fault_id_1: 8 Sender AMS_MASTER
 *   SG_ fault_mask_low  : 0|32@1+ (1,0) [0|0] "" Vector__XXX
 *
 *  BO_ 433 master_fault_id_2: 8 Sender AMS_MASTER
 *   SG_ fault_mask_high : 0|32@1+ (1,0) [0|0] "" Vector__XXX
 * ═══════════════════════════════════════════════════════════════ */
#ifndef POWERTRAIN_T26_MASTER_FAULT_ID_1_FRAME_ID
#define POWERTRAIN_T26_MASTER_FAULT_ID_1_FRAME_ID   0x1B0u
#endif
#ifndef POWERTRAIN_T26_MASTER_FAULT_ID_2_FRAME_ID
#define POWERTRAIN_T26_MASTER_FAULT_ID_2_FRAME_ID   0x1B1u
#endif

/* ═══════════════════════════════════════════════════════════════
 *  INTERNAL STATE
 * ═══════════════════════════════════════════════════════════════ */

static bool active_fault[FAULT_COUNT]; /* is fault N currently active?       */
static FaultContext_t fault_context[FAULT_COUNT]; /* last known context for fault N      */
static uint64_t active_fault_mask; /* bitmask mirror of active_fault[]        */

/* Circular history ring */
static FaultHistoryEntry_t fault_history[FAULT_HISTORY_SIZE];
static uint8_t oldest_fault_entry; /* index of the oldest entry           */
static uint8_t next_fault_entry; /* index where next entry will be written */
static uint8_t fault_entry_count; /* number of valid entries             */

/* ═══════════════════════════════════════════════════════════════
 *  FAULT NAME TABLE  (for UART debug only)
 * ═══════════════════════════════════════════════════════════════ */
static const char *const fault_names[FAULT_COUNT] = { [FAULT_OVERVOLTAGE] = "OVERVOLTAGE", [FAULT_UNDERVOLTAGE] = "UNDERVOLTAGE", [FAULT_OVERTEMPERATURE] = "OVERTEMPERATURE", [FAULT_UNDERTEMPERATURE] = "UNDERTEMPERATURE", [FAULT_OVERTEMPERATURE_DISCHARGE] = "OVERTEMPERATURE_DISCHARGE", [FAULT_UNDERTEMPERATURE_CHARGE] = "UNDERTEMPERATURE_CHARGE", [FAULT_TEMP_SENSOR_OPEN] = "TEMP_SENSOR_OPEN", [FAULT_SLAVE_NOT_DETECTED] = "SLAVE_NOT_DETECTED", [FAULT_PEC_ERROR] = "PEC_ERROR", [FAULT_OPEN_WIRE] = "OPEN_WIRE", [FAULT_ACQUISITION_TIMEOUT] = "ACQUISITION_TIMEOUT", [FAULT_CAN_SEND_ERROR] = "CAN_SEND_ERROR", [FAULT_CAN_INIT_ERROR] = "CAN_INIT_ERROR", [FAULT_CAN_MAILBOX_FULL] = "CAN_MAILBOX_FULL", [FAULT_CAN_BUS_OFF] = "CAN_BUS_OFF", [FAULT_ISA_IVTS_TIMEOUT] = "ISA_IVTS_TIMEOUT", [FAULT_INVERTER_TIMEOUT] = "INVERTER_TIMEOUT", [FAULT_VCU_TIMEOUT] = "VCU_TIMEOUT", [FAULT_PDM_TIMEOUT] = "PDM_TIMEOUT", [FAULT_CHARGER_TIMEOUT] = "CHARGER_TIMEOUT", [FAULT_ACQUISITION_NODE_TIMEOUT] = "ACQUISITION_NODE_TIMEOUT", [FAULT_CONTACTOR_MISMATCH] = "CONTACTOR_MISMATCH", [FAULT_PRECHARGE_TIMEOUT] = "PRECHARGE_TIMEOUT", [FAULT_PRECHARGE_FAILURE] = "PRECHARGE_FAILURE", [FAULT_SDC_TRIGGERED] = "SDC_TRIGGERED", [FAULT_BALANCING_ERROR] = "BALANCING_ERROR", [FAULT_BALANCING_OVERTEMP] = "BALANCING_OVERTEMP", [FAULT_ADC_ERROR] = "ADC_ERROR", [FAULT_CURRENT_SENSOR_ERROR] = "CURRENT_SENSOR_ERROR", [FAULT_SOC_CRITICAL_LOW] = "SOC_CRITICAL_LOW", [FAULT_PACK_VOLTAGE_MISMATCH] = "PACK_VOLTAGE_MISMATCH", [FAULT_EEPROM_READ_ERROR] = "EEPROM_READ_ERROR", [FAULT_EEPROM_WRITE_ERROR] = "EEPROM_WRITE_ERROR", [FAULT_EEPROM_VALIDATION_ERROR] = "EEPROM_VALIDATION_ERROR", [FAULT_STARTUP_FAILURE] = "STARTUP_FAILURE", [FAULT_WATCHDOG_RESET] = "WATCHDOG_RESET", [FAULT_STACK_OVERFLOW] = "STACK_OVERFLOW", };

/* ═══════════════════════════════════════════════════════════════
 *  PRIVATE HELPERS
 * ═══════════════════════════════════════════════════════════════ */

static void history_push(FaultCode_t code, const FaultContext_t *ctx, bool raised) {
	FaultHistoryEntry_t *e = &fault_history[next_fault_entry];
	e->code = code;
	e->ctx = *ctx;
	e->was_raised = raised;

	next_fault_entry = (uint8_t) ((next_fault_entry + 1u) % FAULT_HISTORY_SIZE);

	if (fault_entry_count < FAULT_HISTORY_SIZE) {
		fault_entry_count++;
	} else {
		/* Buffer full – advance head so oldest entry is overwritten */
		oldest_fault_entry= (uint8_t) ((oldest_fault_entry + 1u) % FAULT_HISTORY_SIZE);
	}
}

/** Returns the name string for a fault code, never NULL. */
static const char* fault_name(FaultCode_t code) {
	if ((uint8_t) code < (uint8_t) FAULT_COUNT) {
		const char *n = fault_names[(uint8_t) code];
		if (n != NULL) {
			return n;
		}
	}
	return "UNKNOWN";
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – LIFECYCLE
 * ═══════════════════════════════════════════════════════════════ */

void FaultManager_Init(void) {
	memset(active_fault, 0, sizeof(active_fault));
	memset(fault_context, 0, sizeof(fault_context));
	memset(fault_history, 0, sizeof(fault_history));

	active_fault_mask = 0ULL;
	oldest_fault_entry = 0u;
	next_fault_entry = 0u;
	fault_entry_count = 0u;
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – RAISE / KILL
 * ═══════════════════════════════════════════════════════════════ */

void FaultManager_Raise(FaultCode_t code, const FaultContext_t *ctx) {
	if ((uint8_t) code >= (uint8_t) FAULT_COUNT) {
		return;
	}
	if (ctx == NULL) {
		return;
	}

	const bool already_active = active_fault[(uint8_t) code];

	/* Always update the stored context (new cell, new timestamp, etc.) */
	fault_context[(uint8_t) code] = *ctx;

	if (!already_active) {
		/* Rising edge: set active bit and push a history entry */
		active_fault[(uint8_t) code] = true;
		active_fault_mask |= (1ULL << (uint8_t) code);

		history_push(code, ctx, true);
	}
	/* If already active: context was refreshed above, no duplicate history entry */
}

void FaultManager_Kill(FaultCode_t code) {
	if ((uint8_t) code >= (uint8_t) FAULT_COUNT) {
		return;
	}

	if (!active_fault[(uint8_t) code]) {
		return; /* Nothing to do; avoid polluting history with redundant clears */
	}

	/* Push a cleared event before wiping the state */
	history_push(code, &fault_context[(uint8_t) code], false);

	active_fault[(uint8_t) code] = false;
	active_fault_mask &= ~(1ULL << (uint8_t) code);

	/* Context is intentionally kept: FaultManager_GetContext() still returns
	 * the last-seen context after a fault is cleared (useful for post-mortem). */
}

void FaultManager_KillAll(void) {
	for (uint8_t i = 0u; i < (uint8_t) FAULT_COUNT; i++) {
		if (active_fault[i]) {
			FaultManager_Kill((FaultCode_t) i);
		}
	}
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – QUERY
 * ═══════════════════════════════════════════════════════════════ */

bool FaultManager_IsActive(FaultCode_t code) {
	if ((uint8_t) code >= (uint8_t) FAULT_COUNT) {
		return false;
	}
	return active_fault[(uint8_t) code];
}

uint64_t FaultManager_GetActiveMask(void) {
	return active_fault_mask;
}

bool FaultManager_AnyActive(void) {
	return (active_fault_mask != 0ULL);
}

const FaultContext_t* FaultManager_GetContext(FaultCode_t code) {
	if ((uint8_t) code >= (uint8_t) FAULT_COUNT) {
		return NULL;
	}
	return &fault_context[(uint8_t) code];
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – UART DUMP
 * ═══════════════════════════════════════════════════════════════ */

void FaultManager_DumpUART(void) {
	if (!FaultManager_AnyActive()) {
		printfDebug("[FAULT] No active faults.\r\n");
		return;
	}

	printfDebug("[FAULT] ======== Active Faults ========\r\n");

	for (uint8_t i = 0u; i < (uint8_t) FAULT_COUNT; i++) {
		if (!active_fault[i]) {
			continue;
		}

		const FaultContext_t *c = &fault_context[i];

		/* Common prefix: index, name, timestamp */
		printfDebug("[FAULT] %02u | %-30s | t=%lums", (unsigned) i, fault_name((FaultCode_t) i), (unsigned long) c->timestamp_ms);

		/* Per-fault context suffix */
		switch ((FaultCode_t) i) {
		/*
		 * ── Voltage faults ──────────────────────────────────────────
		 * Shows: slave index, cell index, measured vs threshold [V]
		 */
		case FAULT_OVERVOLTAGE:
		case FAULT_UNDERVOLTAGE:
		case FAULT_PACK_VOLTAGE_MISMATCH:
			printfDebug(" | S%u C%u | %.4fV (lim %.4fV)", (unsigned) c->slave_idx, (unsigned) c->cell_idx, (double) c->measured_value, (double) c->threshold_value);
			break;

			/*
			 * ── Temperature faults ──────────────────────────────────────
			 * Shows: slave index, NTC channel, measured vs threshold [°C]
			 */
		case FAULT_OVERTEMPERATURE:
		case FAULT_UNDERTEMPERATURE:
		case FAULT_OVERTEMPERATURE_DISCHARGE:
		case FAULT_UNDERTEMPERATURE_CHARGE:
		case FAULT_TEMP_SENSOR_OPEN:
		case FAULT_BALANCING_OVERTEMP:
			printfDebug(" | S%u NTC%u | %.1f C (lim %.1f C)", (unsigned) c->slave_idx, (unsigned) c->channel_idx, (double) c->measured_value, (double) c->threshold_value);
			break;

			/*
			 * ── ADBMS slave faults ──────────────────────────────────────
			 * Shows: slave index
			 */
		case FAULT_SLAVE_NOT_DETECTED:
		case FAULT_PEC_ERROR:
		case FAULT_OPEN_WIRE:
			printfDebug(" | Slave=%u", (unsigned) c->slave_idx);
			break;

			/*
			 * ── Acquisition node timeout ────────────────────────────────
			 * Shows: acquisition node index (0-7)
			 */
		case FAULT_ACQUISITION_TIMEOUT:
		case FAULT_ACQUISITION_NODE_TIMEOUT:
			printfDebug(" | AcqNode=%u", (unsigned) c->channel_idx);
			break;

			/*
			 * ── CAN bus faults ──────────────────────────────────────────
			 * Shows: CAN bus index (0=CAN1, 1=CAN2)
			 */
		case FAULT_CAN_SEND_ERROR:
		case FAULT_CAN_INIT_ERROR:
		case FAULT_CAN_MAILBOX_FULL:
		case FAULT_CAN_BUS_OFF:
			printfDebug(" | CAN%u", (unsigned) (c->channel_idx + 1u));
			break;

			/*
			 * ── External node timeouts ──────────────────────────────────
			 * channel_idx maps to FAULT_NODE_xxx constants.
			 * No extra field needed beyond the fault code name itself,
			 * but print the last-measured value if it was set.
			 */
		case FAULT_ISA_IVTS_TIMEOUT:
		case FAULT_INVERTER_TIMEOUT:
		case FAULT_VCU_TIMEOUT:
		case FAULT_PDM_TIMEOUT:
		case FAULT_CHARGER_TIMEOUT:
			/* No extra context needed – the fault name is self-explanatory */
			break;

			/*
			 * ── Contactor mismatch ──────────────────────────────────────
			 * Shows: which contactors are mismatched (bit-decoded)
			 */
		case FAULT_CONTACTOR_MISMATCH:
			printfDebug(" | Mismatch: [%s%s%s%s]", (c->contactor_bits & FAULT_CTC_AIR_POS) ? "AIR+ " : "", (c->contactor_bits & FAULT_CTC_AIR_NEG) ? "AIR- " : "", (c->contactor_bits & FAULT_CTC_PRE) ? "PRE " : "", (c->contactor_bits & FAULT_CTC_DSCH) ? "DSCH " : "");
			break;

			/*
			 * ── Precharge faults ────────────────────────────────────────
			 * Shows: measured vs expected voltage
			 */
		case FAULT_PRECHARGE_TIMEOUT:
		case FAULT_PRECHARGE_FAILURE:
			printfDebug(" | Vmeas=%.2fV Vexp=%.2fV", (double) c->measured_value, (double) c->threshold_value);
			break;

			/*
			 * ── Current / analog ────────────────────────────────────────
			 */
		case FAULT_CURRENT_SENSOR_ERROR:
		case FAULT_ADC_ERROR:
			printfDebug(" | raw/ch=%u val=%.3f", (unsigned) c->channel_idx, (double) c->measured_value);
			break;

			/*
			 * ── SoC ─────────────────────────────────────────────────────
			 */
		case FAULT_SOC_CRITICAL_LOW:
			printfDebug(" | SoC=%.1f%% (lim %.1f%%)", (double) c->measured_value, (double) c->threshold_value);
			break;

		default:
			break;
		}

		printfDebug("\r\n");
	}

	printfDebug("[FAULT] ================================\r\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – CAN TRANSMISSION
 *
 *  Two 8-byte frames carrying the 64-bit fault bitmask, little-endian.
 *  Bytes 4-7 of each frame are reserved / zero for future expansion.
 * ═══════════════════════════════════════════════════════════════ */
void FaultManager_CAN_Send(CAN_HandleTypeDef *hcan) {
	if (hcan == NULL) {
		return;
	}

	const uint64_t mask = active_fault_mask;
	const uint32_t lo = (uint32_t) (mask & 0xFFFFFFFFULL);
	const uint32_t hi = (uint32_t) (mask >> 32u);

	/* Frame 1 – faults 0-31 (lower 32 bits) */
	uint8_t data1[8] = { 0u };
	data1[0] = (uint8_t) ((lo >> 0u) & 0xFFu);
	data1[1] = (uint8_t) ((lo >> 8u) & 0xFFu);
	data1[2] = (uint8_t) ((lo >> 16u) & 0xFFu);
	data1[3] = (uint8_t) ((lo >> 24u) & 0xFFu);
	/* bytes 4-7: reserved, already 0 */

	CAN_TX_Add_To_Queue(hcan,
	POWERTRAIN_T26_MASTER_FAULT_ID_1_FRAME_ID, 8u, data1);

	/* Frame 2 – faults 32-63 (upper 32 bits) */
	uint8_t data2[8] = { 0u };
	data2[0] = (uint8_t) ((hi >> 0u) & 0xFFu);
	data2[1] = (uint8_t) ((hi >> 8u) & 0xFFu);
	data2[2] = (uint8_t) ((hi >> 16u) & 0xFFu);
	data2[3] = (uint8_t) ((hi >> 24u) & 0xFFu);

	CAN_TX_Add_To_Queue(hcan,
	POWERTRAIN_T26_MASTER_FAULT_ID_2_FRAME_ID, 8u, data2);
}

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API – HISTORY
 * ═══════════════════════════════════════════════════════════════ */
void FaultManager_GetHistory(const FaultHistoryEntry_t **out_buf, uint8_t *out_head, uint8_t *out_count) {
	if (out_buf) {
		*out_buf = fault_history;
	}
	if (out_head) {
		*out_head = oldest_fault_entry;
	}
	if (out_count) {
		*out_count = fault_entry_count;
	}
}
