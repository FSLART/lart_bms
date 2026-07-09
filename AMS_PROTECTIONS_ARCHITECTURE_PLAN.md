# AMS Protections Architecture Plan

**Project:** lart_bms — Formula Student AMS/BMS (STM32F412, ADBMS6830 daisychain, ISA IVT-S, dual CAN)
**Status:** design document only — no code changed
**Scope:** AMS error abstraction, SDC monitoring (PC7), ADBMS protection checks, IVT-S current protection, bus voltage protection, precharge/HV_ON safety, EEPROM-ready thresholds

---

## 0. Findings in the current code (read before designing)

These observations drive every decision below. File references are to the current `beta-v2` tree.

### 0.1 Safety logic lives inside the CAN TX module
`BMS_SafetyCheck()` (cell OV/UV, NTC OT) is implemented in `adbms_to_CAN.c` — a file whose actual job is packing CAN frames. It is called from `brain.c` only when `faultCheck` fires (every 250 ms via SysTick) and **only in `IDLE` and `CHARGING`**:

```c
/* brain.c, brain_loop() */
if (AMS_Current_State == IDLE || AMS_Current_State == CHARGING) {
    BMS_SafetyCheck();
}
```

Consequences:
- No cell/temperature protection while `BALANCING` (discharging cells with zero OV/UV/OT supervision).
- No protection during precharge or HV_ON (these are not even AMS states — see 0.4).
- Protection cadence is tied to the CAN/UI housekeeping flag, not to fresh-measurement availability.

### 0.2 AMS error GPIO is written raw, from scattered places
`HAL_GPIO_WritePin(AMS_ERROR_GPIO_Port, AMS_ERROR_Pin, ...)` appears in `brain_start()` (SET), the `IDLE` case (RESET), the `STARTUP` case (RESET), plus several commented-out copies in `adbms_to_CAN.c`, `isa_ivt-s.c` and the watchdog boot check. Nothing arbitrates: `IDLE` clears the pin every 50 ms **unconditionally**, so even if a protection set it, the next IDLE iteration silently un-latches the hardware error line. This is the single most dangerous pattern in the codebase.

### 0.3 SDC input exists but is never read
`MCU_SDC_FB` = **PC7**, already configured as GPIO input in CubeMX-generated `main.c` (line ~835). No code reads it. `FAULT_SDC_TRIGGERED` (code 25) exists in `fault_manager.h` and is never raised. The legacy `ErrorCode_t`/`RaiseError()` path in `brain.c/h` is dead code with broken bit values (`0x30`, `0x50` collide with earlier bits) — should eventually be deleted, not extended.

### 0.4 Precharge is a parallel universe
`PrechargeState_t` runs its own machine via `Precharge_Update()` every `brain_loop()` pass, independent of `AMSStates_t` (which has no PRECHARGE or HV_ON state). Protection-relevant stubs:

```c
bool IsBusVoltageOK(void) { return true; }   /* stub */
bool IsCurrentOK(void)    { return true; }   /* stub */
```

`VERIFY_CURRENT` and `VERIFY_BUS_VOLT` states therefore always pass. `OnPrechargeComplete()` re-checks contactor feedback only every 10 000 calls (`skip_ticks`), i.e. HV_ON supervision cadence is defined by loop speed, not time. `bypassChecks`/`bypassDischarge` flags exist (PCB ground-sharing workaround) and must survive the redesign.

### 0.5 Thresholds are duplicated
Cell limits exist twice: compile-time `SAFETY_CELL_OV_V`/`SAFETY_CELL_UV_V`/`SAFETY_CELL_OT_C` (used by `BMS_SafetyCheck`) and EEPROM-backed `masterCfg.ov_threshold_mV`/`uv_threshold_mV` (loaded by `BmsConfig_Init`, currently only feeding balancing). Two sources of truth, only one of which is configurable. The EEPROM layer already has exactly the right shape (checksum `0xB007`, version byte, `BmsConfig_LoadDefaults()` fallback, `FAULT_EEPROM_*` codes) — protections must consume it, not reinvent it.

### 0.6 fault_manager is good — build on it, don't replace it
Latched-until-`KILL_ERROR` semantics, 64-bit active mask, per-code context, history ring, UART dump, CAN export via `master_to_CAN.c`. What it lacks is **severity policy** (what a fault *does*) — that is precisely the gap the new layer fills. Do not add policy inside `fault_manager`; keep it a passive registry.

### 0.7 IVT-S data path is ready, supervision is not
`isa_ivt-s.c` maintains current/voltage/power getters (`IVT_GetCurrent_mA()` etc.), a 1 s staleness check inside `IVT_FAULT_CHECK()` (commented out of the brain loop), sys/measurement-error frame parsing, and `Check_PackVoltage_and_Current()` (raises `FAULT_PACK_VOLTAGE_MISMATCH`). None of it is wired into any decision that opens contactors or blocks precharge.

---

## 1. High-level architecture

```
                 ┌──────────────────────────────────────────────┐
                 │                  brain.c                     │
                 │  AMS state machine + 1 kHz/250 ms schedulers │
                 └──────┬──────────────────┬────────────────────┘
                        │ calls             │ calls
                        ▼                   ▼
        ┌────────────────────┐   ┌─────────────────────────┐
        │  ams_protection.c  │   │      precharge.c        │
        │  (policy: checks,  │◄──┤ asks: IsHvOn? CheckX()  │
        │  debounce, state-  │   └─────────────────────────┘
        │  dependent limits) │
        └───┬──────────┬─────┘
            │ raises    │ commands
            ▼           ▼
   ┌───────────────┐  ┌──────────────┐
   │ fault_manager │  │  ams_error.c │──► AMS_ERROR GPIO (only writer)
   │ (registry)    │  │ (latch levels│
   └───────────────┘  │  + HV guard) │
                      └──────────────┘

   Data producers (never make policy decisions):
   adBms_Application.c (IC[]/SLAVE[]) · isa_ivt-s.c (getters) ·
   analog_readings.c · precharge.c feedbacks · PC7 GPIO
```

Separation of concerns:

| Layer | Owns | Never does |
|---|---|---|
| `ams_error.c` | AMS_ERROR GPIO, latch level (clearable / latched / power-cycle-only), HV-clear-refusal rule | measurement, thresholds, fault codes |
| `ams_protection.c` | all threshold checks, debounce, state-dependent limits, SDC read, cross-checks | GPIO writes, CAN TX, latching policy internals |
| `fault_manager` | passive record of what fired, context, history, CAN/UART export | deciding consequences |
| `adbms_to_CAN.c` | CAN frame packing only (after migration of `BMS_SafetyCheck`) | protection checks |
| `precharge.c` | sequencing contactors | owning global protection policy — it *asks* `ams_protection` |

Data flow: producers update structs/getters → `ams_protection` evaluates on a fixed cadence + at ADBMS state-machine sync points → raises faults in `fault_manager` → maps severity → commands `ams_error` (and contactor opening / charge stop / precharge block where required).

---

## 2. `ams_error.c/.h` — AMS error abstraction

### 2.1 Public API (proposed)

```c
typedef enum {
    AMS_ERR_CLEARABLE = 0,   /* clears when cause clears + explicit clear   */
    AMS_ERR_LATCHED,         /* survives cause disappearing; software clear */
                             /* allowed only when HV is OFF                 */
    AMS_ERR_POWER_CYCLE,     /* only a full MCU power cycle clears it       */
} AmsErrorLevel_t;

void  AMS_Error_Init(void);                                /* boot: pin SET (fail-safe) */
void  AMS_Error_Trigger(AmsErrorLevel_t lvl, FaultCode_t reason);
bool  AMS_Error_Clear(FaultCode_t reason);                 /* false = refused           */
bool  AMS_Error_IsActive(void);
bool  AMS_Error_IsLatched(void);
bool  AMS_Error_IsPowerCycleOnly(void);
FaultCode_t AMS_Error_FirstReason(void);                   /* for UI/CAN                */
```

Notes on shape:
- `reason` is the existing `FaultCode_t` — no parallel reason enum (kills the dead `ErrorCode_t` in `brain.h` at the same time). Context (which cell, which NTC) already lives in `fault_manager`; `ams_error` stores only a `uint64_t reason_mask` per level.
- `Trigger` at a higher level upgrades an active lower level; never downgrades.

### 2.2 GPIO ownership

`ams_error.c` is the **only** file allowed to touch `AMS_ERROR_GPIO_Port/Pin`. Migration removes the three raw writes in `brain.c` and every commented copy elsewhere. Internally:

```
pin state = active(any level) ? ERROR : OK
```

recomputed only inside `AMS_Error_Trigger/Clear` — no periodic rewriting, so nothing can fight the latch (fixes 0.2). Polarity note: current code uses SET at boot and RESET in IDLE/STARTUP; the module must encapsulate polarity in one `#define` so the rest of the code speaks in `error/ok` terms, not pin levels.

### 2.3 The HV clear-refusal rule (hard requirement)

```c
bool AMS_Error_Clear(FaultCode_t reason) {
    if (AMS_Protection_IsHvOn())   return false;  /* no clears while HV_ON  */
    if (level == AMS_ERR_POWER_CYCLE) return false;
    ...
}
```

`AMS_Protection_IsHvOn()` must be **conservative**: HV is considered ON from the moment `SWITCH_HVNEG` is entered until `KILL`/`WRONG` has verified all contactors open (not merely commanded open). Recommended implementation: derive from `Precharge_GetState()` ∈ {SWITCH_HVNEG … HV_ON} **or** AIR+ feedback active — so a stuck contactor still counts as HV present. This errs on refusing clears, which is the safe direction.

### 2.4 Power-cycle-only latch implementation

A `static bool power_cycle_latch` in `ams_error.c`, initialised to `false` **only** by the C runtime at boot (`.bss` zeroing). No function writes `false` to it — grep-enforceable rule: the identifier appears on the left of `= false` zero times outside its definition. Not stored in EEPROM/backup registers: a genuine power cycle must clear it, and RTC backup domain survives resets we *want* to clear it. Optional hardening: pair with a magic word (`0xDEAD5DC1`) so a stray memory write is detectable, and log the latch reason to fault history immediately so it survives to the UART dump even after reset (history is RAM — accept it's lost on power cycle; the point of the latch is behavioral, not forensic).

Explicitly distinguish from `FAULT_WATCHDOG_RESET` boot behavior: a WWDG reset re-enters `main()` and re-zeros the latch — that is accepted, because the watchdog fault itself is re-raised at boot and policy (§10) decides its level.

---

## 3. SDC monitoring on PC7 (`MCU_SDC_FB`)

- **Init:** already done — CubeMX configures PC7 input in `MX_GPIO_Init()`. Constraint honored: no CubeMX/GPIO changes needed. Polled, not EXTI: 1 ms polling gives deterministic debounce and avoids adding interrupt load; the contactor feedbacks already demonstrate EXTI+debounce complexity (`FeedbackDebounce_t` in precharge.c) that we don't need here.
- **Where:** `ams_protection.c`, function `AMS_Protection_CheckSDC()`, called from the protection tick (§9.1). Not `brain.c` (policy doesn't belong in the scheduler), not `precharge.c` (SDC matters in every state, not only precharge).
- **Debounce:** integrator filter — counter increments each 1 ms tick PC7 reads LOW, decrements (floor 0) when HIGH; trip at `sdc_debounce_ms` (default **20 ms**, EEPROM-configurable later, §7). 20 ms rejects contact bounce and coupled ignition noise but still beats any human-scale reaction. Recovery (fault kill + clearable-error clear eligibility) requires a symmetric 20 ms of continuous HIGH.
- **Reporting:** raise existing `FAULT_SDC_TRIGGERED` with `.measured_value` = debounced level, plus `printfDebug` one-shot on each edge (`printfDebug` is the mandated console path).
- **Severity by state:**
  - SDC LOW while no HV (IDLE/STARTUP/BALANCING, precharge not started): `AMS_ERR_LATCHED` — investigate, clear allowed once loop restored.
  - SDC LOW during precharge (before HV_ON): abort sequence → `KILL`, `AMS_ERR_LATCHED`.
  - **SDC LOW while HV_ON: `AMS_ERR_POWER_CYCLE`** (hard requirement). `AMS_Error_Clear()` refuses; only MCU power-down clears. Contactors opened immediately (they likely already dropped — SDC cuts them by hardware — but software must command open and record the event).

Rationale for the power-cycle rule: SDC opening under load means something upstream (BSPD, IMD, inertia switch, driver E-stop) killed the car while energized — a scrutineering-grade event. Software self-recovery would mask it.

---

## 4. Precharge / HV_ON safety integration

### 4.1 Sensor sourcing decisions

| Quantity | Primary source | Cross-check | Why |
|---|---|---|---|
| Pack voltage (behind AIRs) | ADBMS sum (`g_pack_voltage_sum_mV`, already cached) | IVT-S **U2** channel if wired pack-side | ADBMS always measures the pack; immune to contactor state |
| DC bus voltage (inverter side) | **IVT-S U1** (`IVT_GetPackVoltage_mV()`, cyclic 100 ms already configured) | — (only bus-side sensor available) | Only sensor on the load side of the AIRs |
| Current | **IVT-S I** (`IVT_GetCurrent_mA()`, cyclic 100 ms) | `analog_readings.c` `ams_master_current` plausibility band | IVT-S is the calibrated FS current sensor; analog channel catches gross disagreement (> configurable A for > debounce ms → `FAULT_CURRENT_SENSOR_ERROR`) |

Staleness rule: any IVT-S value older than 3× its cycle time (300 ms) is **invalid** — checks that depend on it must fail safe (block progression / abort precharge), not pass silently. This makes the current `return true` stubs impossible to recreate.

### 4.2 Per-state protection behavior

`precharge.c` keeps sequencing; each decision point calls `ams_protection`:

| Precharge state | Active protection checks | On failure |
|---|---|---|
| before `START` (KILL/idle) | precharge blocked if: any latched/power-cycle AMS error, SDC LOW, IVT stale, **unexpected HV on bus** (U1 > ~30 V with all contactors open → `FAULT_PACK_VOLTAGE_MISMATCH` or new `FAULT_BUS_VOLTAGE_UNEXPECTED`) | request refused, stay KILL |
| `OPEN_ALL` … `CHECKING_AIR_NEG_IS_CLOSED` | contactor feedback (existing `IsTheStateOK`), SDC | → `WRONG` → KILL (existing path) + `AMS_ERR_LATCHED` on contactor mismatch |
| `WAIT_FOR_PRECHARGE_TO_CLOSE` (3 s cap-charge window) | every 100 ms: **precharge current** ≤ limit (default ~20 A — size from R_pre and pack V); **bus voltage rising**: U1 must reach configurable fractions vs. an RC-derived expectation (e.g. ≥ 50 % pack at 1.5 s); timeout already bounded by the 3 s window → keep + raise `FAULT_PRECHARGE_TIMEOUT` if targets missed | abort → `WRONG`; raise `FAULT_PRECHARGE_FAILURE` with measured value; `AMS_ERR_LATCHED` |
| `VERIFY_CURRENT` | replace stub: |I| ≤ precharge settle limit (default ~2 A: caps charged ⇒ near-zero current) using IVT-S + staleness rule | → `WRONG` |
| `VERIFY_BUS_VOLT` | replace stub: `U1 ≥ ratio × pack_V` (default ratio **0.95**, EEPROM later) and pack_V itself sane (ADBMS alive, sum plausible) | → `WRONG` |
| `CHECKING_AIR_POS_IS_CLOSED` → `HV_ON` | contactor feedback (existing) + delta-check U1 ≈ pack within mismatch tolerance | → `WRONG` |
| `HV_ON` (continuous) | time-based (fix `skip_ticks` counter → `getRuntimeMs()`, e.g. every 100 ms): contactor feedback; SDC (→ power-cycle-only, §3); current within discharge/charge limits (§5); U1 vs pack mismatch (welded/backfeed detection); ADBMS protection faults escalate here | → `KILL` + level per §10 |
| after KILL / contactors open | verify feedbacks actually opened; U1 should decay — persistent U1 ≈ pack with AIRs "open" ⇒ welded contactor ⇒ `AMS_ERR_POWER_CYCLE` | log + error |

### 4.3 Interface shape

`precharge.c` calls, never implements:

```c
bool AMS_Protection_PrechargeAllowed(void);                    /* gate at RX_CAN/START   */
bool AMS_Protection_CheckPrechargeCurrent(PrechargeState_t s); /* state-aware limits     */
bool AMS_Protection_CheckPrechargeBusVoltage(PrechargeState_t s, uint32_t elapsed_ms);
bool AMS_Protection_CheckSDC(void);                            /* debounced level        */
bool AMS_Protection_IsHvOn(void);                              /* conservative, §2.3     */
```

Existing `bypassChecks`/`bypassDischarge` remain honored **only** for contactor-feedback checks (their documented purpose — PCB ground-sharing workaround). They must **not** bypass current/voltage/SDC protection: a test-bench flag that silently disables electrical protection is how packs die. If a full-bypass bench mode is genuinely needed, make it a separate, loudly-logged flag.

---

## 5. ADBMS measurement protection (`AMS_Protection_EvaluateAdbmsMeasurements`)

### 5.1 Placement

Move the logic of `BMS_SafetyCheck()` out of `adbms_to_CAN.c` into `ams_protection.c` as `AMS_Protection_EvaluateAdbmsMeasurements(const cell_asic *ics, uint8_t count)`. Keep the hard-won behaviors from the recent debugging sessions (these are load-bearing, do not "clean them up"):
- skip module when `cccrc.cell_pec != 0` (parse writes garbage on PEC failure — vendor code);
- `cell_v < 0` (0x8000 register-reset sentinel, exact −3.4152 V) ⇒ `FAULT_OPEN_WIRE`, never UV;
- boot warmup skip (first N evaluations) until the whole chain has delivered real conversions;
- NTC 1.99 °C / 149 °C open/short window ⇒ skip OT judgment (open-wire reported solely via `FAULT_OW_DETECTED_RTH` — deliberate de-duplication decision);
- S4 NTC3/NTC4 are known-open hardware on this car.

**Call sites** in `adBms_Application.c` — at each point where a measurement set becomes complete and coherent:
1. IDLE machine: end of `ADBMS_IDLE_READ_PREV` / after the RDAC+RDAUX+RDSTAT block (i.e. wherever `memcpy(SLAVE, IC, …)` style snapshotting happens);
2. BALANCING machine: end of `BAL_CYCLE_READ_AVG` — **closes the "no protection while balancing" hole** (0.1). Evaluate on `acell.ac_codes` there, consistent with the balancing data-source decision;
3. open-wire diagnostic phases: after OW evaluation completes;
4. STARTUP: after first full read (feeds warmup counter).

`brain.c`'s 250 ms `faultCheck` block then calls only the *periodic* protections (SDC, IVT, bus voltage) — ADBMS evaluation rides the measurement cadence instead, so it can never judge stale data.

### 5.2 Check list

| Check | Fault (existing unless noted) | Notes |
|---|---|---|
| Cell OV / UV | `FAULT_OVERVOLTAGE` / `FAULT_UNDERVOLTAGE` | thresholds from config (§7), per-cell context |
| Over/under-temperature | `FAULT_OVERTEMPERATURE` / `FAULT_UNDERTEMPERATURE` | UT useful in charging (Li-ion charge < 0 °C damage) → `FAULT_UNDERTEMPERATURE_CHARGE` in CHARGING |
| Open wire (cell / NTC) | `FAULT_OW_DETECTED_CELL` / `FAULT_OW_DETECTED_RTH` | existing diagnostics keep producing; protection layer decides severity |
| PEC / comms | `FAULT_PEC_ERROR` | already raised in `adBmsReadData`; protection adds *policy*: persistent PEC on same IC > N s ⇒ escalate to AMS error (measurements untrustworthy) |
| Stale data | `FAULT_ACQUISITION_TIMEOUT` | timestamp last good full-chain read; > 500 ms in any HV-relevant state ⇒ escalate |
| Slave count | `FAULT_SLAVE_NOT_DETECTED` | `slaves_found` vs `masterCfg.total_ic` (EEPROM already stores topology) |
| Implausible readings | reuse sentinel/garbage rules | 0x8000, out-of-window ITMP (cross-check vs NTC max — same trick as the balancing die-temp guard) |

Consumes `IC[]`/`SLAVE[]` read-only via the existing externs in `adBms_Application.h` — zero knowledge of CAN packing. `adbms_to_CAN.c` shrinks to pure TX (its name finally true).

---

## 6. IVT-S current protection

`AMS_Protection_CheckCurrent(void)` in the periodic tick:

| Condition | Fault | Level |
|---|---|---|
| discharge current > limit for > `current_debounce_ms` | new `FAULT_OVERCURRENT_DISCHARGE` (add to enum tail, ≤ 63 rule OK — 45 used) | latched, open contactors |
| charge current > limit (sign-aware) | new `FAULT_OVERCURRENT_CHARGE` | latched, stop charger (`Charger_…` stop path) + contactors if HV_ON |
| reading stale > 300 ms | existing `FAULT_ISA_IVTS_TIMEOUT` | in HV_ON/precharge: treat as protection-blind ⇒ latched + KILL; in IDLE: clearable warning |
| IVT sys/measurement error frames (`IVT_PROCESS_SYSERRORS` already parses) | `FAULT_CURRENT_SENSOR_ERROR` | latched in HV states |
| IVT vs analog `ams_master_current` disagreement > band | `FAULT_CURRENT_SENSOR_ERROR` | clearable warning (analog channel is coarse) |
| current > idle-expected (~2 A) while **no** HV path should exist | `FAULT_PACK_VOLTAGE_MISMATCH`-family or new dedicated | latched — something is welded/leaking |

State-dependent limits (all config, §7): precharge ≈ 20 A peak / 2 A settle · charging = charger max + margin · discharge/driving = fuse-based (e.g. 130 % of rated for 100 ms style — exact values are electrical-team input, marked open question) · idle ≈ 2 A.

Evaluation sites: periodic tick (always), plus the precharge-specific calls of §4.3. Data via existing getters only — no CAN coupling; `isa_ivt-s.c` keeps parsing frames and answering getters, gains zero policy.

---

## 7. Threshold configuration (EEPROM-ready)

Single accessor pattern, hard defaults now, EEPROM later — no third source of truth:

```c
/* ams_protection_config.h  (optional module; can start as a header) */
typedef struct {
    uint16_t cell_ov_mV, cell_uv_mV;
    int16_t  ot_dC, ut_charge_dC;
    int32_t  i_charge_max_mA, i_discharge_max_mA,
             i_precharge_max_mA, i_precharge_settle_mA, i_idle_max_mA;
    uint8_t  bus_precharge_ratio_pct;      /* 95 */
    uint16_t bus_mismatch_tol_mV;
    uint16_t sdc_debounce_ms, i_debounce_ms, v_debounce_ms;
    uint16_t ivt_stale_ms, adbms_stale_ms, precharge_rise_ms;
} AmsProtectionConfig_t;

const AmsProtectionConfig_t *AMS_Protection_GetConfig(void);
```

- **Phase now:** `GetConfig()` returns `static const` compiled defaults. All checks read through it — zero literals inside check functions. The duplicated `SAFETY_*` macros in `adbms_to_CAN.h` become the *defaults table* and disappear from call sites.
- **Phase EEPROM:** extend `BmsEepromConfig` (new page, **version bump** — layout change without modifying today's layout file is deferred work, respecting the current constraint) and populate the struct in `BmsConfig_Init()`. Existing machinery already does the right dance: checksum `0xB007` → `BmsConfig_LoadDefaults()` on failure → this plan adds `RAISE_ERROR(FAULT_EEPROM_VALIDATION_ERROR)` on fallback plus **range validation** per field (e.g. `cell_ov_mV ∈ [3500, 4400]`); any field out of range ⇒ whole struct rejected ⇒ defaults. Uninitialized thresholds are structurally impossible: the accessor never exposes anything but defaults-or-validated.

---

## 8. Module structure & responsibilities

```
Core/Src/ams_error.c        Core/Inc/ams_error.h         (new)
Core/Src/ams_protection.c   Core/Inc/ams_protection.h    (new)
Core/Inc/ams_protection_config.h                          (new, header-only at first)
```

| Existing file | Change (at implementation time) |
|---|---|
| `brain.c` | remove raw AMS_ERROR writes; `AMS_Error_Init()` in `brain_start()`; call `AMS_Protection_Tick()` on the 250 ms flag (and a 1 ms SDC sample via SysTick counter); FAULT state gets real behavior (§9) |
| `adBms_Application.c` | add `AMS_Protection_EvaluateAdbmsMeasurements()` calls at §5.1 sync points |
| `adbms_to_CAN.c` | `BMS_SafetyCheck` body migrates out; file becomes TX-only |
| `precharge.c` | stubs `IsCurrentOK`/`IsBusVoltageOK` delegate to `ams_protection`; `OnPrechargeComplete` tick-counter → ms-based; gate at request via `AMS_Protection_PrechargeAllowed()` |
| `isa_ivt-s.c` | unchanged producer; `IVT_FAULT_CHECK` staleness logic absorbed by protection layer |
| `fault_manager.c/.h` | unchanged semantics; enum gains `FAULT_OVERCURRENT_DISCHARGE/CHARGE` (+ optional `FAULT_BUS_VOLTAGE_UNEXPECTED`) at tail |
| `bms_eeprom_config.c/.h` | later phase only (§7) |
| `brain.h` | delete dead `ErrorCode_t`/`errorStatus`/`RaiseError` |

Bad-coupling call-outs this layout eliminates: protection inside CAN TX (0.1) · scattered GPIO writes (0.2) · duplicated thresholds (0.5) · precharge owning electrical policy via stubs (0.4) · safety decisions in debug/UI paths (none found — `live_debug` is correctly read-only; keep it that way).

---

## 9. State machine integration

### 9.1 Scheduling
- 1 ms (SysTick counter, like existing `counter_200ms` pattern): SDC integrator sample.
- 250 ms (`faultCheck` flag, existing): `AMS_Protection_Tick()` = SDC verdict, IVT current/staleness, bus-voltage checks, escalation evaluation.
- Measurement-synchronous: ADBMS evaluation (§5.1).
- Every loop: `Precharge_Update()` unchanged; its states call protection functions inline (§4).

### 9.2 Per-state matrix (AMS `AMSStates_t` × precharge overlay)

| State | Active checks | Relaxed/ignored | Clearable? | Contactors/actions |
|---|---|---|---|---|
| STARTUP | warmup counting; slave count; EEPROM validation; watchdog-boot fault | OV/UV/OT during warmup (0x8000 sentinels) | yes (except power-cycle class) | open; precharge blocked until first clean full read |
| IDLE | all ADBMS checks; SDC; IVT staleness; idle-current; unexpected-bus-HV | — | yes, when cause gone & HV off | open if AMS error active; precharge allowed only if `PrechargeAllowed()` |
| BALANCING | **full ADBMS checks (new)**; die-temp guard (exists); SDC; idle-current | UV threshold unchanged (discharge is µA-scale — no relaxation needed) | yes | balancing aborts on any AMS error (`BALANCE_END` sticky path exists) |
| CHARGING | ADBMS + UT-charge; charge overcurrent; charger timeout; SDC | — | yes, charge stops first | stop charger on any AMS error; contactors per charger topology |
| PRECHARGE (overlay) | §4.2 table: SDC, precharge current, bus rise, contactor feedback, timeouts | UI/CAN-error escalation shouldn't abort a sequence (CAN faults ≠ electrical danger) | no clears mid-sequence — abort first | any failure → WRONG → KILL |
| HV_ON (overlay) | everything: ADBMS, SDC (**power-cycle on trip**), discharge/charge current, bus-vs-pack mismatch, contactor feedback @100 ms | — | **nothing clearable while HV_ON** (hard rule, enforced in `AMS_Error_Clear`) | any AMS error → KILL (open all) then normal latch rules apply |
| FAULT | keep evaluating & logging (diagnosis needs data) | — | exit only via explicit operator action + all-clear + HV off | contactors open; precharge & charging blocked |
| RESET_ISA | pass-through (existing 1-shot config state) | — | n/a | inherit previous |

### 9.3 Escalation summary
- warning (no AMS error): sensor disagreement, single PEC hit, IVT timeout in IDLE, `FAULT_OW_DETECTED_RTH` on known-open channels;
- clearable AMS error: OV/UV/OT in no-HV states after debounce, SDC LOW without HV;
- latched: anything that fired in PRECHARGE/HV_ON except below; persistent PEC/stale-data in HV states; contactor mismatch; overcurrent;
- power-cycle-only: **SDC lost during HV_ON**; welded-contactor evidence (bus stays ≈ pack after commanded open).

---

## 10. Critical trigger sequence (single choke point)

```
protection check fails (debounced)
  → RAISE_ERROR(code, context)               /* registry, exact as today   */
  → severity lookup (state-aware table §9.3)
  → AMS_Error_Trigger(level, code)           /* GPIO set, latch recorded   */
  → side effects by state:
        CHARGING  → charger stop command
        PRECHARGE → state = WRONG (abort)
        HV_ON     → state = KILL (open all contactors)
        any       → PrechargeAllowed() == false while error active
  → printfDebug one-shot with code/context   /* console rule: printfDebug  */
```

One function owns this cascade (`ams_protection` internal `escalate()`), so no check ever hand-rolls its own consequences — that is how today's "IDLE clears the pin every 50 ms" class of bug happened.

---

## 11. Implementation phases

1. **Phase 1 — `ams_error` module**: GPIO ownership migration, three levels, HV-refusal with a temporary `IsHvOn()` reading `Precharge_GetState()`. Smallest testable slice; kills bug 0.2 alone.
2. **Phase 2 — `ams_protection` skeleton + SDC**: tick wiring, PC7 integrator, `FAULT_SDC_TRIGGERED`, power-cycle rule. Bench-testable with a jumper.
3. **Phase 3 — ADBMS migration**: move `BMS_SafetyCheck` logic, add BALANCING/OW call sites, stale-data & slave-count checks. Behavior-preserving except the new coverage.
4. **Phase 4 — precharge electrical checks**: replace both stubs, cap-charge monitoring, HV_ON ms-based supervision, `PrechargeAllowed()` gate. Requires bench HV rig.
5. **Phase 5 — IVT current protection + cross-checks** (+ two new fault codes).
6. **Phase 6 — config unification**: accessor struct, delete `SAFETY_*` from call sites.
7. **Phase 7 — EEPROM page + validation** (layout version bump; separate PR by constraint).

Each phase leaves the firmware flashable and testable; no phase depends on a later one.

## 12. Risks / assumptions / open questions

- **Assumption:** PC7 HIGH = SDC OK (per requirement). *Verify against schematic* — if the feedback is through an optocoupler the sense may invert; encapsulate polarity in one define.
- **Assumption:** IVT-S U1 is wired bus-side (inverter side of AIRs), U2 availability unknown. If U1 is actually pack-side, the whole bus-voltage section needs re-sourcing — *confirm with wiring diagram before Phase 4*.
- **Open:** exact current limits (fuse curve, charger max, precharge resistor rating) — electrical team input; plan ships with conservative defaults.
- **Open:** should `bypassChecks` even exist in competition builds? Recommend a `#ifdef BENCH_BUILD` fence.
- **Risk:** `HAL_CAN` RX callbacks (precharge/balancing/charger) run in ISR context and write state consumed by the protection tick — existing pattern, single-word writes, acceptable; do not add multi-field structs written from ISR without a critical section.
- **Risk:** fault_manager single-context-per-code means two simultaneous different-slave OV events show one context (channel_mask mitigation exists for RTH only). Acceptable for policy (severity keys off the code), noted for diagnostics.
- **Known-open hardware:** S4 NTC3/NTC4 — protection must not treat their OW faults as escalation-worthy; they are in the expected-faults set until the harness is fixed.
- **Deliberate non-goal:** no RTOS, no watchdog redesign, no IMD integration (IMD latch is hardware SDC territory; software only observes via PC7).

## 13. Testing plan

Bench (LV, no pack — simulate with supplies where noted):

| # | Test | Expected |
|---|---|---|
| 1 | PC7 jumper HIGH→LOW (no HV) | ≥ 20 ms LOW → `FAULT_SDC_TRIGGERED`, AMS error latched-clearable, `printfDebug` edge log |
| 2 | PC7 glitch < 10 ms | no trip (integrator) |
| 3 | SDC LOW before precharge request | request refused, stays KILL |
| 4 | SDC LOW during cap-charge window | abort → WRONG → KILL, latched |
| 5 | SDC LOW in HV_ON | KILL + power-cycle-only; `AMS_Error_Clear()` returns false; reset via debugger does *not* count — only power removal clears |
| 6 | attempt CAN/console clear of any AMS error while HV_ON | refused |
| 7 | OV sim (bench supply on one tap or threshold lowered via config) | fault + clearable error in IDLE; same trip in BALANCING (new coverage) |
| 8 | UV sim | as 7; verify 0x8000 sentinel still maps to OPEN_WIRE not UV |
| 9 | OT sim (heat gun / resistor swap on NTC) | OT fault; S4 NTC3/4 open-wire produces RTH fault only, no OT |
| 10 | open-wire sim (pull a sense lead on bench harness) | OW fault, no phantom UV |
| 11 | IVT overcurrent (inject CAN frame with high current) | overcurrent fault, contactors commanded open in HV_ON overlay |
| 12 | IVT silence (disconnect IVT CAN) | timeout fault; in HV_ON → KILL; in IDLE → warning |
| 13 | precharge bus-not-rising (disconnect precharge resistor / no load) | rise-check abort at checkpoints, `FAULT_PRECHARGE_FAILURE` |
| 14 | precharge overcurrent (short/oversized load sim via injected IVT frames) | abort within one 100 ms check |
| 15 | bus/pack mismatch before AIR+ (inject U1 = 80 % pack) | `VERIFY_BUS_VOLT` fails, no AIR+ close |
| 16 | EEPROM invalid (corrupt checksum byte) | boot: defaults loaded, `FAULT_EEPROM_VALIDATION_ERROR` raised, thresholds = compiled defaults |
| 17 | WWDG reset during HV_ON latch | boot raises `FAULT_WATCHDOG_RESET`; document that power-cycle latch is lost on reset (accepted, §2.4) |
| 18 | IDLE-loop latch fight regression | set AMS error, run 10 s of IDLE — pin must stay in error state (kills bug 0.2 forever) |

Vehicle (with pack, scrutineering-style): full precharge happy path, SDC pull during driving-ready, charger overcurrent margin test.

---

*End of plan. No source files were modified; this document is the only artifact.*
