#ifndef INC_UARTDMA_H_
#define INC_UARTDMA_H_


int printfUI(const char *format, ...);

// Print to Node-Red UI interface Console
void printfConsole(const char *format, ...);

// Print to BT
int printfDebug(const char *format, ...);

//gay ass name changer
void RN4871_SetName(void);


#endif /* INC_UARTDMA_H_ */
