# Configuração do BMS

Tudo o que se pode afinar sem recompilar está na EEPROM 24FC08 (1 KB) e muda-se
pela consola, no **BLE (RN4871)** ou no **cabo (UART1, 230400 8N1)**. São 101
parâmetros; a tabela completa está no fim deste ficheiro e sai também na
consola com `get all`.

## Como mudar

```
get cell_max_mV             -> #CFG cell_max_mV=4200 mV [3000..4500]
set cell_max_mV 4190        -> #OK cell_max_mV=4190 mV [3000..4500]
set soc_curve_mV[3] 3260    -> um elemento de uma tabela
set soc_curve_pct 0,5,10,20,30,40,50,60,70,80,90,100
set ams_error_enable +PEC_ERROR
diff                        -> o que difere da config em uso
apply                       -> passa a valer já (sem gravar)
save                        -> valida, aplica e grava; no fim: #SAVE ok sequencia=N banco=A|B
```

1. `set` mexe só numa **cópia de edição**; o BMS continua com a config em uso.
2. `apply` e `save` validam a cópia inteira (gamas e coerência: `cell_min <
   low_warn <= high_warn < cell_max`, curva do SOC crescente, 30 % das células
   com NTC, bitrates válidos...). Se estiver incoerente nada muda e a resposta
   diz porquê.
3. Os dois só funcionam com a **HV desligada** (`hv_safe`: pré-carga em KILL e
   os feedbacks dos contactores todos abertos). `set` funciona sempre.
4. Os parâmetros marcados **Reinício** (topologia, bitrates do CAN, frequência
   da ventoinha, nome BLE) ficam gravados mas só mudam depois de `reboot`.
   O resto muda logo no `apply`/`save`.
5. `revert` deita fora a cópia; `defaults` carrega os defaults na cópia
   (só usa depois de `apply`/`save`).

Todas as respostas começam por `#`. A BMS_UI trata como amostras só as linhas
que começam por `{`, por isso a consola pode correr no BLE ao mesmo tempo que
o JSON.

### Comandos

| Comando | O quê |
|---|---|
| `status` | estado do AMS, pré-carga, carregador, `hv_safe`, faults, origem da config, RN4871 |
| `get <nome>` / `get <nome>[i]` / `get all` | valor na cópia de edição |
| `set <nome> <valor>` / `set <nome>[i] <valor>` | tabelas inteiras com `v1,v2,...`; inteiros em decimal ou `0x..`; bools `0/1/on/off` |
| `diff`, `apply`, `save`, `revert`, `defaults` | ver acima |
| `faults` | faults ativos e se cada um segura o AMS_ERROR |
| `live on` / `live off` / `live <ms>` | JSON nesta ligação (só até reiniciar) |
| `json` | uma linha JSON já |
| `fan cells max` / `fan cells auto` (ou `lv`) | ventoinha no máximo |
| `bt reboot` | reinicia o RN4871 |
| `eeprom` | dump da EEPROM em hexadecimal |
| `version` | firmware, versão do esquema, número de parâmetros, data de compilação |
| `reboot` / `bootloader` | só com a HV desligada e fora de um carregamento |
| `unlock <pin>` | no BLE, se `bt_pin` ≠ 0 |

### PIN do BLE

Com `bt_pin = 0` (o default) qualquer telemóvel ligado ao RN4871 pode mudar a
configuração quando a HV está desligada. Com um PIN (1..9999) os comandos que
mudam alguma coisa (`set`, `apply`, `save`, `revert`, `defaults`, `reboot`,
`bootloader`, `fan`, `bt`) pedem `unlock <pin>` uma vez por ligação; ao
desligar volta a trancar. Três PIN errados bloqueiam 10 s. Pelo cabo nunca
pede PIN.

```
set bt_pin 4821
save
```

## Faults que abrem a linha AMS_ERROR

Estes abrem sempre a linha, a config não os desliga (FS 2027 EV 5.8.3, 5.8.10,
5.8.13): `CELL_OVERVOLTAGE`, `CELL_UNDERVOLTAGE`, `CELL_OPEN_WIRE`,
`OVERTEMPERATURE`, `UNDERTEMPERATURE`, `NTC_OPEN_WIRE`, `SLAVE_MISSING`,
`ADBMS_TIMEOUT`, `OVERCURRENT`, `ISA_PACK_TIMEOUT`. Estão em `faults_mandatory`
(`Core/app/faults.c`).

`ams_error_enable` acrescenta outros. Números (são também os bits das máscaras
do JSON e o `fault1_code - 1` do 0x703):

| # | Fault | # | Fault |
|---|---|---|---|
| 0 | CELL_OVERVOLTAGE | 16 | ISA_PACK_TIMEOUT |
| 1 | CELL_UNDERVOLTAGE | 17 | VCU_TIMEOUT |
| 2 | CELL_OPEN_WIRE | 18 | HANDCART_TIMEOUT |
| 3 | OVERTEMPERATURE | 19 | CHARGER_TIMEOUT |
| 4 | UNDERTEMPERATURE | 20 | CONTACTOR_MISMATCH |
| 5 | NTC_OPEN_WIRE | 21 | PRECHARGE_TIMEOUT |
| 6 | DIE_OVERTEMPERATURE | 22 | PRECHARGE_FAILURE |
| 7 | SLAVE_MISSING | 23 | SDC_OPEN |
| 8 | PEC_ERROR | 24 | CHARGER_FAULT |
| 9 | ADBMS_TIMEOUT | 25 | CONFIG_INVALID |
| 10 | OVERCURRENT | 26 | WATCHDOG_RESET |
| 11 | PACK_VOLTAGE_MISMATCH | 27 | MCU_ADC_ERROR |
| 12 | SOC_LOW | 28 | EEPROM_ERROR |
| 13 | CAN1_BUS_OFF | 29 | ISA_PACK_ERROR |
| 14 | CAN2_BUS_OFF | 30 | CELL_VOLTAGE_WARNING |
| 15 | CAN_TX_QUEUE_FULL | 31 | TEMPERATURE_WARNING |

Todas as fontes são limpáveis: quem segura o SDC aberto até ao reset manual é
o latch em hardware (FS 2027 EV 6.1.6). A linha fica em erro pelo menos
`ams_error_min_on_ms`.

## EEPROM

Dois bancos de 512 B, A (0x000) e B (0x200):

| Offset | Tipo | Campo |
|---|---|---|
| 0 | u16 | magic `0x4C42` |
| 2 | u8 | `CONFIG_SCHEMA_VERSION` |
| 3 | u8 | reservado |
| 4 | u32 | sequência (sobe a cada gravação) |
| 8 | u16 | número de parâmetros gravados |
| 10 | u16 | bytes do payload |
| 12 | u32 | CRC32 do esquema (nome, tipo e tamanho de cada parâmetro gravado) |
| 16 | ... | payload: os parâmetros pela ordem da tabela, little-endian |
| 16+n | u32 | CRC32 de tudo o que está antes |

- Grava-se sempre no banco que **não** está em uso, uma página de 16 B por
  passo do `brain_loop` (5 ms de escrita cada); no fim lê-se o banco de volta e
  só então passa a ser o atual. Um corte de energia a meio deixa o banco antigo
  intacto.
- No arranque usa-se o banco válido com a sequência mais alta. Cada valor é
  posto dentro da sua gama e a config inteira é validada; se nenhum banco
  servir, ficam os defaults e o fault `CONFIG_INVALID` (só aviso, não abre o
  AMS_ERROR).
- **Parâmetros novos só no fim da tabela** (`Core/config/bms_config.c`). Uma
  EEPROM gravada por um firmware mais antigo continua a ler-se e os parâmetros
  novos ficam com o default. Mudar a ordem, o tipo ou o tamanho de um
  parâmetro muda o CRC do esquema e a EEPROM passa a ser rejeitada.
- Payload atual: 305 B de 492 possíveis.

## Da página de configuração da BMS_UI para o firmware

| UI (`bms-settings.js`) | Firmware | Conversão |
|---|---|---|
| `v_min`, `v_warn_low`, `v_warn_high`, `v_max`, `v_open_wire` | `cell_min_mV`, `cell_low_warn_mV`, `cell_high_warn_mV`, `cell_max_mV`, `cell_open_wire_mV` | V × 1000 |
| `temp_warn`, `temp_fault` | `temp_high_warn_dC`, `temp_max_dC` | °C × 10 |
| `charge_v_max`, `charge_a_max` | `charge_voltage_dV`, `charge_current_dA` | × 10 |
| `contactor_open_a` | `charge_open_mA` | A × 1000 |
| `bal_v_min`, `bal_v_max`, `bal_delta_mv` | `bal_min_cell_mV`, `bal_max_cell_mV`, `bal_delta_mV` | V × 1000 / mV |
| `bal_die_temp` | `bal_die_stop_dC` | °C × 10 |
| `bal_pulse_ms` | — | já não existe: o balanceamento decide a cada ciclo de 200 ms |
| `fan_temp_start`, `fan_temp_full` | `fan_cells_start_dC`, `fan_cells_full_dC` | °C × 10 |
| `fan_pwm_min`, `fan_pwm_max` | `fan_cells_pwm_min_pct`, `fan_cells_pwm_max_pct` | % |
| `pre_timeout_s`, `pre_bus_pct`, `pre_air_timeout` | `precharge_timeout_ms`, `precharge_bus_percentage`, `precharge_air_wait_ms` | s × 1000 / % / ms |
| `soh_nominal_ah` | `capacity_mAh` | Ah × 1000 |
| `ocv_points` (12 pares) | `soc_curve_pct`, `soc_curve_mV` | % / V × 1000 |
| `ocv_fit_degree` | `soc_fit_degree` | |
| `tempcomp_points` (10) | `tempcomp_temp_C`, `tempcomp_adjust_dmV`, `tempcomp_resistance_dmOhm` | °C / mV × 10 / mΩ × 10 |

## Valores a confirmar no carro

Estes defaults não vinham de nenhum valor testado:

- `overcurrent_discharge_A` = 300 e `overcurrent_charge_A` = 60: confirmar com
  a datasheet da P45B (FS 2027 EV 5.8.10). O 300 é o `dcl_max_a` da UI.
- `isa_bus_channel` = 1: o canal U da ISA do pack que está **depois** do AIR+.
  A pré-carga (`precharge_mode` = 1) espera que esse canal chegue a 95 % da
  soma das células. Se a ISA medir do lado do pack, usar `precharge_mode` = 0
  (só tempo, `precharge_min_ms`).
- `contactor_check_mask` = 0x07: o feedback da descarga não é verificado,
  como no firmware antigo. Antes de ligar o bit 3 confirmar o que o pino diz
  com o SDC fechado e aberto.
- `temp_min_dC`, `temp_low_warn_dC`, `fan_lv_*`, `pack_voltage_tolerance_pct`.

## Parâmetros

| # | Parametro | Default | Gama | Unidade | Reinicio |
|---|---|---|---|---|---|
| 0 | `slaves` | `12` | 1..16 |  | sim |
| 1 | `cells_per_slave` | `12` | 1..16 |  | sim |
| 2 | `ntcs_per_slave` | `6` | 0..10 |  | sim |
| 3 | `cells_parallel` | `3` | 1..10 |  |  |
| 4 | `ntc_disabled_mask` [16] | `0x0,0x0,0xC,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0` | 0x0..0x3FF | bits |  |
| 5 | `cell_min_mV` | `2500` | 2000..4000 | mV |  |
| 6 | `cell_low_warn_mV` | `2800` | 2000..4000 | mV |  |
| 7 | `cell_high_warn_mV` | `4150` | 3000..4500 | mV |  |
| 8 | `cell_max_mV` | `4200` | 3000..4500 | mV |  |
| 9 | `cell_open_wire_mV` | `2300` | 500..3000 | mV |  |
| 10 | `temp_min_dC` | `-200` | -400..400 | 0.1C |  |
| 11 | `temp_low_warn_dC` | `-100` | -400..400 | 0.1C |  |
| 12 | `temp_high_warn_dC` | `500` | 0..800 | 0.1C |  |
| 13 | `temp_max_dC` | `600` | 0..600 | 0.1C |  |
| 14 | `adbms_data_timeout_ms` | `300` | 250..500 | ms |  |
| 15 | `adbms_read_retries` | `2` | 0..5 |  |  |
| 16 | `adbms_missing_retries` | `2` | 0..10 |  |  |
| 17 | `adbms_dead_cycles` | `2` | 1..10 |  |  |
| 18 | `adbms_cth` | `1` | 0..7 | enum |  |
| 19 | `ow_min_gain_pct` | `70` | 10..100 | % |  |
| 20 | `ow_conv_ms` | `20` | 20..60 | ms |  |
| 21 | `ntc_beta` | `3977` | 1000..6000 | K |  |
| 22 | `ntc_r25_ohm` | `5000` | 100..65000 | ohm |  |
| 23 | `ntc_r_fixed_ohm` | `10000` | 100..65000 | ohm |  |
| 24 | `ntc_aux_match_mV` | `20` | 1..500 | mV |  |
| 25 | `overcurrent_discharge_A` | `300` | 1..1000 | A |  |
| 26 | `overcurrent_charge_A` | `60` | 1..500 | A |  |
| 27 | `overcurrent_ms` | `400` | 0..500 | ms |  |
| 28 | `pack_voltage_tolerance_pct` | `10` | 1..50 | % |  |
| 29 | `pack_voltage_mismatch_ms` | `1000` | 100..10000 | ms |  |
| 30 | `isa_pack_timeout_ms` | `600` | 100..1000 | ms |  |
| 31 | `isa_handcart_timeout_ms` | `1000` | 100..5000 | ms |  |
| 32 | `isa_bus_channel` | `1` | 1..3 | U |  |
| 33 | `isa_pack_invert` | `0` | 0..1 |  |  |
| 34 | `isa_handcart_invert` | `0` | 0..1 |  |  |
| 35 | `charge_voltage_dV` | `6000` | 0..6500 | 0.1V |  |
| 36 | `charge_current_dA` | `60` | 0..140 | 0.1A |  |
| 37 | `charge_open_mA` | `500` | 0..5000 | mA |  |
| 38 | `charge_cut_current_mA` | `15000` | 1000..30000 | mA |  |
| 39 | `charge_temp_max_dC` | `500` | 0..600 | 0.1C |  |
| 40 | `charge_settle_ms` | `5000` | 0..10000 | ms |  |
| 41 | `charge_prestart_ms` | `5000` | 0..30000 | ms |  |
| 42 | `charge_command_ms` | `1000` | 100..4000 | ms |  |
| 43 | `charge_status_timeout_ms` | `3000` | 500..10000 | ms |  |
| 44 | `charge_fault_debounce_ms` | `500` | 0..5000 | ms |  |
| 45 | `charge_bus_tolerance_pct` | `20` | 1..50 | % |  |
| 46 | `handcart_timeout_ms` | `1000` | 200..5000 | ms |  |
| 47 | `bal_enabled` | `1` | 0..1 |  |  |
| 48 | `bal_min_cell_mV` | `3000` | 2000..4000 | mV |  |
| 49 | `bal_max_cell_mV` | `4250` | 3000..4500 | mV |  |
| 50 | `bal_delta_mV` | `20` | 1..200 | mV |  |
| 51 | `bal_rough_mV` | `100` | 10..500 | mV |  |
| 52 | `bal_die_stop_dC` | `850` | 400..1200 | 0.1C |  |
| 53 | `bal_die_hold_s` | `60` | 0..600 | s |  |
| 54 | `fan_cells_start_dC` | `300` | 0..900 | 0.1C |  |
| 55 | `fan_cells_full_dC` | `550` | 0..900 | 0.1C |  |
| 56 | `fan_cells_pwm_min_pct` | `20` | 0..100 | % |  |
| 57 | `fan_cells_pwm_max_pct` | `100` | 0..100 | % |  |
| 58 | `fan_lv_start_dC` | `400` | 0..900 | 0.1C |  |
| 59 | `fan_lv_full_dC` | `700` | 0..900 | 0.1C |  |
| 60 | `fan_lv_pwm_min_pct` | `20` | 0..100 | % |  |
| 61 | `fan_lv_pwm_max_pct` | `100` | 0..100 | % |  |
| 62 | `fan_pwm_hz` | `1000` | 100..30000 | Hz | sim |
| 63 | `fan_hysteresis_dC` | `50` | 0..200 | 0.1C |  |
| 64 | `precharge_mode` | `1` | 0..1 | enum |  |
| 65 | `precharge_timeout_ms` | `10000` | 1000..60000 | ms |  |
| 66 | `precharge_bus_percentage` | `95` | 50..100 | % |  |
| 67 | `precharge_air_wait_ms` | `500` | 50..5000 | ms |  |
| 68 | `precharge_min_ms` | `3000` | 0..30000 | ms |  |
| 69 | `precharge_end_current_mA` | `0` | 0..20000 | mA |  |
| 70 | `precharge_lockout_ms` | `10000` | 0..60000 | ms |  |
| 71 | `contactor_debounce_ms` | `200` | 0..2000 | ms |  |
| 72 | `contactor_mismatch_count` | `3` | 1..20 |  |  |
| 73 | `contactor_check_mask` | `0x7` | 0x0..0xF | bits |  |
| 74 | `vcu_timeout_ms` | `0` | 0..10000 | ms |  |
| 75 | `capacity_mAh` | `13500` | 1000..65000 | mAh |  |
| 76 | `soc_fit_degree` | `4` | 1..7 |  |  |
| 77 | `soc_curve_pct` [12] | `0,5,10,20,30,40,50,60,70,80,90,100` | 0..100 | % |  |
| 78 | `soc_curve_mV` [12] | `2500,2806,3010,3248,3384,3469,3537,3622,3741,3894,4064,4200` | 2000..4500 | mV |  |
| 79 | `soc_rest_current_mA` | `1000` | 0..10000 | mA |  |
| 80 | `soc_rest_s` | `1800` | 0..65000 | s |  |
| 81 | `soc_low_pct` | `5` | 0..50 | % |  |
| 82 | `tempcomp_temp_C` [10] | `-20,-10,0,10,20,30,40,50,60,70` | -40..100 | C |  |
| 83 | `tempcomp_adjust_dmV` [10] | `0,0,0,0,0,0,0,0,0,0` | -1000..1000 | 0.1mV |  |
| 84 | `tempcomp_resistance_dmOhm` [10] | `176,158,140,122,104,86,68,50,32,20` | 0..10000 | 0.1mOhm |  |
| 85 | `board_current_mV_per_A` | `264` | 10..2000 | mV/A |  |
| 86 | `board_current_offset_mA` | `0` | -2000..2000 | mA |  |
| 87 | `can1_kbps` | `500` | 125..1000 | kbit/s | sim |
| 88 | `can2_kbps` | `500` | 125..1000 | kbit/s | sim |
| 89 | `can_master_ms` | `100` | 0..10000 | ms |  |
| 90 | `can_slaves_ms` | `250` | 0..10000 | ms |  |
| 91 | `can2_mirror_charging` | `1` | 0..1 |  |  |
| 92 | `bootloader_can_mask` | `0x2` | 0x0..0x3 | bits |  |
| 93 | `bt_name` | `LART Accumulator` | ate 20 caracteres |  | sim |
| 94 | `live_period_ms` | `1000` | 0..10000 | ms |  |
| 95 | `live_uart1_ms` | `1000` | 0..10000 | ms |  |
| 96 | `bt_probe_s` | `30` | 0..3600 | s |  |
| 97 | `bt_reboot_s` | `0` | 0..3600 | s |  |
| 98 | `bt_pin` | `0` | 0..9999 |  |  |
| 99 | `ams_error_enable` | `0x00000000000106BF` | mascara de faults | faults |  |
| 100 | `ams_error_min_on_ms` | `500` | 0..10000 | ms |  |

