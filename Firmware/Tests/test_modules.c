/*
 * test_modules.c - os modulos de decisao um a um, chamados diretamente com o
 * bms preparado a mao (sem o brain_loop por cima): ISA, SOC, ventoinhas, ADC
 * da placa, contactores, carregador (todas as razoes de corte), precarga (os
 * mismatches de cada estado e a VCU calada), faults, brain e boot_ctrl.
 * Correr com Tests/run_tests.sh.
 */

#include "fake_chain.h"
#include "fake_board.h"
#include "bms_config.h"
#include "faults.h"
#include "ams_error.h"
#include "contactors.h"
#include "precharge.h"
#include "charger.h"
#include "isa.h"
#include "soc.h"
#include "fans.h"
#include "board.h"
#include "boot_ctrl.h"
#include "boot_journal.h"
#include "boot_shared.h"
#include "console.h"
#include "powertrain_t26.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void clean(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	Config_SetDefaults(&bms.config);
}

static void boot(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(700);
}

/* ================== ISA ================== */

static void isa_frame(bms_isa_t *isa, bool invert, uint32_t id, int32_t value, uint8_t flags, uint32_t now_ms) {

	uint8_t data[6] = { 0, flags, (uint8_t) (value >> 24), (uint8_t) (value >> 16), (uint8_t) (value >> 8), (uint8_t) value };

	CHECK(Isa_OnFrame(isa, invert, id, data, 6, now_ms));
}

static void test_isa_frames(void) {

	bms_isa_t isa;
	uint8_t data[6] = { 0 };

	clean();
	memset(&isa, 0, sizeof(isa));

	// cada resultado da IVT-S no seu campo, big-endian com sinal
	isa_frame(&isa, false, 0x521, -12345, 0x30, 10);
	isa_frame(&isa, false, 0x522, 400000, 0, 11);
	isa_frame(&isa, false, 0x523, 401000, 0, 12);
	isa_frame(&isa, false, 0x524, 402000, 0, 13);
	isa_frame(&isa, false, 0x525, 255, 0, 14);
	isa_frame(&isa, false, 0x526, -5000, 0, 15);
	isa_frame(&isa, false, 0x527, 777, 0, 16);
	isa_frame(&isa, false, 0x528, 88, 0, 17);
	CHECK((isa.current_mA == -12345) && (isa.error_flags == 0x3u) && (isa.current_rx_ms == 10u));
	CHECK((isa.voltage_mV[0] == 400000) && (isa.voltage_mV[1] == 401000) && (isa.voltage_mV[2] == 402000));
	CHECK((isa.temp_dC == 255) && (isa.power_W == -5000) && (isa.charge_As == 777) && (isa.energy_Wh == 88));
	CHECK((isa.frames == 8u) && (isa.any_rx_ms == 17u));

	// montada ao contrario: sinal trocado na corrente, potencia e energia (as tensoes nao)
	isa_frame(&isa, true, 0x521, 1000, 0, 20);
	isa_frame(&isa, true, 0x522, 1000, 0, 20);
	isa_frame(&isa, true, 0x526, 1000, 0, 20);
	isa_frame(&isa, true, 0x527, 1000, 0, 20);
	isa_frame(&isa, true, 0x528, 1000, 0, 20);
	CHECK((isa.current_mA == -1000) && (isa.voltage_mV[0] == 1000) && (isa.power_W == -1000) && (isa.charge_As == -1000) && (isa.energy_Wh == -1000));

	// fora da gama de IDs: nao e da ISA; curta: e da ISA mas ignorada
	CHECK(!Isa_OnFrame(&isa, false, 0x520, data, 6, 30));
	CHECK(!Isa_OnFrame(&isa, false, 0x529, data, 6, 30));
	CHECK(Isa_OnFrame(&isa, false, 0x521, data, 5, 30));
	CHECK((isa.frames == 13u) && (isa.current_rx_ms == 20u));
}

static void test_isa_step_faults(void) {

	clean();
	Isa_Init();

	// nunca houve trama: morta, idade infinita, fault de timeout
	Isa_Step(1000);
	CHECK(!bms.isa_pack.alive && (bms.isa_pack.age_ms == UINT32_MAX) && bms.faults.isa_pack_timeout);

	isa_frame(&bms.isa_pack, false, 0x521, 5000, 0, 1000);
	isa_frame(&bms.isa_pack, false, 0x523, 590000, 0, 1000);
	Isa_Step(1100);
	CHECK(bms.isa_pack.alive && (bms.isa_pack.age_ms == 100u) && !bms.faults.isa_pack_timeout);
	CHECK((bms.accu.current_mA == 5000) && bms.accu.isa_alive);

	// canal do bus: U1 por omissao, U2 com isa_bus_channel = 2, fora da gama = U1
	bms.config.isa_bus_channel = 2;
	Isa_Step(1100);
	CHECK(bms.accu.voltage_mV == 590000);
	bms.config.isa_bus_channel = 9;
	Isa_Step(1100);
	CHECK(bms.accu.voltage_mV == bms.isa_pack.voltage_mV[0]);

	// calada mais que isa_pack_timeout_ms
	Isa_Step(1000u + bms.config.isa_pack_timeout_ms + 1u);
	CHECK(!bms.isa_pack.alive && bms.faults.isa_pack_timeout);

	// erros da propria IVT (canal, medida, sistema) so contam com ela viva
	isa_frame(&bms.isa_pack, false, 0x521, 0, (uint8_t) (ISA_ERROR_MEASUREMENT << 4), 5000);
	Isa_Step(5010);
	CHECK(bms.faults.isa_pack_error);
	isa_frame(&bms.isa_pack, false, 0x521, 0, 0, 5020);
	Isa_Step(5030);
	CHECK(!bms.faults.isa_pack_error);
}

static void test_isa_overcurrent_and_mismatch(void) {

	clean();
	Isa_Init();

	// FS EV 5.8.10: corrente acima do limite tem de durar overcurrent_ms
	int32_t discharge = -((int32_t) bms.config.overcurrent_discharge_A * 1000) - 1;
	int32_t charge = ((int32_t) bms.config.overcurrent_charge_A * 1000) + 1;

	isa_frame(&bms.isa_pack, false, 0x521, discharge, 0, 1000);
	Isa_Step(1000);
	CHECK(!bms.faults.overcurrent);
	isa_frame(&bms.isa_pack, false, 0x521, discharge, 0, 1000u + bms.config.overcurrent_ms);
	Isa_Step(1000u + bms.config.overcurrent_ms);
	CHECK(bms.faults.overcurrent);

	// o limite de carga e outro (mais baixo)
	isa_frame(&bms.isa_pack, false, 0x521, charge - 2, 0, 2000);
	Isa_Step(2000);
	CHECK(!bms.faults.overcurrent);
	isa_frame(&bms.isa_pack, false, 0x521, charge, 0, 2100);
	Isa_Step(2100);
	isa_frame(&bms.isa_pack, false, 0x521, charge, 0, 2100u + bms.config.overcurrent_ms);
	Isa_Step(2100u + bms.config.overcurrent_ms);
	CHECK(bms.faults.overcurrent);

	// tensao da ISA vs soma das celulas: so em HV_ON, com as celulas validas
	clean();
	Isa_Init();
	bms.cells.cells_valid = true;
	bms.accu.voltage_sum_mV = 500000;
	isa_frame(&bms.isa_pack, false, 0x521, 0, 0, 1000);
	isa_frame(&bms.isa_pack, false, 0x522, 400000, 0, 1000);   // 20 % abaixo
	bms.precharge.state = KILL;
	Isa_Step(1000);
	Isa_Step(1000u + bms.config.pack_voltage_mismatch_ms);
	CHECK(!bms.faults.pack_voltage_mismatch);
	bms.precharge.state = HV_ON;
	isa_frame(&bms.isa_pack, false, 0x521, 0, 0, 3000);
	Isa_Step(3000);
	CHECK(!bms.faults.pack_voltage_mismatch);
	isa_frame(&bms.isa_pack, false, 0x521, 0, 0, 3000u + bms.config.pack_voltage_mismatch_ms);
	Isa_Step(3000u + bms.config.pack_voltage_mismatch_ms);
	CHECK(bms.faults.pack_voltage_mismatch);
	isa_frame(&bms.isa_pack, false, 0x522, 495000, 0, 4500);    // dentro dos 10 %
	isa_frame(&bms.isa_pack, false, 0x521, 0, 0, 4500);
	Isa_Step(4500);
	CHECK(!bms.faults.pack_voltage_mismatch);
}

/* ================== SOC ================== */

static void test_soc_curve_and_ocv(void) {

	bms_config_t *config = &bms.config;

	clean();

	// curva: abaixo do primeiro ponto, acima do ultimo, e interpolada
	CHECK(Soc_PercentFromOcv(config, 0) == (uint16_t) (config->soc_curve_pct[0] * 100u));
	CHECK(Soc_PercentFromOcv(config, 60000) == 10000u);
	uint16_t low = config->soc_curve_mV[3];
	uint16_t high = config->soc_curve_mV[4];
	uint16_t middle = (uint16_t) ((low + high) / 2u);
	uint16_t expected = (uint16_t) ((config->soc_curve_pct[3] * 100u) + (((config->soc_curve_pct[4] - config->soc_curve_pct[3]) * 100u * (middle - low)) / (high - low)));
	CHECK(Soc_PercentFromOcv(config, middle) == expected);
	CHECK(Soc_PercentFromOcv(config, high) == (uint16_t) (config->soc_curve_pct[4] * 100u));

	// OCV: sem corrente e com a compensacao a zero e a propria tensao
	memset(config->temp_compensation_adjust_mV, 0, sizeof(config->temp_compensation_adjust_mV));
	memset(config->temp_compensation_resistance_mOhm, 0, sizeof(config->temp_compensation_resistance_mOhm));
	CHECK(Soc_EstimateOcv(config, 3700, 0, 250) == 3700u);
	CHECK(Soc_EstimateOcv(config, 3700, 30000, 250) == 3700u);

	// R = 20 mOhm (200 em 0,1 mOhm) em 3 paralelos: 30 A no pack = 10 A por celula = 200 mV
	for (uint8_t point = 0; point < CFG_TEMPCOMP_POINTS; point++) {
		config->temp_compensation_resistance_mOhm[point] = 200;
	}
	config->cells_parallel = 3;
	CHECK(Soc_EstimateOcv(config, 3700, 30000, 250) == 3500u);    // a carregar: OCV abaixo
	CHECK(Soc_EstimateOcv(config, 3700, -30000, 250) == 3900u);   // a descarregar: acima
	config->cells_parallel = 0;   // 0 conta como 1
	CHECK(Soc_EstimateOcv(config, 3700, 10000, 250) == 3500u);
	config->cells_parallel = 3;

	// ajuste por temperatura: abaixo do primeiro ponto, acima do ultimo, interpolado
	for (uint8_t point = 0; point < CFG_TEMPCOMP_POINTS; point++) {
		config->temp_compensation_resistance_mOhm[point] = 0;
		config->temp_compensation_adjust_mV[point] = (int16_t) (point * 100);   // 0,1 mV: 0, 10, 20 mV...
	}
	int16_t first_dC = (int16_t) (config->temp_compensation_temp_C[0] * 10);
	int16_t last_dC = (int16_t) (config->temp_compensation_temp_C[CFG_TEMPCOMP_POINTS - 1] * 10);
	int16_t mid_dC = (int16_t) ((config->temp_compensation_temp_C[1] * 10 + config->temp_compensation_temp_C[2] * 10) / 2);

	CHECK(Soc_EstimateOcv(config, 3700, 0, (int16_t) (first_dC - 100)) == 3700u);
	CHECK(Soc_EstimateOcv(config, 3700, 0, (int16_t) (last_dC + 100)) == (3700u + (CFG_TEMPCOMP_POINTS - 1u) * 10u));
	CHECK(Soc_EstimateOcv(config, 3700, 0, mid_dC) == 3715u);

	// limites: nunca negativa, nunca acima de 65535
	config->temp_compensation_adjust_mV[0] = -32000;
	CHECK(Soc_EstimateOcv(config, 100, 0, (int16_t) (first_dC - 100)) == 0u);
	for (uint8_t point = 0; point < CFG_TEMPCOMP_POINTS; point++) {
		config->temp_compensation_adjust_mV[point] = 32000;
	}
	CHECK(Soc_EstimateOcv(config, 65000, 0, 250) == 65535u);
}

static void test_soc_counting_and_rest(void) {

	clean();
	Soc_Init();
	bms.config.soc_rest_s = 2;

	// sem celulas validas nao ha ancora
	Soc_Step(100);
	CHECK(!bms.soc.ready);

	bms.cells.cells_valid = true;
	bms.cells.temps_valid = true;
	bms.cells.cell_min_mV = bms.config.soc_curve_mV[CFG_SOC_POINTS - 1];
	bms.cells.temp_max_dC = 250;
	bms.cells.temp_min_dC = 250;
	Soc_Step(200);
	CHECK(bms.soc.ready && (bms.soc.anchors == 1u) && (bms.soc.initial_percent_x100 > 9000u));

	// a descarregar 13,5 A durante 360 s = 1350 mAh = 10 % de 13500 mAh
	bms.isa_pack.alive = true;
	bms.isa_pack.current_mA = -13500;
	uint32_t now = 1000;

	bms.isa_pack.current_rx_ms = now;
	Soc_Step(now);
	uint16_t start = bms.soc.percent_x100;
	for (uint32_t step = 0; step < 3600u; step++) {
		now += 100u;
		bms.isa_pack.current_rx_ms = now;
		Soc_Step(now);
	}
	CHECK(abs((int) (start - bms.soc.percent_x100) - 1000) <= 2);
	CHECK(bms.soc.used_As == (13500 * 360 / 1000));

	// um buraco de mais de 1 s nas tramas nao se integra
	uint16_t before = bms.soc.percent_x100;

	now += 5000u;
	bms.isa_pack.current_rx_ms = now;
	Soc_Step(now);
	CHECK(bms.soc.percent_x100 == before);

	// a mesma trama duas vezes conta uma vez
	Soc_Step(now + 50u);
	CHECK(bms.soc.percent_x100 == before);

	// repouso (corrente abaixo de soc_rest_current_mA) durante soc_rest_s: nova ancora pela OCV
	bms.isa_pack.current_mA = 100;
	for (uint32_t step = 0; step < 25u; step++) {
		now += 100u;
		bms.isa_pack.current_rx_ms = now;
		Soc_Step(now);
	}
	CHECK(bms.soc.anchors == 2u);
	CHECK(bms.soc.used_As == 0);

	// SOC baixo
	bms.cells.cell_min_mV = bms.config.soc_curve_mV[0];
	bms.isa_pack.current_mA = 50000;   // corrente alta: nao ha repouso
	bms.soc.ready = false;
	Soc_Step(now);
	now += 100u;
	bms.isa_pack.current_rx_ms = now;
	Soc_Step(now);
	CHECK(bms.faults.soc_low);

	// capacidade 0: nao divide por zero
	bms.config.capacity_mAh = 0;
	now += 100u;
	bms.isa_pack.current_rx_ms = now;
	Soc_Step(now);
	CHECK(bms.soc.percent_x100 <= 10000u);
}

/* ================== VENTOINHAS ================== */

static void test_fans(void) {

	bms_fan_cfg_t ramp = { .start_dC = 300, .full_dC = 500, .pwm_min_pct = 20, .pwm_max_pct = 100 };
	bool running = false;

	CHECK(Fans_Ramp(&ramp, 290, 50, &running) == 0u);
	CHECK(Fans_Ramp(&ramp, 300, 50, &running) == (20u * 255u) / 100u);
	CHECK(running);
	CHECK(Fans_Ramp(&ramp, 400, 50, &running) == (60u * 255u) / 100u);
	CHECK(Fans_Ramp(&ramp, 600, 50, &running) == 255u);
	CHECK(Fans_Ramp(&ramp, 260, 50, &running) == (20u * 255u) / 100u);   // dentro da histerese: no minimo
	CHECK(Fans_Ramp(&ramp, 249, 50, &running) == 0u);
	CHECK(!running);

	// passo: a cada 200 ms; sem temperaturas no maximo; o LV ve o die dos ADBMS se for mais quente
	clean();
	Fans_Init();
	bms.cells.temps_valid = false;
	Fans_Step(100);
	CHECK(bms.fan_cells.pwm == 0u);   // ainda nao passaram 200 ms
	Fans_Step(200);
	CHECK((bms.fan_cells.pwm == 255u) && (board.fan_pwm == 255u));

	bms.cells.temps_valid = true;
	bms.cells.temp_max_dC = 200;
	bms.board.mcu_temp_dC = 300;
	bms.cells.die_max_dC = 800;
	Fans_Step(400);
	CHECK(bms.fan_cells.pwm == 0u);
	CHECK((bms.fan_lv.control_temp_dC == 800) && (bms.fan_lv.pwm == 255u));
	bms.cells.die_max_dC = 100;
	Fans_Step(600);
	CHECK(bms.fan_lv.control_temp_dC == 300);

	bms.fan_cells.bypass_on = true;
	bms.fan_lv.bypass_on = true;
	Fans_Step(800);
	CHECK((bms.fan_cells.pwm == 255u) && (bms.fan_lv.pwm == 255u) && (board.fan_pwm == 255u));
}

/* ================== ADC DA PLACA ================== */

static void test_board_adc(void) {

	clean();
	Board_Init();

	// VREFINT = calibracao: VDDA = 3,3 V; temperatura 30 C + (1000 - 940) * 80 / (1200 - 940)
	// MCS1802: 1 A = 264 mV acima de VDDA/2 -> 1914 mV = ADC 2375
	bms.config.board_current_offset_mA = 0;
	board.adc_current_raw = 2375;
	Board_Step(1000);
	CHECK(bms.board.vdda_mV == 3300u);
	CHECK(bms.board.mcu_temp_dC == (int16_t) (300 + ((1000 - 940) * 800) / (1200 - 940)));
	CHECK(abs((int) bms.board.current_mA - 1000) <= 5);
	CHECK(!bms.faults.mcu_adc_error);

	// VDDA mais baixa (VREFINT le mais): a temperatura escala pela VDDA real
	board.adc_vref_raw = 1650;
	Board_Step(1100);
	CHECK(bms.board.vdda_mV == 3000u);

	// a 100 ms: nao refaz as contas antes disso
	board.adc_vref_raw = 1500;
	Board_Step(1150);
	CHECK(bms.board.vdda_mV == 3000u);

	// corrente negativa (offset) fica a 0
	board.adc_vref_raw = 1500;
	bms.config.board_current_offset_mA = -2000;
	Board_Step(1300);
	CHECK(bms.board.current_mA == 0u);

	// sem VREFINT: nao ha medida (fica a anterior)
	board.adc_vref_raw = 0;
	Board_Step(1400);
	CHECK(bms.board.vdda_mV == 3300u);

	// o DMA parou: fault depois de 500 ms sem buffers
	board.adc_vref_raw = 1500;
	board.adc_stalled = true;
	Board_Step(1500);
	run_ms(0);
	for (uint32_t now = 1600; now <= 2200u; now += 100u) {
		Board_Step(now);
	}
	CHECK(bms.faults.mcu_adc_error);
	board.adc_stalled = false;
	fake_now_us += 100000u;   // o contador anda outra vez
	Board_Step(2400);
	CHECK(!bms.faults.mcu_adc_error);
}

/* ================== CONTACTORES ================== */

static void test_contactors_debounce_and_edges(void) {

	clean();
	board.follow_contactors = false;
	board.forced_inputs = BSP_IN_SDC;
	Contactors_Init(0);
	CHECK(!bms.precharge.inputs_valid);
	CHECK(!board.air_pos && !board.air_neg && !board.precharge);

	// o arranque comeca com o que os pinos dizem, mas so e valido depois de contactor_debounce_ms estavel
	bms.precharge.state = KILL;
	Contactors_ReadInputs(100);
	CHECK(!bms.precharge.inputs_valid && bms.precharge.sdc_closed && !bms.faults.sdc_open);
	Contactors_ReadInputs(bms.config.contactor_debounce_ms);
	CHECK(bms.precharge.inputs_valid && bms.precharge.sdc_closed && !bms.faults.sdc_open);

	// um glitch mais curto que o debounce nao muda nada
	board.forced_inputs = BSP_IN_SDC | BSP_IN_AIR_POS;
	Contactors_ReadInputs(300);
	board.forced_inputs = BSP_IN_SDC;
	Contactors_ReadInputs(350);
	Contactors_ReadInputs(600);
	CHECK(!bms.precharge.air_pos_fb);

	// AIR+ fechado de verdade: nao e "HV segura"
	board.forced_inputs = BSP_IN_SDC | BSP_IN_AIR_POS;
	Contactors_ReadInputs(700);
	Contactors_ReadInputs(700u + bms.config.contactor_debounce_ms);
	CHECK(bms.precharge.air_pos_fb && !bms.system.hv_safe);
	board.forced_inputs = BSP_IN_SDC;
	Contactors_ReadInputs(1000);
	Contactors_ReadInputs(1000u + bms.config.contactor_debounce_ms);
	CHECK(bms.system.hv_safe);

	// SDC aberto
	board.forced_inputs = 0;
	Contactors_ReadInputs(1500);
	Contactors_ReadInputs(1500u + bms.config.contactor_debounce_ms);
	CHECK(bms.faults.sdc_open);

	// o EXTI so conta flancos (pelo callback do HAL, como no firmware)
	HAL_GPIO_EXTI_Callback(BSP_IN_PRE);
	HAL_GPIO_EXTI_Callback(BSP_IN_PRE);
	HAL_GPIO_EXTI_Callback(BSP_IN_DISCH);
	HAL_GPIO_EXTI_Callback(0x100);   // pino que nao e um feedback
	Contactors_ReadInputs(2000);
	CHECK((bms.precharge.fb_edges[2] == 2u) && (bms.precharge.fb_edges[3] == 1u) && (bms.precharge.fb_edges[0] == 0u));

	// o Apply so fecha com o AMS_ERROR inativo e o SDC fechado
	bms.precharge.trigger_air_neg_cmd = true;
	Contactors_Apply();
	CHECK(!board.air_neg);
	board.forced_inputs = BSP_IN_SDC;
	Contactors_ReadInputs(3000);
	Contactors_ReadInputs(3000u + bms.config.contactor_debounce_ms);
	Contactors_Apply();
	CHECK(board.air_neg);
	bms.ams_error.active = true;
	Contactors_Apply();
	CHECK(!board.air_neg);
}

/* ================== CARREGADOR ================== */

static void charger_ready(uint32_t now_ms) {

	clean();
	Charger_Init();
	bms.precharge.state = HV_ON;
	bms.precharge.source = PRECHARGE_SOURCE_HANDCART;
	bms.precharge.sdc_closed = true;
	bms.cells.cells_valid = true;
	bms.cells.temps_valid = true;
	bms.cells.cell_max_mV = 3900;
	bms.cells.temp_max_dC = 250;
	bms.accu.voltage_sum_mV = 500000;
	bms.isa_handcart.alive = true;
	bms.isa_handcart.voltage_mV[0] = 500000;
	bms.isa_handcart.current_mA = 5000;
	bms.charger.switch_on = true;
	bms.charger.switch_seen = true;
	bms.charger.switch_rx_ms = now_ms;
	bms.charger.status_seen = true;
	bms.charger.updated_ms = now_ms;
}

static charge_end_t charge_and_cut(void (*breaker)(void)) {

	charger_ready(1000);
	Charger_Step(1000);
	CHECK(bms.charger.state == CHARGER_PRESTART_STOP);
	bms.charger.switch_rx_ms = 1000u + bms.config.charge_prestart_ms;
	bms.charger.updated_ms = 1000u + bms.config.charge_prestart_ms;
	Charger_Step(1000u + bms.config.charge_prestart_ms);
	CHECK((bms.charger.state == CHARGER_CHARGING) && bms.charger.req_enable && (bms.charger.sessions == 1u));
	breaker();
	Charger_Step(1001u + bms.config.charge_prestart_ms);
	return bms.charger.end_reason;
}

static void break_sdc(void) { bms.precharge.sdc_closed = false; }
static void break_ams(void) { bms.ams_error.active = true; }
static void break_isa(void) { bms.isa_handcart.alive = false; }
static void break_hv(void) { bms.precharge.state = KILL; }
static void break_temp(void) { bms.cells.temp_max_dC = bms.config.charge_temp_max_dC; }
static void break_current(void) { bms.isa_handcart.current_mA = -((int32_t) bms.config.charge_cut_current_mA + 1); }
static void break_timeout(void) { bms.charger.updated_ms -= (bms.config.charge_status_timeout_ms + 10u); }
static void break_flags(void) { bms.faults.charger_fault = false; bms.charger.hw_failure = true; bms.charger.fault_since_ms = 1u; }

static void test_charger_cut_reasons(void) {

	CHECK(charge_and_cut(break_sdc) == CHARGE_END_SDC_OPEN);
	CHECK(bms.precharge.kill_request && (bms.charger.state == CHARGER_DONE) && !bms.charger.req_enable);
	CHECK(charge_and_cut(break_ams) == CHARGE_END_AMS_ERROR);
	CHECK(charge_and_cut(break_isa) == CHARGE_END_ISA_TIMEOUT);
	CHECK(charge_and_cut(break_hv) == CHARGE_END_HV_LOST);
	CHECK(charge_and_cut(break_temp) == CHARGE_END_OVERTEMP);
	CHECK(charge_and_cut(break_current) == CHARGE_END_OVERCURRENT);
	CHECK(charge_and_cut(break_timeout) == CHARGE_END_CHARGER_TIMEOUT);
	CHECK(charge_and_cut(break_flags) == CHARGE_END_CHARGER_FAULT);

	// os nomes
	CHECK(strcmp(Charger_EndName(CHARGE_END_OVERTEMP), "OVERTEMP") == 0);
	CHECK(strcmp(Charger_EndName((charge_end_t) 99), "?") == 0);
	CHECK(strcmp(Charger_StateName(CHARGER_STOPPING), "STOPPING") == 0);
	CHECK(strcmp(Charger_StateName((charger_state_t) 99), "?") == 0);
}

static void test_charger_start_checks(void) {

	// a ISA do handcart nao bate com o pack: nao arranca
	charger_ready(1000);
	bms.isa_handcart.voltage_mV[0] = 300000;
	Charger_Step(1000);
	CHECK((bms.charger.state == CHARGER_DONE) && (bms.charger.end_reason == CHARGE_END_BUS_MISMATCH));
	// a tensao da ISA com o sinal trocado conta pelo modulo
	charger_ready(1000);
	bms.isa_handcart.voltage_mV[0] = -500000;
	Charger_Step(1000);
	CHECK(bms.charger.state == CHARGER_PRESTART_STOP);
	// sem a ISA do handcart: timeout
	charger_ready(1000);
	bms.isa_handcart.alive = false;
	Charger_Step(1000);
	CHECK(bms.charger.end_reason == CHARGE_END_ISA_TIMEOUT);
	// celulas invalidas: nao se compara (corta)
	charger_ready(1000);
	bms.cells.cells_valid = false;
	Charger_Step(1000);
	CHECK(bms.charger.end_reason == CHARGE_END_ISA_TIMEOUT || bms.charger.end_reason == CHARGE_END_BUS_MISMATCH);

	// o switch desliga a meio da precarga (ainda sem HV)
	charger_ready(1000);
	bms.precharge.state = SWITCH_PRECHARGE;
	bms.charger.switch_on = false;
	Charger_Step(1000);
	CHECK(bms.charger.end_reason == CHARGE_END_SWITCH_OFF);
	// o handcart cala-se a meio da precarga
	charger_ready(1000);
	bms.precharge.state = SWITCH_PRECHARGE;
	Charger_Step(1000u + bms.config.handcart_timeout_ms + 1u);
	CHECK(bms.charger.end_reason == CHARGE_END_HANDCART_TIMEOUT);

	// no pre-arranque: switch off, handcart calado, HV perdida
	charger_ready(1000);
	Charger_Step(1000);
	bms.charger.switch_on = false;
	Charger_Step(1100);
	CHECK(bms.charger.end_reason == CHARGE_END_SWITCH_OFF);
	charger_ready(1000);
	Charger_Step(1000);
	Charger_Step(1000u + bms.config.handcart_timeout_ms + 1u);
	CHECK(bms.charger.end_reason == CHARGE_END_HANDCART_TIMEOUT);
	charger_ready(1000);
	Charger_Step(1000);
	bms.precharge.state = KILL;
	Charger_Step(1100);
	CHECK(bms.charger.end_reason == CHARGE_END_HV_LOST);
}

static void test_charger_stop_and_latch(void) {

	// switch OFF a carregar: paragem ordenada, espera a corrente cair
	charger_ready(1000);
	Charger_Step(1000);
	bms.charger.switch_rx_ms = bms.charger.updated_ms = 1000u + bms.config.charge_prestart_ms;
	Charger_Step(1000u + bms.config.charge_prestart_ms);
	CHECK(bms.charger.state == CHARGER_CHARGING);
	uint32_t now = 1000u + bms.config.charge_prestart_ms + 10u;

	bms.charger.switch_on = false;
	Charger_Step(now);
	CHECK((bms.charger.state == CHARGER_STOPPING) && (bms.charger.end_reason == CHARGE_END_SWITCH_OFF) && !bms.precharge.kill_request);
	Charger_Step(now + 10u);
	CHECK(bms.charger.state == CHARGER_STOPPING);   // ainda passa corrente
	bms.isa_handcart.current_mA = (int32_t) bms.config.charge_open_mA - 1;
	Charger_Step(now + 20u);
	CHECK((bms.charger.state == CHARGER_DONE) && bms.precharge.kill_request);

	// a corrente nunca cai: desiste ao fim de charge_settle_ms
	charger_ready(1000);
	bms.charger.state = CHARGER_STOPPING;
	bms.charger.state_ms = 1000;
	Charger_Step(1000u + bms.config.charge_settle_ms);
	CHECK(bms.charger.state == CHARGER_DONE);

	// latch: em DONE nao recomeca; um estado estranho vai para DONE
	bms.precharge.kill_request = false;
	Charger_Step(20000);
	CHECK((bms.charger.state == CHARGER_DONE) && !bms.charger.req_enable);
	bms.charger.state = (charger_state_t) 77;
	Charger_Step(20010);
	CHECK(bms.charger.state == CHARGER_DONE);

	// so o flanco OFF->ON do switch arranca outra sessao
	bms.precharge.state = KILL;
	bms.charger.switch_on = true;
	bms.charger.switch_seen = true;
	Charger_OnSwitch(true, 20100);
	CHECK(!bms.precharge.handcart_start);
	Charger_OnSwitch(false, 20200);
	Charger_OnSwitch(true, 20300);
	CHECK(bms.precharge.handcart_start && (bms.charger.state == CHARGER_WAIT_HV));
}

/* ================== PRECARGA ================== */

static void precharge_at(precharge_state_t state, uint8_t feedbacks) {

	clean();
	Precharge_Init(0);
	bms.precharge.inputs_valid = true;
	bms.precharge.sdc_closed = true;
	bms.precharge.source = PRECHARGE_SOURCE_VCU;
	bms.precharge.vcu_request = 1;
	bms.precharge.vcu_request_rx_ms = 1000;
	bms.precharge.sequence_start_ms = 1000;
	bms.precharge.state = state;
	bms.precharge.entered_ms = 1000;
	bms.precharge.air_neg_fb = (feedbacks & CONTACTOR_BIT_AIR_NEG) != 0u;
	bms.precharge.precharge_fb = (feedbacks & CONTACTOR_BIT_PRE) != 0u;
	bms.precharge.air_pos_fb = (feedbacks & CONTACTOR_BIT_AIR_POS) != 0u;
	bms.precharge.discharge_fb = (feedbacks & CONTACTOR_BIT_DISCH) != 0u;
}

static void test_precharge_mismatch_in_each_check(void) {

	static const precharge_state_t states[] = { CHECKING_AIR_NEG_IS_CLOSED, CHECKING_PRECHARGE_IS_CLOSED, VERIFY_BUS_VOLT, VERIFY_CURRENT,
		CHECKING_AIR_POS_IS_CLOSED, CHECKING_PRECHARGE_IS_OPEN };

	// em cada check, um feedback que nao bate (nada fechado) e um mismatch: WRONG e precharge_failure
	for (uint8_t index = 0; index < (sizeof(states) / sizeof(states[0])); index++) {
		precharge_at(states[index], 0);
		Precharge_Step(1010);
		CHECK((bms.precharge.state == WRONG) && bms.faults.contactor_mismatch);
		CHECK((bms.precharge.expected_mask != 0u) && (bms.precharge.actual_mask == 0u));
		Precharge_Step(1020);
		CHECK((bms.precharge.state == KILL) && bms.faults.precharge_failure && (bms.precharge.source == PRECHARGE_SOURCE_NONE));
	}

	// VERIFY_CURRENT: com precharge_end_current_mA so passa com a ISA viva e a corrente baixa
	precharge_at(VERIFY_CURRENT, CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_PRE | CONTACTOR_BIT_DISCH);
	bms.config.precharge_end_current_mA = 500;
	bms.isa_pack.alive = true;
	bms.isa_pack.current_mA = -2000;
	Precharge_Step(1010);
	CHECK(bms.precharge.state == VERIFY_CURRENT);
	bms.isa_pack.current_mA = 400;
	Precharge_Step(1020);
	CHECK(bms.precharge.state == SWITCH_HVPOS);

	// estado desconhecido: WRONG
	precharge_at((precharge_state_t) 99, 0);
	Precharge_Step(1010);
	CHECK(bms.precharge.state == WRONG);
	CHECK(strcmp(Precharge_StateName((precharge_state_t) 99), "?") == 0);
}

static void test_precharge_hv_on_mismatch_counting(void) {

	uint8_t closed = CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_AIR_POS | CONTACTOR_BIT_DISCH;

	precharge_at(HV_ON, closed);
	bms.precharge.mismatch_check_ms = 1000;
	Precharge_Step(1010);
	CHECK((bms.precharge.state == HV_ON) && (bms.precharge.mismatch_count == 0u));

	// AIR+ abre (EMI?): so e real se se repetir contactor_mismatch_count vezes espacadas do debounce
	bms.precharge.air_pos_fb = false;
	uint32_t now = 1010;

	for (uint8_t count = 1; count < bms.config.contactor_mismatch_count; count++) {
		now += bms.config.contactor_debounce_ms;
		Precharge_Step(now);
		CHECK((bms.precharge.state == HV_ON) && (bms.precharge.mismatch_count == count));
		Precharge_Step(now + 1u);   // antes do debounce nao conta outra vez
		CHECK(bms.precharge.mismatch_count == count);
	}
	// volta ao normal: o contador zera
	bms.precharge.air_pos_fb = true;
	Precharge_Step(now + 2u);
	CHECK(bms.precharge.mismatch_count == 0u);

	// persistente: mismatch
	bms.precharge.air_pos_fb = false;
	for (uint8_t count = 0; count < bms.config.contactor_mismatch_count; count++) {
		now += bms.config.contactor_debounce_ms + 5u;
		Precharge_Step(now);
	}
	CHECK((bms.precharge.state == WRONG) && bms.faults.contactor_mismatch);
}

static void test_precharge_vcu_timeout(void) {

	uint8_t closed = CONTACTOR_BIT_AIR_NEG | CONTACTOR_BIT_AIR_POS | CONTACTOR_BIT_DISCH;

	// sem vcu_timeout_ms (por omissao) nunca ha fault
	precharge_at(HV_ON, closed);
	bms.precharge.vcu_request_rx_ms = 0;
	Precharge_Step(100000);
	CHECK(!bms.faults.vcu_timeout);

	precharge_at(HV_ON, closed);
	bms.config.vcu_timeout_ms = 500;
	Precharge_OnVcuAlive(1000);
	Precharge_Step(1400);
	CHECK(!bms.faults.vcu_timeout);
	Precharge_Step(1501);
	CHECK(bms.faults.vcu_timeout);
	Precharge_OnVcuAlive(1600);
	Precharge_Step(1700);
	CHECK(!bms.faults.vcu_timeout);

	// uma sessao do handcart nao depende da VCU
	bms.precharge.source = PRECHARGE_SOURCE_HANDCART;
	Precharge_Step(5000);
	CHECK(!bms.faults.vcu_timeout);
}

/* ================== FAULTS ================== */

static void test_faults_names_and_masks(void) {

	bms_faults_t faults;

	memset(&faults, 0, sizeof(faults));
	CHECK(FAULT_COUNT == 32u);
	CHECK(strcmp(Faults_Name(0), "CELL_OVERVOLTAGE") == 0);
	CHECK(Faults_Name(FAULT_COUNT) == NULL);
	CHECK(Faults_FindByName("cell_overvoltage") == 0);
	CHECK(Faults_FindByName("Sdc_Open") == Faults_FindByName("SDC_OPEN"));
	CHECK(Faults_FindByName("NAO") == -1);
	for (uint8_t index = 0; index < FAULT_COUNT; index++) {
		CHECK(Faults_FindByName(Faults_Name(index)) == (int) index);
	}

	Faults_FromMask(0x80000005ull, &faults);
	CHECK((Faults_Count(&faults) == 3u) && (Faults_ToMask(&faults) == 0x80000005ull));
	Faults_FromMask(0x100000000ull, &faults);   // bit que nao existe
	CHECK(Faults_Count(&faults) == 0u);
}

static void test_ams_error_min_on_time(void) {

	clean();
	AmsError_Init(0);
	bms.system.state = AMS_IDLE;
	bms.config.ams_error_min_on_ms = 500;

	// sai do boot_hold mas fica em erro pelo menos min_on desde que entrou
	AmsError_Step(100);
	CHECK(bms.ams_error.active && !bms.ams_error.boot_hold);
	AmsError_Step(500);
	CHECK(!bms.ams_error.active && !board.ams_error);
	CHECK(bms.ams_error.transitions == 1u);

	// um fault obrigatorio da FS liga sempre, mesmo sem estar no ams_error_enable
	memset(&bms.config.ams_error_enable, 0, sizeof(bms.config.ams_error_enable));
	bms.faults.cell_overvoltage = true;
	AmsError_Step(600);
	CHECK(bms.ams_error.active && board.ams_error);
	CHECK((bms.ams_error.sources_mask & 1u) != 0u);
	// um opcional desligado nao segura a linha
	bms.faults.cell_overvoltage = false;
	bms.faults.soc_low = true;
	AmsError_Step(2000);
	CHECK(!bms.ams_error.active && (bms.ams_error.fault_count == 1u) && (bms.ams_error.sources_mask == 0u));
}

/* ================== BRAIN ================== */

static void test_brain_names_heartbeat_and_callbacks(void) {

	CHECK(strcmp(brain_state_name(AMS_DISCHARGE_TEST), "DISCHARGE_TEST") == 0);
	CHECK(strcmp(brain_state_name(AMS_RESET_ISA), "RESET_ISA") == 0);
	CHECK(strcmp(brain_state_name(AMS_FAULT), "FAULT") == 0);
	CHECK(strcmp(brain_state_name((ams_state_t) 42), "?") == 0);

	// heartbeat: 700, 350, 175... ticks, e volta a 1000
	extern volatile bool toggleHeartbeat;
	uint32_t ticks = 0;
	uint32_t toggles = 0;

	toggleHeartbeat = false;
	for (; ticks < 20000u; ticks++) {
		HAL_SYSTICK_Callback();
		if (toggleHeartbeat) {
			toggleHeartbeat = false;
			toggles++;
		}
	}
	CHECK(toggles > 20u);

	// o brain_loop consome o pedido do LED
	boot();
	toggleHeartbeat = true;
	run_ms(1);
	CHECK(!toggleHeartbeat);
	CHECK(bms.system.loop_max_us >= bms.system.loop_us);
}

static void test_brain_requests_cancelled(void) {

	// pedido de reboot aceite e a HV liga antes dos 200 ms: cancelado
	boot();
	char line[16];

	snprintf(line, sizeof(line), "reboot");
	Console_Execute(CONSOLE_UART, line);
	run_ms(50);
	bms.precharge.sdc_closed = true;
	uint8_t request[1] = { 1 };

	fake_can_inject(CAN_BUS_1, POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID, false, request, 1);
	run_ms(300);
	CHECK(!board.reset_requested);
}

/* ================== BOOT_CTRL ================== */

static void handoff(uint8_t exec_state, uint8_t run) {

	boot_journal_t journal;
	boot_record_t record;
	boot_shared_t shared;

	fake_flash_erase_all();
	BootJournal_Load(&journal);
	memset(&record, 0, sizeof(record));
	record.exec_state = exec_state;
	record.exec_id = 0x1234u;
	record.backup_state = BOOT_BACKUP_VALID;
	CHECK(BootJournal_Write(&journal, &record, false));
	memset(&shared, 0, sizeof(shared));
	shared.run = run;
	shared.boot_version = BOOT_VERSION_PACKED;
	shared.exec_id = 0x1234u;
	BootShared_Seal(&shared);
	BootShared_Write(&shared);
}

static void test_boot_ctrl_corner_cases(void) {

	// nao esta em teste: nada a confirmar
	handoff(BOOT_EXEC_CONFIRMED, BOOT_RUN_CONFIRMED);
	boot();
	CHECK(!BootCtrl_Confirm());

	// o journal ja fala de outra imagem (outro upload pelo meio): nao confirma
	handoff(BOOT_EXEC_PENDING, BOOT_RUN_PENDING);
	boot();
	boot_journal_t journal;

	BootJournal_Load(&journal);
	boot_record_t record = journal.state;

	record.exec_id = 0x9999u;
	CHECK(BootJournal_Write(&journal, &record, false));
	CHECK(!BootCtrl_Confirm() && bms.boot.confirm_failed);
	run_ms(100);
	CHECK(bms.boot.confirm_left_s == 0u);   // desistiu

	// journal cheio (a aplicacao nao apaga): nem confirma nem deixa voltar atras
	handoff(BOOT_EXEC_PENDING, BOOT_RUN_PENDING);
	boot();
	BootJournal_Load(&journal);
	record = journal.state;
	while (BootJournal_Free(&journal) > 0u) {
		CHECK(BootJournal_Write(&journal, &record, false));
	}
	CHECK(!BootCtrl_Confirm() && bms.boot.confirm_failed);
	CHECK(strcmp(BootCtrl_CanRollback(), "journal cheio (reinicia primeiro)") == 0);
	CHECK(!BootCtrl_Reject());

	// pedido do bootloader sem area valida: comeca do zero com os bitrates
	fake_flash_erase_all();
	bms.config.can1_kbps = 250;
	bms.config.can2_kbps = 1000;
	BootCtrl_RequestBootloader();

	boot_shared_t shared;

	BootShared_Read(&shared);
	CHECK(BootShared_Valid(&shared) && (shared.request == BOOT_REQUEST_MAGIC) && (shared.can1_kbps == 250u) && (shared.can2_kbps == 1000u));
	CHECK(shared.boot_version == 0u);

	// sem o bootloader: as flags do reset vem do RCC
	CHECK(BootCtrl_ResetFlags(0x0C000000u) == 0x0C000000u);

	char text[16];

	BootCtrl_VersionText(0x0D010002u, text, sizeof(text));
	CHECK(strcmp(text, "13.1.2") == 0);
}

int main(void) {

	fake_flash_erase_all();

	test_isa_frames();
	test_isa_step_faults();
	test_isa_overcurrent_and_mismatch();
	test_soc_curve_and_ocv();
	test_soc_counting_and_rest();
	test_fans();
	test_board_adc();
	test_contactors_debounce_and_edges();
	test_charger_cut_reasons();
	test_charger_start_checks();
	test_charger_stop_and_latch();
	test_precharge_mismatch_in_each_check();
	test_precharge_hv_on_mismatch_counting();
	test_precharge_vcu_timeout();
	test_faults_names_and_masks();
	test_ams_error_min_on_time();
	test_brain_names_heartbeat_and_callbacks();
	test_brain_requests_cancelled();
	test_boot_ctrl_corner_cases();

	printf("%d verificacoes, %d falhas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
