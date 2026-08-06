---
name: reference-bootloader-jump
description: "Why JumpToBootloader() touches SPI/SYSCFG/CMSIS - deinit sequence, based on gonzabrusco guide"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 0b77038e-1973-4bac-8746-1f6ba4cd27d3
---

`JumpToBootloader()` (bootloader_jumper.c, lart_bms) is the graph's biggest cross-community bridge (betweenness 0.162) **by design, not accident**: before jumping to the STM32 system-bootloader memory region it must deinit/disable all peripherals (SPI, timers, clocks), remap memory (SYSCFG), kill interrupts and use CMSIS barriers (`__DSB`/`__ISB`) — hence edges into HAL SPI, SYSCFG and CMSIS core communities.

Implementation follows: https://github.com/FSLART/can_stm32_programmer/blob/main/support/gonzabrusco_jump_to_bootloader.md (user's own writeup in FSLART/can_stm32_programmer).

**How to apply:** don't flag this fan-out as a code smell in reviews; it's the canonical jump-to-bootloader sequence. See [[project-lart-bms-overview]].
