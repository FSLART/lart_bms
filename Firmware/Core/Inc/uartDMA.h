#ifndef INC_UARTDMA_H_
#define INC_UARTDMA_H_

#include <stdint.h>


int printfUI(const char *format, ...);

// Print to Node-Red UI interface Console
void printfConsole(const char *format, ...);

/* Print to BT (UART2). Sempre activo, ignora o switch abaixo: usar so para o
 * que TEM de sair mesmo com os prints de debug desligados (ex. AMS_ERROR) */
int printfDebugRaw(const char *format, ...);

/* Bytes crus para o BT (UART2), tudo ou nada (ex. trama JSON do live debug).
 * Devolve len, ou 0 se nao coube no ring buffer */
int uart2Write(const char *data, int len);

/* ---------------------------------------------------------------------------
 * Prints de debug no UART2
 *
 * 1 = printfDebug() escreve normalmente
 * 0 = printfDebug() compila para nada. Nenhuma das ~560 chamadas espalhadas
 *     pelo codigo produz trafego, sem ser preciso comentar uma a uma.
 *
 * 2026-09-17: desligado a pedido. No UART2 so saem as transicoes da linha
 * AMS_ERROR, que o ams_error.c manda por printfDebugRaw().
 * --------------------------------------------------------------------------- */
#define UART2_DEBUG_PRINTS  0

#if UART2_DEBUG_PRINTS
#define printfDebug(...)  printfDebugRaw(__VA_ARGS__)
#else
#define printfDebug(...)  ((void) 0)
#endif

//gay ass name changer
void RN4871_SetName(void);

/* Reboot periodico do RN4871 ("$$$" + "R,1"), nao bloqueante.
 * Service: chamar em cada passagem do brain_loop.
 * IsBusy: 1 enquanto a sequencia corre -> nao mandar nada para o UART2 */
void RN4871_Service(void);
uint8_t RN4871_IsBusy(void);


#endif /* INC_UARTDMA_H_ */
