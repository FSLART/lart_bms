---
name: project-lart-bms-overview
description: "Architecture of lart_bms firmware - STM32F412, ADBMS6830 daisychain, dual CAN, IVT-S sensor"
metadata: 
  node_type: memory
  type: project
  originSessionId: 71f7fca9-d43b-42fe-8a9f-fcbd53bf5ad9
---

lart_bms: Formula-Student BMS firmware, STM32F412RET6 (STM32CubeIDE project, `Firmware/`).

Subsystems:
- **ADBMS6830 daisychain** (`adBms_Application.c`, `adbms/`) — up to 12 slave modules, 12 cells + 6 NTC each, isoSPI. `adbms_main()` runs acquisition/balancing state machine, gated by `adbms_current_state` (see [[project-lart-bms-known-bugs]]).
- **Dual CAN**: CAN1 = powertrain bus, 11-bit std IDs only (DBC `powertrain_t26.dbc`, auto-gen pack/unpack in `Core/Src/dbc`). CAN2 = autonomous bus, extended (29-bit) IDs. CAN1 rejects extended frames at single dispatch point in `can.c` (`HAL_CAN_RxFifo0MsgPendingCallback`); CAN2 doesn't.
- **IVT-S current sensor** (`isa_ivt-s.c`) — external CAN current/voltage/power/temp sensor, cyclic msgs parsed in `IVT_CAN_OnMessage`, exposed via `IVT_Get*()` getters.
- **MCP23017 I2C GPIO expander** (`gpio_expander.c`) — drives status LEDs. See [[feedback-mcp23017-isr-safety]] for major bug fixed here.
- **live_debug module** (`live_debug.h/c`, added ~2026-07-03) — single global struct tree (`live_debug`) mirroring AMS state, ADBMS pack overalls, IVT readings, contactor feedback, fan PWM, CAN queue depth. Updated every 500ms from `brain_loop`. Watch as one entry in STM32CubeIDE Live Expressions.
- **brain.c** — top-level `AMS_State` state machine (BALANCING/CHARGING/IDLE/ONMISSION/STARTUP/DISCHARGE_TEST/RESET_ISA/FAULT), drives `brain_loop()` from main loop.

How to apply: CAN1/CAN2 differ in ID-width contract — don't assume both accept extended IDs. Touching ADBMS/IVT/LED code → check [[project-lart-bms-known-bugs]] + [[feedback-mcp23017-isr-safety]] first.
