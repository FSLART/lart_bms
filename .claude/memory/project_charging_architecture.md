---
name: project-charging-architecture
description: "Carregamento do lart_bms — handcart + charger EV Europe no CAN2, protocolo P1000, cortes de segurança"
metadata:
  node_type: memory
  type: project
---

Carregamento do TEK-26E (última revisão 2026-08-06):

## Barramentos
- **CAN2 (250 kbit/s)** = tudo do carregamento: handcart, carregador EV Europe, 2ª ISA, bootloader. **CAN1 (1 Mbit/s)** = powertrain (VCU, telemetria, ISA do pack).
- Telemetria dos slaves espelhada no CAN2 **só durante CHARGING** — no carro o CAN2 não tem nós, ninguém dá ACK, a fila enche para sempre e degenera o check das duas filas cheias em "só o CAN1".

## Carregador: EV Europe OEM3 3.3 kW, modelo **650-6** (300-650 VDC, 6 A máx)
- **Protocolo 1000/1200**, não o 997 dos símbolos base do DBC:
  - BMS → carregador: `0x1806E5F4` (`BMS_ChargingRequest_P1000`)
  - carregador → BMS: `0x18FF50E5` (`Charger_Status_P1000`)
  - No código: símbolos `handcart_t26_*_p1000_*`. Usar os base (997) = carregador fica em `comm_timeout` e não recebe comandos.
- Byte5 `Control`: **0 = carregar, 1 = parar**. Ciclo 1000 ms; se o carregador não receber em 5 s entra em falha de comunicação e desliga a saída.
- Setpoints: **600 V @ 6 A**.
- LED do carregador diagnostica: verde contínuo = standby, verde a piscar = a carregar, vermelho ×3 = falha AC, ×5 = tensão da bateria alta.

## Submáquina do charger.c
`WAIT_HV` → `PRESTART_STOP` (5 s com Control=1) → `CHARGING` → `STOPPING` → `DONE` (latch, só reinicia com o switch OFF→ON).

Antes de arrancar (em `WAIT_HV` com HV_ON): a tensão da ISA do CAN2 tem de bater com `g_pack_voltage_sum_mV` dentro de **20%**, senão não arranca.

Verificações no `CHARGING`, por ordem:
1. **SDC (PC7) LOW** → Control=1 imediato + ForceKill + DONE. Não esperar pelo `Precharge_GetState()`: a FSM só nota a queda dos contactores ~5 s depois (`skip_ticks`).
2. **ISA do CAN2 calada > 1000 ms** → corte (senão o corte de corrente fica cego)
3. perdeu HV_ON → pausa (volta a WAIT_HV)
4. célula máx ≥ **4150 mV** → fim normal (abaixo do OV de 4200 para a carga completa nunca tocar na proteção)
5. temp ≥ **50 °C** · |I| > **15 A** · status timeout 3 s · flags de falha do carregador

`starting_state_fault` = "bateria desligada/invertida". Com a bateria desativada o carregador levanta-a e o BMS corta — correto, mas **sem debounce**: uma única frame mata a carga (latch DONE), e a flag pode piscar transitoriamente no arranque mesmo com bateria boa.

## Ordem de carregamento
0x084 `Handcart_Switch_Feedback` (stream 100 ms): edge OFF→ON arranca a precarga (evita rearranque pós-corte) e mete `AMS_State = CHARGING`. OFF → paragem ordenada: stop → contactores abrem quando |I| < 500 mA, máx 5 s.

Ver [[project-two-isas]], [[project-ams-error-architecture]].
