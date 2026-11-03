/*
 * faults.c
 *
 * Ver faults.h. Os nomes seguem a ordem dos campos do bms_faults_t.
 */

#include "faults.h"
#include <string.h>
#include <ctype.h>

static const char *const fault_names[] = {
	"CELL_OVERVOLTAGE",
	"CELL_UNDERVOLTAGE",
	"CELL_OPEN_WIRE",
	"OVERTEMPERATURE",
	"UNDERTEMPERATURE",
	"NTC_OPEN_WIRE",
	"DIE_OVERTEMPERATURE",
	"SLAVE_MISSING",
	"PEC_ERROR",
	"ADBMS_TIMEOUT",
	"OVERCURRENT",
	"PACK_VOLTAGE_MISMATCH",
	"SOC_LOW",
	"CAN1_BUS_OFF",
	"CAN2_BUS_OFF",
	"CAN_TX_QUEUE_FULL",
	"ISA_PACK_TIMEOUT",
	"VCU_TIMEOUT",
	"HANDCART_TIMEOUT",
	"CHARGER_TIMEOUT",
	"CONTACTOR_MISMATCH",
	"PRECHARGE_TIMEOUT",
	"PRECHARGE_FAILURE",
	"SDC_OPEN",
	"CHARGER_FAULT",
	"CONFIG_INVALID",
	"WATCHDOG_RESET",
	"MCU_ADC_ERROR",
	"EEPROM_ERROR",
	"ISA_PACK_ERROR",
	"CELL_VOLTAGE_WARNING",
	"TEMPERATURE_WARNING",
};

_Static_assert((sizeof(fault_names) / sizeof(fault_names[0])) == (sizeof(bms_faults_t) / sizeof(bool)), "um nome por fault, pela ordem do bms_faults_t");

const bms_faults_t faults_mandatory = {
	.cell_overvoltage = true,
	.cell_undervoltage = true,
	.cell_open_wire = true,
	.overtemperature = true,
	.undertemperature = true,
	.ntc_open_wire = true,
	.slave_missing = true,
	.adbms_timeout = true,
	.overcurrent = true,
	.isa_pack_timeout = true,
};

uint64_t Faults_ToMask(const bms_faults_t *faults) {

	const bool *flags = (const bool*) faults;
	uint64_t mask = 0;

	for (uint8_t fault_index = 0; fault_index < FAULT_COUNT; fault_index++) {
		if (flags[fault_index]) {
			mask |= (1ull << fault_index);
		}
	}

	return mask;
}

void Faults_FromMask(uint64_t mask, bms_faults_t *faults) {

	bool *flags = (bool*) faults;

	for (uint8_t fault_index = 0; fault_index < FAULT_COUNT; fault_index++) {
		flags[fault_index] = ((mask >> fault_index) & 1ull) != 0u;
	}
}

const char* Faults_Name(uint8_t fault_index) {

	return (fault_index < FAULT_COUNT) ? fault_names[fault_index] : NULL;
}

int Faults_FindByName(const char *name) {

	for (uint8_t fault_index = 0; fault_index < FAULT_COUNT; fault_index++) {

		const char *candidate = fault_names[fault_index];
		size_t char_index = 0;

		while ((candidate[char_index] != '\0') && (toupper((unsigned char) name[char_index]) == candidate[char_index])) {
			char_index++;
		}
		if ((candidate[char_index] == '\0') && (name[char_index] == '\0')) {
			return fault_index;
		}
	}

	return -1;
}

uint8_t Faults_Count(const bms_faults_t *faults) {

	uint64_t mask = Faults_ToMask(faults);
	uint8_t count = 0;

	while (mask != 0u) {
		mask &= mask - 1u;
		count++;
	}

	return count;
}
