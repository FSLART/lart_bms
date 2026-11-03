/*
 * uart_link.h
 *
 * Os dois UART da placa, cada um com um ring de TX servido por DMA e RX por
 * interrupcao (ReceiveToIdle) para um ring:
 *   UART_LINK_1 = USART1 (PA9/PA10, 230400) consola por cabo
 *   UART_LINK_2 = USART2 (PA2/PA3, 115200) RN4871 (BLE, sem RTS/CTS)
 *
 * Escrita tudo-ou-nada: uma linha que nao cabe e descartada inteira
 * (frames_dropped), nunca sai meia. Nunca bloqueia nem espera pelo DMA.
 * Um DMA de TX preso mais de 2 s e abortado e recomeca (dma_restarts).
 *
 * Dono de: bms.uart1 e bms.uart2.
 */

#ifndef COMMS_UART_LINK_H_
#define COMMS_UART_LINK_H_

#include "brain.h"

#define UART_LINK_1     0u
#define UART_LINK_2     1u
#define UART_LINK_COUNT 2u

void UartLink_Init(void);
bool UartLink_Write(uint8_t link, const void *data, uint16_t length);   // false = nao coube
uint16_t UartLink_Free(uint8_t link);                                     // bytes livres no ring de TX
bool UartLink_TxIdle(uint8_t link);                                       // ring vazio e DMA parado
uint16_t UartLink_Read(uint8_t link, uint8_t *out, uint16_t max_length);  // bytes recebidos
void UartLink_Service(uint32_t now_ms);                                   // arranca o DMA, vigia encravamentos

#endif /* COMMS_UART_LINK_H_ */
