---
name: feedback-printfdebug-console
description: "lart_bms console output rule: always printfDebug(), never printf/printfUI"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0b77038e-1973-4bac-8746-1f6ba4cd27d3
---

All console/UART debug output in lart_bms firmware goes through `printfDebug()` (declared in `uartDMA.h`, DMA-backed).

**Why:** plain `printf` isn't retargeted to the debug UART; vendor ADI files use `printf`/`printfUI` but that's legacy — user's own code and any code we add must use `printfDebug`.

**How to apply:** when adding any debug/log print, `#include "uartDMA.h"` and call `printfDebug("...\r\n", ...)`. Line endings `\r\n`. See [[project-lart-bms-overview]].
