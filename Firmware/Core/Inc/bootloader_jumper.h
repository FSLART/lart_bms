/*
 * bootloader_jumper.h
 *
 *  Created on: Apr 26, 2026
 *      Author: jpser
 */

#ifndef INC_BOOTLOADER_JUMPER_H_
#define INC_BOOTLOADER_JUMPER_H_

#include <stdint.h>

/* Flag set by the CAN RX ISR, consumed in the main loop (Thread Mode). */
extern volatile uint8_t triggerJumpToBootloader;

void Setup_Bootloader_Jumper(void);
void JumpToBootloader(void);


#endif /* INC_BOOTLOADER_JUMPER_H_ */
