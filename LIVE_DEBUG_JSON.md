# JSON ao vivo (BLE e UART1)

Uma linha JSON com tudo o que o BMS mede e decide, terminada em `\r\n`:

- no **RN4871** (UART2, 115200) a cada `live_period_ms` (1000 por omissão)
- no **UART1** (cabo, 230400) a cada `live_uart1_ms` (1000 por omissão)

Gerada por `LiveJson_Build()` em `Firmware/Core/comms/live_json.c`, à mão (sem
printf), ~4 kB com 12 slaves. As chaves são as do firmware antigo, que a
[BMS_UI](https://github.com/FSLART/BMS_UI) (`backend/decode/live_json.py`) já
lê; os campos novos foram acrescentados e a UI ignora o que não conhece.
Valores todos **inteiros**, a unidade no sufixo:

| Sufixo | Unidade |
|---|---|
| `_mV` / `_mA` | milivolt / miliampere |
| `_dC` / `_cC` | 0,1 °C / 0,01 °C |
| `_dV` / `_dA` | 0,1 V / 0,1 A |
| `_ms` / `_s` | milissegundos / segundos |
| `_As` / `_Wh` | ampere-segundo / watt-hora |
| `percent_x100` | % × 100 |

Para além do JSON só saem:

- as transições da linha AMS_ERROR: `AMS_ERROR line -> ERROR` / `AMS_ERROR line -> OK`
- as respostas da consola, todas começadas por `#` (ver [CONFIGURACAO.md](CONFIGURACAO.md))

## Blocos

| Bloco | Campos |
|---|---|
| `uptime_ms` | |
| `bms` | `state` (STARTUP, IDLE, BALANCING, ONMISSION, CHARGING, FAULT), `prev_state`, `runtime_s`, `wwdg_reset`, `active_faults`, `hv_safe`, `loop_us`, `loop_max_us`, `fw` |
| `ams_error` | `state` (OK/ERROR), `active`, `permanent` (sempre 0: o latch é em hardware), `boot_hold`, `fault_count`, `fault_mask_lo/hi` (bit n = fault n), `ams_mask_lo` (os que seguram a linha), `faults` e `sources` (nomes) |
| `precharge` | `state` (nomes do DBC 0x702), `source` (0 nenhum, 1 VCU, 2 handcart), `vcu_request` (-1 = nunca recebido), `vcu_request_age_ms`, `hv_on_mismatch_count`, `sdc_closed`, `air_pos`, `air_neg`, `precharge_relay`, `discharge` (feedbacks, 1 = fechado), `cmd_air_pos/neg`, `cmd_precharge` (pedidos), `expected`/`actual` (último check: bit0 AIR-, bit1 PRE, bit2 AIR+, bit3 descarga), `bus_mV`, `state_ms`, `hv_on_count` |
| `isa_pack`, `isa_handcart` | `current_mA` (+ = a entrar no pack), `voltage_mV` (o canal do bus), `u1_mV`, `u2_mV`, `u3_mV`, `power_W`, `charge_As`, `energy_Wh`, `temp_dC`, `rx_age_ms` (da trama de corrente), `errors`, `alive` |
| `soc` | `percent_x100`, `used_charge_As`, `ready`, `initial_percent_x100`, `ocv_mV`, `anchors` |
| `charger` | `state` (WAIT_HV, PRESTART_STOP, CHARGING, STOPPING, DONE), `end` (porque acabou), `handcart_switch`, `handcart_age_ms`, `status_ok`, `output_voltage_dV`, `output_current_dA`, flags do carregador, `temp_C`, `requested_current_raw` (dA), `requested_voltage_dV`, `request_enable` |
| `pack` | `slaves_found`, `adbms_state`, `any_pec_error`, `cell_max/min/avg_mV`, `cell_delta_mV`, `temp_max/min_cC`, `voltage_sum_mV`, `cells_valid`, `temps_valid`, `cycles`, `cycle_us`, `overruns`, `pec_total`, `spi_errors`, `config_errors` |
| `balancing` | `active`, `stage` (OFF, ROUGH, FINE, END), `target_mV`, `cells_discharging`, `worst_delta_mV`, `worst_slave`, `worst_cell`, `die_hold`, `mask` (por slave, bit i = célula i+1) |
| `board` | `fan_pwm` e `fan_lv_pwm` (0..255), `mcu_temp_dC`, `vdda_mV`, `master_current_mA` |
| `can1`, `can2` | `started`, `kbps`, `bus_off`, `error_passive`, `tx_errors`, `rx_errors`, `last_error`, `tx_queue`, `tx_frames`, `rx_frames`, `tx_dropped`, `rx_dropped` |
| `eeprom` | `present`, `loaded` (config da EEPROM ou defaults), `bank` (0 = A, 1 = B, 255 = nenhum), `sequence`, `state`, `dirty`, `reboot_pending`, `writes`, `failures` |
| `boot` | o bootloader CAN ([BOOTLOADER.md](BOOTLOADER.md)): `bootloader` (arrancou por ele), `bl_version` (major << 16 \| minor << 8 \| patch), `run` (CONFIRMED, PENDING = em teste, EXTERNAL = gravada pelo ST-LINK, UNKNOWN), `event` (o que o bootloader fez neste arranque: TRIAL, ROLLBACK, ...), `attempts`, `failures`, `confirm_in_s`, `iwdg`, `backup`, `backup_version`, `updates`, `rollbacks`, `last_reason` |
| `bt` | `state` (BOOT, ENTER_CMD, GET_NAME, SET_NAME, EXIT_CMD, DATA, PROBE_ENTER, PROBE_EXIT, REBOOT), `connected`, `alive`, `probes`, `probe_failures` |
| `json` | `sent` (avança um por linha: a UI usa como sequência), `dropped`, `last_len`, `dma_errors`, `dma_restarts`, `bt_reboots` — contadores das linhas **anteriores** desta ligação |

Por slave (`slaves` da config):

| Campo | Formato |
|---|---|
| `cell_mV` | `[slave][célula]`; um slave sem leitura válida fica com o último valor bom (ver `cells_age_ms`) |
| `ntc_dC` | `[slave][ntc]` |
| `die_dC` | temperatura interna de cada ADBMS6830 |
| `pec_flags` | bit0 células, bit1 S-ADC, bit2 aux, bit3 aux redundante, bit4 status, bit5 config (leituras do último ciclo) |
| `ow_cell_mask` | bit i = fio de sense da célula i+1 aberto |
| `ow_ntc_mask` | bit i = NTC i+1 aberto, em curto ou AUX/AUX2 a discordar |
| `cells_age_ms` | idade da última leitura válida das células (acima de `adbms_data_timeout_ms` os números já não valem) |

## RN4871

Só TX/RX estão ligados (sem RST_N nem RTS/CTS). O `rn4871.c`:

1. no arranque entra em modo de comando (`$$$`), lê o nome (`GN`) e, se for
   diferente de `bt_name`, muda-o (`SN,...`) e reinicia o módulo;
2. em modo de dados separa os status do módulo (`%CONNECT...%`,
   `%DISCONNECT%`, `%REBOOT%`) dos comandos da aplicação, que vão para a
   consola;
3. a cada `bt_probe_s` (30 s) confirma que o módulo responde (`$$$` → `CMD>`
   → `---`), com o JSON em pausa ~200 ms; sem resposta reinicia-o às cegas
   (`$$$` + `R,1`), como o firmware antigo fazia de 30 em 30 s
   (`bt_reboot_s` mantém essa opção, desligada por omissão).

Fora do modo de dados nada do BMS vai para o UART2, e nunca sai um `$` (um
`$$$` no meio do texto metia o módulo em modo de comando). O RN4871 não tem
controlo de fluxo: acima de ~3 kB/s de BLE perde bytes, por isso o JSON fica a
1 Hz por omissão.
