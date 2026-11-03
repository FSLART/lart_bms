/*
 * config_store.h
 *
 * Config na EEPROM 24FC08 (1 KB) em dois bancos de 512 B, A e B:
 *
 *   0   u16  magic 0x4C42
 *   2   u8   CONFIG_SCHEMA_VERSION
 *   3   u8   reservado
 *   4   u32  sequencia (sobe a cada gravacao)
 *   8   u16  numero de parametros gravados
 *   10  u16  bytes do payload
 *   12  u32  CRC32 do esquema (nomes, tipos e tamanhos desses parametros)
 *   16  payload (bms_config.c, pela ordem da tabela)
 *   ..  u32  CRC32 de tudo o que esta antes
 *
 * Grava-se sempre no banco que NAO esta em uso e so depois de verificado
 * passa a ser o atual: um corte de energia a meio nunca deixa o BMS sem uma
 * config valida. No arranque usa-se o banco valido com a sequencia mais alta.
 *
 * Edicao: a consola (RN4871 ou UART1) mexe numa copia (ConfigStore_Edit).
 * Apply/Save validam a copia toda e passam para o bms.config os parametros
 * que podem mudar a quente; os CFG_FLAG_REBOOT so mudam depois de reiniciar.
 * Tudo so com a HV desligada (bms.system.hv_safe).
 */

#ifndef CONFIG_CONFIG_STORE_H_
#define CONFIG_CONFIG_STORE_H_

#include "bms_config.h"

#define CONFIG_BANK_SIZE        512u
#define CONFIG_BANK_HEADER      16u
#define CONFIG_BANK_CRC         4u
#define CONFIG_BANK_PAYLOAD_MAX (CONFIG_BANK_SIZE - CONFIG_BANK_HEADER - CONFIG_BANK_CRC)
#define CONFIG_MAGIC            0x4C42u

void ConfigStore_Load(void);                    // arranque: EEPROM -> bms.config (ou defaults)
void ConfigStore_SetAdbmsCheck(bool adbms_ok);  // resultado do ADBMS_Init, entra no config_invalid
void ConfigStore_Step(uint32_t now_ms);         // gravacao em fundo, uma pagina de cada vez

bms_config_t* ConfigStore_Edit(void);           // copia de edicao
void ConfigStore_MarkDirty(void);
const char* ConfigStore_Apply(void);            // NULL = aplicado
const char* ConfigStore_Save(void);             // NULL = gravacao comecou (ver bms.eeprom.state)
void ConfigStore_Revert(void);                  // copia = config ativa
void ConfigStore_Defaults(void);                // copia = defaults
bool ConfigStore_Busy(void);

// imagem de um banco (puro, tambem usado nos testes)
uint16_t ConfigStore_BuildImage(const bms_config_t *config, uint32_t sequence, uint8_t *image, uint16_t size);
bool ConfigStore_ParseImage(const uint8_t *image, uint16_t size, bms_config_t *config, uint32_t *sequence);

#endif /* CONFIG_CONFIG_STORE_H_ */
