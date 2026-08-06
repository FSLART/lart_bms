---
name: project-ams-error-architecture
description: "Linha AMS_ERROR do lart_bms — ams_error.c é o único dono do pino, todas as fontes clearable"
metadata:
  node_type: memory
  type: project
---

Arquitetura da linha AMS_ERROR (2026-08-06). Base: `AMS_PROTECTIONS_ARCHITECTURE_PLAN.md` na raiz do repo.

## ams_error.c é o ÚNICO ficheiro que escreve no pino
Polaridade encapsulada num sítio: **SET = ERRO, RESET = OK** (PC8). API: `AMS_Error_Init/Trigger/TriggerLatched/Clear/IsActive/IsPermanent`.

`AMS_Error_Init()` arranca em **ERRO** (fail-safe); o deteta-slaves limpa quando a chain bate certo. Nunca reescrever o pino num loop periódico — era o bug original (o IDLE punha RESET a cada 50 ms e des-latchava tudo).

## Todos os erros são clearable (decisão do utilizador)
`AMS_Error_TriggerLatched()` existe mas **não tem callers**. Cada fonte usa Trigger por nível + Clear na transição (edge).

Fontes que disparam:

| Fonte | Ficheiro |
|---|---|
| OW célula (limiar + delta) | adBms_Application.c |
| OW aux/NTC | adBms_Application.c |
| Chain BAD (slaves ≠ esperados) | adBms_Application.c |
| OV / UV / OT / **OW por tensão < 2,3 V** | adbms_to_CAN.c (`ovuvot_fault_now`) |
| PEC flood (> 1/4 dos devices) | adbms_to_CAN.c |
| ISA do pack calada > 600 ms | brain.c |
| 0x084 do handcart calado > 1000 ms (só em CHARGING) | brain.c |
| Ambas as filas CAN TX cheias > 500 ms | can.c |
| Mismatch de contactor (WRONG e HV_ON) | precharge.c |

## Limitação conhecida: flag única partilhada
Uma só flag para todas as fontes. Se a fonte A dispara e a B faz o seu edge-clear, apaga o erro de A. O edge minimiza mas não elimina. Foi decisão deliberada (o utilizador rejeitou a bitmask de razões: "só quero permanente vs não permanente").

## Diagnóstico
- `live_debug.ams_error` — `state_name` ("OK"/"ERROR"/"ERROR_PERMANENT") + `faults[8]` com os **nomes** dos faults ativos + máscara em dois uint32.
- Breakpoint em `ams_error.c` na linha `ams_error_active = 1` com condição `ams_error_active == 0` → o **call stack** diz a fonte. Funcionou várias vezes.
- Consola imprime as transições ("AMS_ERROR line -> ERROR/OK").
- ⚠️ Parar o CPU dispara o WWDG e cascata de timeouts CAN/ISA — preferir consola/live_debug a breakpoints.

Regra de ordem: **segurança primeiro, log depois** — `RAISE_ERROR` + `AMS_Error_Trigger()` antes do `printfDebug` (que formata 256 bytes e pode ser chamado 144× por ciclo).
