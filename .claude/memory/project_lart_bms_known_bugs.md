---
name: project-lart-bms-known-bugs
description: "Open/unresolved bugs found during code review of lart_bms firmware, not yet fixed"
metadata: 
  node_type: memory
  type: project
  originSessionId: 71f7fca9-d43b-42fe-8a9f-fcbd53bf5ad9
---

Unresolved as of 2026-07-03:

**Last cell voltage of last physical slave in ADBMS6830 daisychain reads real-looking negative value, always triggers undervoltage fault.**
- Confirmed: only on UART print (not tested on CAN yet), averaged-register read path, always last physical slave regardless of chain length.
- Leading hypothesis: 0x8000 sentinel (register never updated by valid read) → `1.5 + (-32768*0.00015) ≈ -3.42V`, decoded as real. Root cause likely PEC failure on weakest daisychain link (chain end) or averaging-register-not-ready timing (RDAC needs N cycles after ADCV; last IC gets cmd last).
- User update 2026-07-03: CAN value correct, UART negative with "correct-looking" magnitude. Math constraint: print path is `(int16+10000)*0.00015`, min possible = -3.4152V (code 0x8000) — exact negation of >1.5V is impossible in int16, so "correct magnitude negative" ≈ sentinel/garbage coinciding with ~3.4V cells.
- Key finding: only FAULT_UNDERVOLTAGE raiser is `BMS_SafetyCheck` on `cell.c_codes` (same array CAN uses) → corruption is intermittent; vendor `adBmsReadData` parses data into c_codes even when PEC fails, so a bad tail-of-chain read poisons last IC's group D (c_codes[9..11]; bytes 4-5 = cell 12 sit right before PEC).
- Mitigations landed 2026-07-03 (beta-v2): `BMS_SafetyCheck` skips module if `cccrc.cell_pec != 0`; `printVoltages` prints `(raw=0x%04X)` after negative Cell/AvgCell values.
- **Superseded 2026-08-06**: o tratamento do valor negativo passou a ser `cell_v = fabsf(cell_v)` — o sentinela 0x8000 (−3,4152 V) vira +3,4152 V e deixa de disparar UV fantasma. Open-wire passou a ser detetado por **`cell_v < 2,30 V`** (`SAFETY_CELL_OW_V` → `FAULT_OW_DETECTED_CELL`), ver [[project-hardware-quirks]]. Root cause (chain-end signal integrity vs read-length) unproven — waiting raw hex from hardware. raw=0x8000 exact → sentinel/register-reset; anything else → tail corruption.

**CAN_Service recovery throttle** — FIXED 2026-07-03 (beta-v2): `last_try` now pointer to `last_can1_try`/`last_can2_try` static, throttle writes back via `*last_try = now`.

**CAN TX queue race**: `can1TxQueue`/`can2TxQueue` head/tail touched from both ISR (RX callbacks enqueueing responses) and main loop (`CanTx_ProcessSelectedQueue`), not `volatile`, no critical section. Not fixed.

**CAN1 filter accepts everything** (`CAN_Init` in `can.c`): ID=0/mask=0, every bus frame interrupts MCU though only ~5-6 IDs consumed. 14 hardware filter banks unused. Not fixed — biggest cheap efficiency win available.

**Fan PWM clamp** — FIXED 2026-07-03 (beta-v2): clamp against `timer_max` (ARR), unsigned `<0` check removed. Histerese de 5°C (`FAN_HYSTERESIS_C`): mantém PWM mínimo (`fan_table[1].pwm`) enquanto desce dentro da banda.
- **Rampa atualizada 2026-08-06**: tabela linear **38 °C (PWM 0) → 45 °C (PWM 255)**. `fan_table[1].pwm` = 23. Liga acima de 38 °C, só desliga a ≤33 °C (38−5).
- Detalhe menor: na fatia 38,0–38,6 °C o PWM interpola 0→23, podendo ficar abaixo do arranque da ventoinha. O piso só aplica quando o PWM é exatamente 0 (`pwm_8bit == 0`). Inofensivo, janela de 0,6 °C.

**adBms6830ParseCell** (`adBms6830ParseCreate.c`): `calloc`/`free` per parse call in hot measurement path (vendor ADI code), `exit(0)` on alloc failure kills firmware silently, 8-bit `address` var overflows past ~7 ICs on `ALL_GRP` reads (12-slave systems affected only on that path — cell/temp per-group reads unaffected). Not fixed.

How to apply: don't re-report these as "new" in future reviews — check list first. Update/remove entries once user confirms a fix landed.
