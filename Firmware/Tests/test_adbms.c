/*
 * test_adbms.c - testes da camada ADBMS no PC, sem hardware.
 *
 * Codigo real: brain.c, adBms_Application.c, adbms_cell_read.c,
 * adbms_temp_read.c, adbms_open_wire.c e adbms_balancing.c. A cadeia e
 * simulada (fake_chain.c). Cada teste arranca do zero com brain_start().
 * Correr com Tests/run_tests.sh.
 */

#include "fake_chain.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
static int checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* arranque limpo e um ciclo completo (acordar ~20 ms + ciclo ~155 ms) */
static void boot(void) {

	fake_reset();
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(300);
}

/* corre ate acabarem cycle_count ciclos completos (limite 10 s, para um teste
 * errado nao ficar preso); usar quando o resultado so aparece no fim do ciclo */
static void run_cycles(uint32_t cycle_count) {

	uint32_t target_cycles = bms.adbms.cycles + cycle_count;

	for (uint32_t elapsed_ms = 0; (elapsed_ms < 10000u) && (bms.adbms.cycles < target_cycles); elapsed_ms++) {
		run_ms(1);
	}
}

static bool near(int32_t value, int32_t expected, int32_t tolerance) {

	return abs(value - expected) <= tolerance;
}

/* ms ate a condicao ficar verdadeira, em passos de 1 ms (limit_ms = nunca) */
static uint32_t ms_until(bool (*condition)(void), uint32_t limit_ms) {

	for (uint32_t elapsed_ms = 0; elapsed_ms < limit_ms; elapsed_ms++) {
		if (condition()) {
			return elapsed_ms;
		}
		run_ms(1);
	}
	return limit_ms;
}

static bool ov_active(void) { return bms.faults.cell_overvoltage; }
static bool ot_active(void) { return bms.faults.overtemperature; }
static bool timeout_active(void) { return bms.faults.adbms_timeout; }

/* ================== CONVERSOES ================== */

static void test_code_conversion(void) {

	CHECK(ADBMS_CodeToMv(0) == 1500);
	CHECK(ADBMS_CodeToMv(14667) == 3700);           // 150 uV/LSB + 1,5 V
	CHECK(!ADBMS_CodeValid(INT16_MAX));             // nunca convertido
	CHECK(!ADBMS_CodeValid(INT16_MIN));             // registo limpo
	CHECK(ADBMS_CodeValid(0));
	CHECK(ADBMS_Fresh(1000, 1300));
	CHECK(!ADBMS_Fresh(1000, 1301));                // mais de 300 ms
	CHECK(!ADBMS_Fresh(0, 100));                    // 0 = nunca houve dados
}

/* ================== ARRANQUE E CADEIA ================== */

static void test_healthy_chain(void) {

	boot();
	run_ms(700);

	CHECK(bms.cells.slaves_found == 12);
	CHECK(!bms.faults.slave_missing);
	CHECK(!bms.faults.adbms_timeout);
	CHECK(!bms.faults.pec_error);
	CHECK(!bms.faults.cell_overvoltage && !bms.faults.cell_undervoltage);
	CHECK(!bms.faults.overtemperature && !bms.faults.undertemperature);
	CHECK(!bms.faults.ntc_open_wire && !bms.faults.cell_open_wire);
	CHECK(bms.adbms.config_ok[0] && bms.adbms.config_ok[11]);
	CHECK(bms.cells.cell_mV[0][0] == 3700 && bms.cells.cell_mV[11][11] == 3700);
	CHECK(bms.cells.cell_min_mV == 3700 && bms.cells.cell_max_mV == 3700);
	CHECK(bms.accu.voltage_sum_mV == 144u * 3700u);
	CHECK(near(bms.cells.ntc_dC[5][2], 250, 2));    // 25,0 C
	CHECK(near(bms.cells.die_dC[0], 300, 2));
	CHECK(chain.clrflag_writes > 0u);               // flags de diagnostico limpas em cada ciclo
	CHECK(bms.faults.config_invalid);               // sem EEPROM: corre com os defaults
}

static void test_cycle_timing(void) {

	boot();
	run_ms(1000);

	uint32_t cycles = bms.adbms.cycles;
	run_ms(1000);

	CHECK((bms.adbms.cycles - cycles) == 5u);       // um ciclo por tick de 200 ms
	CHECK(bms.adbms.overruns == 0u);
	CHECK(bms.adbms.cycle_us < 200000u);            // cabe no tick
}

static void test_wakes_sleeping_chain(void) {

	fake_reset();
	chain.asleep = true;
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(300);

	CHECK(!chain.asleep);                           // os pulsos de CS acordaram os 12
	CHECK(bms.cells.slaves_found == 12);
	CHECK(!bms.faults.slave_missing);
}

static void test_late_slave_needs_second_wake(void) {

	fake_reset();
	chain.wake_pulses_needed[11] = 20;              // o ultimo slave nao acorda com os primeiros 16 pulsos
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(300);

	CHECK(chain.wake_pulses >= 32u);                // acordou a cadeia uma segunda vez
	CHECK(bms.cells.slaves_found == 12);
	CHECK(!bms.faults.slave_missing);               // nao era um slave perdido
}

static void test_missing_slave(void) {

	fake_reset();
	chain.present = 11;
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(700);

	CHECK(bms.faults.slave_missing);                // so depois de voltar a acordar
	CHECK(bms.cells.slaves_found == 11);            // continua a medir os que respondem
	CHECK(bms.cells.cell_mV[10][0] == 3700);
	CHECK(bms.faults.adbms_timeout);                // o slave 12 nunca deu dados

	chain.present = 12;                             // slave volta
	run_ms(700);
	CHECK(!bms.faults.slave_missing);
	CHECK(!bms.faults.adbms_timeout);
}

static void test_dead_chain_recovers(void) {

	fake_reset();
	chain.present = 0;
	memset(&bms, 0, sizeof(bms));
	brain_start();
	run_ms(300);

	CHECK(bms.faults.slave_missing);                // so o enter_fault o poe com 0 slaves
	CHECK(bms.faults.adbms_timeout);
	CHECK(bms.cells.slaves_found == 0u);

	chain.wake_pulses = 0;                          // FAULT so dura 1 ms (FAULT_RETRY_US):
	run_ms(100);                                    // a cadeia continua a ser acordada
	CHECK(chain.wake_pulses >= 32u);

	chain.present = 12;
	run_ms(300);
	CHECK(bms.adbms.state != ADBMS_FAULT);
	CHECK(bms.cells.slaves_found == 12);
	CHECK(!bms.faults.adbms_timeout);
}

static void test_config_readback_mismatch(void) {

	boot();
	chain.config_corrupt[2] = true;
	run_ms(600);

	CHECK(!bms.adbms.config_ok[2]);
	CHECK(bms.adbms.config_errors > 0u);
	CHECK(bms.faults.adbms_timeout);                // slave com config errada nao publica
}

static void test_config_validation(void) {

	boot();

	bms.config.ntc_disabled_mask[0] = 0x003F;       // slave 1 sem NTC nenhum
	CHECK(!ADBMS_Init());                           // FS 2027 EV 5.8.6: menos de 30 %

	bms.config.ntc_disabled_mask[0] = 0;
	bms.config.slaves = 0;
	CHECK(!ADBMS_Init());
	CHECK(bms.config.slaves == BMS_MAX_SLAVES);     // medir a mais e o lado seguro

	bms.config.slaves = 12;
	bms.config.cells_per_slave = 40;
	CHECK(!ADBMS_Init());
	CHECK(bms.config.cells_per_slave == BMS_MAX_CELLS);
}

static void test_tick_timer_checked(void) {

	boot();
	CHECK(ADBMS_Init());                            // TIM6 como no .ioc: 200 ms

	fake_tim6.ARR = 500u - 1u;                      // alguem mudou o periodo no CubeMX
	CHECK(!ADBMS_Init());

	fake_tim6.ARR = 200u - 1u;
	fake_tim6.PSC = 32000u - 1u;                    // ou o prescaler: 2 kHz
	CHECK(!ADBMS_Init());
}

/* ================== CELULAS ================== */

static void test_overvoltage_fast(void) {

	boot();
	run_ms(400);
	chain.cell_mV[3][7] = 4250;

	uint32_t ms = ms_until(ov_active, 1000);
	CHECK(ms <= 220u);                              // FS 2027: 500 ms; aqui um ciclo
	CHECK(bms.cells.cell_max_mV == 4250);
	CHECK(bms.cells.cell_max_slave == 4 && bms.cells.cell_max_cell == 8);

	chain.cell_mV[3][7] = 4100;
	run_ms(300);
	CHECK(!bms.faults.cell_overvoltage);            // nao e latch: o latch esta fora do BMS
}

static void test_undervoltage(void) {

	boot();
	chain.cell_mV[0][0] = 2400;
	run_ms(300);

	CHECK(bms.faults.cell_undervoltage);
	CHECK(bms.cells.cell_min_mV == 2400);
}

static void test_pec_retry_corrects(void) {

	boot();
	uint32_t before = bms.pec.total;
	chain.pec_type = Cell;
	chain.pec_fail_reads[4] = 2;                    // duas leituras estragadas, a 3a passa
	run_ms(300);

	CHECK(bms.pec.total == before + 2u);
	CHECK(!bms.pec.last_read[4]);
	CHECK(!bms.faults.pec_error);
	CHECK(!bms.faults.adbms_timeout);
}

static void test_pec_persistent_times_out(void) {

	boot();
	run_ms(400);
	chain.pec_type = Cell;
	chain.pec_always[4] = true;

	uint32_t ms = ms_until(timeout_active, 2000);
	CHECK(ms <= 350u);                              // FS 2027 T 11.9: maximo 500 ms

	run_cycles(1);                                  // o pec_error so muda no fim do ciclo
	CHECK(bms.faults.pec_error);
	CHECK(bms.pec.last_read[4]);

	chain.pec_always[4] = false;
	run_cycles(2);
	CHECK(!bms.faults.adbms_timeout);
	CHECK(!bms.faults.pec_error);
}

static void test_sentinel_not_published(void) {

	boot();
	run_ms(300);
	chain.cell_sentinel[6][1] = true;               // registo nunca convertido / limpo
	run_ms(600);

	CHECK(bms.cells.cell_mV[6][1] == 3700);         // ficou o ultimo valor bom
	CHECK(bms.faults.adbms_timeout);
}

static void test_c_vs_s_mismatch(void) {

	boot();
	chain.cs_flt[1] = 1u << 3;                      // C-ADC e S-ADC discordam na celula 4
	chain.cell_mV[1][0] = 3600;
	run_ms(600);

	CHECK(bms.cells.cell_mV[1][0] == 3700);         // slave nao publicado
	CHECK(bms.faults.adbms_timeout);
}

static void test_spi_fault_flag(void) {

	boot();
	chain.spiflt[8] = true;
	run_ms(600);

	CHECK(bms.faults.adbms_timeout);
}

static void test_unused_channel_ignored(void) {

	boot();
	chain.cell_mV[0][12] = 0;                       // canal 13 nao tem celula (12s)
	run_ms(600);

	CHECK(!bms.faults.cell_undervoltage);
	CHECK(!bms.faults.cell_open_wire);
}

/* ================== TEMPERATURAS ================== */

static void test_temperature_curve(void) {

	boot();
	chain.gpio_mV[0][0] = fake_ntc_mV(-10.0);
	chain.gpio_mV[0][1] = fake_ntc_mV(45.0);
	chain.gpio_mV[0][4] = fake_ntc_mV(59.0);
	run_ms(300);

	CHECK(near(bms.cells.ntc_dC[0][0], -100, 3));
	CHECK(near(bms.cells.ntc_dC[0][1], 450, 3));
	CHECK(near(bms.cells.ntc_dC[0][4], 590, 3));
	CHECK(near(bms.cells.temp_max_dC, 590, 3));
	CHECK(near(bms.cells.temp_min_dC, -100, 3));
	CHECK(!bms.faults.overtemperature);             // 59 C ainda esta dentro
}

static void test_overtemperature_fast(void) {

	boot();
	run_ms(400);
	chain.gpio_mV[7][5] = fake_ntc_mV(65.0);

	uint32_t ms = ms_until(ot_active, 2000);
	CHECK(ms <= 400u);                              // FS 2027: 1 s; aqui menos de metade
	CHECK(near(bms.cells.temp_max_dC, 650, 3));
}

static void test_undertemperature(void) {

	boot();
	chain.gpio_mV[2][0] = fake_ntc_mV(-25.0);
	run_ms(300);

	CHECK(bms.faults.undertemperature);
}

static void test_ntc_open_and_short(void) {

	boot();
	chain.gpio_mV[4][1] = 2999;                     // aberto: o GPIO vai ao VREF2
	chain.gpio_mV[9][0] = 5;                        // curto ao V-
	run_ms(300);

	CHECK(bms.cells.ntc_open_wire[4][1]);
	CHECK(bms.cells.ntc_open_wire[9][0]);
	CHECK(bms.faults.ntc_open_wire);
	CHECK(!bms.faults.overtemperature);             // NTC em falha nao conta para o max/min
}

static void test_aux_redundancy_mismatch(void) {

	boot();
	chain.gpio2_offset_mV[5][3] = 50;               // AUX2 longe do AUX
	run_ms(300);

	CHECK(bms.cells.ntc_open_wire[5][3]);
	CHECK(bms.faults.ntc_open_wire);

	chain.gpio2_offset_mV[5][3] = 8;                // dentro do erro dos dois ADCs
	run_ms(300);
	CHECK(!bms.faults.ntc_open_wire);
}

static void test_disabled_ntc_ignored(void) {

	boot();
	chain.gpio_mV[2][2] = 2999;                     // slave 3, NTC 3 e 4: harness partido
	chain.gpio_mV[2][3] = 5;
	run_ms(300);

	CHECK(!bms.cells.ntc_open_wire[2][2]);
	CHECK(!bms.faults.ntc_open_wire);
	CHECK(!bms.faults.overtemperature && !bms.faults.undertemperature);
}

static void test_bad_vref2(void) {

	boot();
	run_ms(300);
	chain.vref2_mV[1] = 2800;                       // fora do datasheet
	run_ms(600);

	CHECK(bms.faults.adbms_timeout);
}

static void test_die_overtemperature(void) {

	boot();
	chain.die_dC[10] = 900;
	run_ms(300);

	CHECK(bms.faults.die_overtemperature);
	CHECK(near(bms.cells.die_dC[10], 900, 2));
}

/* ================== OPEN-WIRE ================== */

static void test_open_wire_healthy(void) {

	boot();
	run_ms(300);

	CHECK(!bms.faults.cell_open_wire);
	CHECK(!bms.cells.cell_open_wire[0][0]);
}

static void test_open_wire_broken_sense(void) {

	boot();
	chain.ow_pct[3][5][1] = 20;                     // fio partido: na passagem impar o condensador descarrega
	run_ms(300);

	CHECK(bms.cells.cell_open_wire[3][5]);
	CHECK(bms.faults.cell_open_wire);

	chain.ow_pct[3][5][1] = 90;                     // fio reparado
	run_ms(300);
	CHECK(!bms.faults.cell_open_wire);
}

static void test_open_wire_low_cell(void) {

	boot();
	chain.cell_mV[8][2] = 2200;                     // abaixo dos 2,3 V da config
	run_ms(300);

	CHECK(bms.cells.cell_open_wire[8][2]);
	CHECK(bms.faults.cell_open_wire);
}

static void test_open_wire_pec_keeps_result(void) {

	boot();
	chain.ow_pct[0][0][0] = 10;
	run_ms(300);
	CHECK(bms.cells.cell_open_wire[0][0]);

	chain.pec_type = S_volt;
	chain.pec_always[0] = true;                     // passagens invalidas: nao se reavalia
	chain.ow_pct[0][0][0] = 90;
	run_ms(300);
	CHECK(bms.cells.cell_open_wire[0][0]);          // fica o resultado anterior, nao "tudo bem"
}

/* ================== BALANCEAMENTO ================== */

static void test_balancing_needs_hv_off(void) {

	boot();
	chain.cell_mV[0][0] = 3760;
	run_cycles(2);

	CHECK(bms.balancing.stage == BALANCING_OFF);    // precarga nao esta em KILL
	CHECK(bms.balancing.dcc_mask[0] == 0u);
}

static void test_balancing_fine_and_end(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[0][0] = 3760;                     // 60 mV acima: so FINE
	chain.cell_mV[5][11] = 3720;
	run_cycles(2);

	CHECK(bms.balancing.stage == BALANCING_FINE);
	CHECK(bms.balancing.target_mV == 3700);
	CHECK(bms.balancing.dcc_mask[0] == 0x0001u);
	CHECK(bms.balancing.dcc_mask[5] == 0x0800u);
	CHECK(bms.balancing.cells_discharging == 2u);
	CHECK(chain.cfgb[0].dcc == 0x0001u);            // escrito no chip
	CHECK(bms.balancing.worst_slave == 1 && bms.balancing.worst_cell == 1);

	chain.cell_mV[0][0] = 3705;                     // dentro de metade do delta (10 mV)
	chain.cell_mV[5][11] = 3705;
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging == 0u);
	CHECK(chain.cfgb[0].dcc == 0u);                 // desligado no chip

	run_cycles(2);
	CHECK(bms.balancing.stage == BALANCING_OFF);
}

static void test_balancing_off_during_measurements(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[0][0] = 3760;
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging > 0u);

	chain.conversions_with_dcc = 0;
	run_cycles(5);
	CHECK(chain.conversions_with_dcc == 0u);        // ADCV e ADSV sempre com os DCC a 0
	CHECK(chain.cfgb[0].dcc == 0x0001u);            // e a descarga volta depois do open-wire
	CHECK(bms.balancing.cells_discharging > 0u);
}

static void test_balancing_rough_first(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[2][0] = 3900;                     // 200 mV acima
	chain.cell_mV[2][1] = 3750;                     // 50 mV acima
	run_cycles(2);

	CHECK(bms.balancing.stage == BALANCING_ROUGH);
	CHECK(bms.balancing.dcc_mask[2] == 0x0001u);    // so a que esta mais de 100 mV acima
}

static void test_balancing_ignores_out_of_window(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[1][0] = 4300;                     // acima dos 4,25 V da config: lixo, nao se descarrega
	chain.cell_mV[1][1] = 3760;
	run_cycles(2);

	CHECK((bms.balancing.dcc_mask[1] & 0x0001u) == 0u);
	CHECK((bms.balancing.dcc_mask[1] & 0x0002u) != 0u);
}

static void test_balancing_stops_on_hv(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[0][0] = 3760;
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging > 0u);

	bms.precharge.state = SWITCH_HVNEG;             // a precarga arrancou
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging == 0u);
	CHECK(chain.cfgb[0].dcc == 0u);
}

static void test_balancing_die_hold(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[0][0] = 3760;
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging > 0u);

	chain.die_dC[0] = 900;
	run_cycles(2);
	CHECK(bms.balancing.die_ot_hold);
	CHECK(bms.balancing.cells_discharging == 0u);

	chain.die_dC[0] = 300;
	run_ms(30000);
	CHECK(bms.balancing.die_ot_hold);               // ainda dentro dos 60 s
	CHECK(bms.balancing.stage == BALANCING_OFF);

	run_ms(31000);
	CHECK(!bms.balancing.die_ot_hold);
	CHECK(bms.balancing.cells_discharging > 0u);    // recomecou
}

static void test_balancing_stops_without_data(void) {

	boot();
	bms.precharge.state = KILL;
	chain.cell_mV[0][0] = 3760;
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging > 0u);

	chain.pec_type = Cell;
	chain.pec_always[7] = true;                     // perde as medicoes de um slave
	run_cycles(2);
	CHECK(bms.balancing.cells_discharging == 0u);   // nunca descarregar as cegas
}

/* ================== MAIN ================== */

typedef struct {
	const char *name;
	void (*run)(void);
} test_t;

#define TEST(test_function) { #test_function, test_function }

int main(void) {

	const test_t tests[] = {
		TEST(test_code_conversion),
		TEST(test_healthy_chain),
		TEST(test_cycle_timing),
		TEST(test_wakes_sleeping_chain),
		TEST(test_late_slave_needs_second_wake),
		TEST(test_missing_slave),
		TEST(test_dead_chain_recovers),
		TEST(test_config_readback_mismatch),
		TEST(test_config_validation),
		TEST(test_tick_timer_checked),
		TEST(test_overvoltage_fast),
		TEST(test_undervoltage),
		TEST(test_pec_retry_corrects),
		TEST(test_pec_persistent_times_out),
		TEST(test_sentinel_not_published),
		TEST(test_c_vs_s_mismatch),
		TEST(test_spi_fault_flag),
		TEST(test_unused_channel_ignored),
		TEST(test_temperature_curve),
		TEST(test_overtemperature_fast),
		TEST(test_undertemperature),
		TEST(test_ntc_open_and_short),
		TEST(test_aux_redundancy_mismatch),
		TEST(test_disabled_ntc_ignored),
		TEST(test_bad_vref2),
		TEST(test_die_overtemperature),
		TEST(test_open_wire_healthy),
		TEST(test_open_wire_broken_sense),
		TEST(test_open_wire_low_cell),
		TEST(test_open_wire_pec_keeps_result),
		TEST(test_balancing_needs_hv_off),
		TEST(test_balancing_fine_and_end),
		TEST(test_balancing_off_during_measurements),
		TEST(test_balancing_rough_first),
		TEST(test_balancing_ignores_out_of_window),
		TEST(test_balancing_stops_on_hv),
		TEST(test_balancing_die_hold),
		TEST(test_balancing_stops_without_data),
	};

	for (size_t test_index = 0; test_index < sizeof(tests) / sizeof(tests[0]); test_index++) {
		int failures_before = failures;
		tests[test_index].run();
		printf("%s %s\n", (failures == failures_before) ? "ok    " : "FALHOU", tests[test_index].name);
	}

	printf("\n%d verificacoes, %d falhadas\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}
