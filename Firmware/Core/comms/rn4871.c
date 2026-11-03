/*
 * rn4871.c
 *
 * Ver rn4871.h. Respostas do modulo (RN4870/71 User Guide): "CMD> " ao
 * entrar em modo de comando, "AOK" / "Err" aos comandos, "END" ao sair,
 * e os status entre '%' no modo de dados.
 */

#include "rn4871.h"
#include "uart_link.h"
#include "console.h"
#include <string.h>

#define BOOT_WAIT_MS      1500u   // o modulo arranca depois de alimentado ou de um reboot
#define QUIET_MS          100u    // linha calada antes do "$$$"
#define IDLE_WAIT_MS      2000u   // a espera do TX vazio para a vigia
#define COMMAND_TIMEOUT   1000u
#define REBOOT_GAP_MS     200u    // entre o "$$$" e o "R,1"
#define REBOOT_WAIT_MS    2500u
#define ENTER_RETRIES     3u

static const char *const state_names[] = { "BOOT", "ENTER_CMD", "GET_NAME", "SET_NAME", "EXIT_CMD", "DATA", "PROBE_ENTER", "PROBE_EXIT", "REBOOT" };

_Static_assert((sizeof(state_names) / sizeof(state_names[0])) == (BT_REBOOT + 1), "um nome por estado do RN4871");

static char response[96];        // o que o modulo respondeu em modo de comando
static uint8_t response_length;
static char token[48];           // %...% em modo de dados
static uint8_t token_length;
static bool in_token;
static bool sent;                // o comando deste estado ja foi enviado
static uint8_t reboot_phase;
static uint32_t sent_ms;
static uint8_t retries;
static bool reboot_requested;
static bool reboot_seen;
static bool reboot_in_command;   // ja em modo de comando (depois do SN): sem "$$$"

const char* Rn4871_StateName(bt_state_t state) {

	return ((unsigned) state <= (unsigned) BT_REBOOT) ? state_names[state] : "?";
}

static void enter(bt_state_t state, uint32_t now_ms) {

	bms.bt.state = state;
	bms.bt.state_ms = now_ms;
	sent = false;
	reboot_phase = 0;
	response_length = 0;
	response[0] = '\0';
}

static void send_command(const char *text, uint32_t now_ms) {

	response_length = 0;
	response[0] = '\0';
	(void) UartLink_Write(UART_LINK_2, text, (uint16_t) strlen(text));
	sent = true;
	sent_ms = now_ms;
}

static bool response_has(const char *text) {

	return strstr(response, text) != NULL;
}

static void alive(uint32_t now_ms) {

	bms.bt.rn4871_alive = true;
	bms.bt.last_ok_ms = now_ms;
}

void Rn4871_Init(uint32_t now_ms) {

	memset(&bms.bt, 0, sizeof(bms.bt));
	in_token = false;
	token_length = 0;
	retries = 0;
	reboot_requested = false;
	reboot_seen = false;
	reboot_in_command = false;
	bms.bt.last_probe_ms = now_ms;
	bms.bt.last_reboot_ms = now_ms;
	enter(BT_BOOT, now_ms);
}

bool Rn4871_DataMode(void) {

	return bms.bt.state == BT_DATA;
}

bool Rn4871_Write(const char *text, uint16_t length) {

	if (!Rn4871_DataMode() || (memchr(text, '$', length) != NULL)) {
		return false;
	}
	return UartLink_Write(UART_LINK_2, text, length);
}

void Rn4871_RequestReboot(void) {

	reboot_requested = true;
}

/* ================== RX ================== */

static void handle_token(uint32_t now_ms) {

	token[token_length] = '\0';
	alive(now_ms);

	if ((strncmp(token, "CONNECT", 7) == 0) || (strncmp(token, "STREAM_OPEN", 11) == 0)) {
		bms.bt.connected = true;
	} else if (strncmp(token, "DISCONNECT", 10) == 0) {
		bms.bt.connected = false;
		Console_LinkReset(CONSOLE_BLE);   // o PIN volta a ser pedido na proxima ligacao
	} else if (strncmp(token, "REBOOT", 6) == 0) {
		bms.bt.connected = false;
		reboot_seen = true;
		Console_LinkReset(CONSOLE_BLE);
	}
}

static void handle_byte(char character, uint32_t now_ms) {

	bool command_mode = (bms.bt.state != BT_DATA) && (bms.bt.state != BT_BOOT);

	if (command_mode) {
		if (response_length >= (sizeof(response) - 1u)) {
			memmove(response, &response[32], response_length - 32u);   // fica com o fim
			response_length = (uint8_t) (response_length - 32u);
		}
		response[response_length++] = character;
		response[response_length] = '\0';
		if (strstr(response, "%REBOOT%") != NULL) {
			reboot_seen = true;
		}
		return;
	}

	// modo de dados: %STATUS% do modulo ou bytes da aplicacao
	if (character == '%') {
		if (in_token) {
			handle_token(now_ms);
			in_token = false;
		} else {
			in_token = true;
			token_length = 0;
		}
		return;
	}

	if (in_token) {
		if (token_length < (sizeof(token) - 1u)) {
			token[token_length++] = character;
		} else {
			in_token = false;   // nao era um status
		}
		return;
	}

	Console_Feed(CONSOLE_BLE, character);
}

/* ================== MAQUINA ================== */

void Rn4871_Step(uint32_t now_ms) {

	uint8_t bytes[64];
	uint16_t count;

	while ((count = UartLink_Read(UART_LINK_2, bytes, sizeof(bytes))) > 0u) {
		for (uint16_t index = 0; index < count; index++) {
			handle_byte((char) bytes[index], now_ms);
		}
	}

	bms_bt_t *bt = &bms.bt;
	uint32_t waited_ms = now_ms - sent_ms;

	switch (bt->state) {

	case BT_BOOT:
		reboot_seen = false;
		if ((now_ms - bt->state_ms) >= BOOT_WAIT_MS) {
			enter(BT_ENTER_CMD, now_ms);
		}
		break;

	case BT_ENTER_CMD:
		if (!sent) {
			if (UartLink_TxIdle(UART_LINK_2) && ((now_ms - bt->state_ms) >= QUIET_MS)) {
				send_command("$$$", now_ms);
			}
		} else if (response_has("CMD>")) {
			alive(now_ms);
			retries = 0;
			enter(BT_GET_NAME, now_ms);
		} else if (waited_ms >= COMMAND_TIMEOUT) {
			if (++retries >= ENTER_RETRIES) {
				// nao responde: segue em modo de dados (pode estar a funcionar) e a vigia trata
				retries = 0;
				bt->rn4871_alive = false;
				bt->last_probe_ms = now_ms;
				enter(BT_DATA, now_ms);
			} else {
				enter(BT_ENTER_CMD, now_ms);
			}
		}
		break;

	case BT_GET_NAME:
		if (!sent) {
			send_command("GN\r", now_ms);
		} else if (response_has("CMD>")) {
			// resposta: "<nome>\r\nCMD> "
			char *end = strstr(response, "\r");
			size_t length = (end != NULL) ? (size_t) (end - response) : 0u;

			if (length >= sizeof(bt->name)) {
				length = sizeof(bt->name) - 1u;
			}
			memcpy(bt->name, response, length);
			bt->name[length] = '\0';
			enter((strcmp(bt->name, bms.config.bt_name) == 0) ? BT_EXIT_CMD : BT_SET_NAME, now_ms);
		} else if (waited_ms >= COMMAND_TIMEOUT) {
			enter(BT_EXIT_CMD, now_ms);
		}
		break;

	case BT_SET_NAME:
		if (!sent) {
			char command[8 + CFG_NAME_LEN];
			size_t length = strlen(bms.config.bt_name);

			memcpy(command, "SN,", 3);
			memcpy(&command[3], bms.config.bt_name, length);
			command[3 + length] = '\r';
			command[4 + length] = '\0';
			send_command(command, now_ms);
		} else if (response_has("AOK")) {
			reboot_in_command = true;
			enter(BT_REBOOT, now_ms);   // o nome so muda depois de reiniciar o modulo
		} else if (response_has("Err") || (waited_ms >= COMMAND_TIMEOUT)) {
			enter(BT_EXIT_CMD, now_ms);
		}
		break;

	case BT_EXIT_CMD:
		if (!sent) {
			send_command("---\r", now_ms);
		} else if (response_has("END") || (waited_ms >= COMMAND_TIMEOUT)) {
			if (response_has("END")) {
				alive(now_ms);
			}
			bt->last_probe_ms = now_ms;
			enter(BT_DATA, now_ms);
		}
		break;

	case BT_DATA:
		if (reboot_seen) {
			reboot_seen = false;
			enter(BT_BOOT, now_ms);   // reiniciou sozinho (ou o comando da consola): voltar a verificar o nome
		} else if (reboot_requested || ((bms.config.bt_reboot_s > 0u) && ((now_ms - bt->last_reboot_ms) >= ((uint32_t) bms.config.bt_reboot_s * 1000u)))) {
			reboot_requested = false;
			enter(BT_REBOOT, now_ms);
		} else if ((bms.config.bt_probe_s > 0u) && ((now_ms - bt->last_probe_ms) >= ((uint32_t) bms.config.bt_probe_s * 1000u))) {
			enter(BT_PROBE_ENTER, now_ms);
		}
		break;

	case BT_PROBE_ENTER:
		if (!sent) {
			if (UartLink_TxIdle(UART_LINK_2) && ((now_ms - bt->state_ms) >= QUIET_MS)) {
				bt->probes++;
				send_command("$$$", now_ms);
			} else if ((now_ms - bt->state_ms) >= IDLE_WAIT_MS) {
				bt->last_probe_ms = now_ms;   // o TX nunca esvaziou: tenta-se mais tarde
				enter(BT_DATA, now_ms);
			}
		} else if (response_has("CMD>")) {
			alive(now_ms);
			enter(BT_PROBE_EXIT, now_ms);
		} else if (waited_ms >= COMMAND_TIMEOUT) {
			bt->probe_failures++;
			bt->rn4871_alive = false;
			enter(BT_REBOOT, now_ms);
		}
		break;

	case BT_PROBE_EXIT:
		if (!sent) {
			send_command("---\r", now_ms);
		} else if (response_has("END") || (waited_ms >= COMMAND_TIMEOUT)) {
			bt->last_probe_ms = now_ms;
			enter(BT_DATA, now_ms);
		}
		break;

	case BT_REBOOT:
	default:
		/* as cegas, como o firmware antigo: "$$$", 200 ms, "R,1". O "\r" antes
		 * do R,1 fecha o que o modulo tiver na linha (um "$$$" a mais em modo
		 * de comando seria lido como "$$$R,1" e dava "Err"). Depois do SN ja
		 * se esta em modo de comando: vai direto ao R,1 */
		if (reboot_phase == 0u) {
			if (reboot_in_command) {
				reboot_in_command = false;
				reboot_phase = 1;
				sent_ms = now_ms - REBOOT_GAP_MS;
			} else if ((UartLink_TxIdle(UART_LINK_2) && ((now_ms - bt->state_ms) >= QUIET_MS)) || ((now_ms - bt->state_ms) >= IDLE_WAIT_MS)) {
				send_command("$$$", now_ms);
				reboot_phase = 1;
			}
		} else if ((reboot_phase == 1u) && (waited_ms >= REBOOT_GAP_MS)) {
			send_command("\rR,1\r", now_ms);
			reboot_phase = 2;
			bt->rn4871_reboots++;
			bt->last_reboot_ms = now_ms;
			bt->connected = false;
			Console_LinkReset(CONSOLE_BLE);
		} else if ((reboot_phase == 2u) && (reboot_seen || (waited_ms >= REBOOT_WAIT_MS))) {
			enter(BT_BOOT, now_ms);
		}
		break;
	}
}
