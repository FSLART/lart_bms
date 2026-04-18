/*
 * fault_manager.h
 *
 *  Created on: Apr 14, 2026
 *      Author: jpser
 */

#ifndef INC_FAULT_MANAGER_H_
#define INC_FAULT_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 *  FAULT CODES
 *  Each code maps to one bit in a uint64_t active-mask.
 *  Maximum 63 codes (0 … 62).  FAULT_COUNT must stay ≤ 63.
 * ═══════════════════════════════════════════════════════════════ */
typedef enum {
	/* ── Voltage ─────────────────────────────────────── */
	FAULT_OVERVOLTAGE = 0u, /* cell V > max          */
	FAULT_UNDERVOLTAGE = 1u, /* cell V < min          */

	/* ── Temperature ─────────────────────────────────── */
	FAULT_OVERTEMPERATURE = 2u, /* generic OT            */
	FAULT_UNDERTEMPERATURE = 3u, /* generic UT            */
	FAULT_OVERTEMPERATURE_DISCHARGE = 4u, /* OT during discharge   */
	FAULT_UNDERTEMPERATURE_CHARGE = 5u, /* UT during charge      */
	FAULT_TEMP_SENSOR_OPEN = 6u, /* NTC open / impossible value */

	/* ── ADBMS slave layer ───────────────────────────── */
	FAULT_SLAVE_NOT_DETECTED = 7u, /* isoSPI no response    */
	FAULT_PEC_ERROR = 8u, /* CRC/PEC mismatch      */
	FAULT_OPEN_WIRE = 9u, /* OW diagnostic tripped */
	FAULT_ACQUISITION_TIMEOUT = 10u, /* data acq. PCB timeout (0-7) */

	/* ── CAN bus ─────────────────────────────────────── */
	FAULT_CAN_SEND_ERROR = 11u, /* HAL_CAN_AddTxMessage failed */
	FAULT_CAN_INIT_ERROR = 12u, /* HAL_CAN_Start failed        */
	FAULT_CAN_MAILBOX_FULL = 13u, /* all TX mailboxes occupied   */
	FAULT_CAN_BUS_OFF = 14u, /* HAL_CAN_ERROR_BOF detected  */

	/* ── External node heartbeat timeouts ───────────── */
	FAULT_ISA_IVTS_TIMEOUT = 15u, /* no IVT-S message received   */
	FAULT_INVERTER_TIMEOUT = 16u, /* inverter heartbeat lost      */
	FAULT_VCU_TIMEOUT = 17u, /* VCU heartbeat lost           */
	FAULT_PDM_TIMEOUT = 18u, /* PDM heartbeat lost           */
	FAULT_CHARGER_TIMEOUT = 19u, /* charger heartbeat lost       */
	FAULT_ACQUISITION_NODE_TIMEOUT = 20u, /* specific acq. node silent    */

	/* ── Contactors / precharge ──────────────────────── */
	FAULT_CONTACTOR_MISMATCH = 21u, /* feedback ≠ commanded state  */
	FAULT_PRECHARGE_TIMEOUT = 22u, /* precharge took too long      */
	FAULT_PRECHARGE_FAILURE = 23u, /* V never reached threshold    */
	FAULT_SDC_TRIGGERED = 24u, /* shutdown circuit opened      */

	/* ── Cell balancing ──────────────────────────────── */
	FAULT_BALANCING_ERROR = 25u, /* unexpected state in balancer */
	FAULT_BALANCING_OVERTEMP = 26u, /* temperature rose during bal. */

	/* ── Analog / internal ───────────────────────────── */
	FAULT_ADC_ERROR = 27u, /* DMA/ADC peripheral error     */
	FAULT_CURRENT_SENSOR_ERROR = 28u, /* MCS1802 reading out of range */

	/* ── Energy / state ──────────────────────────────── */
	FAULT_SOC_CRITICAL_LOW = 29u, /* SoC below hard limit         */
	FAULT_PACK_VOLTAGE_MISMATCH = 30u, /* sum-of-cells ≠ pack voltage  */

	/* ── EEPROM / non-volatile storage ──────────────── */
	FAULT_EEPROM_READ_ERROR = 31u, FAULT_EEPROM_WRITE_ERROR = 32u, FAULT_EEPROM_VALIDATION_ERROR = 33u, /* stored checksum mismatch     */

	/* ── Startup / internal ──────────────────────────── */
	FAULT_STARTUP_FAILURE = 34u, /* init sequence failed         */
	FAULT_WATCHDOG_RESET = 35u, /* system recovered from WDG    */
	FAULT_STACK_OVERFLOW = 36u, /* stack canary tripped         */

	/* ── add new codes above this line ─────────────── */
	FAULT_COUNT /* sentinel – must be ≤ 63      */
} FaultCode_t;

/* Compile-time guard: too many fault codes will silently overflow the mask */
#if (FAULT_COUNT > 63)
#error "Too many fault codes: uint64_t mask supports a maximum of 63 faults."
#endif

/* ═══════════════════════════════════════════════════════════════
 *  FAULT CONTEXT
 *  Carries who/what/where/how-much for every raised fault.
 *  Fields unused by a given fault are left zero – check the
 *  per-fault notes in FaultManager_DumpUART() for guidance.
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
	uint32_t timestamp_ms; /* HAL_GetTick() at the moment of RAISE_ERROR()       */

	float measured_value; /* voltage [V], temperature [°C], current [A], …      */
	float threshold_value; /* the limit that was breached (for quick comparison)  */

	uint8_t slave_idx; /* ADBMS6830 daisy-chain index, 0-based (0 = first)   */
	uint8_t cell_idx; /* cell within a slave, 0-based (0 = C1)              */

	/* channel_idx is multi-purpose:
	 *  - CAN faults     → 0 = CAN1, 1 = CAN2
	 *  - Acq. timeouts  → acquisition node index (0-7)
	 *  - Temp. faults   → NTC channel index within the slave
	 *  - External nodes → see enum below (use FAULT_NODE_xxx constants)
	 */
	uint8_t channel_idx;

	/* contactor_bits  – one bit per contactor, SET = mismatch detected
	 *   bit 0  AIR+  (CONTACT_AIR_positivo)
	 *   bit 1  AIR–  (CONTACT_AIR_negativo)
	 *   bit 2  PRE   (CONTACT_PRE)
	 *   bit 3  DSCH  (CONTACT_DSCH)
	 */
	uint8_t contactor_bits;

} FaultContext_t;

/* Contactor bit-mask helpers */
#define FAULT_CTC_AIR_POS    (1u << 0u)
#define FAULT_CTC_AIR_NEG    (1u << 1u)
#define FAULT_CTC_PRE        (1u << 2u)
#define FAULT_CTC_DSCH       (1u << 3u)

/* channel_idx constants for external node timeouts */
#define FAULT_NODE_ISA       0u
#define FAULT_NODE_INVERTER  1u
#define FAULT_NODE_VCU       2u
#define FAULT_NODE_PDM       3u
#define FAULT_NODE_CHARGER   4u

/* channel_idx constants for CAN faults */
#define FAULT_CAN_BUS_1      0u
#define FAULT_CAN_BUS_2      1u

/* ═══════════════════════════════════════════════════════════════
 *  HISTORY
 *  A circular log of the last FAULT_HISTORY_SIZE raise/kill events.
 * ═══════════════════════════════════════════════════════════════ */
#define FAULT_HISTORY_SIZE   32u

typedef struct {
	FaultCode_t code;
	FaultContext_t ctx;
	bool was_raised; /* true = fault raised;  false = fault cleared */
} FaultHistoryEntry_t;

/* ═══════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ═══════════════════════════════════════════════════════════════ */

/** Initialise internal state. Call once before anything else. */
void FaultManager_Init(void);

/**
 * Raise a fault.
 * Prefer the RAISE_ERROR() macro (see below) over calling this directly.
 * Only the first occurrence pushes a history entry; repeated raises just
 * update the stored context (e.g. a different cell now reporting OV).
 */
void FaultManager_Raise(FaultCode_t code, const FaultContext_t *ctx);

/**
 * Clear a specific fault.
 * Use KILL_ERROR(code) macro below.
 */
void FaultManager_Kill(FaultCode_t code);

/** Clear every active fault (e.g. operator-confirmed safe state). */
void FaultManager_KillAll(void);

/* ── Query ───────────────────────────────────────── */

/** Returns true if this specific fault is currently active. */
bool FaultManager_IsActive(FaultCode_t code);

/**
 * Returns a 64-bit bitmask of all currently active faults.
 * Bit N is set when fault code N is active.
 */
uint64_t FaultManager_GetActiveMask(void);

/** Returns true if ANY fault is currently active. */
bool FaultManager_AnyActive(void);

/**
 * Returns a pointer to the last stored context for a fault code.
 * Valid even if the fault has since been cleared (holds last-seen context).
 * Returns NULL if code is out of range.
 */
const FaultContext_t* FaultManager_GetContext(FaultCode_t code);

/* ── Output ──────────────────────────────────────── */

/** Pretty-print all active faults + context to the debug UART. */
void FaultManager_DumpUART(void);

/**
 * Pack the 64-bit active-mask into two CAN frames and enqueue them.
 * Frame 1 (ID 0x1B0): bits  0-31 (faults FAULT_OVERVOLTAGE … FAULT_EEPROM_READ_ERROR)
 * Frame 2 (ID 0x1B1): bits 32-63 (faults FAULT_EEPROM_WRITE_ERROR … future)
 * Add these IDs to powertrain_t26.dbc as master_fault_id_1 / master_fault_id_2.
 */
void FaultManager_CAN_Send(CAN_HandleTypeDef *hcan);

/* ── History ─────────────────────────────────────── */

/**
 * Get a read-only pointer to the circular history buffer.
 * The buffer holds the last FAULT_HISTORY_SIZE raise+kill events in
 * chronological order (oldest first via the returned head index).
 *
 * @param out_buf   [out] pointer to history array base
 * @param out_head  [out] index of the oldest entry (start iterating here)
 * @param out_count [out] number of valid entries (≤ FAULT_HISTORY_SIZE)
 */
void FaultManager_GetHistory(const FaultHistoryEntry_t **out_buf, uint8_t *out_head, uint8_t *out_count);

/* ═══════════════════════════════════════════════════════════════
 *  CONVENIENCE MACROS
 *
 *  RAISE_ERROR uses C99 compound-literal + designated initialisers,
 *  so only the fields you care about need to be supplied.
 *  timestamp_ms is always filled in automatically.
 *
 *  Examples:
 *
 *  // Overvoltage on slave 3, cell 7
 *  RAISE_ERROR(FAULT_OVERVOLTAGE,
 *              .slave_idx       = 3,
 *              .cell_idx        = 7,
 *              .measured_value  = cell_v,
 *              .threshold_value = cfg->max_cell_v);
 *
 *  // Slave not responding
 *  RAISE_ERROR(FAULT_SLAVE_NOT_DETECTED, .slave_idx = m);
 *
 *  // CAN mailbox full on CAN1
 *  RAISE_ERROR(FAULT_CAN_MAILBOX_FULL, .channel_idx = FAULT_CAN_BUS_1);
 *
 *  // ISA IVT-S timeout
 *  RAISE_ERROR(FAULT_ISA_IVTS_TIMEOUT);
 *
 *  // Contactor mismatch on AIR+ and DSCH
 *  RAISE_ERROR(FAULT_CONTACTOR_MISMATCH,
 *              .contactor_bits = FAULT_CTC_AIR_POS | FAULT_CTC_DSCH);
 *
 *  // Acquisition node 2 silent
 *  RAISE_ERROR(FAULT_ACQUISITION_TIMEOUT, .channel_idx = 2);
 *
 *  // Clear the overvoltage alarm
 *  KILL_ERROR(FAULT_OVERVOLTAGE);
 * ═══════════════════════════════════════════════════════════════ */

#define RAISE_ERROR(code, ...)                                      \
    FaultManager_Raise(                                             \
        (code),                                                     \
        &(FaultContext_t){ .timestamp_ms = HAL_GetTick(),           \
                           ##__VA_ARGS__ }                          \
    )

#define KILL_ERROR(code) FaultManager_Kill(code)

#ifdef __cplusplus
}
#endif

#endif /* INC_FAULT_MANAGER_H_ */
