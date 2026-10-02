---
name: project-live-debug-json
description: "JSON do live_debug no UART2 (BT RN4871) — 1 Hz, chaves legíveis, tudo inteiro, tudo-ou-nada; decisões do utilizador"
metadata:
  type: project
---

Desde 2026-10-01 o `LiveDebug_Update()` (brain_loop, **1 s**) preenche a struct `live_debug` e manda-a inteira como **uma linha JSON** (`...}\r\n`) no UART2 → RN4871 (BT). Tudo dentro de `live_debug.c` (o utilizador não quer módulo novo). Relacionado: [[project-lart-bms-overview]], [[reference-clocks-can-watchdog]].

Decisões do utilizador:
- **1 Hz** (live_debug passou de 500 ms para 1000 ms): ~3,1 KB/trama; a 500 ms seriam ~6 KB/s e o RN4871 **não tem flow control** (perde bytes calado acima de ~2–5 KB/s de BLE).
- **Chaves legíveis** (`"cell_mV"`, `"isa_pack"`...), não curtas+doc. Estados saem pelo **nome** (`"HV_ON"`, `"IDLE"`, `"WAIT_HV"`, `"END"`).
- Tem de conter "tudo de tudo": inclui ISA do pack (CAN1) e do handcart (CAN2), `vcu_request` da VCU (−1 = nunca recebido), SDC, contactores, SOC, carregador, faults pelo nome, balanceamento (máscara calculada = 0x706), e por slave: células, NTC, die, PEC flags, open-wire.

Regras técnicas:
- **Só inteiros** (mV, mA, 0,1 °C `_dC`, 0,01 °C `_cC`, `percent_x100`): `%f` no newlib-nano é lento e o brain_loop tem ~65 ms de WWDG. Floats da struct convertidos por `fx()` (NaN/Inf → 0).
- Buffer `LIVE_DEBUG_JSON_BUF` 6144: típico 3064 B, pior caso absoluto 5886 B (64 faults, tudo no máximo de dígitos).
- `uart2Write()` (uartDMA.c) é **tudo-ou-nada**: trama que não cabe no ring (10000 B) é descartada inteira → `json.dropped`. Os contadores `json.sent/dropped` numa trama referem-se às anteriores.
- Vigia do DMA (2026-10-02): `HAL_UART_ErrorCallback` reenvia o bloco (`uart2DmaErrors`); `uart2Write` faz `HAL_UART_AbortTransmit` + recomeço se o TX estiver ocupado > 2 s (`uart2DmaRestarts`), e corre **antes** do teste de ring cheio. Testa `gState` e não `HAL_UART_GetState()`. Ambos no JSON (`json.dma_errors/dma_restarts`) para distinguir encravamento do MCU vs RN4871/BLE.
- `printfDebug` continua desligado (`UART2_DEBUG_PRINTS 0`); no UART2 só saem o JSON e as transições AMS (`printfDebugRaw`).
- Validação sem hardware: réplica Python que extrai os format strings do próprio `live_debug.c` e faz `json.loads` (típico + pior caso).
