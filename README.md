# ⚡ BMS STM32F412 – ADBMS6822 / ADBMS6830 Project

## 📘 Overview

This project implements a **Battery Management System (BMS)** using the **Analog Devices ADBMS6822** as the main interface IC and an **STM32F412RET6** microcontroller as the host controller.  
The system manages daisy-chained **ADBMS6830** devices over **isoSPI**, monitors an **IVT-s current/voltage sensor** via **CAN**, and communicates with a **Node-RED dashboard** or other UIs through **UART (DMA)** and **Bluetooth**.

In addition to cell voltage and temperature monitoring, the system integrates:
- Contactor and precharge control logic
- PID-based fan management for temperature regulation
- RTC timestamping and on-chip MCU temperature sensing
- EEPROM-based configuration storage
- Multi-threaded (interrupt-driven) timing and open-wire diagnostics

---

## 🧱 System Architecture

### MCU
- **STM32F412RET6**
- **Core clock:** 100 MHz  
- **Firmware:** HAL-based, modular C implementation  
- **RTOS:** None (bare-metal cooperative scheduling via timers)

### ICs & Peripherals
| Peripheral | Function | Interface | Notes |
|-------------|-----------|------------|-------|
| **ADBMS6822** | Main BMS interface | SPI1 (2 Mbps) | Controls isoSPI daisy chain |
| **ADBMS6830** | Stack monitoring ICs | isoSPI | Voltage & temperature sensing |
| **EEPROM (24xx)** | Configuration storage | I²C1 | RTOS-safe delay and locking |
| **IVT-s Sensor** | Current, voltage, power monitoring | CAN1 | 11-bit classic CAN messages |
| **Charger Interface** | External charger comms | CAN2 | 125 Kbps |
| **Node-RED UI / Data Stream** | Telemetry + JSON dashboard | USART1 (DMA, 230400 baud) | JSON structured stream |
| **Bluetooth / Secondary UART** | Optional secondary interface | USART2 (DMA) | Uses `printfDmaBT()` |
| **RTC (LSE)** | Real-time timestamping | RTC | via `time_rtc.c` |
| **MCU Temp Sensor** | Internal junction monitor | ADC1 | via `temperatures.c` |
| **Contactors** | High-voltage relay control | GPIO | via `contactors.c` |
| **Cooling Fans** | PID-regulated thermal management | GPIO / PWM | via `fan_management.c` |

---

## ⚙️ Hardware & Pin Mapping (Summary)

| Function | Peripheral | Frequency / Baud | Notes |
|-----------|-------------|------------------|-------|
| SPI1 | BMS Interface (ADBMS6822) | 2 Mbps | CPOL=1, CPHA=1 |
| CAN1 | IVT-s Sensor (Powertrain bus) | 1 Mbps | Filters U1, U2, U3, I, T, W |
| CAN2 | Charger interface | 125 kbps | 11-bit ID frames |
| I²C1 | EEPROM | 400 kHz | 24xx-compatible |
| USART1 | Node-RED UART stream | 230400 baud (DMA) | JSON packets |
| USART2 | Bluetooth / secondary debug | 115200 baud (DMA) | Optional |
| TIM2 / TIM5 | Delay timers | µs precision | Used in `bms_delayUs()` |
| TIM6 | Open-wire wait timer | ms precision | Used in `OW_StartWaitMs()` |
| TIM8 | Periodic task scheduler | 800 ms interrupt | Main loop tick |
| TIM10 / TIM11 | Fault checks & UI updates | Configurable | Controlled in `main.c` |
| PC13 | User button | EXTI15_10 | State toggle interrupt |

---

## 🚀 Core Features

### 🔌 BMS Core (ADBMS6822 / 6830)
- SPI Mode 3 communication (2 Mbps)
- Continuous & redundant ADC conversions
- PEC (CRC10/15) validation and error handling
- Auto wake-up and open-wire checks
- Voltage, segment, and temperature parsing
- Per-module statistics (min, max, delta)

### 🧠 MCU Utilities
- Microsecond delay functions via TIM2/TIM5
- Safe SPI wake-up and manual CS control
- RTC time and date retrieval
- MCU junction temperature measurement
- EEPROM read/write with safety delay and lock

### 🧩 System Control
- Contactor logic (precharge, AIR+, AIR−, discharge)
- Fan speed control via PID loop (`fan_management.c`)
- Configurable state machine:
  - `STARTUP`
  - `IDLE`
  - `BALANCING`
  - `CHARGING`
  - `ONMISSION`
  - `INACTIVE`
- Fault check and UI update handled via periodic interrupts

### 📡 Communication
- **CAN1 (1 Mbps):** IVT-s sensor (U1, U2, U3, I, T, W)
- **CAN2 (125 kbps):** Charger bus
- **UART1 (DMA):** JSON stream for Node-RED dashboard
- **UART2 (DMA):** Bluetooth/alternate console output
- **EEPROM I²C:** Persistent configuration and data logs

### 🧮 PID-Based Fan Control
- `PID_Init()`, `PID_Update()`, and `PID_FromZieglerNichols()` for adaptive fan speed
- Anti-windup and NaN-safe logic
- Supports auto-tuning from Ziegler–Nichols parameters

### 🌡️ Temperature Management
- BMS IC thermistors (`bms_parseAuxVoltage`)
- Segment and IC-level temperature capture
- Internal MCU junction temperature via ADC
- Real-time fan response and logging

### ⚙️ Contactor & Precharge System
- Software-controlled relays for precharge, AIR+, AIR−, discharge
- Safe startup and shutdown logic (`OpenAllContactors()`)

### 🕐 Real-Time Clock
- Human-readable timestamps (`YYYY-MM-DD HH:MM:SS`)
- Used for logs, diagnostics, and session tracking

### 🧾 Startup Info (UI)
- On boot, firmware sends a JSON payload:
  ```json
  [{
    "startui": {
      "software": {"codename": "bms_lart", "version": "alpha-v2"},
      "hardware": {"version": "????", "codename": "chicote"},
      "project_link": "github.com/EsTaNG9/lart_bms"
    }
  }]
  ```
- Used by Node-RED / dashboard clients to auto-sync firmware state

---

## 🧪 Operation Flow (Main Loop)

1. **Wake-up & Initialization**
   - Initialize peripherals, start timers and interrupts.
   - Wake up all ADBMS ICs and send configuration frames.

2. **State Machine Execution**
   - Each state handles measurement, balancing, charging, and telemetry independently.

3. **Cell Measurement**
   ```c
   bms_wakeupChain();
   bms_startAdcvCont();
   bms_delayMsActive(12);
   bms_readAvgCellVoltage();
   bms_getAuxMeasurement();
   ```

4. **Temperature + Segment Sensing**
   - Measured through `bms_getAuxVoltage()` and stored in structured arrays.

5. **Data Streaming**
   - Formatted via `printfDma()` into UART DMA buffer.
   - Transmitted to Node-RED or serial terminal.

6. **Fault Checking & UI Update**
   - Controlled by periodic flags set in `TIM8` and `TIM10` interrupts.

---

## 🗃️ Node-RED / JSON Integration

The firmware emits structured JSON packets through UART for real-time monitoring.  
Example:
```json
{
  "dieTemp": 24.5,
  "SegVoltage": 37.47,
  "rth_temps": {"ic": 1, "temps": [23.4, 24.1, 25.0]},
  "v_avgCell_min": 3.715,
  "v_avgCell_max": 3.728
}
```

Node-RED dashboard includes:
- Voltage & temperature gauges
- Per-cell charts
- Fault & PEC indicators
- CAN + relay state monitors

To import: **Node-RED → Import → Clipboard → paste `flows.json`**

---

## ⏱️ Timing Overview

| Timer | Purpose | Period | Module |
|-------|----------|---------|--------|
| TIM2 | µs delay | Variable | `bms_mcuWrapper.c` |
| TIM5 | µs delay / secondary timer | Variable | `bms_mcuWrapper.c` |
| TIM6 | Open-wire wait | Dynamic (ms) | `bms_mcuWrapper.c` |
| TIM8 | Main periodic scheduler | 800 ms | `main.c` |
| TIM10 | Fault checks | Configurable | `main.c` |
| TIM11 | UI updates | Configurable | `main.c` |

---

## 🧩 Project Structure

```
├── Core/
│   ├── Src/
│   │   ├── main.c
│   │   ├── bms_libWrapper.c
│   │   ├── bms_mcuWrapper.c
│   │   ├── isa_ivt-s.c
│   │   ├── contactors.c
│   │   ├── fan_management.c
│   │   ├── time_rtc.c
│   │   ├── temperatures.c
│   │   ├── uartDMA.c
│   │   └── version.c
│   └── Inc/
│       └── headers and datatypes
├── NodeRED/
│   └── flows.json
└── README.md
```

---

## 🧰 Build & Configuration Notes

- **Compiler flags:** add `-u _printf_float` to enable float printing via `printf`.
- Ensure **SPI1 clock** supports ~2 Mbps by adjusting APB2 prescaler.
- Verify **TIM base frequencies** (64 MHz reference).
- **EEPROM address** default: `0xA0`.
- **UART1 / UART2 DMA** must be properly linked in `stm32f4xx_it.c`.

---

## ✅ Status

| Feature | Status |
|----------|--------|
| ADBMS6822 SPI Comm | ✅ Verified |
| isoSPI Daisy Chain | ✅ Verified |
| PEC (CRC10/CRC15) | ✅ Verified |
| IVT-s CAN Interface | ✅ Stable |
| EEPROM Storage | ✅ Working |
| UART1 DMA Stream | ✅ Operational |
| Bluetooth UART2 | ✅ Optional |
| Contactor Control | ✅ Tested |
| PID Fan Control | ✅ Tunable |
| RTC & MCU Temp | ✅ Functional |
| Node-RED Dashboard | ✅ Ready |

---

## 📜 License

This firmware is developed for internal research and testing of **BMS communication and safety control** on STM32F4-series microcontrollers.  
All rights reserved © 2025.
