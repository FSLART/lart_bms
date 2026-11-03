/*
 * can_messages.c
 *
 * Ver can_messages.h. Os campos crus do DBC sao preenchidos com inteiros: as
 * escalas do DBC batem com as unidades do bms (mV, 0,01 C, 0,1 V...).
 */

#include "can_messages.h"
#include "can_bus.h"
#include "charger.h"
#include "isa.h"
#include "precharge.h"
#include "faults.h"
#include "powertrain_t26.h"
#include "handcart_t26.h"
#include <string.h>

/* ENTER_BOOT do protocolo do bootloader da ACU (mcu_boot bootloader.dbc):
 * 0x601, DLC 8, 01 00 00 00 00 00 00 00. So no CAN2: no CAN1 o 0x601 e o
 * SLAVE_01_VOLTAGE_ID_2 que o proprio BMS envia */
#define ENTER_BOOT_FRAME_ID 0x601u

#define DBC_SLAVES        12u   // o DBC so tem Slave_01..Slave_12
#define FRAMES_PER_SLAVE  7u    // 0x600 + 7 x slave + k
#define SLAVE_IDLE        0xFFu

static uint32_t master_last_ms;
static uint32_t slaves_last_ms;
static uint32_t charger_last_ms;
static uint8_t slave_cursor = SLAVE_IDLE;
static uint8_t balancing_cursor;
static bool last_request_enable;
static bool bootloader_requested;

void CanMessages_Init(void) {

	master_last_ms = 0;
	slaves_last_ms = 0;
	charger_last_ms = 0;
	slave_cursor = SLAVE_IDLE;
	balancing_cursor = 0;
	last_request_enable = false;
	bootloader_requested = false;
}

bool CanMessages_TakeBootloaderRequest(void) {

	bool requested = bootloader_requested;

	bootloader_requested = false;
	return requested;
}

/* ================== RX ================== */

static void on_programmer(uint8_t bus, const can_frame_t *frame) {

	struct powertrain_t26_start_programmer_t message;

	if (((bms.config.bootloader_can_mask >> bus) & 1u) == 0u) {
		return;
	}
	if ((powertrain_t26_start_programmer_unpack(&message, frame->data, frame->dlc) == 0) && (message.programming_request != 0u)) {
		bootloader_requested = true;
	}
}

static void on_enter_boot(const can_frame_t *frame) {

	static const uint8_t enter[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };

	if ((((bms.config.bootloader_can_mask >> CAN_BUS_2) & 1u) != 0u) && (frame->dlc == 8u) && (memcmp(frame->data, enter, sizeof(enter)) == 0)) {
		bootloader_requested = true;
	}
}

static void process_can1(const can_frame_t *frame, uint32_t now_ms) {

	if (frame->extended) {
		return;   // o CAN1 so tem IDs de 11 bits
	}

	if (Isa_OnFrame(&bms.isa_pack, bms.config.isa_pack_invert, frame->id, frame->data, frame->dlc, now_ms)) {
		return;
	}

	switch (frame->id) {

	case POWERTRAIN_T26_START_PRE_CHARGE_FRAME_ID: {
		struct powertrain_t26_start_pre_charge_t message;
		if (powertrain_t26_start_pre_charge_unpack(&message, frame->data, frame->dlc) == 0) {
			Precharge_OnVcuRequest(message.precharge_request, now_ms);
		}
		break;
	}

	case POWERTRAIN_T26_VCU_STATES_FRAME_ID:
		Precharge_OnVcuAlive(now_ms);
		break;

	case POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID:
		on_programmer(CAN_BUS_1, frame);
		break;

	default:
		break;
	}
}

static void process_can2(const can_frame_t *frame, uint32_t now_ms) {

	if (frame->extended) {

		struct handcart_t26_charger_status_p1000_t message;

		if ((frame->id == HANDCART_T26_CHARGER_STATUS_P1000_FRAME_ID) && (handcart_t26_charger_status_p1000_unpack(&message, frame->data, frame->dlc) == 0)) {

			charger_status_t status = {
				.output_voltage_dV = message.output_voltage,
				.output_current_dA = message.output_current,
				.hw_failure = message.hw_failure != 0u,
				.temp_otp = message.temp_otp != 0u,
				.input_voltage_fault = message.input_voltage_fault != 0u,
				.starting_state_fault = message.starting_state_fault != 0u,
				.comm_timeout = message.comm_timeout != 0u,
				.temperature_C = (int16_t) ((int16_t) message.charger_temperature - 40),
			};
			Charger_OnStatus(&status, now_ms);
		}
		return;
	}

	if (Isa_OnFrame(&bms.isa_handcart, bms.config.isa_handcart_invert, frame->id, frame->data, frame->dlc, now_ms)) {
		return;
	}

	switch (frame->id) {

	case HANDCART_T26_HANDCART_SWITCH_FEEDBACK_FRAME_ID: {
		struct handcart_t26_handcart_switch_feedback_t message;
		if (handcart_t26_handcart_switch_feedback_unpack(&message, frame->data, frame->dlc) == 0) {
			Charger_OnSwitch(message.switch_feedback != 0u, now_ms);
		}
		break;
	}

	case POWERTRAIN_T26_START_PROGRAMMER_FRAME_ID:
		on_programmer(CAN_BUS_2, frame);
		break;

	case ENTER_BOOT_FRAME_ID:
		on_enter_boot(frame);
		break;

	default:
		break;
	}
}

void CanMessages_Process(uint32_t now_ms) {

	can_frame_t frame;

	// limite por chamada: um barramento a inundar nao prende o brain_loop
	for (uint8_t count = 0; (count < 32u) && CanBus_Receive(CAN_BUS_1, &frame); count++) {
		process_can1(&frame, now_ms);
	}
	for (uint8_t count = 0; (count < 32u) && CanBus_Receive(CAN_BUS_2, &frame); count++) {
		process_can2(&frame, now_ms);
	}
}

/* ================== TX ================== */

static uint16_t clamp_u16(int32_t value) {

	return (uint16_t) ((value < 0) ? 0 : ((value > 65535) ? 65535 : value));
}

static uint8_t clamp_u8(int32_t value) {

	return (uint8_t) ((value < 0) ? 0 : ((value > 255) ? 255 : value));
}

static void send(uint8_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t dlc, bool high_priority) {

	can_frame_t frame = { .id = id, .extended = extended, .dlc = dlc };

	memcpy(frame.data, data, dlc);
	(void) CanBus_Send(bus, &frame, high_priority);
}

static bool slave_cells_fresh(uint8_t slave_index, uint32_t now_ms) {

	uint32_t valid_ms = bms.adbms.cells_valid_ms[slave_index];

	return (valid_ms != 0u) && ((now_ms - valid_ms) <= bms.config.adbms_data_timeout_ms);
}

static bool slave_temps_fresh(uint8_t slave_index, uint32_t now_ms) {

	uint32_t valid_ms = bms.adbms.temps_valid_ms[slave_index];

	return (valid_ms != 0u) && ((now_ms - valid_ms) <= bms.config.adbms_data_timeout_ms);
}

/* NTC desligado na config: herda o anterior (em cascata), como no firmware
 * antigo, para o Grafana nao mostrar zeros. So para mostrar: nas protecoes
 * os desligados sao ignorados */
static int16_t ntc_for_display(uint8_t slave_index, uint8_t ntc_index) {

	for (int16_t source = ntc_index; source >= 0; source--) {
		if (((bms.config.ntc_disabled_mask[slave_index] >> source) & 1u) == 0u) {
			return bms.cells.ntc_dC[slave_index][source];
		}
	}

	return 0;
}

/* dois faults para o 0x703: primeiro os que seguram o AMS_ERROR. Codigo =
 * numero do fault + 1 (0 = nenhum); indice = slave e celula/NTC se fizer sentido */
static void fault_codes(uint8_t codes[2], uint8_t index_type[2], uint8_t index_value[2]) {

	uint64_t mask = (bms.ams_error.sources_mask != 0u) ? bms.ams_error.sources_mask : bms.ams_error.faults_mask;
	uint8_t found = 0;

	memset(codes, 0, 2);
	memset(index_type, 0, 2);
	memset(index_value, 0, 2);

	for (uint8_t fault_index = 0; (fault_index < 64u) && (found < 2u); fault_index++) {

		if (((mask >> fault_index) & 1u) == 0u) {
			continue;
		}

		codes[found] = (uint8_t) (fault_index + 1u);

		switch (fault_index) {
		case 0:   // CELL_OVERVOLTAGE
			index_type[found] = bms.cells.cell_max_slave;
			index_value[found] = bms.cells.cell_max_cell;
			break;
		case 1:   // CELL_UNDERVOLTAGE
			index_type[found] = bms.cells.cell_min_slave;
			index_value[found] = bms.cells.cell_min_cell;
			break;
		case 3:   // OVERTEMPERATURE
			index_type[found] = bms.cells.temp_max_slave;
			index_value[found] = bms.cells.temp_max_ntc;
			break;
		case 4:   // UNDERTEMPERATURE
			index_type[found] = bms.cells.temp_min_slave;
			index_value[found] = bms.cells.temp_min_ntc;
			break;
		default:
			break;
		}
		found++;
	}
}

static void send_master(uint8_t bus) {

	uint8_t data[8];

	{
		struct powertrain_t26_master_msc_id_1_t message = { 0 };

		message.mcu_vref = clamp_u8(((int32_t) bms.board.vdda_mV - 2000) / 10);   // 0,01 V, offset 2 V
		message.mcu_temperature = clamp_u8(bms.board.mcu_temp_dC / 10);
		message.ams_current_draw = bms.board.current_mA;
		message.master_firmware_version = BMS_FIRMWARE_VERSION;
		message.adbms_pec_error = bms.faults.pec_error ? 1u : 0u;
		message.master_fan_pwm = bms.fan_cells.pwm;
		message.master_state = (uint8_t) bms.system.state;
		message.fault_counter = bms.ams_error.fault_count;
		if (powertrain_t26_master_msc_id_1_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_MSC_ID_1_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_MSC_ID_1_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_master_pre_charge_id_1_t message = { 0 };

		message.precharge_ctc_air_pos_state = bms.precharge.air_pos_fb ? 1u : 0u;
		message.precharge_ctc_air_min_state = bms.precharge.air_neg_fb ? 1u : 0u;
		message.precharge_ctc_charge_state = bms.precharge.precharge_fb ? 1u : 0u;
		message.precharge_ctc_discharge_state = bms.precharge.discharge_fb ? 1u : 0u;
		message.precharge_state = (uint8_t) bms.precharge.state;
		if (powertrain_t26_master_pre_charge_id_1_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_PRE_CHARGE_ID_1_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_master_msc_id_2_t message = { 0 };
		uint8_t codes[2];
		uint8_t index_type[2];
		uint8_t index_value[2];

		fault_codes(codes, index_type, index_value);
		message.master_runtime = clamp_u16((int32_t) (bms.system.uptime_ms / 1000u));
		message.fault1_code = codes[0];
		message.fault1_index_type = index_type[0];
		message.fault1_index_value = index_value[0];
		message.fault2_code = codes[1];
		message.fault2_index_type = index_type[1];
		message.fault2_index_value = index_value[1];
		if (powertrain_t26_master_msc_id_2_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_MSC_ID_2_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_MSC_ID_2_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_master_msc_id_3_t message = { 0 };

		message.overall_maximum_voltage = bms.cells.cell_max_mV;
		message.overall_minimum_voltage = bms.cells.cell_min_mV;
		message.overall_maximum_temperature = clamp_u16((int32_t) bms.cells.temp_max_dC * 10);   // 0,01 C
		message.overall_minimum_temperature = clamp_u16((int32_t) bms.cells.temp_min_dC * 10);
		if (powertrain_t26_master_msc_id_3_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_MSC_ID_3_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_MSC_ID_3_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_master_soc_accumulator_t message = { 0 };

		message.soc_integer = (uint8_t) (bms.soc.percent_x100 / 100u);
		message.soc_float = (bms.soc.percent_x100 > 0u) ? (uint16_t) (bms.soc.percent_x100 - 1u) : 0u;   // escala 0,01, offset 0,01
		if (powertrain_t26_master_soc_accumulator_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_SOC_ACCUMULATOR_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_SOC_ACCUMULATOR_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_master_msc_id_4_t message = { 0 };
		uint8_t slaves = (bms.config.slaves > 0u) ? bms.config.slaves : 1u;

		if (balancing_cursor >= slaves) {
			balancing_cursor = 0;
		}
		message.slaves_detected = bms.cells.slaves_found;
		message.bal_bitmask_slave_id = (uint8_t) (balancing_cursor + 1u);
		message.bal_bitmask = bms.balancing.dcc_mask[balancing_cursor];
		if (powertrain_t26_master_msc_id_4_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_MASTER_MSC_ID_4_FRAME_ID, false, data, POWERTRAIN_T26_MASTER_MSC_ID_4_LENGTH, true);
		}
	}
	{
		struct powertrain_t26_ams_sdc_feedback_t message = { 0 };

		message.sdc_state = bms.precharge.sdc_closed ? 1u : 0u;
		if (powertrain_t26_ams_sdc_feedback_pack(data, &message, sizeof(data)) > 0) {
			send(bus, POWERTRAIN_T26_AMS_SDC_FEEDBACK_FRAME_ID, false, data, POWERTRAIN_T26_AMS_SDC_FEEDBACK_LENGTH, true);
		}
	}
}

/* as 7 tramas de um slave, com as funcoes do Slave_01 (o layout e igual em
 * todos, so o ID muda: 0x600 + 7 x slave). Sem dados frescos vai a zeros:
 * um valor velho com ar de bom e pior que um zero */
static void send_slave(uint8_t bus, uint8_t slave_index, uint32_t now_ms) {

	uint32_t base_id = POWERTRAIN_T26_SLAVE_01_VOLTAGE_ID_1_FRAME_ID + ((uint32_t) FRAMES_PER_SLAVE * slave_index);
	bool cells_fresh = slave_cells_fresh(slave_index, now_ms);
	bool temps_fresh = slave_temps_fresh(slave_index, now_ms);
	uint8_t cell_count = bms.config.cells_per_slave;
	uint16_t cell_mV[12] = { 0 };
	uint16_t ntc_cC[6] = { 0 };
	uint8_t data[8];

	for (uint8_t cell_index = 0; cells_fresh && (cell_index < 12u) && (cell_index < cell_count); cell_index++) {
		cell_mV[cell_index] = bms.cells.cell_mV[slave_index][cell_index];
	}
	for (uint8_t ntc_index = 0; temps_fresh && (ntc_index < 6u) && (ntc_index < bms.config.ntcs_per_slave); ntc_index++) {
		ntc_cC[ntc_index] = clamp_u16((int32_t) ntc_for_display(slave_index, ntc_index) * 10);
	}

	{
		struct powertrain_t26_slave_01_voltage_id_1_t message = { cell_mV[0], cell_mV[1], cell_mV[2], cell_mV[3] };
		if (powertrain_t26_slave_01_voltage_id_1_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 0u, false, data, 8u, false);
		}
	}
	{
		struct powertrain_t26_slave_01_voltage_id_2_t message = { cell_mV[4], cell_mV[5], cell_mV[6], cell_mV[7] };
		if (powertrain_t26_slave_01_voltage_id_2_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 1u, false, data, 8u, false);
		}
	}
	{
		struct powertrain_t26_slave_01_voltage_id_3_t message = { cell_mV[8], cell_mV[9], cell_mV[10], cell_mV[11] };
		if (powertrain_t26_slave_01_voltage_id_3_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 2u, false, data, 8u, false);
		}
	}

	uint16_t hottest_cC = 0;
	uint16_t coldest_cC = UINT16_MAX;

	for (uint8_t ntc_index = 0; ntc_index < 6u; ntc_index++) {
		if (temps_fresh && (ntc_index < bms.config.ntcs_per_slave) && (((bms.config.ntc_disabled_mask[slave_index] >> ntc_index) & 1u) == 0u)) {
			hottest_cC = (ntc_cC[ntc_index] > hottest_cC) ? ntc_cC[ntc_index] : hottest_cC;
			coldest_cC = (ntc_cC[ntc_index] < coldest_cC) ? ntc_cC[ntc_index] : coldest_cC;
		}
	}
	if (coldest_cC == UINT16_MAX) {
		coldest_cC = hottest_cC;
	}

	{
		struct powertrain_t26_slave_01_temperature_id_1_t message = { ntc_cC[0], ntc_cC[1], ntc_cC[2], ntc_cC[3] };
		if (powertrain_t26_slave_01_temperature_id_1_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 3u, false, data, 8u, false);
		}
	}
	{
		struct powertrain_t26_slave_01_temperature_id_2_t message = { 0 };
		message.temperature_value_5 = ntc_cC[4];
		message.temperature_value_6 = ntc_cC[5];
		message.temperature_maximum = hottest_cC;
		message.temperature_delta = (uint16_t) (hottest_cC - coldest_cC);
		if (powertrain_t26_slave_01_temperature_id_2_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 4u, false, data, 8u, false);
		}
	}

	uint32_t sum_mV = 0;
	uint16_t lowest_mV = UINT16_MAX;
	uint16_t highest_mV = 0;
	uint8_t open_wires = 0;
	uint8_t over = 0;
	uint8_t under = 0;
	uint8_t first_bad_cell = 0;

	for (uint8_t cell_index = 0; cells_fresh && (cell_index < cell_count); cell_index++) {

		uint16_t voltage_mV = bms.cells.cell_mV[slave_index][cell_index];

		sum_mV += voltage_mV;
		lowest_mV = (voltage_mV < lowest_mV) ? voltage_mV : lowest_mV;
		highest_mV = (voltage_mV > highest_mV) ? voltage_mV : highest_mV;
		open_wires += bms.cells.cell_open_wire[slave_index][cell_index] ? 1u : 0u;

		if ((voltage_mV > bms.config.cell_max_mV) || (voltage_mV < bms.config.cell_min_mV)) {
			over += (voltage_mV > bms.config.cell_max_mV) ? 1u : 0u;
			under += (voltage_mV < bms.config.cell_min_mV) ? 1u : 0u;
			if (first_bad_cell == 0u) {
				first_bad_cell = (uint8_t) (cell_index + 1u);
			}
		}
	}
	if (!cells_fresh || (cell_count == 0u)) {
		lowest_mV = 0;
	}

	{
		struct powertrain_t26_slave_01_msc_id_1_t message = { 0 };
		message.module_voltage_sum = clamp_u16((int32_t) sum_mV);
		message.module_voltage_avg = (cells_fresh && (cell_count > 0u)) ? (uint16_t) (sum_mV / cell_count) : 0u;
		message.module_voltage_min = lowest_mV;
		message.module_voltage_max = highest_mV;
		if (powertrain_t26_slave_01_msc_id_1_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 5u, false, data, 8u, false);
		}
	}
	{
		struct powertrain_t26_slave_01_msc_id_2_t message = { 0 };
		message.module_voltage_delta = (uint16_t) (highest_mV - lowest_mV);
		message.module_ic_voltage = clamp_u16((int32_t) sum_mV);   // tensao do modulo vista pelo IC
		message.module_open_wire = open_wires;
		message.module_ic_temperature = temps_fresh ? clamp_u16(bms.cells.die_dC[slave_index] / 10) : 0u;
		message.module_overvoltage = over;
		message.module_undervoltage = under;
		message.module_under_over_identifier = first_bad_cell;
		if (powertrain_t26_slave_01_msc_id_2_pack(data, &message, sizeof(data)) > 0) {
			send(bus, base_id + 6u, false, data, 8u, false);
		}
	}
}

static void send_charger_request(void) {

	struct handcart_t26_bms_charging_request_p1000_t message = { 0 };
	uint8_t data[8];

	message.max_charging_voltage = bms.charger.req_voltage_dV;   // 0,1 V
	message.max_charging_current = bms.charger.req_current_dA;   // 0,1 A
	message.control = bms.charger.req_enable ? 0u : 1u;          // 0 = carregar, 1 = parar

	if (handcart_t26_bms_charging_request_p1000_pack(data, &message, sizeof(data)) > 0) {
		send(CAN_BUS_2, HANDCART_T26_BMS_CHARGING_REQUEST_P1000_FRAME_ID, true, data, HANDCART_T26_BMS_CHARGING_REQUEST_P1000_LENGTH, true);
	}
}

void CanMessages_Transmit(uint32_t now_ms) {

	const bms_config_t *config = &bms.config;
	bool mirror = config->can2_mirror_charging && Charger_SessionActive();
	uint8_t slaves = (config->slaves < DBC_SLAVES) ? config->slaves : DBC_SLAVES;

	if ((config->can_master_ms > 0u) && ((now_ms - master_last_ms) >= config->can_master_ms)) {
		master_last_ms = now_ms;
		send_master(CAN_BUS_1);
		if (mirror) {
			send_master(CAN_BUS_2);
		}
		balancing_cursor++;
	}

	// slaves: um por chamada, para espalhar as 84 tramas pelo barramento
	if ((config->can_slaves_ms > 0u) && (slave_cursor == SLAVE_IDLE) && ((now_ms - slaves_last_ms) >= config->can_slaves_ms)) {
		slaves_last_ms = now_ms;
		slave_cursor = 0;
	}
	if (slave_cursor != SLAVE_IDLE) {
		if (slave_cursor >= slaves) {
			slave_cursor = SLAVE_IDLE;
		} else if (CanBus_FreeSlots(CAN_BUS_1, false) >= FRAMES_PER_SLAVE) {
			send_slave(CAN_BUS_1, slave_cursor, now_ms);
			if (mirror && (CanBus_FreeSlots(CAN_BUS_2, false) >= FRAMES_PER_SLAVE)) {
				send_slave(CAN_BUS_2, slave_cursor, now_ms);
			}
			slave_cursor++;
		}
	}

	// carregador: periodico com o handcart presente, e logo que o pedido muda
	bool charger_link = bms.charger.handcart_connected || Charger_SessionActive();

	if (charger_link && (((now_ms - charger_last_ms) >= config->charge_command_ms) || (bms.charger.req_enable != last_request_enable))) {
		charger_last_ms = now_ms;
		last_request_enable = bms.charger.req_enable;
		send_charger_request();
	}
}
