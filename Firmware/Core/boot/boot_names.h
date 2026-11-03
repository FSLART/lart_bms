/*
 * boot_names.h
 *
 * Nomes dos estados, acontecimentos e erros do boot_layout.h (logs do
 * bootloader, consola e JSON da aplicacao).
 */

#ifndef BOOT_NAMES_H_
#define BOOT_NAMES_H_

#include <stdint.h>

const char* BootNames_Error(uint8_t error);
const char* BootNames_Run(uint8_t run);
const char* BootNames_Event(uint8_t event);
const char* BootNames_ExecState(uint8_t state);
const char* BootNames_BackupState(uint8_t state);

#endif /* BOOT_NAMES_H_ */
