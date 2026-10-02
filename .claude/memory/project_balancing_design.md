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
- **Balance math reads `cell.c_codes`** (RDCV) since 2026-10-01 — same voltages as dashboard/SafetyCheck, read every balancing cycle. Was `acell.ac_codes` (RDAC); switched after the car showed cells 1–4 of S03–S08 never discharging (mask 0xFF0) while the viewer (c_codes) had them highest — the averaged register disagreed with what was displayed, root cause not measured.
- **BALANCE_ON_TIME_MS must stay < 1.8s** (ADBMS6830 tSLEEP min): no commands go out during the ON pulse; watchdog timeout clears DCC bits and sleeps the IC. Set to 1500ms. If pulse must grow, add a mid-pulse keep-alive read instead.
- **BALANCE_END is sticky in BAL_CYCLE_INIT**: the stage recompute is guarded by `if (balanceStage != BALANCE_END)`.
- Die-temp guard at 85°C (`BALANCE_DIE_TEMP_LIMIT_C`, STATA itmp) raises FAULT_BALANCING_OVERTEMP and ends balancing. Hardened 2026-07-04 after S12 garbage ITMP (125.6°C) killed a session: requires stat_pec==0, plausibility -40..150°C, 2 consecutive over-limit cycles.
- FAULT_TEMP_SENSOR_OPEN no longer raised anywhere (enum kept for CAN compat): OW_DETECTED_RTH is the single NTC-open fault.
- Cells outside [3000, 4400] mV are excluded from min/target/discharge (upper limit raised from 4250 on 2026-09-30 so over-charged cells DO discharge; >4400 = garbage).

**Automatic mode (2026-09-30, user decisions):**
- No CAN control: `START_BALANCING` handler deleted, message ignored.
- Auto-start evaluated at the end of every IDLE cycle (after OW): `BatteryPack_NeedsBalancing` → max−min > `start_delta_mV` (30 mV). Ends at the 8 mV deadband → 22 mV hysteresis.
- Only with precharge in `KILL` and AMS_ERROR inactive. Leaving KILL (precharge starting) or AMS_ERROR → `Balancing_Stop()` at once (DCC=0 written), back to IDLE; resumes when back in KILL. So: balances from boot until HV, never while HV/charging.
- Die OT end → auto-start blocked 60 s (`BALANCE_DIE_OT_HOLD_MS`), otherwise it oscillates at 85°C.
- Each balancing cycle: RDCV is read too (c_codes fresh) and `BMS_SafetyCheck` runs in BALANCING; open-wire (`OpenWire_Step`, shared by IDLE/CHARGING/BALANCING) runs every cycle with DCC off.
- **OW wait (2026-10-02):** `OpenWire_Step()` waits `OW_CONV_WAIT_MS` (25 ms) after each ADSV (was 15/10; BALANCING calls it ungated). Never below ~20 ms. Raising it did NOT fix the false OW on cells 1–4 — that was the read-order overflow, see [[project-lart-bms-known-bugs]].
- CAN 0x706 `Master_MSC_ID_4`: one slave per 250 ms send, round-robin per bus, `bal_bitmask_slave_id` 1-based, `bal_bitmask` = last computed mask (`Balancing_GetMask`).
- **PEC skip (2026-09-30):** slaves with `cell_pec != 0` are skipped per cycle in FindMin/NeedsBalancing/DetermineStage and get mask 0 in ComputeModule; they rejoin as soon as they read clean. A skipped slave keeps the session open (DetermineStage returns FINE instead of END). `ReadCellGroups(Cell|AvgCell)` ORs the PEC over groups A..F in app code (vendor keeps `=`, i.e. only group F, which has NO populated cells with 12s → the old flag was useless). Vendor untouched.
- Logic mirrored in a Python simulator (session scratchpad `sim_balancing.py`, not in repo): all new-code checks pass (the one FAIL kept on purpose reproduces the old KILL level-clear, fixed — see [[project-ams-error-architecture]]).

**Why:** several of these look like bugs to a reviewer (frozen min, unused parity enum, magic 1500ms) but are deliberate, datasheet- or test-driven.
**How to apply:** check this file before "improving" cell_balancing.c / BALANCING state machine.
