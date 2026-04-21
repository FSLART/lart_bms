/*
 * bms_eeprom_config.h
 *
 *  Created on: Nov 2, 2025
 *      Author: jpser
 */

#ifndef BMS_EEPROM_CONFIG_H
#define BMS_EEPROM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "ee24.h"
#include "cell_balancing.h"
#include "fault_manager.h"
#include "uartDMA.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Page 0  –  Header + topology */
#define EEPROM_ADDRESS_CHECKSUM              0x000   /* uint16_t  –  0xB007 means chip is programmed   */
#define EEPROM_ADDRESS_VERSION               0x002   /* uint8_t   –  increment when layout changes      */
#define EEPROM_ADDRESS_TOTAL_IC              0x003   /* uint8_t   –  number of ADBMS6830 ICs in chain   */
#define EEPROM_ADDRESS_CELL_COUNT            0x004   /* uint8_t   –  cells per IC                       */
#define EEPROM_ADDRESS_AUX_COUNT             0x005   /* uint8_t   –  aux channels per IC                */
#define EEPROM_ADDRESS_RAUX_COUNT            0x006   /* uint8_t   –  raux channels per IC               */
#define EEPROM_ADDRESS_USE_FILTERED_CELLS    0x007   /* uint8_t   –  1 = use filtered ADC channel       */
#define EEPROM_ADDRESS_BYPASS_DISCHARGE      0x008   /* uint8_t   –  1 = skip discharge feedback check  */
#define EEPROM_ADDRESS_BYPASS_CHECKS         0x009   /* uint8_t   –  1 = skip all feedback checks       */
#define EEPROM_ADDRESS_OUTPUT_MODE           0x00A   /* uint8_t   –  0 = PWM,  1 = DCC                 */
#define EEPROM_ADDRESS_MIN_PWM_TO_ENABLE     0x00B   /* uint8_t   –  min PWM duty before DCC fires      */
/* 0x00C – 0x00F  reserved */

/* Page 1  –  Voltage thresholds (uint16_t, millivolts) */
#define EEPROM_ADDRESS_MIN_CELL_MV           0x010   /* uint16_t  –  min allowed cell voltage           */
#define EEPROM_ADDRESS_MAX_CELL_MV           0x012   /* uint16_t  –  max sane cell voltage              */
#define EEPROM_ADDRESS_OV_THRESHOLD_MV       0x014   /* uint16_t  –  over-voltage threshold             */
#define EEPROM_ADDRESS_UV_THRESHOLD_MV       0x016   /* uint16_t  –  under-voltage threshold            */
#define EEPROM_ADDRESS_OWC_THRESHOLD_MV      0x018   /* uint16_t  –  cell open-wire threshold           */
#define EEPROM_ADDRESS_MIN_ROUGH_BAL_MV      0x01A   /* uint16_t  –  rough balance start delta          */
#define EEPROM_ADDRESS_DEADBAND_MV           0x01C   /* uint16_t  –  balance deadband                   */
#define EEPROM_ADDRESS_FULLSCALE_MV          0x01E   /* uint16_t  –  delta that maps to PWM max (15)    */

/* Page 2  –  Timing + aux open-wire threshold (uint32_t) */
#define EEPROM_ADDRESS_BALANCE_ON_TIME_MS    0x020   /* uint32_t  –  balancing active time              */
#define EEPROM_ADDRESS_BALANCE_SETTLE_MS     0x024   /* uint32_t  –  settle time after balancing        */
#define EEPROM_ADDRESS_AVG_CONV_WAIT_MS      0x028   /* uint32_t  –  wait after ADC conversion          */
#define EEPROM_ADDRESS_OWA_THRESHOLD_MV      0x02C   /* uint32_t  –  aux open-wire threshold            */

/* Page 3  –  More timing (uint32_t) */
#define EEPROM_ADDRESS_PRECHARGE_LOCKOUT_MS  0x030   /* uint32_t  –  min time between precharge inits   */
#define EEPROM_ADDRESS_CONTACTOR_DELAY_MS    0x034   /* uint32_t  –  contactor engagement check delay   */
#define EEPROM_ADDRESS_FEEDBACK_DEBOUNCE_MS  0x038   /* uint32_t  –  feedback signal debounce time      */
/* 0x03C – 0x03F  reserved */

#define EEPROM_CHECKSUM_VALUE    0xB007   /* Written on first program, checked on read */
#define EEPROM_CONFIG_VERSION 1        /* Bump this if the memory map changes       */

typedef struct {

	/* Topology */
	uint8_t total_ic; /* number of ADBMS6830 ICs (e.g. 2)        */
	uint8_t cell_count; /* cells per IC (e.g. 12)                   */
	uint8_t aux_count; /* aux channels per IC (e.g. 6)             */
	uint8_t raux_count; /* raux channels per IC (e.g. 6)            */

	/* Flags */
	uint8_t use_filtered_cells; /* 0 = raw ADC,  1 = filtered ADC           */
	uint8_t bypass_discharge; /* 0 = check discharge feedback, 1 = skip   */
	uint8_t bypass_checks; /* 0 = normal,  1 = skip all feedback checks */
	uint8_t output_mode; /* 0 = PWM,  1 = DCC                        */
	uint8_t min_pwm_to_enable; /* minimum PWM value before balancing fires  */

	/* Voltage thresholds (millivolts) */
	uint16_t min_cell_mV; /* protect damaged cells – skip below this  */
	uint16_t max_cell_mV; /* safety check – flag unsafe above this     */
	uint16_t ov_threshold_mV; /* over-voltage limit                        */
	uint16_t uv_threshold_mV; /* under-voltage limit                       */
	uint16_t owc_threshold_mV; /* cell open-wire detection threshold        */
	uint16_t min_rough_balancing_mV;/* min delta to trigger rough balancing      */
	uint16_t deadband_mV; /* ignore deltas smaller than this           */
	uint16_t fullscale_mV; /* delta that maps to PWM = 15               */

	/* Timing (milliseconds) */
	uint32_t balance_on_time_ms; /* how long balancing stays active           */
	uint32_t balance_settle_ms; /* settle time after balancing               */
	uint32_t avg_conv_wait_ms; /* wait after triggering ADC conversion      */
	uint32_t owa_threshold_mV; /* aux open-wire threshold (stored as uint32)*/
	uint32_t precharge_lockout_ms; /* cooldown between precharge requests       */
	uint32_t contactor_delay_ms; /* delay before checking contactor feedback  */
	uint32_t feedback_debounce_ms; /* debounce window for feedback signals      */

} BmsEepromConfig;

extern BmsEepromConfig masterCfg;

/*
 * Load factory/code defaults into a config struct (no EEPROM access).
 * Call this to get a known-good starting point before writing to EEPROM.
 */
void BmsConfig_LoadDefaults(void);

/*
 * Check if the EEPROM has ever been programmed by reading the estang number.
 * Returns true if the estang value 0xB007 is found at address 0x000.
 */
bool BmsConfig_IsProgrammed(EE24_HandleTypeDef *eeprom_id);

/*
 * Write a full BmsEepromConfig struct to the EEPROM.
 * Also writes the estang number and version.
 * Returns true on success.
 */
bool BmsConfig_Write(EE24_HandleTypeDef *eeprom_id);

/*
 * Read a full BmsEepromConfig struct from the EEPROM.
 * Returns true on success. Does not check estang – call BmsConfig_IsProgrammed first.
 */
bool BmsConfig_Read(EE24_HandleTypeDef *eeprom_id);

void BmsConfig_ToBalanceConfig(balance_config_t *balancing_config);

bool BmsConfig_Init(EE24_HandleTypeDef *eeprom_id, I2C_HandleTypeDef *hi2c);

void BmsConfig_DumpEEPROM(EE24_HandleTypeDef *eeprom_id);

void BmsConfig_DumpUART(void);

#ifdef __cplusplus
}
#endif

#endif /* BMS_EEPROM_CONFIG_H */
