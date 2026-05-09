# BMS STM32F412 + ADBMS6830

![Master V2 PCB](PCB/master_v2.png)

Simple Battery Management System firmware for an STM32F412 master board using Analog Devices ADBMS6830 battery-monitor ICs.

The project reads cell voltages, temperatures, current sensor data and contactor feedback, then sends the important information over CAN to the Powertrain Bus.

## Main hardware

- STM32F412RET6 microcontroller
- ADBMS6830 battery monitoring ICs
- isoSPI daisy chain for the BMS slaves
- CAN bus for telemetry and commands
- External EEPROM for saved configuration
- IVT-S current / voltage sensor
- Precharge, AIR+, AIR- and discharge contactors
- PWM fan output

## Main features

- Cell voltage measurement
- Thermistor temperature measurement
- Open-wire detection
- Passive cell balancing
- Precharge state machine
- Contactor feedback service
- CAN telemetry using `powertrain_t26.dbc`
- Fault manager with active fault tracking
- SOC estimate using cell voltage and IVT-S ampere-second counter
- UART debug output using DMA
- Watchdog reset detection
- CAN bootloader jump command

## Important firmware modules

| File | Purpose |
| --- | --- |
| `main.c` | STM32 HAL startup and peripheral initialization |
| `brain.c` | Main BMS state machine |
| `adBms_Application.c` | ADBMS6830 measurement and balancing flow |
| `adbms_to_CAN.c` | Sends slave voltage, temperature and status data over CAN |
| `master_to_CAN.c` | Sends master board status, faults and contactor data over CAN |
| `precharge.c` | Controls the precharge sequence and contactors |
| `contactors.c` | Simple open / close functions for contactors |
| `cell_balancing.c` | Passive balancing logic |
| `analog_readings.c` | ADC + DMA readings for MCU temperature, VREF and on board current sensor |
| `isa_ivt-s.c` | IVT-S CAN sensor configuration and decoding |
| `soc.c` | SOC estimation |
| `fault_manager.c` | Fault storage, history and CAN fault reporting |
| `can.c` | CAN RX callbacks and TX queue |
| `fan_management.c` | Temperature-based PWM fan control |
| `bms_eeprom_config.c` | EEPROM configuration storage |
| `bootloader_jumper.c` | Jump to STM32 system bootloader over CAN command |
| `uartDMA.c` | UART debug / UI output using DMA |

## BMS states

The main state machine is defined in `brain.h`:

```c
BALANCING
CHARGING
IDLE
ONMISSION
STARTUP
FAULT
```

## Basic firmware flow

1. `main.c` initializes the STM32 peripherals.
2. `brain_start()` initializes CAN, EEPROM, fan control, analog readings, IVT-S, precharge and fault handling.
3. `brain_loop()` runs forever.
4. Depending on the BMS state, the firmware reads the ADBMS6830 chain, balances cells, checks faults and sends CAN messages.
5. If a serious fault happens, the system can open the contactors and move to a safe state.

## CAN communication

The firmware uses the `powertrain_t26.dbc` file.

CAN is used for:

- Slave cell voltages
- Slave temperatures
- Module status
- Master board status
- Fault reporting
- IVT-S sensor readings
- Precharge commands
- Cell balancing commands
- Bootloader jump command

## Precharge sequence

The precharge module controls the high-voltage startup sequence:

1. Open all contactors
2. Close AIR-
3. Close precharge contactor
4. Check current and bus voltage
5. Close AIR+
6. Open precharge contactor
7. Keep checking contactor feedback

If something is wrong, the firmware opens the contactors and enters a safe state.

## Cell balancing

Balancing is passive and controlled through the ADBMS6830 discharge outputs.

The firmware:

- Finds the lowest valid cell voltage in the pack
- Compares every cell against that minimum
- Enables discharge on cells that are too high
- Stops balancing when the cells are inside the configured deadband

## Fault handling

The fault manager tracks active faults and stores context such as:

- Time of the fault
- Measured value
- Threshold value
- Slave index
- Cell index
- CAN channel
- Contactor mismatch bits

Faults are also sent over CAN so they can be shown in the dashboard.

## Build target

This project is made for STM32CubeIDE / STM32 HAL and targets:

```text
STM32F412RET6
```
