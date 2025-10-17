#pragma once

#include <stdbool.h>
#include "bms_datatypes.h"
#include "main.h"

typedef enum {
    OW_START = 0,
    OW_WAIT,
    OW_CONTINUE,
    OW_END
} bms_ow_state_t;

typedef enum {
    OW_INTACT = 0,
    OW_OPEN,
    OW_SUSPECT,
    OW_INVALID_LOWV
} bms_ow_status_t;

extern volatile bool            bms_ow_timer_done;
extern volatile bms_ow_state_t  bms_ow_next_state;

void bms_init(void);

void bms_readSid(void);

void bms_readConfigA(void);

void bms_readConfigB(void);



void bms68_setGpo45(uint8_t twoBitIndex);


void bms_startAdcvCont(void);

void bms_readAvgCellVoltage(void);

void bms_readSVoltage(void);

//void bms_openWireCheck(void);

void bms_getAuxMeasurement(void);


float bms_calculateBalancing(float delta_threshold);

void bms_startBalancing(float deltaThreshold);

void bms_startDischarge(float threshold);
void bms_stopDischarge(void);

void bms29_setGpo(void);
void bms_readVB(void);


void bms_printRthTempsJson(void);

void send_ad68_ui(void);

void bms_balancingMeasureVoltage(void);

void ad68_dump_csv_bt(void);

void OW_StartWaitMs(uint32_t ms, bms_ow_state_t next);

void bms_openWireCheck(bms_ow_state_t *ow_state, bms_ow_status_t *ow_status[TOTAL_AD68][TOTAL_CELL]);
