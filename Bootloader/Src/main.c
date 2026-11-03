/*
 * main.c
 *
 * Bootloader CAN do BMS master (STM32F412RE). Arranque:
 *
 *   1. contactores abertos e AMS_ERROR em erro, antes de tudo
 *   2. relogio (HSE -> 64 MHz), TIM2, UART1 para os logs
 *   3. o pedido da aplicacao na RAM (0xB00710AD) e os bitrates dela
 *   4. journal + verificacao do EXEC e do BACKUP (bl_core): arrancar, repor
 *      o backup ou ficar
 *   5. CAN1 e CAN2: heartbeat 0x581, diagnostico 0x582..0x589, pedidos 0x602
 *   6. depois de um reset do watchdog ou pelo pino, 500 ms a espera do
 *      uploader (um INFO segura o bootloader); sem isso arranca logo
 *
 * No bootloader fica-se ate um RESET do uploader, 30 s sem comandos a meio
 * de uma sessao, ou 5 min parado depois de um pedido sem sessao. Sem nada
 * para arrancar fica-se ate haver um upload (recovery, LED a piscar rapido).
 */

#include "stm32f4xx.h"
#include "bl_hw.h"
#include "bl_can.h"
#include "bl_core.h"
#include "bl_protocol.h"
#include "bl_log.h"
#include "bl_port.h"
#include "boot_shared.h"
#include "boot_names.h"

#define WINDOW_MS        500u
#define IDLE_EXIT_MS     300000u
#define REPLY_TIMEOUT_MS 100u
#define DIAG_FLUSH_MS    20u
#define DEFAULT_KBPS     500u

static bl_core_t core;
static bool can_up[BL_BUS_COUNT];
static uint32_t heartbeat_ms;
static bool window_used;

/* ================== PORT ================== */

uint32_t BlPort_Millis(void) {

	return BlHw_Millis();
}

void BlPort_Write(const char *text, uint32_t length) {

	BlHw_UartWrite(text, length);
}

static void send_diagnostic(uint8_t bus) {

	uint32_t id;
	uint8_t data[8];
	int kind = BlDiag_Next(bus, &id, data);

	if ((kind >= 0) && BlCan_Send(bus, id, data, 1u, NULL)) {
		BlDiag_Sent(bus, kind);
	}
}

void BlPort_DiagFlush(void) {

	uint32_t start = BlHw_Millis();

	while ((BlHw_Millis() - start) < DIAG_FLUSH_MS) {

		bool busy = false;

		for (uint8_t bus = 0; bus < BL_BUS_COUNT; bus++) {
			if (!can_up[bus]) {
				bl_diag.pending[bus] = 0;
			} else if (bl_diag.pending[bus] != 0u) {
				send_diagnostic(bus);
				busy = true;
			}
		}
		if (!busy) {
			break;
		}
	}
}

/* ================== CAN ================== */

static bool valid_kbps(uint16_t kbps) {

	return (kbps == 125u) || (kbps == 250u) || (kbps == 500u) || (kbps == 1000u);
}

static void heartbeat(bool now_please) {

	uint32_t now = BlHw_Millis();
	uint8_t data[8];

	if (!now_please && ((now - heartbeat_ms) < BL_HEARTBEAT_MS)) {
		return;
	}
	heartbeat_ms = now;
	BlProtocol_Heartbeat(now, data);
	for (uint8_t bus = 0; bus < BL_BUS_COUNT; bus++) {
		if (can_up[bus]) {
			(void) BlCan_Send(bus, BL_CAN_HEARTBEAT_ID, data, 1u, NULL);
		}
	}
}

static bool send_reply(uint8_t bus, const uint8_t reply[8]) {

	uint32_t mailbox;

	if (!BlCan_Send(bus, BL_CAN_RESPONSE_ID, reply, 0u, &mailbox)) {
		BlDiag_Error(BOOT_ERR_CAN_QUEUE, 0u, BL_SOURCE_CAN, BlCan_ErrorStatus(bus));
		return false;
	}

	uint32_t start = BlHw_Millis();

	while (BlCan_TxPending(bus, mailbox)) {
		if ((BlHw_Millis() - start) > REPLY_TIMEOUT_MS) {
			BlDiag_Error(BOOT_ERR_CAN_TIMEOUT, 0u, BL_SOURCE_CAN, BlCan_ErrorStatus(bus));
			BlCan_Abort(bus, mailbox);
			return false;
		}
	}
	return true;
}

// uma trama de cada bus; true = houve um comando aceite
static bool service(void) {

	bool accepted = false;

	for (uint8_t bus = 0; bus < BL_BUS_COUNT; bus++) {

		uint32_t id;
		uint8_t data[8];
		uint8_t dlc;
		uint8_t reply[8];

		if (!can_up[bus] || !BlCan_Receive(bus, &id, data, &dlc) || (id != BL_CAN_REQUEST_ID)) {
			continue;
		}

		uint8_t status = BlProtocol_Frame(&core, bus, data, dlc, reply);
		bool sent = send_reply(bus, reply);

		if (status == BL_OK) {
			accepted = true;
			if (data[0] == BL_CMD_INFO) {
				heartbeat(true);
			}
		}
		if (bl_session.reset_requested) {
			BlPort_DiagFlush();
			BlLog("RESET pedido pelo uploader%s", sent ? "" : " (a resposta nao saiu)");
			BlHw_Reset();
		}
	}

	heartbeat(false);
	for (uint8_t bus = 0; bus < BL_BUS_COUNT; bus++) {
		if (can_up[bus]) {
			send_diagnostic(bus);
		}
	}
	return accepted;
}

/* ================== ARRANCAR A APLICACAO ================== */

static void boot_application(void) {

	if (!BlCore_PrepareBoot(&core)) {
		BlLog("nada para arrancar (%s): fica no bootloader a espera de um upload", BootNames_Error(core.reason));
		BlDiag_Snapshot();
		return;
	}

	boot_shared_t shared;

	memset(&shared, 0, sizeof(shared));
	shared.can1_kbps = core.can1_kbps;
	shared.can2_kbps = core.can2_kbps;
	shared.reset_flags = core.reset_flags;
	shared.run = core.run;
	shared.event = core.event;
	shared.attempts = (core.run == BOOT_RUN_PENDING) ? core.journal.state.attempts : 0u;
	shared.flags = (uint8_t) ((core.iwdg ? BOOT_SHARED_FLAG_IWDG : 0u) | (core.backup_ok ? BOOT_SHARED_FLAG_BACKUP : 0u) | (window_used ? BOOT_SHARED_FLAG_WINDOW : 0u));
	shared.boot_version = BOOT_VERSION_PACKED;
	shared.exec_id = core.exec.id;
	shared.request = 0u;
	BootShared_Seal(&shared);
	BootShared_Write(&shared);

	BlLog("a arrancar v%u.%u.%u+%u (%s, %s)%s", core.exec.version_major, core.exec.version_minor, core.exec.version_revision, core.exec.version_build,
			BootNames_Run(core.run), BootNames_Event(core.event), core.iwdg ? ", IWDG armado" : "");
	BlDiag_Snapshot();
	BlPort_DiagFlush();

	if (core.iwdg) {
		BlHw_ArmIwdg();
	}
	BlHw_Jump(BOOT_APP_ADDRESS);
}

/* ================== MAIN ================== */

int main(void) {

	BlHw_SafeOutputs();

	bool hse = BlHw_ClockInit();

	BlHw_TimerInit();
	BlHw_UartInit();

	uint32_t reset_flags = BlHw_TakeResetFlags();
	boot_shared_t shared;

	BootShared_Read(&shared);

	bool requested = (shared.request == BOOT_REQUEST_MAGIC);

	BOOT_SHARED->request = 0u;   // consumido: o proximo reset ja nao fica

	BlLog("LART BMS bootloader %u.%u.%u, reset 0x%08x%s", BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR, BOOT_VERSION_PATCH, reset_flags,
			hse ? "" : ", SEM HSE (CAN fora de tolerancia)");

	BlCore_Start(&core, reset_flags, requested, DEFAULT_KBPS, DEFAULT_KBPS);

	// bitrates: os da aplicacao (RAM), senao os da ultima sessao (journal), senao 500
	if (BootShared_Valid(&shared) && valid_kbps(shared.can1_kbps) && valid_kbps(shared.can2_kbps)) {
		core.can1_kbps = shared.can1_kbps;
		core.can2_kbps = shared.can2_kbps;
	} else if (core.journal.found && valid_kbps(core.journal.state.can1_kbps) && valid_kbps(core.journal.state.can2_kbps)) {
		core.can1_kbps = core.journal.state.can1_kbps;
		core.can2_kbps = core.journal.state.can2_kbps;
	}

	can_up[BL_CAN1] = BlCan_Init(BL_CAN1, core.can1_kbps, BlHw_PclkHz(), BL_CAN_REQUEST_ID);
	can_up[BL_CAN2] = BlCan_Init(BL_CAN2, core.can2_kbps, BlHw_PclkHz(), BL_CAN_REQUEST_ID);
	BlLog("CAN1 %u kbit/s %s, CAN2 %u kbit/s %s", core.can1_kbps, !can_up[BL_CAN1] ? "FALHOU" : (BlCan_Synced(BL_CAN1) ? "ok" : "sem bus"), core.can2_kbps,
			!can_up[BL_CAN2] ? "FALHOU" : (BlCan_Synced(BL_CAN2) ? "ok" : "sem bus"));

	BlProtocol_Init(BlHw_Millis());
	BlDiag_FromCore(&core);
	heartbeat(true);
	BlPort_DiagFlush();

	bool stay = requested || !BlCore_CanBoot(&core);

	// depois de um watchdog ou de um reset pelo pino o uploader pode apanhar o bootloader
	if (!stay && BlCore_WindowReset(reset_flags)) {

		uint32_t start = BlHw_Millis();

		BlLog("janela de %u ms para o uploader", WINDOW_MS);
		while (!stay && ((BlHw_Millis() - start) < WINDOW_MS)) {
			stay = service();
		}
		if (stay) {
			window_used = true;
			core.entry_flags |= BL_ENTRY_WINDOW;
			BlDiag_FromCore(&core);
			BlLog("uploader na janela: fica no bootloader");
		}
	}

	if (!stay) {
		boot_application();   // so volta se nao houver nada para arrancar
	}

	for (;;) {

		(void) service();

		uint32_t now = BlHw_Millis();

		if (BlProtocol_Busy()) {
			if ((now - bl_session.last_ok_ms) > BL_INACTIVITY_MS) {
				BlDiag_Error(BOOT_ERR_INACTIVITY, 1u, BL_SOURCE_BOOT, bl_session.received);
				BlPort_DiagFlush();
				BlLog("30 s sem comandos a meio da sessao: reset");
				BlHw_Reset();
			}
		} else if (BlCore_CanBoot(&core) && ((now - bl_session.last_ok_ms) > IDLE_EXIT_MS)) {
			BlLog("5 min sem uploader: volta a aplicacao");
			boot_application();
			bl_session.last_ok_ms = BlHw_Millis();
		}

		// piscar lento a espera de um upload, rapido sem nada para arrancar
		uint32_t period = BlCore_CanBoot(&core) ? 250u : 100u;

		BlHw_Led(((now / period) & 1u) != 0u);
	}
}
