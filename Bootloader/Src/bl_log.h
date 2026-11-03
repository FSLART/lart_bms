/*
 * bl_log.h
 *
 * Linhas de texto para o UART1, sem printf (o bootloader tem 32 KB).
 * Formatos: %s %c %u %d %x e largura com zeros (%08x, %3u). Cada chamada e
 * uma linha, comecada por "[boot] " e acabada em \r\n.
 */

#ifndef BL_LOG_H_
#define BL_LOG_H_

#include <stdint.h>

void BlLog(const char *format, ...);

#endif /* BL_LOG_H_ */
