# CAN to Foxglove – BMS Human Interface

## Overview

This repository provides a **drop-in CAN → Foxglove solution** for interfacing with a **Battery Management System (BMS)** based on **ADBMS6822 / ADBMS6830**, using an **STM32F412** as a CAN gateway.

The goal is to expose **BMS telemetry and configuration** in a clean, human-readable way through **Foxglove Studio**, enabling:
- Live monitoring
- Parameter tuning
- Debugging
- System validation

This document is self-contained and can be used directly as a project README.

---

## Hardware & Stack

- **MCU:** STM32F412  
- **BMS ICs:** ADBMS6822, ADBMS6830  
- **Bus:** CAN (request/response model)  
- **UI:** Foxglove Studio  

---

## High-Level Architecture

ADBMS68xx  <── isoSPI ──>  STM32F412  <── CAN ──>  Foxglove

The STM32:
- Translates CAN commands into BMS register writes
- Converts raw register values into scaled physical units
- Publishes telemetry to Foxglove topics

---

## Register Scaling Model

Physical Value = (Register × LSB) + Offset

Signed registers use two’s complement.

| Register Group | Width | LSB | Offset | Unit |
|---------------|------|-----|--------|------|
| CxV, SxV, ACxV | 16 | 0.00015 | 1.5 | V |
| VUV, VOV | 12 | 0.0024 | 1.5 | V |
| VPV | 16 | 0.00375 | 37.5 | V |
| ITMP | 16 | 0.02 | −73.0 | °C |

---

## Internal Digital Filtering (IIR)

Y[n] = Y[n−1] + (X[n] − Y[n−1]) / a

| FC[2:0] | −3 dB Cutoff (Hz) | a |
|--------|------------------|---|
| 000 | Disabled | N/A |
| 001 | 110 | 2 |
| 010 | 45 | 4 |
| 011 | 21 | 8 |
| 100 | 10 | 16 |
| 101 | 5 | 32 |
| 110 | 1.25 | 128 |
| 111 | 0.625 | 256 |

---

## CAN Interface

| Type | ID |
|----|----|
| Command | 0x311 |
| Response | 0x312 |

---

## Supported Commands

| Command | Range | Default | Description |
|-------|-------|---------|------------|
| REFON | true / false | false | Enable reference regulator |
| CTH | 000–111 | 001 | ADC comparison threshold |
| COMM_BK | true / false | false | Halt isoSPI communication |
| MUTE_ST | true / false | false | Mute cell discharge |
| SNAP_ST | true / false | false | Freeze registers |
| VUV | 0x000–0xFFF | 0x800 | Undervoltage threshold |
| VOV | 0x000–0xFFF | 0x7FF | Overvoltage threshold |
| FC | 000–111 | 000 | Digital filter select |
| DCCx | true / false | false | Cell discharge control |
| RST | true / false | false | Reset configuration |

Threshold Voltage = Register × 16 × 150 µV + 1.5 V

---

## Foxglove Integration

- Live telemetry plots
- Threshold sliders
- Filter selection
- Discharge toggles
- Snapshot & reset controls

---

## Design Principles

- Deterministic CAN behavior
- Human-readable scaling
- Safe runtime configuration
- Foxglove-first UX

---

## Status

Active development. Core protocol stable.
