---
name: feedback-mcp23017-isr-safety
description: Never do blocking I2C (or any blocking peripheral call) inside a CAN/interrupt context in this firmware
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 71f7fca9-d43b-42fe-8a9f-fcbd53bf5ad9
---

Rule: no blocking I2C (or blocking HAL calls) inside interrupt context (CAN RX callbacks, EXTI, timer ISRs) in this codebase.

Why: `MCP23017_LED()` used to call `HAL_I2C_Mem_Write()` directly. Called from `HAL_CAN_RxFifo0MsgPendingCallback` (CAN RX ISR) and `RAISE_ERROR` paths. Blocking I2C inside ISR stalled it long enough to drop CAN frames and brick the bus — user: "connection to the MCP GPIO expander bricked CAN communication a lot of times," why all MCP23017 code was commented out pre-session.

Fix applied (commit `b129dbc`, 2026-07-03): `MCP23017_LED()` now only updates bitmask + dirty flag (interrupt-safe, no I2C). New `MCP23017_Flush()` does the actual I2C write, once per `brain_loop` pass (main-loop-only). User confirmed, requested as standalone task.

Also: mute repeated I2C error prints (still raises fault, prints once until recovery) via static `mcp23017_error_printed` flag.

User wants expander I2C comms to STAY DISABLED for now (`MCP23017_Flush()` call in `brain.c` commented out on purpose) even though now safe. Don't re-enable unless asked.

How to apply: future peripheral driver work (I2C/SPI/UART) callable from ISR → same pattern: ISR touches only a local flag/buffer, main loop does the blocking transfer.
