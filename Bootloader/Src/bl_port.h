/*
 * bl_port.h
 *
 * O que a logica do bootloader (bl_core, bl_protocol, bl_log) precisa do
 * hardware. No F412 vem do bl_hw.c; nos testes no PC (Firmware/Tests/
 * test_boot.c) de fakes. A flash e a do boot_layout.h (BootFlash_*).
 */

#ifndef BL_PORT_H_
#define BL_PORT_H_

#include <stdbool.h>
#include <stdint.h>

uint32_t BlPort_Millis(void);

// texto para o UART1 (230400, o cabo da consola da aplicacao)
void BlPort_Write(const char *text, uint32_t length);

// manda o que houver de diagnostico no CAN (no maximo ~20 ms) antes de um apagar longo
void BlPort_DiagFlush(void);

#endif /* BL_PORT_H_ */
