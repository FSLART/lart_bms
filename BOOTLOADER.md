# Bootloader CAN

O BMS grava-se pelo CAN, sem ST-LINK, e guarda sempre o firmware anterior: se o
novo não arrancar, não se confirmar ou for pedido, o bootloader volta a ele
sozinho. O protocolo é o do bootloader da ACU (`ACU_V3.0`, branch
`dev_serrada`, `mcu_boot/docs/CAN_PROTOCOL.md`): as mesmas tramas e o mesmo
[DBC](Bootloader/bootloader.dbc), com o que o BMS tem a mais anotado.

| Pasta | O quê |
|---|---|
| `Bootloader/` | o bootloader (32 KB, sem HAL): `Src/`, `Makefile`, linker, DBC |
| `Bootloader/tools/can_upload.py` | o uploader (empacota o `.bin` e grava) |
| `Firmware/Core/boot/` | o que o bootloader e a aplicação partilham: mapa da flash, journal, formato da imagem, SHA-256 |
| `Firmware/Core/app/boot_ctrl.c` | o lado da aplicação: confirmação, pedidos, consola e JSON |

## Mapa da flash

| Endereço | Setores | Tamanho | O quê |
|---|---|---|---|
| `0x08000000` | S0, S1 | 32 KB | bootloader |
| `0x08008000` | S2, S3 | 2 × 16 KB | journal: o estado do update |
| `0x08010000` | S4, S5 | 192 KB | **EXEC**: a imagem que corre. Cabeçalho MCUboot (512 B) + aplicação a partir de `0x08010200` |
| `0x08040000` | S6, S7 | 256 KB | **BACKUP**: cópia do último firmware bom |

O STM32F412RE tem um só banco de flash e não faz remap (o L476 da ACU faz): a
aplicação corre sempre do EXEC e o "dual bank" é feito por cópia. Antes de
apagar o EXEC para um update, o bootloader guarda lá o firmware bom; para
voltar atrás copia o BACKUP de volta para o EXEC.

Os últimos 32 B da RAM (`0x2003FFE0`) ficam fora dos dois linkers e passam
informação entre a aplicação e o bootloader: o pedido de ficar no bootloader
(`0xB00710AD` em `0x2003FFFC`, a mesma palavra da ACU), os bitrates CAN da
config, e no sentido contrário o `RCC->CSR` do reset e o estado da imagem.

## Um update, passo a passo

1. O uploader manda `0x0DC` (ou `0x601`). A aplicação, **só com a HV desligada
   e fora de um carregamento**, escreve o pedido na RAM e reinicia.
2. O bootloader arranca com os contactores abertos e o AMS_ERROR em erro (e
   assim fica até a aplicação o largar), vê o pedido e fica à espera.
3. **BEGIN**: se o firmware que está no EXEC é bom (confirmado, reposto do
   backup, ou gravado pelo ST-LINK) e ainda não está no BACKUP, copia-o para
   lá e verifica-o (SHA-256). Só depois apaga o EXEC. Alguns segundos.
4. **DATA**: 4 bytes por trama, cada uma confirmada (stop-and-wait).
5. **END**: CRC-32 da transferência, e na flash cabeçalho, TLV, SHA-256,
   vetores e o descritor da placa (`BMS1`: não instala o firmware da ACU por
   engano). A imagem fica **em teste**.
6. **RESET**: arranca a imagem nova com o IWDG armado (~4 s; a aplicação
   refresca-o no `brain_loop`).
7. A aplicação confirma-se ao fim de `boot_confirm_s` (30 s) a correr. A partir
   daí é o firmware bom, e é o que vai para o BACKUP no próximo update.

### Quando volta sozinho ao firmware anterior

| Situação | Razão (0x587 / consola) |
|---|---|
| a imagem em teste reinicia pelo watchdog (WWDG ou IWDG) 3 vezes | `CRASHED` |
| 10 arranques sem se confirmar (cortes de energia incluídos) | `UNCONFIRMED` |
| upload ou reposição interrompidos (reset, falta de energia, o uploader parou) | `INCOMPLETE` |
| a imagem do EXEC estragou-se (verificada a cada arranque) | `HASH`, `HEADER`, `TLV`, `VECTORS` |
| `boot rollback` na consola | `MANUAL` |

Desligar e ligar o carro com a imagem em teste não é uma falha: só conta para
os 10 arranques. Se não houver backup (o EXEC nunca teve firmware bom), a
imagem fica (evento `ACCEPTED`) e passa a ser a boa; sem nada que arranque, o
bootloader fica à espera de um upload (LED a piscar rápido).

**Falta de energia em qualquer ponto** de um update deixa sempre um firmware
que arranca: o journal só marca o BACKUP como válido depois de o verificar, e o
EXEC só é apagado depois disso. O `test_boot` corta a energia em cada uma das
~1600 escritas de um update completo (e mais umas durante a recuperação) e
confirma isso.

## Instalar o bootloader (uma vez por placa)

1. Compilar: `make` em `Bootloader/` (dá `build/bms_bootloader.hex`). No
   Windows, com o `make` e o `arm-none-eabi-gcc` que vêm com o STM32CubeIDE, ou
   no próprio CubeIDE: *File → New → Makefile Project with Existing Code*,
   pasta `Bootloader`, toolchain *MCU ARM GCC*, e *Build*.
2. Gravar o `.hex` com o STM32CubeProgrammer (ST-LINK, SWD), ou `make flash`.
   Na primeira vez fazer antes um *Full chip erase*: os setores do journal têm
   restos do firmware antigo (o bootloader aguenta, mas fica mais limpo).
3. Gravar a aplicação com o CubeIDE, como sempre. O bootloader arranca-a.

Depois disto **nunca fazer *Full chip erase*** a partir do projeto da
aplicação: apagava o bootloader. O CubeIDE por omissão só apaga os setores que
grava (S4, S5), e deixa o bootloader e o journal.

A aplicação está ligada a `0x08010200` (`Firmware/STM32F412RETX_FLASH.ld`): sem
o bootloader na placa não arranca. Um `.bin` antigo (ligado a `0x08000000`)
também não serve para o uploader.

## Desenvolver com o CubeIDE

Nada muda: *Run* / *Debug* gravam a aplicação pelo ST-LINK e o bootloader
arranca-a (cabeçalho apagado = imagem "RAW", gravada à mão). Uma imagem RAW é
`EXTERNAL`: o bootloader não lhe conta arranques, não a põe em teste e nunca a
troca pelo backup. Se for a que estiver no EXEC no próximo update por CAN, é
ela que vai para o BACKUP (com um cabeçalho gerado e o SHA-256).

O `main.c` mete o `SCB->VTOR` na tabela de vetores da aplicação, por isso
também funciona se o debugger a arrancar diretamente.

## Gravar pelo CAN

```bash
pip install -r Bootloader/tools/requirements.txt
```

```bash
python Bootloader/tools/can_upload.py Firmware/Debug/lart_bms.bin --interface pcan --channel PCAN_USBBUS1
```

| Opção | |
|---|---|
| `--interface`, `--channel` | o adaptador do python-can: `pcan`/`PCAN_USBBUS1`, `slcan`/`COM5`, `socketcan`/`can0`, ... |
| `--bitrate` | 500000 por omissão (o `can1_kbps`/`can2_kbps` da config) |
| `--entry programmer` | **por omissão**: `0x0DC` START_PROGRAMMER, no CAN1 ou no CAN2 |
| `--entry enter-boot` | `0x601` ENTER_BOOT da ACU, só no CAN2 |
| `--entry none` | já está no bootloader (consola `bootloader`, ou depois de um reset) |
| `--status` | entra no bootloader, mostra o diagnóstico e fica |
| `--reset` | sai do bootloader (arranca a aplicação) |
| `--no-reset` | não reinicia no fim |
| `--version 13.1.0+7`, `--build` | versão no cabeçalho (por omissão a do descritor e a data do ficheiro) |
| `--package-only saida.bin` | só empacota |

O `.bin` sai do CubeIDE (*Convert to binary file* já está ligado no Debug).
O uploader aceita também o `.hex` ou uma imagem já empacotada. Antes de tocar
no BMS verifica os vetores (ligada a `0x08010200`, pilha abaixo de
`0x2003FFE0`) e o descritor da placa.

O protocolo é stop-and-wait (cada 4 bytes esperam pela resposta): ~1 a 3
minutos para os ~165 KB do Debug, conforme o adaptador. A imagem cabe até 192
KB com cabeçalho; se o Debug (-O0) passar disso, compilar com -Og ou -Os.

## Entrar no bootloader

| Como | Onde | Condição |
|---|---|---|
| `0x0DC` START_PROGRAMMER, `programming_request = 1` | CAN1 e CAN2 (`bootloader_can_mask`, `0x3`) | HV desligada, sem carregamento |
| `0x601` ENTER_BOOT, `01 00 00 00 00 00 00 00` | só CAN2 (no CAN1 o `0x601` é o `SLAVE_01_VOLTAGE_ID_2` do próprio BMS) | idem |
| `bootloader` na consola (cabo ou BLE) | | idem |
| uploader a pedir INFO durante os 500 ms depois de um reset do watchdog ou pelo pino (NRST) | | uma aplicação que rebenta logo também se grava |
| nada para arrancar | | fica sempre |

No bootloader fica-se até ao RESET do uploader, 30 s sem comandos a meio de uma
sessão (aí reinicia, e um upload incompleto volta ao backup), ou 5 min parado
depois de um pedido sem sessão.

No CAN1 o bootloader responde no `0x580`..`0x589` e ouve o `0x602`, que no
powertrain é o `SLAVE_01_VOLTAGE_ID_3`: enquanto o BMS está no bootloader esse
ID não é usado por mais ninguém, mas um logger vai mostrar lixo nesse slave.

## No BMS

Consola (cabo ou BLE; as que mudam alguma coisa pedem o PIN no BLE):

| Comando | |
|---|---|
| `boot` | `#BOOT bootloader=1.0.0 run=PENDING event=TRIAL attempts=1 failures=0 confirm_in_s=24 fw=13.1.0 backup=13.0.0 updates=3 rollbacks=0 last=NONE` |
| `boot confirm` | confirma já a imagem em teste |
| `boot rollback` | volta ao firmware do backup (marca a imagem e reinicia; só com a HV desligada) |
| `bootloader` | entra no bootloader CAN |

`run`: `CONFIRMED` (instalada por CAN e confirmada, ou reposta), `PENDING` (em
teste), `EXTERNAL` (gravada pelo ST-LINK), `UNKNOWN` (arrancou sem o
bootloader). O JSON tem o bloco `boot` com os mesmos campos
([LIVE_DEBUG_JSON.md](LIVE_DEBUG_JSON.md)). O parâmetro `boot_confirm_s`
(5..600 s) está na EEPROM ([CONFIGURACAO.md](CONFIGURACAO.md)).

## Diagnóstico

- **CAN**: heartbeat `0x581` a cada 500 ms (`0x55`, tick, versão do
  bootloader) e `0x582`..`0x589` (os da ACU: estado, versão, decisão do
  arranque, progresso, erros, rollback). O `can_upload.py --status` mostra-os
  descodificados. No BMS os "bancos" são 1 = EXEC e 2 = BACKUP, e
  `BoardType = 2`.
- **UART1** (o cabo da consola, 230400): uma linha `[boot] ...` por decisão
  (journal, imagens, decisão, rollback, upload).
- **LED** (PB10): a piscar a 2 Hz à espera de um upload, rápido sem nada para
  arrancar.

## Diferenças para o bootloader da ACU

| | ACU (L476) | BMS (F412) |
|---|---|---|
| A/B | dois bancos com remap | EXEC + cópia no BACKUP |
| Volta atrás | só se a imagem nova estiver estragada | também se rebentar, não se confirmar, ou a pedido |
| Bus | um, 250 kbit/s | CAN1 e CAN2, os bitrates da config (500) |
| Entrada | `0x601` | `0x0DC` nos dois, `0x601` só no CAN2, janela depois de um watchdog |
| Imagem | MCUboot | MCUboot + descritor `BMS1` na aplicação (`0x08010400`) |
| Código | MCUboot + HAL, 64 KB | sem HAL nem MCUboot, ~14 KB |

O uploader da ACU não serve tal como está para o BMS (verifica a RAM do L476 e
usa 250 kbit/s); o do BMS fala o mesmo protocolo.

## Testes

`Firmware/Tests/run_tests.sh` corre também:

- `test_boot`: a lógica do bootloader (`Bootloader/Src/bl_core.c`,
  `bl_protocol.c`, `bl_log.c`) numa flash simulada: protocolo, journal,
  imagens, rollback por watchdog, por arranques sem confirmar, por upload
  interrompido, manual, flash estragada, corte de energia em cada escrita de
  um update, e erros da flash (uma operação ou três seguidas) em cada operação
  de um update e de um rollback. Depois de cada falha o arranque seguinte tem
  de arrancar o firmware antigo ou o novo, nunca ficar sem nada
- `test_boot_hw`: o bootloader inteiro (`startup.c`, `main.c`, `bl_hw.c`,
  `bl_can.c`, `bl_flash.c` e a lógica) num STM32F412 simulado
  (`Firmware/Tests/stm32_model.c`). Os registos ficam nos endereços
  verdadeiros e cada escrita é apanhada (page fault + um passo do CPU), sem
  mudar o código do bootloader. Cada arranque corre num processo novo, com a
  flash e a RAM partilhadas entre arranques como no chip. Verifica a ordem das
  saídas seguras, o relógio (HSE, PLL, latência da flash), o TIM2 a contar ms,
  o bit timing e os filtros do bxCAN, a FIFO a transbordar, o CAN sem ACK ou
  sem bus, o driver da flash com erros injetados, o IWDG, o salto para a
  aplicação (VTOR, MSP, relógio e periféricos repostos), e um upload completo
  trama a trama. O modelo regista o que o RM0402 não deixa fazer (escrever num
  periférico sem relógio, filtros fora do FINIT, BTR fora do modo de init,
  programar sem apagar, latência curta para o HCLK, ...) e o teste exige zero.
  Só corre em x86-64 Linux; nos outros diz que salta
- `test_can_upload.py`: o uploader (imagens, HEX, cabeçalho, TLV, SHA-256,
  repetições, timeouts, recusas, diagnósticos, `--status`, `--reset`) e uploads
  contra o bootloader real num bus CAN virtual (precisa do python-can e do
  cantools)
- no `test_system`: o lado da aplicação (confirmação, `boot`, rollback, 0x0DC e
  0x601, flags do reset)
