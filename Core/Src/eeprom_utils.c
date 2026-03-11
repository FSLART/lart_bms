#include "eeprom_utils.h"

#define UART_BUFFER_SIZE 64
//#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)

static EE24_HandleTypeDef ee24fc08;  // Static to this file

uint8_t uart_rx_buffer[1];                    // Temp buffer for 1 byte
uint8_t uart_message[UART_BUFFER_SIZE];       // Full message buffer
volatile uint8_t uart_index = 0;
volatile bool uart_message_ready = false;

static UART_HandleTypeDef *global_uart = NULL;  // For putchar fallback

bool Write_EEPROM(EEPROM_Comms *comms, uint8_t type, uint16_t value, bool debug) {
	global_uart = comms->huart;
	uint8_t info[16] = { 0xFF };
	bool foundType = false;
	//bool foundEmpty = false;
	uint8_t emptyColum = 99;
	uint8_t foundColum = 99;

	// medir tamanho do value e separa-los no array
	uint8_t value_size = (value <= 0xFF) ? 1 : 2;

	uint8_t value_data[2] = { 0xFF };
	value_data[0] = value & 0xFF;
	value_data[1] = (value >> 8) & 0xFF;

	if (debug) {
		printfDebug("WRITE EEPROM: Valores recebidos na função\n");
		printfDebug("    uint8_t type: %#04x\n", type);
		printfDebug("    uint16_t value: %d\n", value);
		printfDebug("    bool debug: %s\n", debug ? "true" : "false");
		printfDebug(" \n");
	}

	// Inincializacao
	if (EE24_Init(&ee24fc08, comms->hi2c, EE24_ADDRESS_DEFAULT)) {

		if (!EE24_Read(&ee24fc08, 0x00000010, info, 16, 1000)) {
			if (debug)
				printfDebug("WRITE EEPROM: Falha na leitura do EEPROM\n");
			return false;
		}

		// Verificar se ja existe existe o tipo de valor de calibrcao
		for (int i = 0; i < 16; i++) {
			if (info[i] == type) {
				foundType = true;
				foundColum = i;
				if (debug)
					printfDebug("WRITE EEPROM: Econtrou o tipo de valor %#04x na coluna %d \n", type, foundColum);
				break;
			}
		}

		// Se n encontrar,vamos procurar o primeiro espaço vazio para criar um novo spot
		if (!foundType) {
			for (int i = 0; i < 16; i++) {
				if (info[i] == 0xFF) {
					emptyColum = i;
					if (debug)
						printfDebug("WRITE EEPROM: Econtrou a coluna %d vazia\n", emptyColum);
					break;
				}
			}
		}

		// Se encontrou um coluna vazia bota escrever na linha que ela representa
		if (emptyColum < 98) {
			info[emptyColum] = type;
			if (!EE24_Write(&ee24fc08, 0x00000010, info, 16, 1000)) {
				if (debug)
					printfDebug("WRITE EEPROM: Falha ao gravar um novo tipo\n");
				return false;
			}
			foundColum = emptyColum;
			emptyColum = 99;
		}

		//se habemos coluna crl
		if (foundColum <= 16) {
			uint16_t writeAddress = (foundColum + 8) * 16; // Offset de 32 lines * 16 bytes por line
			if (debug)
				printfDebug("WRITE EEPROM: Vamos tentar gravar o valor %d na linha %d (offset 8 linhas)(%#04x)\n", value, foundColum + 8, writeAddress);

			// Criar buffer da linha inteira
			uint8_t line_buffer[16] = { 0xFF };
			line_buffer[0] = value_size;  // Primeiro byte = tamanho

			// Gravar valor do byte 1 em diante (em big-endian)
			for (uint8_t i = 0; i < value_size; i++) {
				line_buffer[1 + i] = value_data[value_size - 1 - i];
			}

			// Gravar valor invertido nos últimos bytes (little-endian)
			for (uint8_t i = 0; i < value_size; i++) {
				//line_buffer[15 - i] = value_data[i];
				line_buffer[15 - i] = value_data[value_size - 1 - i];
			}

			//Escrever os valores
			if (!EE24_Write(&ee24fc08, writeAddress, line_buffer, 16, 1000)) {
				if (debug)
					printfDebug("WRITE EEPROM: Falha ao gravar valor\n");
				return false;
			}

		} else {
			if (debug) {
				printfDebug("WRITE EEPROM: Nenhuma coluna válida encontrada, provavelmente num ha espaco\n");
				printfDebug("    foundColum: %d \n", foundColum);
				printfDebug("    foundType: %s\n", foundType ? "true" : "false");
			}
			return false;
		}

		if (debug)
			printfDebug("WRITE EEPROM: Escrita na EEPROM feita com sucesso\n");
		return true;
	} else {

		if (debug)
			printfDebug("WRITE EEPROM: Falha na conexão com o EEPROM \n");
		return false;
	}
}

int Read_EEPROM(EEPROM_Comms *comms, uint8_t type, bool debug) {
	global_uart = comms->huart;
	uint8_t info[16] = { 0xFF };
	uint8_t foundColum = 99;

	// Iniciar EEPROM
	if (!EE24_Init(&ee24fc08, comms->hi2c, EE24_ADDRESS_DEFAULT)) {
		if (debug)
			printfDebug("READ EEPROM: Falha na conexão com o EEPROM \n");
		return -1;
	}

	// Ler o índice de tipos na linha 0x10
	if (!EE24_Read(&ee24fc08, 0x00000010, info, 16, 1000)) {
		if (debug)
			printfDebug("READ EEPROM: Falha na leitura da EEPROM\n");
		return -1;
	}

	// Procurar pelo tipo
	for (uint8_t i = 0; i < 16; i++) {
		if (info[i] == type) {
			foundColum = i;
			if (debug)
				printfDebug("READ EEPROM: Tipo %#04x encontrado na coluna %d\n", type, i);
			break;
		}
	}

	if (foundColum == 99) {
		if (debug)
			printfDebug("READ EEPROM: Tipo %#04x não encontrado\n", type);
		return -1;
	}

	// Calcular o endereço da linha de dados
	uint16_t readLine = (foundColum + 8) * 16;
	uint8_t line_buffer[16] = { 0xFF };

	// Ler a linha da EEPROM
	if (!EE24_Read(&ee24fc08, readLine, line_buffer, 16, 1000)) {
		if (debug)
			printfDebug("READ EEPROM: Falha ao ler a linha de dados\n");
		return -1;
	}

	// Determinar o tamanho do valor
	uint8_t value_size = line_buffer[0];
	if (value_size != 1 && value_size != 2) {
		if (debug)
			printfDebug("READ EEPROM: Tamanho de valor guardado inválido: %d\n", value_size);
		return -1;
	}

	// Reconstruir o valor invertido ( 2 ultimos bytes) - pedi ajuda ao chat aqui, n sei cm funfa :c
	uint16_t value_inv = 0;
	for (uint8_t i = 0; i < value_size; i++) {
		value_inv |= line_buffer[15 - i] << (8 * i);
	}

	// Inverter bytes para obter valor em big-endian - pedi ajuda ao chat aqui, n sei cm funfa :c
	if (value_size == 2) {
		uint8_t high_byte = (value_inv & 0xFF00) >> 8;
		uint8_t low_byte = (value_inv & 0x00FF);
		value_inv = (low_byte << 8) | high_byte;
	}

	// Reconstruir valor (big-endian) - pedi ajuda ao chat aqui, n sei cm funfa :c
	uint16_t value = 0;
	for (uint8_t i = 0; i < value_size; i++) {
		value <<= 8;
		value |= line_buffer[1 + i];
	}

	if (debug) {
		printfDebug("READ EEPROM: Valor lido (primeiros bytes): %d (0x%04X)\n", value, value);
		printfDebug("READ EEPROM: Valor lido (ultimos bytes): %d (0x%04X)\n", value_inv, value_inv);
		printfDebug("    Tamanho: %d byte(s)\n", value_size);
		printfDebug("    Dados (primeiros bytes): ");
		for (uint8_t i = 0; i < value_size; i++) {
			printfDebug("%02X ", line_buffer[1 + i]);
		}
		printfDebug("\n    Dados (ultimos bytes): ");
		for (uint8_t i = 0; i < value_size; i++) {
			printfDebug("%02X ", line_buffer[14 + i]);
		}
		printfDebug("\n");
	}

	if (value_inv == value) {
		printfDebug("READ EEPROM: Leitura correta :) \n");
		return value;
	} else {
		printfDebug("READ EEPROM: Erro na confirmação do valor lido,  %d (0x%04X) !=  %d (0x%04X) \n", value, value, value_inv, value_inv);
		return -1;
	}

}

/*bool Analyze_EEPROM(EEPROM_Comms *comms) {
	global_uart = comms->huart;
	uint8_t info[16] = { 0xFF };

	if (!EE24_Init(&ee24fc08, comms->hi2c, EE24_ADDRESS_DEFAULT)) {
		printfDebug("PRINT EEPROM: Falha na conexão com o EEPROM\n");
		return false;
	}

	if (!EE24_Read(&ee24fc08, 0x00000010, info, 16, 1000)) {
		printfDebug("PRINT EEPROM: Falha na leitura do índice\n");
		return false;
	}

	printfDebug("\n");
	printfDebug("+----------+----------+-------------------+-----------------------+------------+\n");
	printfDebug("| Colum |  Type  | Value (dec) |   Value (hex)    | Status  |\n");
	printfDebug("+----------+----------+-------------------+-----------------------+------------+\n");

	for (uint8_t col = 0; col < 16; col++) {
		uint8_t type = info[col];
		if (type == 0xFF)
			continue;

		uint16_t address = (col + 8) * 16;
		uint8_t buffer[16] = { 0xFF };

		if (!EE24_Read(&ee24fc08, address, buffer, 16, 1000)) {
			printfDebug("|   %-6d   | 0x%02X  |     ERR     |     ERR     | READ FAIL |\n", col, type);
			continue;
		}

		uint8_t size = buffer[0];
		if (size != 1 && size != 2) {
			printfDebug("|   %-6d   | 0x%02X  |  INVALID   |   INVALID   | BAD SIZE |\n", col, type);
			continue;
		}

		// Read value (big-endian)
		uint16_t value = 0;
		for (uint8_t i = 0; i < size; i++) {
			value <<= 8;
			value |= buffer[1 + i];
		}

		// Read value (little-endian confirmation)
		uint16_t value_inv = 0;
		for (uint8_t i = 0; i < size; i++) {
			value_inv |= buffer[15 - i] << (8 * i);
		}

		if (size == 2) {
			// Convert little-endian to big-endian for comparison
			uint8_t high = (value_inv & 0xFF00) >> 8;
			uint8_t low = value_inv & 0x00FF;
			value_inv = (low << 8) | high;
		}

		// Determine status
		const char *status = (value == value_inv) ? "OK" : "MISMATCH";

		printfDebug("|   %-6d   | 0x%02X  |   %-16d | 0x%09X |   %-8s |\n", col, type, value, value, status);
	}

	printfDebug("+----------+----------+-------------------+-----------------------+------------+\n");
	printfDebug("Which column do you want to modify (0-15) (any value above 15 will exit)? \n");
	printfDebug("you may want to select an empty one for new entry: \n");

	memset(uart_message, 0, sizeof(uart_message));
	uart_index = 0;
	uart_message_ready = false;
	HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 1);

	while (!uart_message_ready) {
	}
	uart_message_ready = false;

	int column = atoi((const char*) uart_message);
	if (column < 0 || column > 15) {
		printfDebug("Invalid column.\r\n");
		return false;
	}

	// Ask action
	printfDebug("Enter 'E' to Edit, 'C' to Clean and 'N' for new type and value: \n");
	memset(uart_message, 0, sizeof(uart_message));
	uart_index = 0;
	uart_message_ready = false;
	HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 1);
	while (!uart_message_ready) {
	}
	uart_message_ready = false;

	char action = uart_message[0];

	if (action == 'C' || action == 'c') {
		// Clean index and value line
		info[column] = 0xFF;
		if (!EE24_Write(&ee24fc08, 0x00000010, info, 16, 1000)) {
			printfDebug("Failed to clean index entry.\r\n");
			return false;
		}

		uint8_t clean_line[16];
		memset(clean_line, 0xFF, 16);
		if (!EE24_Write(&ee24fc08, (column + 8) * 16, clean_line, 16, 1000)) {
			printfDebug("Failed to clean EEPROM line.\r\n");
			return false;
		}

		printfDebug("Column %d cleaned successfully.\r\n", column);

	} else if (action == 'E' || action == 'e') {
		// Edit value
		printfDebug("Enter new value (0–65535):\n");
		memset(uart_message, 0, sizeof(uart_message));
		uart_index = 0;
		uart_message_ready = false;
		HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 1);
		while (!uart_message_ready) {
		}
		uart_message_ready = false;
		uint16_t new_value = (uint16_t) atoi((const char*) uart_message);

		// Build buffer with value
		uint8_t buffer[16];
		memset(buffer, 0xFF, 16);
		buffer[0] = 2;
		buffer[1] = (new_value >> 8) & 0xFF;
		buffer[2] = new_value & 0xFF;
		buffer[15] = (new_value >> 8) & 0xFF;
		buffer[14] = new_value & 0xFF;

		if (!EE24_Write(&ee24fc08, (column + 8) * 16, buffer, 16, 1000)) {
			printfDebug("Failed to write new value.\r\n");
			return false;
		}

		printfDebug("Column %d updated with value %d (0x%04X)\r\n", column, new_value, new_value);

	} else if (action == 'N' || action == 'n') {
		// Add new type and value
		printfDebug("Enter new type (in hex, e.g. 0x01):\n");
		memset(uart_message, 0, sizeof(uart_message));
		uart_index = 0;
		uart_message_ready = false;
		HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 1);
		while (!uart_message_ready) {
		}
		uart_message_ready = false;
		uint8_t new_type = (uint8_t) strtol((const char*) uart_message, NULL, 0); // case-insensitive hex parse

		// Check for duplicate type (numeric comparison)
		bool duplicate = false;
		uint8_t eeprom_index[16];
		if (!EE24_Read(&ee24fc08, 0x00000010, eeprom_index, 16, 1000)) {
			printfDebug("Failed to read EEPROM index for duplicate check.\n");
			return false;
		}
		for (uint8_t i = 0; i < 16; i++) {
			if (eeprom_index[i] == new_type) {
				duplicate = true;
				break;
			}
		}

		if (duplicate) {
			printfDebug("Type 0x%02X already exists. Cannot add duplicate.\n", new_type);
			return false;
		}

		printfDebug("Enter new value (0–65535):\n");
		memset(uart_message, 0, sizeof(uart_message));
		uart_index = 0;
		uart_message_ready = false;
		HAL_UART_Receive_IT(&huart1, uart_rx_buffer, 1);
		while (!uart_message_ready) {
		}
		uart_message_ready = false;
		uint16_t new_value = (uint16_t) atoi((const char*) uart_message);

		// Call Write_EEPROM directly
		if (Write_EEPROM(comms->hi2c, new_type, new_value, true)) {
			printfDebug("New type 0x%02X with value %d added successfully.\n", new_type, new_value);
		} else {
			printfDebug("Failed to add new type.\n");
			return false;
		}

	} else {
		printfDebug("Deu merda idk kkkk.\r\n");
		return false;
	}

	return true;
}
*/
