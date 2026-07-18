# lart_bms — Fluxogramas (PlantUML)

Fluxos das partes críticas do firmware, em linguagem humana — os nomes de código ficam só nas notas de rodapé de cada secção. Copia cada bloco `@startuml…@enduml` para o editor PlantUML.

Cores: verde = sucesso/OK · amarelo = decisão/espera · vermelho = falha.

---

## 01 — Arranque & Watchdog

O watchdog é um "cão de guarda" por hardware: se o programa principal travar e deixar de lhe fazer refresh, o micro é reiniciado à força. A causa do reset é guardada logo no arranque, antes de ativar o watchdog, para se saber se o boot veio de um reset normal ou de um travamento.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam ActivityBackgroundColor #1b2823
skinparam ActivityFontColor #ffffff
skinparam ActivityBorderColor #3f5a50
skinparam ActivityDiamondBackgroundColor #332a16
skinparam ActivityDiamondFontColor #ffffff

start
:Guardar causa do reset\n(reset normal ou watchdog?);
if (Debugger ligado?) then (sim)
  :Congelar watchdog\n(para poder usar breakpoints);
else (nao)
endif
:Inicializar perifericos\n(GPIO, CAN, SPI, ADC, timers);
:Ativar watchdog\n(janela ~40 ms);
if (Boot causado por watchdog?) then (sim)
  #f3d3d0:Registar falha\n"reset por travamento";
else (boot normal)
endif
#d4ecdd:Refresh ao watchdog;
:Arranque do cerebro\n(contactores abertos, CAN, EEPROM, faults);
repeat
  #d4ecdd:Refresh ao watchdog;
  :Uma volta do programa principal;
  #d4ecdd:Refresh ao watchdog;
repeat while (Programa responde?) is (sim) not (travou)
#f3d3d0:Watchdog dispara\n-> reinicio forcado do micro;
stop
@enduml
```

> Código: `main.c` — WWDG presc 8 / window 127 / counter 127, refresh antes e depois de `brain_loop()`, causa do reset em `RCC->CSR` → `watchdog_flag`.

---

## 02 — Relógio interno de tarefas (1 ms)

Uma interrupção de 1 ms serve de metrónomo: não faz trabalho pesado, só levanta bandeiras. O programa principal vê as bandeiras e executa as tarefas na volta seguinte.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam ActivityBackgroundColor #1b2823
skinparam ActivityFontColor #ffffff
skinparam ActivityBorderColor #3f5a50
skinparam ActivityDiamondBackgroundColor #332a16
skinparam ActivityDiamondFontColor #ffffff

start
:Tick de 1 ms;
if (Passaram 250 ms?) then (sim)
  #d4ecdd:Pedir verificacao de seguranca\n+ envio de dados por CAN;
else (nao)
endif
if (Passaram 800 ms?) then (sim)
  #d4ecdd:Pedir atualizacao da consola\n(lista de falhas por UART);
else (nao)
endif
if (Passou 1 s?) then (sim)
  #d4ecdd:Somar 1 s ao tempo de vida;
else (nao)
endif
:Piscar LED de batimento\n(cadencia acelerada);
stop
@enduml
```

> Código: `brain.c · HAL_SYSTICK_Callback` — flags `faultCheck` (250 ms), `updateUI` (800 ms), `runtime_sec` (1 s), `heartbeat()`.

---

## 03 — Estados principais do AMS

O AMS vive num de poucos modos. Arranca em "primeira leitura", passa a repouso, e só sai de repouso por ordem via CAN (balancear ou carregar). Qualquer estado inválido cai em falha.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam StateBackgroundColor #1b2823
skinparam StateFontColor #ffffff
skinparam StateBorderColor #3f5a50

state "Arranque\n(primeira leitura completa\nda cadeia de slaves)" as ARR
state "Repouso\n(mede tudo a cada 50 ms)" as REP #1c3324
state "Balanceamento\n(descarrega celulas altas)" as BAL
state "Carregamento\n(dialogo com o carregador)" as CAR
state "Reconfigurar sensor\nde corrente" as ISA
state "Falha" as FAL #f3d3d0

[*] --> ARR
ARR --> REP : leitura completa\n+ SOC inicial calculado
ARR --> FAL : estado invalido
REP --> BAL : ordem CAN "balancear"
BAL --> REP : balanceamento terminou\n(convergiu ou ordem de paragem)
REP --> CAR : ordem CAN do carregador
CAR --> REP
REP --> ISA : pedido de reconfiguracao
ISA --> REP : volta ao estado anterior
REP --> FAL : estado invalido
FAL --> [*]

note right of REP
  A cada 250 ms (em repouso e carga):
  verificacao de seguranca das celulas
  + envio de tudo por CAN
end note
@enduml
```

> Código: `brain.c · brain_loop` — `AMSStates_t`: STARTUP, IDLE, BALANCING, CHARGING, RESET_ISA, FAULT.

---

## 04 — Ciclo de medição em repouso

As leituras aos slaves são feitas por fases curtas, sem nunca bloquear o programa: dispara-se a conversão, sai-se, e volta-se mais tarde para ler o resultado. No fim testa-se fio partido (open wire) e tira-se uma "fotografia" dos dados para o resto do firmware usar.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam StateBackgroundColor #1b2823
skinparam StateFontColor #ffffff
skinparam StateBorderColor #3f5a50

state "Ler tensoes instantaneas\ne lancar nova conversao" as F1
state "Ler tensoes medias\ne lancar leitura dos NTC" as F2
state "Ler NTC\ne lancar canais auxiliares" as F3
state "Ler auxiliares\n+ estado interno dos chips" as F4
state "Teste fio partido\n(celulas pares)" as F5
state "Teste fio partido\n(celulas impares)" as F6
state "Avaliar resultados\ndo teste de fio partido" as F7
state "Fotografia dos dados\n(copia para uso geral)" as SNAP #1c3324

[*] --> F1
F1 --> F2 : espera 10 ms
F2 --> F3 : espera 10 ms
F3 --> F4
F4 --> F5
F5 --> F6
F6 --> F7
F7 --> SNAP
SNAP --> F1
@enduml
```

> Código: `adBms_Application.c · adbms_main` — fases `ADBMS_IDLE_*`, RDCV/RDAC/RDAUX/RDRAX/RDSTAT, open-wire par/ímpar, `memcpy(SLAVE, IC, …)`.

---

## 05 — Ciclo de balanceamento

Descarrega → pára → deixa assentar → mede → recalcula. O pulso de descarga tem de ficar **abaixo de 1.8 s**: os chips têm um watchdog interno que, sem comandos, apaga os interruptores de descarga e adormece. Há ainda uma guarda térmica — se o chip aquecer demasiado 2 ciclos seguidos, o balanceamento termina com falha registada.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam StateBackgroundColor #1b2823
skinparam StateFontColor #ffffff
skinparam StateBorderColor #3f5a50

state "Avaliar situacao\n(ainda ha celulas altas?\nchips a boa temperatura?)" as INIT
state "Escolher celulas a descarregar\n(acima do minimo congelado)" as COMP
state "Ligar interruptores\nde descarga" as APPLY
state "Descarga ativa\n1.5 s (abaixo do limite\nde 1.8 s dos chips)" as ON
state "Desligar descarga" as STOP
state "Deixar tensoes assentar\n(100 ms)" as SET
state "Lancar nova medicao" as SAVG
state "Esperar conversao\n(20 ms)" as WAVG
state "Ler tensoes medias\n+ fotografia dos dados" as RAVG
state "Terminar\n(descarga a zero,\nvolta a repouso)" as END #1c3324

[*] --> INIT
INIT --> END : celulas equilibradas\nou chip quente 2x\nou ordem de paragem CAN
INIT --> COMP : ainda ha trabalho
COMP --> APPLY
APPLY --> ON
ON --> STOP
STOP --> SET
SET --> SAVG
SAVG --> WAVG
WAVG --> RAVG
RAVG --> INIT
END --> [*]
@enduml
```

> Código: `adBms_Application.c · BAL_CYCLE_*` — `BALANCE_ON_TIME_MS = 1500` < tSLEEP 1.8 s, `BALANCE_SETTLE_MS = 100`, guarda die-temp 85 °C ×2, alvo = mínimo congelado no arranque da sessão.

---

## 06 — Sequência de precarga

Fecha os contactores por ordem, confirmando o retorno (feedback) de cada um antes de avançar. O condensador do bus é carregado através da resistência de precarga antes de fechar o contactor positivo — senão a corrente de choque soldava-o. Qualquer confirmação errada aborta e abre tudo.

```plantuml
@startuml
skinparam defaultFontName Monospaced
skinparam StateBackgroundColor #1b2823
skinparam StateFontColor #ffffff
skinparam StateBorderColor #3f5a50

state "Abrir todos\nos contactores" as ST
state "Confirmar tudo aberto" as OP
state "Fechar contactor negativo\n+ confirmar feedback" as NEG
state "Fechar precarga\n(carrega condensador ~3 s,\nfeedback vigiado durante a carga)" as PRE
state "Verificar corrente\n(por implementar)" as VC #332a16
state "Verificar tensao do bus\n(por implementar)" as VB #332a16
state "Fechar contactor positivo\n+ confirmar feedback" as POS
state "Abrir precarga\n+ confirmar aberta" as OFFP
state "Alta tensao ligada\n(vigilancia continua\ndos feedbacks)" as HV #1c3324
state "Falha" as FAL #f3d3d0
state "Corte\n(abre todos os contactores)" as KILL #f3d3d0

[*] --> ST : ordem CAN "iniciar precarga"\n(so aceite em corte, lockout 10 s)
ST --> OP
OP --> NEG
NEG --> PRE
PRE --> VC
VC --> VB
VB --> POS
POS --> OFFP
OFFP --> HV

NEG --> FAL : feedback errado
PRE --> FAL : feedback errado
VC --> FAL : corrente fora do limite
VB --> FAL : bus nao subiu
POS --> FAL : feedback errado
OFFP --> FAL : feedback errado
HV --> KILL : feedback errado\nou ordem CAN de paragem
FAL --> KILL
KILL --> ST : nova ordem CAN\n(apos lockout)
@enduml
```

> Código: `precharge.c · Precharge_Update` — `PrechargeState_t`; "Falha" = `WRONG`, "Corte" = `KILL`; verificações de corrente/bus são stubs (`IsCurrentOK`/`IsBusVoltageOK` devolvem `true`) — pontos de entrada da futura camada de proteção.
