---
name: project-hardware-quirks
description: "Defeitos de hardware conhecidos do TEK-26E e como o firmware os contorna"
metadata:
  node_type: memory
  type: project
---

Defeitos reais da bancada/carro (2026-08-06) e como o firmware lida com eles.

## NTC desativados — slave 3, NTC 3 e 4
Harness partido. Fonte única de verdade: **`NTC_IsBypassed(slave_1b, ntc_1b)`** em adBms_Application.c. Editar só essa tabela.
(Era o slave 10 antes da nova disposição dos slaves — o número muda quando reordenam.)

**`NTC_ResolveSource()`** faz o canal desativado **herdar o valor do anterior**, em cascata: NTC3→NTC2, NTC4→NTC3→NTC2. Aplicado no envio CAN e no live_debug (para o Grafana não mostrar zeros). **Não** aplicado ao open-wire (um fio partido está partido) nem às proteções OT/tmax (herdar ali seria um duplicado inútil).

## Open-wire no tap de sense
Assinatura típica: uma célula colapsa (~0 mV) e a **adjacente rail** contra os díodos de clamp **BAV70W** (~6 V, lido como ~5930 mV) — as duas partilham o nó C(n). O ADBMS mede diferencialmente entre taps adjacentes; o C-ADC só vai até +5,5 V, logo 5930 mV é fora de gama, não uma tensão real.

**Não é o IC**: com as outras 10 células boas e PEC OK, o silício está bom — o problema é a ligação de sense (fio/conector/solda/200R). Caso resolvido: célula 11 do slave 1.

⚠️ Perigo: a célula railada alta faz `g_pack_vmax_mV` disparar → o charger acha que a carga terminou. O vmax em `Master_MSC_3` **não tem filtro de plausibilidade**.

## Deteção rápida de open-wire
Célula **< 2,3 V** = fio de sense partido (a P45B nunca desce dos 2,5 V em serviço). Está no `BMS_SafetyCheck` (250 ms) porque as fases dedicadas do ADBMS são lentas: **7 fases × gate do brain**. Gate em CHARGING = 25 ms (~175 ms/ciclo); **nunca baixar de ~20 ms** — a fase `OW_READ_EVEN_START_ODD` precisa de 15 ms de conversão e um gate mais curto lê dados inválidos → OW falso.

O limiar do aux (`pup > 10000 || pdown > 0`) é hair-trigger e tem `//TODO: DEFINIR THESHOLDS` — o `pdown > 0` oscila com ruído.

## CAN1 com problemas na bancada
`can1_tx_error_counter = 255` (error-passive), `last_error_code = 1` (stuff error), fila a 1023. Causa provável: sem outro nó a dar ACK e/ou sem terminação de 120 Ω. Diagnóstico exposto em `live_debug.can` (state, bus_off, hw_error, tx/rx error counters, last_error_code).

## Sentinela 0x8000
Registo por escrever lê **−3,4152 V**. O `fabsf` no `BMS_SafetyCheck` transforma em +3,4152 V — assim não dispara UV fantasma no boot. `getTemperatureCAN` devolve 1,99 °C para código muito negativo (aparece como 2.0 no Grafana = raux nunca convertido) e 150 °C para NTC aberto com pull-up.

Ver [[project-lart-bms-known-bugs]].
