/*
 * contactors.c
 *
 * Ver contactors.h. O debounce e por tempo: uma entrada so muda depois de
 * estar estavel contactor_debounce_ms. O EXTI ja nao decide nada (o firmware
 * antigo fazia latch no flanco e um glitch de EMI ficava preso); so conta.
 */

#include "contactors.h"
#include "bsp.h"

static uint8_t stable;                      // BSP_IN_* depois do debounce
static uint8_t candidate[BSP_IN_COUNT];
static uint32_t candidate_ms[BSP_IN_COUNT];
static bool settled[BSP_IN_COUNT];
static volatile uint16_t edge_count[BSP_IN_COUNT];

void Contactors_Init(uint32_t now_ms) {

	uint8_t raw = Bsp_ReadInputs();

	Bsp_SetContactors(false, false, false);

	for (uint8_t input = 0; input < BSP_IN_COUNT; input++) {
		candidate[input] = (raw >> input) & 1u;
		candidate_ms[input] = now_ms;
		settled[input] = false;
		edge_count[input] = 0;
	}
	stable = raw;
	bms.precharge.inputs_valid = false;
}

void Contactors_OnEdge(uint16_t gpio_pin) {

	uint8_t input_bit = Bsp_InputFromPin(gpio_pin);

	for (uint8_t input = 0; input < BSP_IN_COUNT; input++) {
		if (input_bit == (1u << input)) {
			edge_count[input]++;
		}
	}
}

void Contactors_ReadInputs(uint32_t now_ms) {

	uint8_t raw = Bsp_ReadInputs();
	bool all_settled = true;

	for (uint8_t input = 0; input < BSP_IN_COUNT; input++) {

		uint8_t level = (raw >> input) & 1u;

		if (level != candidate[input]) {
			candidate[input] = level;
			candidate_ms[input] = now_ms;
		} else if ((now_ms - candidate_ms[input]) >= bms.config.contactor_debounce_ms) {
			stable = (uint8_t) ((stable & ~(1u << input)) | (level << input));
			settled[input] = true;
		}

		all_settled = all_settled && settled[input];
		bms.precharge.fb_edges[input] = edge_count[input];
	}

	bms_precharge_t *precharge = &bms.precharge;

	precharge->air_pos_fb = (stable & BSP_IN_AIR_POS) != 0u;
	precharge->air_neg_fb = (stable & BSP_IN_AIR_NEG) != 0u;
	precharge->precharge_fb = (stable & BSP_IN_PRE) != 0u;
	precharge->discharge_fb = (stable & BSP_IN_DISCH) != 0u;
	precharge->sdc_closed = (stable & BSP_IN_SDC) != 0u;
	precharge->inputs_valid = all_settled;

	bms.faults.sdc_open = all_settled && !precharge->sdc_closed;

	/* HV segura: tudo pedido aberto, a precarga parada e os feedbacks
	 * (com debounce) a dizer que esta tudo aberto. Um AIR soldado nao passa */
	bms.system.hv_safe = all_settled && (precharge->state == KILL) && !precharge->trigger_air_pos_cmd && !precharge->trigger_air_neg_cmd
			&& !precharge->trigger_precharge_cmd && !precharge->air_pos_fb && !precharge->air_neg_fb && !precharge->precharge_fb;
}

void Contactors_Apply(void) {

	const bms_precharge_t *precharge = &bms.precharge;
	bool allowed = !bms.ams_error.active && precharge->sdc_closed && precharge->inputs_valid;

	Bsp_SetContactors(allowed && precharge->trigger_air_pos_cmd, allowed && precharge->trigger_air_neg_cmd, allowed && precharge->trigger_precharge_cmd);
}
