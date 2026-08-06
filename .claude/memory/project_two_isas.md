---
name: project-two-isas
description: "Duas ISAs IVT-S no lart_bms — pack no CAN1, handcart no CAN2, responsabilidades separadas"
metadata:
  node_type: memory
  type: project
---

O TEK-26E tem **dois sensores IVT-S diferentes**, com os **mesmos CAN IDs** (0x521-0x528) em barramentos diferentes. Nunca misturar.

| | ISA do pack (CAN1) | ISA do handcart (CAN2) |
|---|---|---|
| Presença | sempre | só durante o carregamento |
| Alimenta | `ivt` (SOC, As, tensão do pack) | struct `ivt_can2` separada |
| Getters | `IVT_GetCurrent_mA()` etc. | `IVT_Get*Can2_*()` |
| Timeout | > 600 ms → **AMS error** (brain.c) | > 1000 ms → **corta a carga** (charger.c) |

**A corrente de carregamento passa pela ISA do CAN2**, não pela do pack. Por isso o corte de 15 A e o settle (< 500 mA) leem o CAN2. Ter isto no CAN1 era um buraco silencioso — o corte nunca disparava durante a carga.

**NUNCA registar `IVT_CAN_OnMessage` no CAN2**: contaminaria o SOC/As do pack. O handler do CAN2 (`IVT_CAN2_Presence_OnMessage`) desempacota para `ivt_can2` e nunca chama `SOC_*` nem `Check_PackVoltage_and_Current`.

## Como a "vida" do sensor é avaliada — só pelo ID
O carimbo do `lastTime` acontece para **qualquer** ID em 0x521..0x528. Não olha ao payload, DLC, nem às flags de erro. Consequências:
- Basta uma das 8 tramas cíclicas (ex. só a U3 em 0x524) para o sensor contar como vivo, mesmo que a corrente (0x521) nunca chegue → `ivt.iBatt` fica congelado sem aviso.
- A struct desempacotada traz `system_error`, `measurement_error`, `channel_error`, `ocs` e `msg_count` — **todos ignorados**. Já se viu tráfego real com `system_error=1` e `channel_error=1` aceite como bom.

Cuidado a corrigir isto: o `IVT_CAN_OnMessage` está registado como callback **geral** do CAN1 e recebe todas as tramas do barramento. Carimbar em todas fazia o timeout nunca disparar (bug já corrigido — agora filtra a gama de IDs).

## Comandos de configuração (envio manual)
Todos vão para **`0x411`, DLC 8**; respostas em `0x511`; resultados cíclicos em 0x521-0x528.
Estrutura dos 0x20-0x27: `byte0`=canal, `byte1`=modo (0x02 cíclico), `bytes2-3`=período ms big-endian.
`0x3A XX` = RESTART_to_Bitrate — **o mapeamento de `XX` não está documentado no repo**; o código só tem `0x02` rotulado "1 Mbit/s" por comentário (não verificado). Errar o valor deixa o sensor num bitrate desconhecido (recupera-se varrendo 125k/250k/500k/1M).

Ver [[project-charging-architecture]].
