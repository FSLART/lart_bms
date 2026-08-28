# Firmware v3 — Architecture Plan (ground-up rewrite)

> **Plan only. No implementation.** Purpose: settle the structure before the first line
> of code is written.
>
> Target: STM32F412RET6, 12× ADBMS6830 in isoSPI daisy chains via ADBMS6822,
> 3p144s Molicel P45B pack. Formula Student (TEK-26E), automotive context — the decision
> criterion is **resilience and auditability**, not elegance.

---

## 0. The three v2 problems the rewrite must fix by design

1. **No single source of truth.** Values live scattered: `SLAVE[]` (raw ADI structs),
   `ivt` (ISA), loose globals (`g_pack_voltage_sum_mV`), precharge state inside its own
   module. Answering "what was the system state at this instant?" means visiting six
   places.

2. **Logic is welded to hardware.** `brain.c`, `charger.c` and `adbms_to_CAN.c` call
   `HAL_GPIO_ReadPin` and talk SPI directly. None of it is testable off-board.

3. **Conversion and policy are mixed.** The same `for` loop that walks cells converts
   raw→mV, decides open-wire, raises the fault and prints.

All three have the same fix: **an abstraction layer between driver and logic**, plus a
**pure core** testable without hardware or an RTOS.

---

## 1. Repository layout and layering

```
lart_bms/
├── .github/            CI: build, host tests, DBC drift check, artifact upload
├── build/              build output — gitignored
├── config/             build-level config: board variants, toolchain
├── docker/             pinned toolchain image for reproducible builds
├── docs/               this plan, flowcharts, datasheet notes
├── firmware/
│   ├── app/
│   │   └── core/       ← PURE. No HAL, no RTOS, no time source. All the logic.
│   ├── runtime/        ← tasks, queues, mutexes, semaphores, ISR callbacks
│   ├── bsp/            ← pins, clocks, peripheral init, board variants
│   ├── config/         ← compile-time defaults and clamp limits
│   ├── drivers/        ← device drivers: adbms, isa, eeprom, contactors, fans
│   ├── hal/            ← ST HAL + CMSIS
│   ├── ld/             ← linker scripts
│   └── vendor/         ← ADI ADBMS6830 lib, FreeRTOS, generated DBC
├── sim/                host-side simulation and CAN replay
├── tests/              host unit tests + golden vector fixtures
└── tools/              sync_dbc.sh, flashing, log parsing
```

### 1.1 The `core` / `runtime` split

An earlier draft was self-contradictory: it demanded `app/` compile for x86 while putting
mutexes and FreeRTOS tasks inside it. Corrected:

| | `app/core/` | `runtime/` |
|---|---|---|
| Contains | state types, controllers, fault evaluation, SOC, balancing policy, protocol encode/decode, all conversions | tasks, queues, mutexes, semaphores, ISR callbacks, the state instance |
| May include | `config/`, `vendor/dbc` (pure codec) | everything |
| May **not** include | HAL, FreeRTOS, `bsp/`, any time source | — |
| Tested | host, `tests/` | on target |

**Acceptance test: `firmware/app/core/` compiles and its tests run on x86 with neither
HAL nor FreeRTOS present.**

### 1.2 Dependency rule

```
runtime → app/core
runtime → drivers → bsp → hal
drivers → vendor
app/core → config, vendor/dbc          (pure codec only)
```

- `app/core/` never includes HAL, FreeRTOS, `bsp/`, or any hardware header.
- `drivers/` decides no policy. It converts, validates, tags quality, returns.
- `drivers/` does not know a BMS exists. It knows an ADBMS6830 exists.
- `bsp/` is the only place that knows which pin is which. A board revision is a `bsp/`
  change and nothing else.

### 1.3 The call shape that makes it work

`app/core/` never reaches for anything — everything arrives as a parameter, **including
time**. A runtime task is the only thing that touches the state instance:

```c
/* in runtime/ */
BmsInputs inputs;
BmsState_GetLatestData(&inputs);          /* mutexes hidden inside */

PrechargeOutputs outputs;
PrechargeController_Update(&precharge_context, &inputs, config, now_ms, &outputs);

BmsOutputs_UpdateRequested(&outputs.requested_outputs);
```

Consequences:
- `PrechargeController_Update()` calls nothing. No `HAL_GetTick()`, no
  `xTaskGetTickCount()`, no state accessor. Same inputs → same outputs, always.
- The mutex is invisible to `core`, so `core` cannot deadlock, cannot forget to unlock,
  and cannot hold a lock while computing.
- Host tests drive `now_ms` directly: a 10-second precharge timeout is tested in zero
  real seconds.

### 1.4 Inside `app/core/`

```
app/core/
├── state/          state types + the read/update API declarations       (§2)
├── config/         BmsConfig type + validation and clamping             (§2.10)
├── controllers/    BmsMode, Precharge, Charging, Balancing              (§3)
├── faults/         fault checks → fault policy engine                   (§7)
├── soc/            estimator
├── safety/         requested outputs → safe outputs                     (§5)
└── protocol/       CAN frame ⇄ state, both directions
```

`protocol/` rather than `telemetry/`: it holds **both** directions — `VcuFrame →
VcuDeviceData` and `BmsState → frame` — and "telemetry" implies outbound only.

The state **API is declared in `core`**; the state **instance, its mutexes and the API
implementation live in `runtime/`**. `core` never calls it — the runtime task reads, then
passes the result down.

### 1.5 Each driver splits in two

```
drivers/adbms/
├── adbms.c          transport: SPI/DMA via bsp, chain sequencing, timing  ← impure, thin
├── adbms_decode.c   raw bytes → mV/cC + PEC check + validity              ← PURE, testable
└── adbms.h
```

Same for `drivers/isa/` (`isa_decode.c`) and `drivers/eeprom/` (`config_decode.c`). The
pure half holds the logic and is unit-tested; the impure half is thin enough that there
is little left to get wrong.

### 1.6 Generated DBC code

`powertrain_t26.c/.h` and `handcart_t26.c/.h` go in **`firmware/vendor/dbc/`**:
generated, externally sourced (T26_DBC CI), never hand-edited, overwritten by
`sync_dbc.sh`.

**The dependency rule stated correctly:** not "app may not use vendor", but —

> `app/core/` must not include anything that touches hardware or the RTOS.

The DBC codec touches neither, so `core` may use it directly and still compile for x86.
The ADI library may not.

**But DBC symbols must not reach the controllers.** Lesson from the P997 → P1000 change:
renaming the protocol meant touching every call site. With a thin adapter the controller
says *"request 600 V, 6 A"* and only `core/protocol/` knows symbol names.

**Reproducibility:** `sync_dbc.sh` moves to `tools/` and must **pin a ref**. It defaults
to `main` today, so a build depends on whatever upstream `main` was that day — defeating
the pinned toolchain in `docker/`. Commit a tag or `dbc.lock`, and add a CI job running
`sync_dbc.sh --check` that fails on drift (also catching hand-edits of generated files).
The sync deliberately pulls only `powertrain_t26` and `handcart_t26`; `eveurope_charger`
was removed on purpose.

### 1.7 Supporting folders

- **`vendor/`** — ADI library untouched, documented patch on top (§13).
- **`ld/`** — declares the `.noinit` RAM section that survives reset (§6.4).
- **`sim/`** — host replay of recorded CAN logs through the real `core` code (§8).
- **`docker/` + `.github/`** — a pinned toolchain means the binary is reproducible and
  CI-built rather than "whatever CubeIDE had installed". For a scrutineered car, being
  able to rebuild a known binary matters.
- **`build/`** — gitignored, settling the v2 build-artifact problem.

---

## 2. The central state

### 2.1 Shape: blocks, one writer each

```c
typedef struct
{
    BatteryPackData     battery;          /* ← ADBMS publisher task     */
    AnalogSensorData    analog_sensors;   /* ← analog task              */
    DigitalInputData    digital_inputs;   /* ← digital input task       */
    IsaSensorData       isa_sensors;      /* ← CAN receive task         */
    CanDeviceData       can_devices;      /* ← CAN receive task         */
    BmsOperatingState   operating_state;  /* ← application task         */
    FaultStatus         faults;           /* ← fault manager ONLY (§7)  */
} BmsState;
```

```
BatteryPackData
├─ ic[12]
│   ├─ cell_mV[12], averaged_cell_mV[12], ntc_temperature_cC[10], die_temperature_cC
│   ├─ group_valid_mask                bit per register group        (§2.4)
│   ├─ group_last_update_ms[]          last good read per group
│   └─ group_pec_error_count[]         consecutive PEC failures
├─ total_voltage_mV, minimum_cell_mV, maximum_cell_mV, cell_spread_mV
├─ minimum_cell_index, maximum_cell_index
├─ minimum_temperature_cC, maximum_temperature_cC, hottest_ntc_index
├─ communication_ok[2], detected_ic_count[2]
└─ update_counter, last_update_ms
```

```
DigitalInputData    sdc_is_closed, air_positive_is_closed, air_negative_is_closed,
                    precharge_relay_is_closed, ignition_is_on
AnalogSensorData    master_current_mA, reference_voltage_mV,
                    board_temperature_cC, hv_bus_voltage_mV
IsaSensorData       pack (CAN1) / handcart (CAN2), each with last_*_received_ms
CanDeviceData       vcu, inverter, pdm, charger — decoded + last_received_ms
```

Every block carries its own **`update_counter` and `last_update_ms`**.

**Configuration is deliberately absent** — see §2.10.

### 2.2 There is no single coherent instant

Producers are asynchronous: ADBMS at its own cadence, analog ~20 ms, CAN on arrival,
controllers at 10 ms. **There is never a moment when the whole `BmsState` represents one
acquisition.** A single top-level counter would imply a coherence that does not exist, so
there is none.

- Each block has its **own** `update_counter` and `last_update_ms`.
- `BmsState_GetLatestData(&inputs)` reads all blocks. The result holds data of
  **different ages, and that is explicit** — every block carries its own age.
- Consumers needing one block read only that block.

### 2.3 Access API — read and update, never a shared pointer

Handing readers a `const BmsState *` was wrong: `const` prevents writing but does nothing
about concurrency, and a reader can stash the pointer into memory mutated under it.
Replaced by value semantics:

```c
void BmsState_GetBatteryData    (BatteryPackData *out);        /* reader */
void BmsState_UpdateBatteryData (const BatteryPackData *in);   /* writer */
/* ... one pair per block ... */
void BmsState_GetLatestData     (BmsInputs *out);
```

- **The mutex is 100 % hidden.** No caller takes or gives a lock; there is no lock to
  forget, and no way to hold one while computing.
- **No pointer into live state ever escapes.** Callers work on their own copy.
- `BmsState_Update*()` **replaces the whole block in one call** — the producer builds it
  privately first, so the critical section is a copy of known size (§2.6 R1). "Update"
  here always means atomic whole-block replacement, never partial mutation.

**Cost, stated honestly:** every read copies. `BatteryPackData` is ~1.5 kB, so a caller
that only needs minimum/maximum/total should not read all of it. The out-parameter makes
the copy visible in the signature, and narrow accessors exist for the common cases:

```c
void BmsState_GetBatterySummary(BatteryPackSummary *out);   /* cheap — use by default */
```

Reserve the full read for code that genuinely walks all 144 cells.

### 2.4 Data quality is part of the data

**Store timestamps; derive age and liveness.** Storing an age or an `is_alive` flag would
force the writer to keep updating a block when nothing arrived. Store only
`last_received_ms` / `group_last_update_ms[]`, then:

```
age_ms   = now_ms - last_received_ms
is_alive = age_ms < timeout_ms
```

This preserves single-writer and is robust to a producer going quiet — exactly the case
that matters.

**Granularity follows the PEC.** PEC is per register group, not per IC. Bad PEC on group D
of IC 7 corrupts cells 9–12 of that IC while groups A–C may be fine. Hence
`group_valid_mask`, `group_last_update_ms[]`, `group_pec_error_count[]` — per group, not
per IC.

#### PEC quarantine policy

**On PEC error: do not convert, do not write, do not update the group's timestamp.**

| Outcome | Value | `group_last_update_ms` |
|---|---|---|
| PEC OK | written | **updated** |
| PEC error | not written | **not updated** |

Not writing alone is only half the job: the struct then keeps the *previous* value, and
ten seconds later that cell still reads 4.100 V and looks healthy — invisible stale data
is worse than visible garbage. Leaving the timestamp untouched is what makes
**`data_too_old`** detectable. Consumers judge by **age**, never by a latched flag: one
PEC error means "no fresh data this cycle" (tolerable); ten consecutive means the datum
is too old and stops being trusted.

**Encapsulation is what makes this hold.** Garbage in `cell_asic` is only safe if nothing
else can read it. That is where v2 leaks: `adBms6830_FindMinVoltageGlobally()` reads
`acell.ac_codes` directly with no PEC guard, which is how the SOC seed can reach 0 %. In
v3 `cell_asic` is private to `drivers/adbms/` and unreachable from `core`. The policy
becomes compiler-enforced.

### 2.5 Units and naming rules

| Quantity | Type | Unit | Suffix |
|---|---|---|---|
| Cell voltage | `uint16_t` | mV | `_mV` |
| Pack voltage | `int32_t` | mV | `_mV` |
| Current | `int32_t` | mA | `_mA` |
| Temperature | `int16_t` | centi-°C | `_cC` |
| Charge | `int32_t` | As | `_As` |
| Time | `uint32_t` | ms | `_ms` |

**No `float` crosses into `state/`.** Float stays inside the `*_decode.c` files. Reason:
the `(uint16_t)`-cast-of-a-negative-float bug that seeds SOC at 0 % is only possible
because a float crosses layers unvalidated.

**Sign convention: one, declared in the struct.** `current_mA > 0 = charge entering the
pack`. The producer normalises; nobody downstream thinks about it again. This kills the
`DISCHARGE_SIGN` class of bug.

**Naming rules**, so the vocabulary survives contact with new code:

1. **Every physical quantity carries its unit suffix.** No bare `voltage`, no bare
   `timeout`.
2. **Booleans assert the true condition**: `sdc_is_closed`, `hv_is_safe`, `data_valid`,
   `fault_detected`. Never `_fb`, `_flag`, `_state` for a boolean.
   This is not cosmetic — `sdc_fb` does not say which level means what, and v2 shipped a
   polarity bug on exactly that class of name (AMS_ERROR SET vs RESET).
3. **Functions read `Module_Verb...()`**: `BmsState_GetBatteryData`,
   `PrechargeController_Update`, `Safety_GetSafeOutputs`.
4. **Out-parameters come last.**
5. **No invented abbreviations.** Domain acronyms everyone in the field already knows are
   fine and are the only ones allowed:
   `BMS CAN ADC GPIO SPI I2C DMA PEC CRC HV SDC AIR NTC SOC EEPROM ADBMS ISA PWM IC`.
   Anything else is spelled out: `config` not `cfg`, `context` not `ctx`,
   `digital_inputs` not `dio`, `command` not `cmd`, `update_counter` not `seq`.

Saving five characters is not worth making the next reader decode them.

### 2.6 Concurrency rules

The FreeRTOS design — ADBMS task takes the mutex only to publish its result, ISR hands
off via `xQueueOverwriteFromISR` to a periodic task — is the baseline. Five rules:

**R1 — the mutex covers the *update*, never the *work*.** Guaranteed structurally by
§2.3: the API only ever copies under the lock.

**R2 — one mutex per writer group, not one global.**

| Mutex | Covers | Writer |
|---|---|---|
| `battery_mutex` | `battery` | ADBMS publisher |
| `sensor_mutex` | `analog_sensors`, `digital_inputs` | analog task, digital input task |
| `can_mutex` | `isa_sensors`, `can_devices` | CAN receive task |
| `operating_state_mutex` | `operating_state` | application task |

`faults` uses none (R3). `RequestedOutputs` uses none (§2.8). `BmsConfig` uses none —
immutable after boot (§2.10).

**R3 — the safety path never blocks on a lock.** See §2.7.

**R4 — `xSemaphoreCreateMutex`, never `xSemaphoreCreateBinary`; never `portMAX_DELAY`.**
The mutex variant has **priority inheritance**; a binary semaphore does not, and using one
here is textbook priority inversion. Every take carries a timeout and an explicit failure
action: skip this cycle, count it, raise a fault if it persists.

**R5 — measure hold and wait times** per mutex, exposed in `live_debug`.

### 2.7 Immediate shutdown path — and hiding the atomics

⚠️ A `uint64_t __atomic_fetch_or()` is **not** a single-instruction read-modify-write on a
32-bit Cortex-M4. There is no 64-bit exclusive load/store, so the compiler emits a library
call that disables interrupts or takes a lock — the opposite of what a safety path wants.
**Use `uint32_t`**, which compiles to an `LDREX`/`STREX` loop and is genuinely lock-free.

**But nobody outside one file should ever see an atomic.** The shared variable lives in a
single module and is reached only through named functions:

```c
/* safety_shared.c — the ONLY place this variable exists */
static _Atomic uint32_t immediate_shutdown_reasons;

void     Safety_SetImmediateShutdown  (SafetyReason reason);
void     Safety_ClearImmediateShutdown(SafetyReason reason);
uint32_t Safety_GetImmediateShutdownReasons(void);
```

So the SDC code reads:

```c
if (sdc_is_open) {
    Safety_SetImmediateShutdown(SAFETY_REASON_SDC_OPEN);
}
```

Nobody touching the SDC needs to know anything about atomics, Cortex-M4, `LDREX`/`STREX`,
bit masks or concurrency. That knowledge stays encapsulated in the module where it
belongs.

**ISRs mark a *condition*; they do not write fault state.** If debounce, latch and reset
are centralised (§7), the fault manager owns the fault set. An ISR sets a reason, which
the safety layer reads for immediate action *and* the fault manager consumes as a check
result for bookkeeping.

**Who clears a reason.** `immediate_shutdown_reasons` represents **current physical
conditions**, not latched faults. The producer that set a reason also clears it:

```c
if (sdc_is_open) { Safety_SetImmediateShutdown  (SAFETY_REASON_SDC_OPEN); }
else             { Safety_ClearImmediateShutdown(SAFETY_REASON_SDC_OPEN); }
```

| | Lifetime | Owner |
|---|---|---|
| `immediate_shutdown_reasons` bit | exactly as long as the physical condition | the producer |
| `persistent_shutdown_faults` | debounce / latch / reset rule (§7.4) | fault manager |

So after an SDC drop and recovery, with `FAULT_SDC_OPEN = RESET_ON_POWER_CYCLE`: the
immediate reason is gone, the persistent fault remains, and the shutdown **continues**
because it follows the persistent fault. The safety layer's shutdown input is therefore
**`immediate_shutdown_reasons | persistent_shutdown_faults`** — the first for reaction
speed, the second for persistence.

### 2.8 Requested outputs are not state

`BmsState` answers *what is measured / what exists*. It must not answer *what we want to
happen*:

```c
typedef struct
{
    OutputState  air_positive, air_negative, precharge_relay;
    bool         charger_enabled;
    int32_t      charge_current_mA, charge_voltage_mV;
    uint8_t      fan_pwm_percent;
    uint16_t     balancing_mask[12];
} RequestedOutputs;
```

Controllers write `requested_outputs`; only the safety layer turns that into physical
output (§5).

#### How the safety task reads them without a mutex

The controllers produce `requested_outputs` in the application task, but the safety task
never takes a lock. **Double buffer plus atomic index — also hidden behind an API:**

```c
/* bms_outputs.c — the only place the buffers and the atomic index exist */
void BmsOutputs_UpdateRequested(const RequestedOutputs *in);   /* application task */
void BmsOutputs_GetRequested   (RequestedOutputs *out);        /* safety task      */
```

Internally: the writer fills the inactive buffer completely, then does one atomic store of
the index; the reader atomically loads the index and copies that buffer. No mutex, and no
possibility of reading a half-updated struct.

Two notes on rigour:

1. As specified the safety task has **higher priority than the application task** on a
   single core, so it cannot be preempted mid-copy — the race cannot occur today.
2. That depends on a priority assignment, which is fragile. Make it independent:
   **re-read the index after copying and retry if it changed** (~3 lines, no third buffer).
   The guarantee then holds however priorities are later rearranged.

### 2.9 RAM

| Block | Bytes |
|---|---|
| `battery` (incl. per-group validity and timestamps) | ≈ 1.5 kB |
| all other blocks | ≈ 0.5 kB |
| **`BmsState`** | **≈ 2.0 kB** |
| `BmsConfig` (separate, §2.10) | ≈ 0.2 kB |

The STM32F412RET6 has **256 kB SRAM** — under 1 %. Budget separately for **reader
copies**: each task holding a `BatteryPackData` costs another ~1.5 kB, which is why §2.3
provides `BmsState_GetBatterySummary()`. Add `-Wl,--print-memory-usage` to the build and
watch it every commit. Where RAM actually goes is the ADI `cell_asic` array.

### 2.10 `BmsConfig` lives outside `BmsState`

An earlier draft was inconsistent: config was referenced by the controllers, the mutex
table and the RAM budget, but absent from the state diagram. Resolved by keeping them
separate, because they are conceptually different:

```
BmsState   = dynamic system state — measured, changing, per-block timestamped
BmsConfig  = validated configuration — static for the life of a run
```

The runtime passes both:

```c
BalancingController_Update(&balancing_context, &inputs, config, now_ms, &outputs);
```

This also avoids repeatedly copying configuration that essentially never changes.

**Config is immutable while the system runs.** `Config_Load()` runs once at boot, before
any other task starts. Writing new configuration (§9.3) is permitted only with HV down and
**takes effect on the next reset** — never by mutating a struct other tasks are reading.

That immutability is what makes the one pointer exception safe: **`const BmsConfig *` may
be passed directly**, no copy, no mutex. It is the only shared pointer in the design, and
it is safe precisely because nothing writes it after boot. `BmsState` gets no such
exception.

---

## 3. Runtime: tasks and cadences

### 3.1 Task set

| Task | Cadence | Priority | Blocks on |
|---|---|---|---|
| `safety_task` | 5 ms | **highest** | nothing — never blocks, never takes a mutex |
| `can_receive_task` | event | high | receive queue from ISR |
| `application_task` | 10 ms | med-high | state reads only |
| `digital_input_task` | 10 ms | med | `xQueueOverwrite` from GPIO ISRs |
| `analog_task` | 20 ms | med | ADC/DMA semaphore |
| `adbms_task` | see §3.2 | med | SPI/DMA semaphore |
| `can_transmit_task` | 10 ms | med | transmit queue |
| `housekeeping_task` | 100 ms | low | EEPROM I2C/DMA, UART, live_debug |

`safety_task` is deliberately highest priority and lock-free. Everything else may be late;
this one may not.

**The fault manager has no task of its own** — it runs inside `application_task`, and a
separate task would buy nothing. Fixed order:

```
application_task @ 10 ms

    Read latest BMS data
            ↓
    Check for faults
            ↓
    Update fault status
            ↓
    Update BMS controllers
            ↓
    Update BMS operating state
            ↓
    Update requested outputs
```

```c
BmsInputs inputs;
BmsState_GetLatestData(&inputs);

FaultChecks_Run(&inputs, config, now_ms, fault_checks);
FaultManager_Update(fault_checks, now_ms);

BmsModeController_Update   (&mode_context,      &inputs, config, now_ms, &outputs);
PrechargeController_Update (&precharge_context, &inputs, config, now_ms, &outputs);
ChargingController_Update  (&charging_context,  &inputs, config, now_ms, &outputs);
BalancingController_Update (&balancing_context, &inputs, config, now_ms, &outputs);

BmsState_UpdateOperatingState(&new_operating_state);
BmsOutputs_UpdateRequested(&requested_outputs);
```

Faults are processed **before** the controllers so they act on the current fault set, not
on last cycle's. Every step is a pure call on data already read; only the first and last
two lines touch the state API.

### 3.2 ADBMS cadence — ⚠️ a flat 25 ms was wrong

A flat 25 ms period does not survive contact with the datasheet: redundant measurement
plus even/odd open-wire alone is in the tens of milliseconds, before SPI transfer time,
AUX, status, and **two chains**.

The task wakes often to advance its state machine; the **acquisition cadences are separate
and each derives from its own FTTI** (fault-tolerant time interval):

| Acquisition | Cadence driver |
|---|---|
| Cell voltages | fastest — OV/UV detection FTTI |
| Aux / NTC temperatures | slower — thermal time constants are seconds |
| Open-wire diagnostics | its own FTTI; does not need every cycle |
| Redundant (S-ADC) measurement | its own interval |

⚠️ **To confirm:** the FS rules' required measurement and reaction intervals for cell
voltage and temperature, and the ADBMS6830 datasheet conversion times. The FTTI numbers
must come from those, not from a guess.

### 3.3 Temporal coherence — SNAP / UNSNAP

Successive register-group reads do not necessarily correspond to the same measurement
instant; the datasheet recommends **SNAP / UNSNAP** to freeze result registers so a
multi-group, multi-IC read is coherent.

**Decision: when a coherent pack reading is needed, use `SNAP` → read groups A…F →
`UNSNAP`.** Without it you can end up with cells 1–3 from one instant and 10–12 from
another — which directly undermines goal #1 of this document.

### 3.4 Controllers

All in `application_task`, sequential, non-blocking, pure (§1.3). Internally they are
finite state machines; the names say what they control, not how they are implemented:

| Controller | Reads | Writes |
|---|---|---|
| `BmsModeController` | everything | `operating_state.mode` |
| `PrechargeController` | `battery`, `digital_inputs`, `analog_sensors`, `isa_sensors` | `operating_state.precharge`, requested outputs |
| `ChargingController` | `battery`, `isa_sensors.handcart`, `can_devices.charger` | `operating_state.charging`, requested outputs |
| `BalancingController` | `battery`, config | `requested_outputs.balancing_mask` |

**There is no ADBMS controller here.** An earlier draft listed one in two places at once.
Corrected: **the ADBMS acquisition state machine belongs to `adbms_task` /
`drivers/adbms`**, because it is transport, sequencing and timing — ADCV → wait → RDCV →
open-wire → publish. `application_task` must not know that `ADCV`, `RDCVA` or `ADSV`
exist; it receives published `BatteryPackData`.

Rule: **a controller never calls another controller.** They communicate through state and
requested outputs.

---

## 4. ADBMS6830 LPCM

**Recommendation: last phase, optional.** LPCM has the ADBMS measure autonomously at a
programmable interval, compare against stored OV/UV thresholds, and signal on a crossing —
with the MCU asleep.

Worth it because the accumulator sits for months with cells connected, and a cell drifting
to UV unobserved is a real risk. Not worth prioritising because **with AIRs open, LPCM
protects by *notification*, not *actuation*** — and each extra boot mode ("woke from LPCM"
vs "woke from ignition") is new bug surface.

**Cheap now:** keep LPCM registers writable in the config layer and give the ADBMS state
machine a low-power state from day one, even if never entered. Adding it later is then a
branch, not a rewrite.

⚠️ **To confirm in the datasheet:** register map, fault-signal behaviour along the chain,
and measured LPCM vs sleep current.

---

## 5. Output ownership — one writer for the outputs that matter

**This was the largest hole in an earlier draft.** It had the precharge controller
emitting contactor commands *and* the safety task performing "the contactor cut" — two
writers on the single most important output of the BMS.

Corrected. **The safety layer is the sole physical owner of the critical outputs.**

```
controllers
     ↓  requested_outputs.air_negative = OUTPUT_CLOSED      (a request, nothing more)
safety layer
     ↓  Safety_GetSafeOutputs(&requested_outputs,
     ↓                        immediate_shutdown_reasons,
     ↓                        required_safety_actions,
     ↓                        &safe_outputs);
drivers
     ↓  Contactor_Set(&safe_outputs);                       ← the ONLY call site
```

- `Contactor_Set()` and the AMS_ERROR pin have exactly **one** call site each, in the
  safety layer. Nothing else links against them.
- A shutdown wins **structurally**, not by task ordering or by who ran last. There is no
  interleaving in which a stale controller command re-closes a contactor afterwards.
- `Safety_GetSafeOutputs()` is a pure function in `core/safety/`, so every precedence case
  is host-testable.
- Same pattern for charger enable and any other safety-relevant output.

This keeps the `ams_error.c` single-owner pattern from v2 — which worked — and extends it
to the contactors, where v2 did not have it.

### 5.1 `hv_is_safe` — a physical predicate, not a mode

Several rules depend on "HV is off" (§7.4 fault reset, §9.3 config writes, §11
bootloader). **`operating_state.mode` must not be the authority**, because the logical
mode can believe HV is down while an AIR is welded shut.

```
hv_is_safe =  contactor feedbacks read OPEN
          AND hv_bus_voltage_mV < SAFE_HV_THRESHOLD_mV
          AND those measurements are fresh
```

- **Fail-safe on missing data.** If the HV measurement is too old or its source is
  unavailable, `hv_is_safe` is **false**, never "unknown, therefore proceed".
- Until the independent HV sense exists (§6.2), `hv_bus_voltage_mV` comes from the ISA or
  the cell sum — exactly the weaker case, and another reason that measurement is worth
  adding. The predicate does not change when it lands; only its input quality does.

```
fault_reset_allowed   = hv_is_safe
config_write_allowed  = hv_is_safe
bootloader_allowed    = hv_is_safe AND not charging AND contactors commanded open
```

---

## 6. Hardware-adjacent decisions

### 6.1 Split the daisy chain in two

**Adopted.** The ADBMS6822 is two independent transceivers; drive **ICs 1–6 from one and
7–12 from the other** as two chains — "as if the battery were cut in half".

Better than the reversible chain first suggested, because of which failure it attacks:

| | Reversible (1×12, both ends) | **Split (2×6)** |
|---|---|---|
| Chain length | 12 ICs | **6 ICs** |
| Attacks tail corruption | no | **yes — halves the length, the likely root cause** |
| Single break | all 12 still reachable | ICs beyond the break in that half are lost |
| Cable runs | long loop around the pack | **shorter, follows physical segments** |

The recovery given up is largely theoretical: losing 6 ICs is 72 cells, an AMS error and a
stopped car either way. Reversibility improves *recovery*; splitting reduces the
*probability* of the fault occurring. Probability is the better lever.

**Architectural consequence — phase 0, cannot be retrofitted.** The ADI library assumes
one chain. Two chains need an explicit context:

```c
typedef struct
{
    SpiHandle    *spi;
    ChipSelect    chip_select;
    uint8_t       ic_count;
    CellAsic     *ics;
    AdbmsSequenceState sequence_state;
    bool          communication_ok;
    uint8_t       pec_error_count[MAX_ICS_PER_CHAIN];
} AdbmsChain;
```

Every driver function takes `AdbmsChain *chain` first. No chain globals anywhere.
Ripples: `battery.ic[12]` is a **flat view** (chain A → 0–5, chain B → 6–11) with the
mapping in config; `communication_ok[2]`; PEC flood evaluated **per chain** (§7.2).

⚠️ **Narrowed open question.** The ADBMS6822 exposes two full signal sets —
`MOSI/MISO/SCK/CS` and `MOSI2/MISO2/SCK2/CS2` — so this is *not* a matter of selecting a
port by register. **What must be checked is how the current PCB routed them to the
STM32:** one shared SPI peripheral with two chip selects, two separate SPI peripherals, or
the second set not routed at all. That decides sequential vs concurrent chain reads, and
whether the new PCB needs a change.

### 6.2 Independent HV bus voltage measurement

Today `Charger_BusVoltagePlausible()` compares **two** sources: ISA over CAN2 and the
ADBMS cell sum. Both can fail — the ISA can freeze or be the wrong sensor; the cell sum
depends on all 12 ICs.

A **third, fully local source**: an isolated divider across the HV bus into a fourth ADC
channel (the ADC already runs DMA with 3 channels; a fourth is trivial in firmware).

- **Precharge completion without CAN.** A safety decision should not depend on a bus.
- **Welded contactor detection.** AIRs commanded open but bus still at pack voltage —
  currently invisible without the ISA.
- **2-of-3 voting** with a clear diagnosis instead of "plausibility failed".

⚠️ HV hardware item: isolation method, divider rating, creepage/clearance, rules
compliance. Needs electrical review, not just a firmware change.

### 6.3 Black box

A QSPI NOR flash logging decimated state to a ring buffer, with a frozen window around
each fault, dumped afterwards and replayed in `BMS_UI`.

**Honest caveat:** if `BMS_UI` is connected and logging CAN during every session, you
already have most of this. The gap is only running with nothing connected. Low priority
unless that gap is real.

### 6.4 Watchdog

**⚠️ The current one is effectively disabled.** `HAL_WWDG_Refresh(&hwwdg)` is called from
~18 places in `bms_eeprom_config.c` and from inside `ee24.c`. If an EEPROM read hangs, the
stuck code keeps feeding the watchdog from the inside. It cannot detect the hang it exists
to detect. "Refresh in strategic places" is exactly the pattern to remove: every extra
refresh site removes coverage.

**One refresh site, gated on an alive mask** — but that needs arithmetic.

**⚠️ First compute what the WWDG can actually do; the check period is constrained by
hardware, not chosen freely.**

```
watchdog_timeout = (4096 × prescaler × (counter − 0x3F)) / PCLK1
```

with `prescaler ∈ {1,2,4,8}` and a 7-bit counter, so at most `0x7F − 0x3F = 64` counts.
At PCLK1 = 50 MHz (F412 maximum) and prescaler 8:

```
4096 × 8 × 64 / 50e6 ≈ 41.9 ms
```

**The maximum WWDG period is roughly 42 ms**, less if PCLK1 is lower. That rules out a
100 ms check period outright, so the design is two-tier:

- **`watchdog_check_period` ≤ ~40 ms**, covering only the fast mandatory tasks:
  `safety_task` (5 ms), `application_task` (10 ms), `digital_input_task` (10 ms),
  `can_transmit_task` (10 ms), `analog_task` (20 ms).
- **Slow tasks are not members.** `housekeeping_task` (100 ms) is monitored by a
  **deadline counter checked inside the period**: the refresher verifies "housekeeping last
  ran < 300 ms ago", not "housekeeping ran this period".

**Mechanism.** Each mandatory task sets its bit in `tasks_alive_mask` on completing a
cycle. The single refresher checks all mandatory bits set → clear the mask and refresh;
otherwise **do not refresh** and let the reset happen. Fast tasks setting their bit more
than once per period is fine — the bit means "ran at least once", and per-task deadline
violations are caught separately by the budget instrumentation (§3.1).

**`.noinit` needs a magic value, version and checksum.** Otherwise random SRAM after a
power-on is interpreted as a previous alive mask. Store
`{magic, version, crc, reset_count, last_tasks_alive_mask, last_task_id}` and treat it as
invalid unless magic and CRC both match.

**⚠️ Correction to an earlier claim.** "Wire the watchdog output to AMS_ERROR" was wrong:
the STM32 **WWDG is internal and causes a reset — it has no output pin and is not itself a
safety output.** What must be guaranteed is:

1. The **electrical design** drives AMS_ERROR to the safe state during hang, reset and
   boot (pull-up / default-safe logic), so a resetting MCU cannot present "OK".
2. After a WWDG reset the firmware **keeps the fault persistent** rather than booting
   clean — which is what the `.noinit` block and the boot-time `RCC_CSR_WWDGRSTF` check
   are for. Keep that check (`brain.c:171`) and add persistence, so a paddock reset is not
   invisible. Knowing *which task* failed to check in is the whole diagnostic value.

An external watchdog with a real output remains an option, but it is a different component
from the WWDG, not a rewiring of it.

### 6.5 Removing the MCP23017

Agreed: invisible on the assembled car, costs board area, and is the sole reason for the
"no blocking I2C in ISR" rule. Replace with **2–3 GPIO LEDs** (heartbeat + fault). Frees
the I2C bus to the EEPROM alone.

---

## 7. Fault manager

### 7.1 Detection and policy are separate layers

An earlier draft claimed "a new fault is one table row". Not true on its own — a table
cannot express *how* to test contactor mismatch, precharge timeout, pack-voltage mismatch,
OV across 144 cells, or CAN bus-off. Detection logic has to live somewhere.

**Detection (in the owning module, pure):** each module emits check results.

```c
typedef struct
{
    FaultId  id;
    bool     fault_detected;   /* the module's own logic decided this        */
    bool     data_valid;       /* was the underlying datum fresh and PEC-good? */
    uint32_t reading_id;       /* which acquisition produced it       (§7.3) */
    uint16_t context;          /* which IC/cell/group/device, for diagnostics */
} FaultCheckResult;
```

**Policy (the table, `const` in flash):** what to do with a result.

| Field | Purpose |
|---|---|
| `id` | stable identifier, on CAN and in the log |
| `severity` | INFO / WARNING / CRITICAL |
| `debounce_ms` / `heal_ms` | see §7.3 |
| `fault_reset_rule` | see §7.4 |
| `required_safety_actions` | mask: AMS_ERROR, open contactors, stop charging, log only |
| `require_data_valid` | drop results built on too-old or PEC-bad data |

Usage:

```c
FaultCheckResult fault_checks[FAULT_COUNT];
FaultChecks_Run(&inputs, config, now_ms, fault_checks);
FaultManager_Update(fault_checks, now_ms);
```

Policy is uniform and auditable in one place; detection stays with the module that
understands the physics; the table can be printed at boot as documentation that cannot go
stale. `require_data_valid` centralises what are today scattered manual guards.

**The fault manager is the sole writer of `faults`.** ISRs contribute through
`Safety_SetImmediateShutdown()` (§2.7), never by writing fault state directly.

### 7.2 Loss of monitoring: two rules

| Rule | Catches | Latency | Scope |
|---|---|---|---|
| **PEC flood ≥ 1/3 of a chain** | communication collapse | 1 cycle | per chain |
| **Per-group `data_too_old`** | an IC or group dying quietly | N ms | per group |

The flood rule alone has a blind spot: across 12 ICs, 1/3 = 4, so three ICs could stay
permanently dead and never trip it — 36 unmonitored cells. The age rule is the real safety
net; flood is the fast detector. Both required.

**Threshold defined: `≥ 1/3`, evaluated per chain.** For a 6-IC chain that is **2 ICs**.
(An earlier draft said `> 1/3` while treating 2 as the threshold — inconsistent; `≥` is
the intended, stricter reading.)

### 7.3 Debounce in milliseconds, not cycles

Counting "cycles" is unsafe when the consumer runs faster than the producer: the fault
engine at 10 ms against ADBMS data at ~25 ms would count **the same measurement three
times** and satisfy a three-cycle debounce from a single sample.

Use **`debounce_ms` / `heal_ms`** as the interface, and gate increments on `reading_id`
changing — one count per genuine new acquisition. Both protections, no double counting.

### 7.4 Fault reset rule must know about HV

Self-clearing versus power-cycle is not enough. An AMS error must not simply disappear
while HV is live.

| `fault_reset_rule` | Meaning |
|---|---|
| `RESET_AUTOMATIC` | resets when the condition heals |
| `RESET_ONLY_WHEN_HV_SAFE` | may reset only once `hv_is_safe` (§5.1) |
| `RESET_ON_POWER_CYCLE` | survives until a power cycle |

Plus a **global override**: any fault whose `required_safety_actions` include `AMS_ERROR`
**can never reset while `hv_is_safe` is false**, regardless of `heal_ms`. Cases such as
SDC loss with HV live are `RESET_ON_POWER_CYCLE` outright.

The authority is the **physical predicate `hv_is_safe`, not the operating mode** — a
welded AIR must not let a safety fault reset just because the mode says HV is off.

---

## 8. Testability

Because `app/core/` touches neither HAL nor RTOS nor a time source, **it compiles for
x86**. Scenario tests build synthetic inputs, call
`*Controller_Update(context, inputs, config, now_ms, outputs)` and assert — no board,
milliseconds.

Cases that today need HV live and real risk: precharge where the bus fails to rise;
charger reporting a transient fault (validates debounce); ISA dying mid-charge; a cell
crossing OV during balancing; PEC failing on 2 of 6 ICs in one chain; the `0x8000`
sentinel; a shutdown arriving in the same cycle as a close command (§5 arbitration).

### 8.1 Pure boundaries

| Impure (thin) | Pure (all the logic) |
|---|---|
| `Eeprom_ReadRaw()` | `Config_Decode()`, `Config_Clamp()` |
| CAN receive ISR | `Isa_DecodeFrame()`, `CanDevice_DecodeFrame()` |
| SPI/DMA transfer | `Adbms_ParseGroup()`, `Pec15_Check()` |
| ADC/DMA callback | `Adc_ToCurrent_mA()`, `Adc_ToTemperature_cC()` |
| GPIO read/write | `PrechargeController_Update()`, `Safety_GetSafeOutputs()` |

`Config_ReadFromEeprom()` is the composition of an impure `Eeprom_ReadRaw()` and a pure
`Config_Decode()`. Only the pure half needs testing, and it covers CRC mismatch,
out-of-range clamping, version migration, truncated blobs, a blank `0xFF` chip, and an A/B
pair with one bank corrupt.

### 8.2 Golden vectors

A **golden vector** is a committed pair: real input bytes plus expected result.

The v2 sentinel bug needed the pack, 12 slaves and luck to reproduce. Instead: dump the raw
bytes once when it happens, commit them, and it reproduces forever on any PC in
microseconds.

```c
/* tests/fixtures/adbms_group_sentinel.h — bench capture 2026-08-06, slave 12, group D */
static const uint8_t RAW_SENTINEL[8] = { 0x00, 0x80, 0x12, 0xA3, 0x44, 0x10, 0x7B, 0xC2 };
```

```c
void test_sentinel_0x8000_is_rejected(void)
{
    CellGroupData result;
    Adbms_ParseGroup(RAW_SENTINEL, &result);
    TEST_ASSERT_FALSE(result.data_valid);
}
```

The value is in who it protects: a year later somebody "improves" the parse and CI fails
immediately instead of the bug returning silently.

Worth capturing: a normal frame; the `0x8000` sentinel; bad PEC; all-`0xFF` (dead chain);
an NTC frame at the open-wire boundary; one CAN frame per ISA/VCU/inverter/charger ID;
EEPROM blobs (valid, bad CRC, blank, out-of-range).

### 8.3 Determinism, not mock-avoidance

"Needing a mock is a symptom" is the right direction but too absolute. The actual goal is
a **deterministic core**, and the main threat is not dependencies — it is **time**.

A controller that calls `HAL_GetTick()` or `xTaskGetTickCount()` stops being host-pure
immediately, no matter how clean its other boundaries are. So **time is an input**:

```c
PrechargeController_Update(&precharge_context, &inputs, config, now_ms, &outputs);
```

Same for charging timeouts, fault debounce (§7.3), SOC rest detection, and age checks.
Timeouts become ordinary test parameters.

With inputs passed in and time passed in, there is genuinely nothing left to mock in
`core`. Where hardware remains (`Eeprom_ReadRaw`, DMA callbacks) there is so little logic
that it is tested on target or not at all.

Keep the framework boring: Unity, or plain C asserts with a Makefile.

---

## 9. Configuration

### 9.1 The contract is a schema, not the UI

`BMS_UI`'s `bms-settings.js` is a *consumer*, not the contract. The contract is an
**independent schema file (YAML/JSON)** from which both are generated:

```
config/bms_parameters.yaml
   ├──→ firmware/config/bms_parameters.h   (defaults, clamp limits, EEPROM offsets)
   └──→ BMS_UI/frontend/js/bms-settings.js
```

Otherwise the UI ranges and the firmware clamps drift apart, and one of them will be wrong
at the moment it matters. Same generation pattern as `sync_dbc.sh`.

### 9.2 UI fields vs current EEPROM

| Section | Field | Unit | Range | In EEPROM v2? |
|---|---|---|---|---|
| Cells | `v_min` | V | 2.0–4.0 | ✅ |
| Cells | `v_warn_low` / `v_warn_high` | V | — | ❌ |
| Cells | `v_max` | V | 3.0–4.5 | ✅ |
| Cells | `v_open_wire` | V | 0.5–3.0 | ✅ |
| Cells | `temp_warn` / `temp_fault` | °C | 20–90 | ❌ |
| Charging | `charge_v_max` | V | 0–650 | ❌ (hardcoded 600) |
| Charging | `charge_a_max` | A | 0–6 | ❌ (hardcoded 6) |
| Charging | `contactor_open_a` | A | 0–5 | ❌ (hardcoded 0.5) |
| Balancing | `bal_v_min` / `bal_v_max` | V | — | ~ / ❌ |
| Balancing | `bal_delta_mv` | mV | 1–200 | ✅ |
| Balancing | `bal_die_temp` | °C | 40–120 | ❌ |
| Balancing | `bal_pulse_ms` | ms | 100–1800 | ✅ |
| Fans | `fan_temp_start` / `fan_temp_full` | °C | — | ❌ (hardcoded 38/45) |
| Fans | `fan_pwm_min` / `fan_pwm_max` | % | 0–100 | ❌ |
| Precharge | `pre_timeout_s` | s | 1–60 | ❌ |
| Precharge | `pre_bus_pct` | % | 50–100 | ❌ |
| Precharge | `pre_air_timeout` | ms | 50–5000 | ✅ |
| Future | `dcl_*`, `soh_nominal_ah` | — | — | ❌ |
| Curves | `ocv_points`, `tempcomp_points` | tables | — | ❌ (in code) |

Most of what is a `#define` today the UI already expects to be configurable. A configurable
OCV curve also fixes the SOC seed problem.

### 9.3 EEPROM rules

1. **CRC over the whole blob**, not a magic number. `0xB007` says somebody wrote, not that
   the content is intact.
2. **Two banks (A/B) with a sequence number.** Write inactive, verify CRC, then mark
   current. A power cut mid-write can never leave the BMS without valid configuration.
3. **Never trust the value read.** Clamp against `firmware/config/` limits on load.
4. **Version and migration**, explicit. Never reinterpret old bytes with a new layout.
5. **Writes blocked unless `hv_is_safe`**, enforced in firmware independently of the UI.

**⚠️ Size budget — prove it fits.** The 24FC08 is **1024 bytes total**. Two banks means
roughly **≤ 480 bytes per bank** after header, version, sequence number and CRC. The OCV
and temperature-compensation curves must be shown to fit, or be reduced (fewer points, or
`uint8_t` deltas rather than full pairs). A phase 0 check, not a phase 7 discovery.
⚠️ Also verify the 24FC08's internal block structure and page size before choosing bank
boundaries.

**Graded response to invalid configuration.** "Use default and raise a fault" is too
coarse:

| Corrupt parameter | Response |
|---|---|
| Fan curve, warning thresholds, telemetry rates | default + WARNING, continue |
| OCV curve | default + WARNING, SOC marked low-confidence |
| Safety thresholds (OV/UV/OT/open-wire) | default + CRITICAL, **HV inhibited** |
| Topology (IC count, cells per IC, chain mapping) | **HV inhibited**, no safe default exists |

A wrong topology means the whole cell mapping is wrong; there is no sane default, so it
must block HV outright.

---

## 10. Phases

**`Config_Load()` exists from phase 0 with a compile-time-defaults backend.** Faults,
balancing, precharge and charging all need configuration long before phase 7. Phase 7 swaps
only the *backend* to EEPROM; no logic ever learns where the values came from.

| Phase | Content | Done when |
|---|---|---|
| **0** | Skeleton: `core`/`runtime` split, state read/update API, `BmsConfig` + `Config_Load()` (defaults backend), `AdbmsChain` context, requested-outputs double buffer, **WWDG timing computed from the actual PCLK1 and `watchdog_check_period` proven to fit the hardware window**, CI + host tests | `app/core/` compiles and tests on x86 with no HAL/RTOS |
| **1** | `bsp/` + `drivers/adbms` on both chains with SNAP/UNSNAP, `adbms_decode` unit-tested, publisher task | **144 correct cells via `live_debug`/UART** (telemetry is phase 2) |
| **2** | Analog, digital inputs, ISA, CAN devices, CAN filters, prioritised transmit, protocol layer | CAN1 interrupt load measured and low |
| **3** | Fault checks + policy engine, `safety_task`, **output arbitration (§5)** | every table row has a host test |
| **4** | `BmsModeController`, `PrechargeController`, tested on host **before** seeing HV | precharge scenarios pass in CI |
| **5** | Balancing, fans, SOC | functional parity with v2 |
| **6** | `ChargingController` (P1000) with the cut-offs validated in v2 | full charge on the bench |
| **7** | EEPROM backend: A/B banks, CRC, size budget proven, schema generation | interrupted write breaks nothing |
| **8** | Independent HV sense (needs HV review), black box if the UI log is insufficient | — |
| **9** | LPCM, if justified | — |

Phases 0–3 are the foundation; 4+ go fast *because* they exist.

---

## 11. Carry over from v2

- **`ams_error.c` single-owner pattern** — right idea; extended in v3 to cover the
  contactors too (§5).
- **Two-ISA separation** (pack on CAN1, handcart on CAN2, same IDs) — kept explicit as
  `isa_sensors.pack` / `isa_sensors.handcart`.
- **`sync_dbc.sh`** — moves to `tools/`, gains a pinned ref (§1.6).
- **ADBMS acquisition split into phases** — right idea; moves entirely into
  `drivers/adbms` (§3.4).
- **`live_debug`** — repointed at the state structs, plus the §2.6 R5 and §3.1
  instrumentation.
- **Bootloader / programming service.** v2 jumps to the STM32 system bootloader on a CAN
  command, and that deliberate deinit sequence must not be lost. It belongs in `runtime/`
  as an explicit service, with the trigger arbitrated like any other command:
  `bootloader_allowed = hv_is_safe AND not charging AND contactors commanded open` (§5.1).
  Not a priority, but not something to let quietly disappear in a rewrite.
- **Project memory in `.claude/memory/`.**

---

## 12. v2 bugs v3 must make structurally impossible

| v2 bug | Prevented by |
|---|---|
| SOC seed, no PEC guard, negative-float cast → 0 % | §2.4 quarantine + §2.5 no float at boundary |
| `DISCHARGE_SIGN` ambiguity | §2.5 single convention at the producer |
| SOC never re-anchored to OCV at rest | §9.2 OCV curve in EEPROM |
| CAN1 filter accepts everything | §14 hardware filter banks |
| head/tail race in CAN queues | FreeRTOS queues |
| `printfDebug` flood → `FAULT_UART2_TX` | §14 drop-and-count |
| `calloc`/`free`/`exit(0)` in parse | §13 static buffers |
| Charger fault cuts on one frame | §7.3 `debounce_ms` |
| Aux open-wire hair trigger | §9 configurable + §7.3 debounce |
| ISA "alive" by ID with frozen values | §2.4 stored timestamps, derived liveness |
| Dead always-true condition in the charger | §8 host tests |
| Tail-of-chain corruption | §6.1 split chain |
| WWDG refreshed from inside the EEPROM driver | §6.4 one refresh site + check period |
| Two potential writers on the contactors | §5 safety layer sole owner |
| AMS_ERROR polarity confusion (SET vs RESET) | §2.5 booleans that assert the condition |

---

## 13. The ADI library — keep, isolate, patch

Keeping it is right: command encoding, PEC and the register map are many hours to rewrite,
and rewriting adds risk with no gain. It lives in **`firmware/vendor/`, reachable only
through `drivers/adbms/`**. Three fixes first:

1. **`calloc`/`free` per parse call in the hot path** → statically sized buffers.
2. **`exit(0)` on allocation failure** → kills the firmware silently. Unacceptable.
3. **8-bit address variable overflowing past ~7 ICs on `ALL_GRP` reads** → armed bomb at
   12 ICs.

Strategy: `git subtree` into `vendor/adi_adbms6830/` with a **documented patch** on top, so
fixes stay visible and re-appliable. Fixes must never become indistinguishable from vendor
code.

---

## 14. Communications

**isoSPI over DMA** — the biggest single win: the CPU is free during transfer and the
ADBMS sequence becomes genuinely non-blocking (start transfer, return, resume on the
completion semaphore). DMA does not remove ADBMS timing requirements; those become
sequence states. Buffers must be DMA-accessible and not stack-local. Every transaction
needs a timeout with a recovery path — `communication_ok = false`, re-init, raise a fault.

**CAN** — bxCAN has no DMA. The equivalent win is elsewhere: **use the hardware filter
banks** (CAN1 accepts everything today, so the MCU is interrupted by every bus frame while
consuming ~6 IDs, with 14 banks idle — the cheapest improvement in this plan); use all 3
transmit mailboxes; separate transmit by priority so a fault frame never queues behind
telemetry.

**I2C/EEPROM over DMA** — low priority; writes are rare. The bigger gain is that removing
the MCP23017 leaves only the EEPROM on the bus.

**UART** — already DMA. New rule: **if the buffer is full, drop and count.** Never block,
never fault because of a print. Event prints are edge-triggered.

---

## 15. Open decisions

1. **SDC latency budget** — `ISR → queue → task` adds one task period. State the
   worst-case number explicitly; if too slow, the escape hatch is
   `Safety_SetImmediateShutdown()` called directly from the ISR (§2.7).
2. **ADBMS6822 PCB routing** (§6.1) — shared SPI with two chip selects, two SPI
   peripherals, or second set unrouted? Decides sequential vs concurrent chain reads.
3. **FTTI numbers** (§3.2) — from the FS rules and the ADBMS datasheet, not from a guess.
   These and `watchdog_check_period` (§6.4) constrain each other: an acquisition slower
   than the check period cannot be a member and needs a deadline counter instead.
4. **EEPROM size budget** (§9.3) — prove the curves fit in ~480 bytes per bank, or reduce
   them.
5. **LPCM** (§4) — datasheet plus current measurement.
6. **Independent HV sense** (§6.2) — needs an HV electrical review.
7. **Black box** (§6.3) — first establish whether `BMS_UI` CAN logging already covers it.
