/*
 * console.h
 *
 * Consola de texto, a mesma no cabo (UART1) e no BLE (RN4871, UART2). Uma
 * linha por comando, terminada em \r ou \n. Todas as respostas comecam por
 * '#' (a BMS_UI so le como amostra as linhas que comecam por '{').
 *
 *   status                       resumo numa linha
 *   get <nome|all>               valor da copia de edicao ("get all" lista tudo)
 *   set <nome>[i] <valor>        altera a copia de edicao (tabelas: v1,v2,... ou [i])
 *   diff                         o que difere entre a copia e a config ativa
 *   apply                        valida e aplica a quente (so com a HV desligada)
 *   save                         valida, aplica e grava na EEPROM (banco A/B)
 *   revert | defaults            copia = ativa | copia = defaults
 *   faults                       faults ativos, e quais seguram o AMS_ERROR
 *   live <on|off|ms>             JSON nesta ligacao (so ate reiniciar)
 *   json                         um JSON ja
 *   fan <cells|lv> <max|auto>    bypass das ventoinhas
 *   bt reboot                    reinicia o RN4871
 *   eeprom                       dump da EEPROM em hexadecimal
 *   version | help
 *   reboot | bootloader          so com a HV desligada
 *   unlock <pin>                 no BLE, se bt_pin != 0, antes de alterar o que quer que seja
 */

#ifndef COMMS_CONSOLE_H_
#define COMMS_CONSOLE_H_

#include "brain.h"

#define CONSOLE_UART  0u   // = UART_LINK_1
#define CONSOLE_BLE   1u   // = UART_LINK_2
#define CONSOLE_COUNT 2u

void Console_Init(void);
void Console_Feed(uint8_t console, char character);
void Console_Step(uint32_t now_ms);
void Console_LinkReset(uint8_t console);
bool Console_Print(uint8_t console, const char *text);   // texto ja com \r\n
int32_t Console_LivePeriod(uint8_t console);              // periodo do JSON nesta ligacao, 0 = desligado
bool Console_TakeJsonRequest(uint8_t console);            // pedido "json" pendente
bool Console_TakeRebootRequest(void);                     // consomem o pedido
bool Console_TakeBootloaderRequest(void);

// executa uma linha (sem o \r\n); exposto para os testes
void Console_Execute(uint8_t console, char *line);

#endif /* COMMS_CONSOLE_H_ */
