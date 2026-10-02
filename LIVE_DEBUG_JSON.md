# JSON do live_debug no UART2 — guia de leitura

Uma linha JSON por segundo no UART2 (Bluetooth, RN4871), terminada em `\r\n`.
Gerada por `LiveDebug_SendJson()` em `Firmware/Core/Src/live_debug.c`, a partir
da struct `live_debug`. Valores todos **inteiros**; a unidade está no sufixo:

| Sufixo | Unidade | Exemplo |
|---|---|---|
| `_mV` / `_mA` | milivolt / miliampere | `4011` = 4,011 V |
| `_dC` | 0,1 °C | `452` = 45,2 °C |
| `_cC` | 0,01 °C | `4400` = 44,00 °C |
| `_dV` / `_dA` | 0,1 V / 0,1 A | `6000` = 600,0 V |
| `_ms` / `_s` | milissegundos / segundos | |
| `_As` | ampere-segundo (coulomb) | `3600` = 1 Ah |
| `percent_x100` | % × 100 | `8660` = 86,60 % |

Além do JSON, no UART2 só aparecem as transições da linha AMS (uma linha de texto cada):

```
AMS_ERROR line -> ERROR
AMS_ERROR line -> ERROR (PERMANENT, only reboot clears)
AMS_ERROR line -> OK
```

---

## 1. `bms` — máquina de estados principal

```json
"bms":{"state":"IDLE","prev_state":"STARTUP","runtime_s":123,"wwdg_reset":0,"active_faults":2}
```

| `state` | Quando | O que o BMS faz |
|---|---|---|
| `STARTUP` | Arranque | Conta os slaves, lê tudo uma vez, inicia o SOC → `IDLE` |
| `IDLE` | Normal, HV ligada ou desligada | Ciclo de leitura a cada 50 ms + open-wire; decide se arranca balanceamento |
| `BALANCING` | Δ(máx−mín) > 30 mV, precarga em `KILL`, sem AMS_ERROR | Ciclos de 1,5 s de descarga + medição + open-wire |
| `CHARGING` | Switch do handcart ON (0x084) estando em `IDLE` | Leitura a cada 25 ms + submáquina do carregador |
| `RESET_ISA` | Pedido de reconfiguração da ISA | Reconfigura a ISA e volta ao estado anterior |
| `FAULT` | Estado desconhecido no `brain_loop` | ⚠️ Fica preso: o ADBMS deixa de ser lido, tensões/temperaturas congelam |
| `ONMISSION`, `DISCHARGE_TEST` | Existem no enum mas o `brain_loop` não os trata | Se aparecerem, o ciclo seguinte cai em `FAULT` |

- `prev_state` — estado da iteração anterior do loop (mostra de onde veio).
- `wwdg_reset` — `1` = o último arranque foi provocado pelo watchdog (o loop encravou > 65 ms).
- `active_faults` — quantos faults estão ativos no fault manager.

---

## 2. `ams_error` — linha AMS_ERROR (pino PC8)

```json
"ams_error":{"state":"ERROR","active":1,"permanent":0,"fault_count":2,"fault_mask_lo":4,"fault_mask_hi":0,"faults":["OVERTEMPERATURE","PEC_ERROR"]}
```

| `state` | Significa |
|---|---|
| `OK` | Linha em OK |
| `ERROR` | Erro limpável: limpa sozinho quando a causa desaparece |
| `ERROR_PERMANENT` | Latch permanente: só um power cycle limpa |

> Ao arrancar a linha vai a `ERROR` (fail-safe) e só passa a `OK` quando a chain de slaves está completa.

**`faults`** — nomes dos faults ativos. `fault_mask_lo` é o bit *n* para o código *n* (0–31), `fault_mask_hi` para 32–63.
Os que o código **levanta hoje**:

| Código | Nome | Origem |
|---|---|---|
| 0 | `OVERVOLTAGE` | Célula > 4,20 V |
| 1 | `UNDERVOLTAGE` | Célula < 2,80 V |
| 2 | `OVERTEMPERATURE` | NTC ≥ 65 °C |
| 7 | `SLAVE_NOT_DETECTED` | Menos slaves do que os 12 esperados |
| 8 | `PEC_ERROR` | Qualquer leitura isoSPI com CRC errado |
| 11–15 | `CAN_SEND_ERROR`, `CAN_INIT_ERROR`, `CAN_MAILBOX_FULL`, `CAN_BUS_OFF`, `CAN_RECEIVE_ERROR` | Driver CAN |
| 22 | `CONTACTOR_MISMATCH` | Feedback dos contactores ≠ esperado |
| 27 | `BALANCING_OVERTEMP` | Die do ADBMS ≥ 85 °C a balancear |
| 29 | `CURRENT_SENSOR_ERROR` | ISA |
| 31 | `PACK_VOLTAGE_MISMATCH` | ISA vs soma das células |
| 32–34 | `EEPROM_READ_ERROR`, `EEPROM_WRITE_ERROR`, `EEPROM_VALIDATION_ERROR` | Configuração na EEPROM |
| 36 | `WATCHDOG_RESET` | Arranque por WWDG |
| 38, 40 | `UART1_TX`, `UART2_TX` | Buffer de TX cheio |
| 42 | `OW_DETECTED_CELL` | Fio de sense de célula aberto |
| 43 | `OW_DETECTED_RTH` | Fio de NTC aberto |
| 44 | `GPIO_EXPANDER` | MCP23017 |

Os restantes nomes do enum (`UNDERTEMPERATURE`, `VCU_TIMEOUT`, `PRECHARGE_TIMEOUT`, ...) existem mas nada os levanta.

---

## 3. `precharge` — sequência de precarga e contactores

```json
"precharge":{"state":"HV_ON","vcu_request":1,"vcu_request_age_ms":40,"hv_on_mismatch_count":0,"sdc_closed":1,"air_pos":1,"air_neg":1,"precharge_relay":0,"discharge":0}
```

**`state`** — sequência normal (≈ 4,5 s de `RX_CAN` a `HV_ON`):

```
KILL ──pedido──> RX_CAN → START → OPEN_ALL → SWITCH_HVNEG → WAIT_FOR_AIR_NEG_TO_CLOSE (300 ms)
  → CHECKING_AIR_NEG_IS_CLOSED → SWITCH_PRECHARGE → WAIT_FOR_PRECHARGE_TO_CLOSE (3 s)
  → CHECKING_PRECHARGE_IS_CLOSED → VERIFY_CURRENT → VERIFY_BUS_VOLT → SWITCH_HVPOS
  → WAIT_FOR_AIR_POS_TO_CLOSE (300 ms) → CHECKING_AIR_POS_IS_CLOSED → TURN_OFF_PRECHARGE
  → WAIT_FOR_PRECHARGE_TO_OPEN (300 ms) → CHECKING_PRECHARGE_IS_OPEN → HV_ON
```

| `state` | Significa |
|---|---|
| `KILL` | Tudo aberto, HV desligada. Estado de repouso. Só daqui arranca nova precarga |
| `RX_CAN` | Pedido aceite, a arrancar a sequência |
| `WAIT_*` / `CHECKING_*` / `SWITCH_*` | A meio da sequência acima |
| `HV_ON` | Precarga completa, HV ligada |
| `WRONG` | Um check falhou → AMS_ERROR + `CONTACTOR_MISMATCH`, a seguir `KILL` |

| Campo | Valores |
|---|---|
| `vcu_request` | `-1` nunca recebido desde o boot · `0` VCU pede desligar · `1` VCU pede precarga |
| `vcu_request_age_ms` | ms desde a última trama da VCU (`4294967295` = nunca) |
| `hv_on_mismatch_count` | 0–2 = checks seguidos falhados em `HV_ON` (EMI a ser filtrada); ao 3.º vai para `KILL` |
| `sdc_closed` | `1` shutdown circuit fechado · `0` aberto |
| `air_pos`, `air_neg`, `precharge_relay`, `discharge` | Feedback dos contactores, `1` = fechado |

Combinações esperadas:

| `state` | air_neg | precharge_relay | air_pos |
|---|---|---|---|
| `KILL` | 0 | 0 | 0 |
| `WAIT_FOR_PRECHARGE_TO_CLOSE` | 1 | 1 | 0 |
| `WAIT_FOR_PRECHARGE_TO_OPEN` | 1 | 1→0 | 1 |
| `HV_ON` | 1 | 0 | 1 |

---

## 4. `isa_pack` / `isa_handcart` — sensores de corrente ISA IVT-S

```json
"isa_pack":{"current_mA":-2700,"voltage_mV":601600,"power_W":-1624,"charge_As":-5400,"temp_dC":312,"rx_age_ms":8}
```

- `isa_pack` = ISA do **pack**, no CAN1 (alimenta o SOC). `isa_handcart` = ISA do **handcart**, no CAN2 (corrente de carga).
- `current_mA` negativo = a sair do pack (descarga) no sentido configurado da ISA.
- `rx_age_ms` — idade da última trama. **> 1000 = ISA calada** (no carro sem handcart o `isa_handcart` fica sempre alto, é normal).

---

## 5. `soc`

```json
"soc":{"percent_x100":8660,"used_charge_As":-5400,"ready":1}
```

`ready = 0` até ao `STARTUP` acabar (o SOC é semeado pela célula mais baixa no arranque).

---

## 6. `charger` — submáquina do carregador (só corre em `bms.state = CHARGING`)

```json
"charger":{"state":"CHARGING","handcart_switch":1,"handcart_age_ms":60,"status_ok":1,"output_voltage_dV":6000,"output_current_dA":55,"hw_failure":0,"temp_otp":0,"input_voltage_fault":0,"starting_state_fault":0,"comm_timeout":0,"temp_C":35,"requested_current_raw":60}
```

```
WAIT_HV ──precarga em HV_ON──> PRESTART_STOP (5 s) ──> CHARGING ──célula ≥ 4,25 V──> STOPPING ──corrente < 0,5 A ou 5 s──> DONE
                                                          └── corte de segurança ──────────────────────────────> DONE
```

| `state` | Significa |
|---|---|
| `WAIT_HV` | À espera que a precarga chegue a `HV_ON` |
| `PRESTART_STOP` | HV ligada; 5 s a mandar "stop" antes de pedir carga |
| `CHARGING` | A pedir carga, a vigiar células/temperatura/corrente |
| `STOPPING` | Pediu stop, à espera que a corrente caia antes de abrir contactores |
| `DONE` | Terminado (fim normal ou corte). Só recomeça com o switch do handcart OFF → ON |

Cortes que mandam para `DONE`: SDC aberto, ISA do handcart calada, temperatura ≥ 60 °C, corrente > 15 A,
carregador calado 3 s, flags de falha do carregador, ISA vs soma das células > 20 % no arranque.

| Campo | Valores |
|---|---|
| `handcart_switch` | `1` switch do handcart ON |
| `handcart_age_ms` | Idade da última 0x084 (`4294967295` = handcart nunca visto) |
| `status_ok` | `1` = recebeu status do carregador nos últimos 3 s (os campos abaixo só valem com `1`) |
| `hw_failure`, `temp_otp`, `input_voltage_fault`, `starting_state_fault`, `comm_timeout` | Flags do próprio carregador, `1` = falha |

---

## 7. `pack` — agregados do ADBMS

```json
"pack":{"slaves_found":12,"adbms_state":"ONGOING","any_pec_error":0,"cell_max_mV":4011,"cell_min_mV":3886,"temp_max_cC":4400,"temp_min_cC":3300,"voltage_sum_mV":573300}
```

| `adbms_state` | Significa |
|---|---|
| `START` | Ciclo de leitura acabou de começar |
| `ONGOING` | A meio do ciclo |
| `END` | Ciclo terminado |

`slaves_found` < 12 → falta um slave na chain (a linha AMS fica em `ERROR` desde o boot).

---

## 8. `balancing`

```json
"balancing":{"active":1,"stage":"ROUGH","phase":"ON_TIME","target_mV":3886,"cells_discharging":48,"cell_max_mV":4011,"cell_min_mV":3886,"delta_mV":125,"worst_delta_mV":125,"worst_slave":3,"worst_cell":1,"mask":[0,0,4095,4095,4095,4095,4095,4095,0,0,0,0]}
```

| `stage` | Significa |
|---|---|
| `ROUGH` | Só descarrega células ≥ 100 mV acima do alvo |
| `FINE` | Descarrega todas > 8 mV acima do alvo |
| `END` | Convergiu (todas dentro de 8 mV) ou parou |

**`phase`** — ciclo de ~1,7 s:

```
INIT → COMPUTE → APPLY → ON_TIME (1,5 s a descarregar) → STOP_DISCHARGE → SETTLE (100 ms)
     → START_AVG → WAIT_AVG (20 ms) → READ_AVG → OW (open-wire) → INIT ...
```

| Campo | Significa |
|---|---|
| `active` | `1` = `bms.state` é `BALANCING` |
| `target_mV` | Alvo congelado no início da sessão (a célula mais baixa) |
| `mask` | Por slave (S01..S12), bit *i* = célula *i*+1 a descarregar. `4095` = 12 células, `4080` = células 5–12 |
| `worst_*` | Célula mais acima do alvo; `worst_delta_mV = 0` = convergido |

Pára sozinho se: precarga sai de `KILL`, AMS_ERROR ativo, die ≥ 85 °C (bloqueia 60 s), ou convergiu.

---

## 9. `board`, `can1`/`can2`, `json`

```json
"board":{"fan_pwm":230,"mcu_temp_dC":452,"vdda_mV":3290,"master_current_mA":163},
"can1":{"started":1,"state":2,"bus_off":0,"tx_errors":0,"rx_errors":0,"last_error":0,"hw_error":0,"tx_queue":3},
"json":{"sent":99,"dropped":0,"last_len":3064}
```

- `fan_pwm` 0–255 (rampa: 38 °C → 0, 45 °C → 255).

**CAN — `state`**: `0` RESET · `1` READY · `2` LISTENING (normal, a funcionar) · `3` SLEEP_PENDING · `4` SLEEP_ACTIVE · `5` ERROR

**CAN — `last_error`**: `0` nenhum · `1` stuff · `2` form · `3` ACK (ninguém a responder no barramento) · `4` bit recessivo · `5` bit dominante · `6` CRC · `7` definido por software

**CAN — `tx_errors`**: sobe quando as tramas não recebem ACK. ≥ 128 = error-passive, `bus_off = 1` = desistiu.

**CAN — `hw_error`** (bits somados): `1` warning · `2` passive · `4` bus-off · `8` stuff · `16` form · `32` ACK · `64` bit rec · `128` bit dom · `256` CRC · `512` overrun FIFO0 · `1024` overrun FIFO1 · `4096`/`16384`/`65536` falha de TX nas mailboxes 0/1/2

**`json`**: contadores das tramas **anteriores**. `dropped` a subir = ring buffer do UART2 cheio (o BT não está a escoar).
`dma_errors` = erros do DMA/UART de TX (reenvia sozinho). `dma_restarts` = TX encravado > 2 s, o firmware fez abort e recomeçou.
Se o UART2 parar e voltar com `dma_restarts` maior, o encravamento foi no MCU; se voltar igual, foi do lado do RN4871/BLE.
`bt_reboots` = reboots periódicos do RN4871 (de 30 em 30 s, `"$$$"` + `"R,1\r"`). Em cada um a ligação BLE cai durante ~2 s e o telemóvel tem de voltar a ligar; falta 1–3 tramas.

---

## 10. Por slave

```json
"cell_mV":[[3965,3966,...],[...]],
"ntc_dC":[[331,332,330,331,333,330],[...]],
"die_dC":[452,448,...],
"pec_flags":[0,0,0,1,0,...],
"ow_cell_mask":[0,0,...],
"ow_ntc_mask":[0,0,12,0,...]
```

| Campo | Formato | Notas |
|---|---|---|
| `cell_mV` | `[slave][célula]`, 12 × 12 | Negativo / ~−3415 = registo nunca escrito (0x8000) |
| `ntc_dC` | `[slave][ntc]`, 12 × 6 | ~20 (2 °C) = NTC aberto · ~1500 (150 °C) = curto. Slave 3 NTC3/NTC4 estão desativados e mostram o valor do NTC2 |
| `die_dC` | 12 | Temperatura interna de cada ADBMS6830. ±9999 = leitura lixo |
| `pec_flags` | 12 | bit0 `1` cell · bit1 `2` avg · bit2 `4` aux · bit3 `8` raux · bit4 `16` status. `0` = tudo limpo |
| `ow_cell_mask` | 12 | bit *i* = fio da célula *i*+1 aberto |
| `ow_ntc_mask` | 12 | bit *i* = fio do NTC *i*+1 aberto |

---

## 11. Exemplos completos por situação

**Parado, LV ligada, a balancear sozinho**
```json
{"uptime_ms":184000,"bms":{"state":"BALANCING","prev_state":"BALANCING","runtime_s":184,"wwdg_reset":0,"active_faults":0},"ams_error":{"state":"OK","active":0,"permanent":0,"fault_count":0,"fault_mask_lo":0,"fault_mask_hi":0,"faults":[]},"precharge":{"state":"KILL","vcu_request":-1,"vcu_request_age_ms":4294967295,"hv_on_mismatch_count":0,"sdc_closed":1,"air_pos":0,"air_neg":0,"precharge_relay":0,"discharge":0},...,"balancing":{"active":1,"stage":"ROUGH","phase":"ON_TIME","target_mV":3886,"cells_discharging":72,...,"mask":[0,0,4095,4095,4095,4095,4095,4095,0,0,0,0]},...}
```

**VCU pediu precarga, a meio da sequência** (balanceamento já parou sozinho)
```json
{...,"bms":{"state":"IDLE",...},"precharge":{"state":"WAIT_FOR_PRECHARGE_TO_CLOSE","vcu_request":1,"vcu_request_age_ms":20,"hv_on_mismatch_count":0,"sdc_closed":1,"air_pos":0,"air_neg":1,"precharge_relay":1,"discharge":0},...,"balancing":{"active":0,"stage":"END",...,"mask":[0,0,0,0,0,0,0,0,0,0,0,0]},...}
```

**A andar (HV ligada)**
```json
{...,"bms":{"state":"IDLE",...},"precharge":{"state":"HV_ON","vcu_request":1,"vcu_request_age_ms":30,"hv_on_mismatch_count":0,"sdc_closed":1,"air_pos":1,"air_neg":1,"precharge_relay":0,"discharge":0},"isa_pack":{"current_mA":-85000,"voltage_mV":585200,...,"rx_age_ms":6},...}
```

**EMI a mexer nos feedbacks em HV_ON (filtrada)**
```json
{...,"precharge":{"state":"HV_ON",...,"hv_on_mismatch_count":1,...},"ams_error":{"state":"OK",...,"faults":["CONTACTOR_MISMATCH"]},...}
```
O fault aparece e desaparece; só ao 3.º check seguido vai para `KILL` com AMS_ERROR.

**A carregar no handcart**
```json
{...,"bms":{"state":"CHARGING",...},"precharge":{"state":"HV_ON",...},"isa_handcart":{"current_mA":5480,"voltage_mV":598300,...,"rx_age_ms":11},"charger":{"state":"CHARGING","handcart_switch":1,"handcart_age_ms":40,"status_ok":1,"output_voltage_dV":5983,"output_current_dA":55,...},...}
```

**Sobretemperatura (AMS_ERROR)**
```
AMS_ERROR line -> ERROR
```
```json
{...,"ams_error":{"state":"ERROR","active":1,"permanent":0,"fault_count":1,"fault_mask_lo":4,"fault_mask_hi":0,"faults":["OVERTEMPERATURE"]},"pack":{...,"temp_max_cC":6620,...},"ntc_dC":[[...],[...],[331,662,...],...],...}
```

**Slave com isoSPI mau (EMI)**
```json
{...,"ams_error":{...,"faults":["PEC_ERROR"]},"pack":{...,"any_pec_error":1,...},"pec_flags":[0,0,0,0,0,0,0,0,0,0,0,3],...}
```
`3` no S12 = cell + avg com CRC errado: nesse ciclo o S12 é ignorado pelo SafetyCheck e pelo balanceamento.

**Fio de sense partido**
```json
{...,"ams_error":{...,"faults":["OW_DETECTED_CELL"]},"ow_cell_mask":[1024,0,0,...],"cell_mV":[[...,3965,120,5930,...],...]}
```
`1024` = bit 10 = célula 11 do S01. A célula ao lado fica alta (~5,9 V, encostada ao clamp).

**BT não está a escoar**
```json
{...,"json":{"sent":412,"dropped":37,"last_len":3071}}
```
`dropped` a subir: o RN4871 não aguenta o débito (perde tramas inteiras, nunca metades).
