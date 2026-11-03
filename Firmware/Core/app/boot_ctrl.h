/*
 * boot_ctrl.h
 *
 * O lado da aplicacao do bootloader CAN (pasta Bootloader/, BOOTLOADER.md):
 *
 *   descritor   0x08010400: placa "BMS1" e versao. O bootloader so instala e
 *               arranca firmware com ele (nao instala o da ACU por engano)
 *   arranque    o que o bootloader deixou nos ultimos 32 B da RAM: porque
 *               reiniciou, se esta imagem esta em teste, se houve rollback
 *   confirmacao uma imagem nova fica em teste ate correr boot_confirm_s
 *               seguidos; ai escreve CONFIRMED no journal. Sem isso o
 *               bootloader acaba por voltar ao firmware anterior
 *   pedidos     entrar no bootloader (0x0DC, 0x601 no CAN2, consola) e voltar
 *               ao firmware anterior ("boot rollback")
 */

#ifndef APP_BOOT_CTRL_H_
#define APP_BOOT_CTRL_H_

#include "brain.h"
#include "boot_layout.h"   // boot_run_t, boot_event_t, BOOT_ERR_* (os valores de bms.boot)

// flags do ultimo reset: as que o bootloader guardou (ele limpa o RCC->CSR), senao rcc_csr
uint32_t BootCtrl_ResetFlags(uint32_t rcc_csr);

void BootCtrl_Init(uint32_t now_ms);
void BootCtrl_Step(uint32_t now_ms);

bool BootCtrl_Confirm(void);                 // confirma ja (se a imagem estiver em teste)
const char* BootCtrl_CanRollback(void);      // NULL = pode voltar ao backup, senao porque nao
bool BootCtrl_Reject(void);                  // marca a imagem para o bootloader repor o backup (reiniciar a seguir)
void BootCtrl_RequestBootloader(void);       // pedido na RAM (com os bitrates da config); reiniciar a seguir

// versao empacotada (major << 24 | minor << 16 | revision) como texto "13.1.0"
void BootCtrl_VersionText(uint32_t packed, char *out, uint8_t size);

#endif /* APP_BOOT_CTRL_H_ */
