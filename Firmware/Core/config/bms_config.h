/*
 * bms_config.h
 *
 * Tabela de parametros da configuracao. Cada linha da tabela diz, para um
 * campo do bms_config_t: o nome (o mesmo na consola, no RN4871 e no JSON), a
 * unidade, a gama permitida e se so muda depois de reiniciar. A ordem da
 * tabela e a ordem dos bytes na EEPROM:
 *
 *   REGRA: parametros novos so no FIM da tabela, e nunca mudar o tipo ou o
 *   tamanho de um que ja exista. Assim uma EEPROM antiga continua a ler-se
 *   (os parametros que ela nao tem ficam com o default). Mudar a ordem muda o
 *   CRC do esquema e a EEPROM passa a ser rejeitada (defaults + config_invalid).
 *
 * Funcoes puras: nao tocam no hardware nem no bms global, recebem o
 * bms_config_t em que trabalham.
 */

#ifndef CONFIG_BMS_CONFIG_H_
#define CONFIG_BMS_CONFIG_H_

#include <stddef.h>
#include "brain.h"

#define CONFIG_SCHEMA_VERSION 1u   // sobe so quando a EEPROM antiga tem de ser rejeitada

typedef enum {
	CFG_BOOL = 0,
	CFG_U8,
	CFG_I8,
	CFG_U16,
	CFG_I16,
	CFG_U32,
	CFG_STR,       // char[count], texto com terminador
	CFG_FAULTS     // bms_faults_t, na EEPROM e no texto como mascara de 64 bits
} cfg_type_t;

#define CFG_FLAG_REBOOT 0x01u   // so passa a valer depois de reiniciar
#define CFG_FLAG_HEX    0x02u   // mostrado em hexadecimal

typedef struct {
	const char *key;
	const char *unit;
	uint16_t offset;           // offsetof(bms_config_t, campo)
	uint8_t  type;             // cfg_type_t
	uint8_t  count;            // elementos (1 = escalar); CFG_STR: tamanho com o terminador
	int32_t  min;
	int32_t  max;
	uint8_t  flags;
} cfg_param_t;

extern const bms_config_t config_defaults;
extern const cfg_param_t config_params[];
extern const uint16_t config_param_count;

void Config_SetDefaults(bms_config_t *config);
uint16_t Config_Clamp(bms_config_t *config);                 // poe tudo dentro das gamas; devolve quantos mudaram
const char* Config_Validate(const bms_config_t *config);     // NULL = coerente, senao a razao (texto curto)

// acesso por nome
int Config_Find(const char *key);                            // indice na tabela, -1 se nao existir
bool Config_GetElement(const bms_config_t *config, uint16_t param_index, uint8_t element, int32_t *value);
bool Config_SetElement(bms_config_t *config, uint16_t param_index, uint8_t element, int32_t value);   // false = fora da gama
size_t Config_FormatValue(const bms_config_t *config, uint16_t param_index, char *out, size_t size);
const char* Config_ParseValue(bms_config_t *config, uint16_t param_index, int element, const char *text);   // NULL = ok
bool Config_ParamEqual(const bms_config_t *first, const bms_config_t *second, uint16_t param_index);

// EEPROM
uint16_t Config_PayloadSize(uint16_t param_count);
uint16_t Config_Serialize(const bms_config_t *config, uint8_t *out, uint16_t size);          // bytes escritos, 0 = nao cabe
bool Config_Deserialize(bms_config_t *config, const uint8_t *data, uint16_t size, uint16_t param_count);
uint32_t Config_SchemaCrc(uint16_t param_count);
uint32_t Config_Crc32(const uint8_t *data, size_t size, uint32_t crc);   // crc = 0 no inicio

// parametros a quente
void Config_ApplyLive(bms_config_t *active, const bms_config_t *edited);
bool Config_RebootPending(const bms_config_t *active, const bms_config_t *edited);

#endif /* CONFIG_BMS_CONFIG_H_ */
