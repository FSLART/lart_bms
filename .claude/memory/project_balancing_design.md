---
name: project-balancing-design
description: "Cell balancing design decisions for lart_bms (ADBMS6830, on/off DCC only) - user-tested constraints, do not undo"
metadata: 
  node_type: memory
  type: project
  originSessionId: 0b77038e-1973-4bac-8746-1f6ba4cd27d3
---

Balancing design locked 2026-07-03 with user (beta-v2), see [[project-lart-bms-overview]]:

- **On/off DCC only, no PWM.** 15Ω discharge resistors, ~220mA, all 12 cells may discharge simultaneously (hardware sized for it). `balance_parity_t` is legacy from an abandoned PWM attempt — unused, don't wire it up.
- **Frozen target min** (`balance_start_min_mV` latched at balance start): user tested live-min and balancing never converged (cells dipping below moving min restart the process). Keep frozen. Don't "fix" to live recompute.
- **Balance math reads `acell.ac_codes`** (average registers, RDAC) — the only registers refreshed during BALANCING state. c_codes go stale there (RDCV commented out in balance read phase).
- **BALANCE_ON_TIME_MS must stay < 1.8s** (ADBMS6830 tSLEEP min): no commands go out during the ON pulse; watchdog timeout clears DCC bits and sleeps the IC. Set to 1500ms. If pulse must grow, add a mid-pulse keep-alive read instead.
- **BALANCE_END is sticky in BAL_CYCLE_INIT**: CAN stop sets it directly from RX; the stage recompute is guarded by `if (balanceStage != BALANCE_END)` — reordering that breaks CAN stop.
- Die-temp guard at 85°C (`BALANCE_DIE_TEMP_LIMIT_C`, STATA itmp) raises FAULT_BALANCING_OVERTEMP and ends balancing. Hardened 2026-07-04 after S12 garbage ITMP (125.6°C) killed a session: requires stat_pec==0, plausibility -40..150°C, 2 consecutive over-limit cycles; prints `BAL ITMP GARBAGE`/`BAL DIE OT` with raw hex.
- FAULT_TEMP_SENSOR_OPEN no longer raised anywhere (enum kept for CAN compat): user chose OW_DETECTED_RTH as the single NTC-open fault. S4 NTC3/NTC4 are known-open hardware, expected faults.
- Cells outside [3000, 4250] mV are excluded from min/target/discharge, no abort — user decision.
- Balancing triggered via CAN only (POWERTRAIN_T26_START_BALANCING), maybe charger-integrated later.

**Why:** several of these look like bugs to a reviewer (frozen min, unused parity enum, magic 1500ms) but are deliberate, datasheet- or test-driven.
**How to apply:** check this file before "improving" cell_balancing.c / BALANCING state machine.
