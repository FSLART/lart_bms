---
name: reference-clocks-can-watchdog
description: "Relogios, bit timing dos dois CAN e janela real do WWDG no lart_bms — numeros derivados, nao estao escritos em lado nenhum"
metadata:
  node_type: memory
  type: reference
---

Números calculados a partir do `main.c`. Não aparecem escritos em lado nenhum, e
custam a re-derivar.

## Relógios

HSE **8 MHz**, PLLM 4, PLLN 64, PLLP 2:

```
SYSCLK = 8/4 * 64 / 2 = 64 MHz
AHB    = /1  -> 64 MHz
APB1   = /2  -> PCLK1 = 32 MHz
```

O `PCLK1 = 32 MHz` é o que manda no bit timing do CAN e na janela do WWDG.

## CAN — os dois a 500 kbit/s

```
bitrate = PCLK1 / (prescaler * (1 + BS1 + BS2))
```

Com prescaler 8 (→ 4 MHz) e `BS1=2, BS2=5` (8 TQ): **500 kbit/s** nos dois barramentos.

⚠️ **Sample point a 37,5 %** (`(1+BS1)/TQ`). O normal é 75–87,5 %. Funciona em
bancada; é frágil a cabos longos e a nós com clock menos preciso.
`BS1_5TQ` + `BS2_2TQ` mantém os 500k e põe o sample point a 75 %.

**Bug apanhado 2026-09-17:** o CAN1 esteve com `BS2_1TQ` → 4 TQ → **1 Mbit/s**
contra um barramento a 500k. Sintoma: o BMS não transmitia nada, com erros de bit
em todas as tramas e bus-off (`AutoBusOff = ENABLE`). Já corrigido. Se voltar a
aparecer "o BMS não manda nada para o CAN", **conferir o bit timing primeiro**.

## WWDG — a janela é curta

```
t = 4096 * prescaler * (counter - 0x3F) / PCLK1
  = 4096 * 8 * (127 - 63) / 32e6 = 41,9 ms  (com prescaler 8)
```

Com a configuração real (`Prescaler = 8`, `Counter = 127`, `Window = 127`) e
`PCLK1 = 32 MHz` → **~65,5 ms** de timeout. `Window = 127` significa que não há
restrição de refresh antecipado.

O refresh acontece **só antes e depois do `brain_loop()`**, no `while(1)` do
`main.c`. Nada lá dentro o alimenta. Logo:

> **O `brain_loop()` tem um orçamento duro de ~65,5 ms por iteração.**

Consequências práticas:
- Qualquer coisa pesada dentro do `brain_loop` (serializações grandes, muitos
  `%f` no printf) pode estourar a janela e provocar **reset em loop**. Sintoma:
  CAN e UART aparentemente mortos, e o `Master Runtime` no dashboard a voltar a 0.
- Parar o alvo no GDB (Black Magic) provoca reset por watchdog. Para inspecionar
  com o sistema a correr, usar o CAN e não o debugger — ver
  [[project-lart-bms-overview]].
- O boot já deteta `RCC_CSR_WWDGRSTF` e imprime `"BOOT: WWDG reset detected!"`.
