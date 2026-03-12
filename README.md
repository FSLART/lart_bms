# ⚡ BMS STM32F412 -- ADBMS6830 Project

## 📘 Overview

This project implements a **Battery Management System (BMS)** using the
**Analog Devices ADBMS6830** battery monitoring IC and an
**STM32F412RET6** microcontroller as the system controller.

The firmware manages a **daisy-chained stack of ADBMS6830 devices via
isoSPI**, monitors analog and CAN sensors, and exports telemetry through
**CAN bus and UART DMA**.

Main responsibilities of the system include:

-   Cell voltage and temperature monitoring
-   CAN telemetry broadcasting
-   Contactor and precharge state-machine control
-   Analog sensor acquisition via ADC + DMA
-   Fault monitoring and error reporting
-   UART telemetry for debugging / UI integration

The project is implemented in **bare-metal C using STM32 HAL**, with
modular drivers for each subsystem.

------------------------------------------------------------------------

# 🧱 System Architecture

## MCU

-   **STM32F412RET6**
-   Cortex-M4 @ **100 MHz**
-   HAL-based firmware
-   **Bare-metal scheduling (interrupt driven)**

------------------------------------------------------------------------

# 🔋 Battery Monitoring IC

### ADBMS6830

The **ADBMS6830** monitors battery cells and auxiliary channels in a
daisy-chained architecture.

Key capabilities:

-   **12 cell voltage measurements per IC**
-   **6 auxiliary inputs**
-   **isoSPI communication**
-   **PEC (CRC) validation**
-   **Open-wire detection**

The driver uses the **Analog Devices BMS library** and parses results
into structured arrays.

------------------------------------------------------------------------

# 📡 Communication Interfaces

  Interface     Purpose
  ------------- ------------------------------------
  SPI1          Communication with ADBMS6830 chain
  CAN1          Telemetry broadcast
  UART (DMA)    Debug console / telemetry output
  ADC1          Analog sensor readings
  GPIO / EXTI   Contactor feedback interrupts

------------------------------------------------------------------------

# 🧩 Firmware Modules

## 🧠 System Controller

### `brain.c`

Central firmware logic:

-   State machine management
-   Fault detection
-   Runtime counters
-   Scheduler interaction

Main BMS states:

    STARTUP
    IDLE
    BALANCING
    CHARGING
    ONMISSION
    INACTIVE

------------------------------------------------------------------------

## 🔌 Precharge & Contactor Control

### `precharge.c`

Implements the high-voltage system startup sequence.

Precharge stages:

    START
    OPEN_ALL
    SWITCH_HVNEG
    DELAY1
    VERIFY1
    SWITCH_PRECHARGE
    DELAY2
    VERIFY2
    VERIFY_CURRENT
    VERIFY_BUS_VOLT
    SWITCH_HVPOS
    DELAY3
    VERIFY3
    TURN_OFF_PRECHARGE
    DELAY4
    VERIFY4
    END

Additional states:

    KILL
    RX_CAN
    WRONG

------------------------------------------------------------------------

# 🔋 Battery Telemetry → CAN

### `adbms_to_CAN.c`

This module converts ADBMS measurements into CAN frames defined by the
AMS DBC file.

Main functions:

    ADBMS_CAN_SendVoltages_Module()
    ADBMS_CAN_SendTemperatures_Module()
    ADBMS_CAN_SendAll()

------------------------------------------------------------------------

# 🌡️ Temperature Monitoring

Temperature measurements come from two sources:

### BMS Thermistors

Measured through the **ADBMS6830 auxiliary channels**.

### MCU Internal Temperature

The STM32 internal temperature sensor is read through **ADC1**.

------------------------------------------------------------------------

# 📊 Analog Sensor Acquisition

### `analog_readings.c`

Handles **ADC measurements using DMA**.

Example inputs:

-   Current sensor
-   Voltage sense
-   MCU temperature
-   Vref reference

------------------------------------------------------------------------

# ⚙️ Contactor Feedback System

The contactor feedback pins trigger **external interrupts (EXTI)**.

Each interrupt updates a feedback counter representing the mechanical
contactor state.

This ensures:

-   Contactors physically switched
-   Wiring integrity
-   No stuck relays

------------------------------------------------------------------------

# ⏱️ Timing System

Timers are used for multiple timing tasks:

  Timer     Purpose
  --------- ---------------------
  TIM2      Microsecond delays
  TIM5      SPI timing
  SysTick   1 ms system tick
  EXTI      Feedback interrupts

------------------------------------------------------------------------

# 🧠 Error Handling

The BMS tracks system errors through an error status structure.

Examples:

    ERROR_SDC_TRIGGERED
    ERROR_IMD_TRIGGERED
    ERROR_CONTACTORS_MISMATCH
    ERROR_CAN_FAILED
    ERROR_OVERVOLTAGE
    ERROR_OVERCURRENT
    ERROR_BMS_OW
    ERROR_BMS_FAIL

------------------------------------------------------------------------

# 📂 Project Structure

    Core/
    ├── Src/
    │   ├── main.c
    │   ├── brain.c
    │   ├── precharge.c
    │   ├── contactors.c
    │   ├── can.c
    │   ├── adbms_to_CAN.c
    │   ├── analog_readings.c
    │   ├── uartDMA.c
    │   ├── mcuWrapper.c
    │   ├── serialPrintResult.c
    │   └── adBms_Application.c
    │
    ├── Inc/
    │   ├── brain.h
    │   ├── precharge.h
    │   ├── can.h
    │   ├── adbms_to_CAN.h
    │   ├── analog_readings.h
    │   └── main.h
    │
    Drivers/
    ├── STM32 HAL
    └── Analog Devices ADBMS6830 library

------------------------------------------------------------------------

# ✅ Current Status

  Feature                   Status
  ------------------------- --------------------
  ADBMS6830 Communication   ✅ Working
  isoSPI Daisy Chain        ✅ Verified
  CAN Telemetry             ✅ Operational
  Precharge Sequence        ✅ Implemented
  ADC Analog Readings       ✅ Working
  Contactor Feedback        ✅ Interrupt based
  UART DMA Debug            ✅ Working
  Fault Handling            ✅ Implemented

------------------------------------------------------------------------

# 📜 License

This firmware is developed for research and testing of **Battery
Management Systems using STM32 and Analog Devices BMS ICs**.

All rights reserved © 2026.
