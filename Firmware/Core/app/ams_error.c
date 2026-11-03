/*
 * ams_error.c
 *
 * Ver ams_error.h.
 */

#include "ams_error.h"
#include "faults.h"
#include "bsp.h"

void AmsError_Init(uint32_t now_ms) {

	bms_ams_error_t *ams_error = &bms.ams_error;

	ams_error->active = true;
	ams_error->boot_hold = true;
	ams_error->since_ms = now_ms;
	ams_error->transitions = 0;
	Bsp_SetAmsError(true);
}

uint64_t AmsError_EnabledMask(void) {

	return Faults_ToMask(&bms.config.ams_error_enable) | Faults_ToMask(&faults_mandatory);
}

void AmsError_Step(uint32_t now_ms) {

	bms_ams_error_t *ams_error = &bms.ams_error;

	// o boot_hold so cai uma vez, quando o arranque acaba
	if (ams_error->boot_hold && (bms.system.state != AMS_STARTUP)) {
		ams_error->boot_hold = false;
	}

	ams_error->faults_mask = Faults_ToMask(&bms.faults);
	ams_error->fault_count = Faults_Count(&bms.faults);
	ams_error->sources_mask = ams_error->faults_mask & AmsError_EnabledMask();
	Faults_FromMask(ams_error->sources_mask, &ams_error->sources);

	bool want_error = ams_error->boot_hold || (ams_error->sources_mask != 0u);

	// em erro fica pelo menos ams_error_min_on_ms, para o latch e a luz o verem
	if (ams_error->active && !want_error && ((now_ms - ams_error->since_ms) < bms.config.ams_error_min_on_ms)) {
		want_error = true;
	}

	if (want_error != ams_error->active) {
		ams_error->active = want_error;
		ams_error->since_ms = now_ms;
		ams_error->transitions++;
	}

	// reescrito em cada ciclo: um so escritor, e o pino nunca fica diferente do estado
	Bsp_SetAmsError(ams_error->active);
}
