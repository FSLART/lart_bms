#ifndef INC_UARTDMA_H_
#define INC_UARTDMA_H_


int printfDma(const char *format, ...);

// Print to Node-Red UI interface Console
void printConsole(const char *format, ...);

// Print to BT
int printfDmaBT(const char *format, ...);


#endif /* INC_UARTDMA_H_ */
