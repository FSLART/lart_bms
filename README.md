# BMS STM32F412 - ADBMS6822 - ADBMS6830 Project

## 📘 Overview

This project implements a Battery Management System (BMS) interface using the **Analog Devices ADBMS6822** IC, with the **STM32F412RET** MCU.  
It handles communication with the daisy-chained ADBMS6830 over **isoSPI**, retrieves battery monitoring data, manages EEPROM over I²C, monitors IVT sensor data via **Classic CAN**, and streams measurements to a **Node-RED dashboard** via UART.

The system is designed to:
- Communicate with a daisy-chained stack of ADBMS6830 devices.
- Perform wake-up, configuration, and data readout cycles.
- Handle CAN communication with an IVT sensor and charger.
- Store configuration or log data in an external EEPROM (via I²C).
- Use periodic timers for scheduled tasks and delay measurements.
- Stream data over UART for real-time dashboards.

---

## 🛠️ Hardware Setup

- **MCU:** STM32F412RETx
- **Communication:**
  - **SPI1** – Communication with ADBMS6822 (2 Mbps)
  - **CAN1** – Communication with POWERTRAIN network (1 Mbps)
  - **CAN2** – Communication with Battery Charger (125 Kbps)
  - **I²C** – EEPROM (24xx series)
  - **USART1 (DMA)** – Data stream to Node-RED (230400 baud)
- **Peripherals:**
  - **TIM2, TIM5** – microsecond timers for delay functions
  - **TIM8** – periodic interrupt every 800 ms
  - **GPIO PC13** – user push button (external interrupt)

---

## 📦 Features

- ✅ SPI communication with ADBMS6822 (includes wake-up and PEC handling)
- ✅ Classic CAN communication (bxCAN)
- ✅ EEPROM driver with RTOS-aware delay and locking
- ✅ UART streaming to Node-RED dashboards
- ✅ External interrupt handling for user button
- ✅ Multiple periodic timers
- ✅ CRC10/CRC15 calculation for PEC integrity checks

---

## 🌀 SPI Communication (ADBMS6822)

- **Baud rate:** 2 Mbps  
- **Mode:** SPI Mode 3 (CPOL = 1, CPHA = 1)  
- **NSS:** Controlled manually via GPIO (software chip select)  

Wake-up example:

```c
void bms_wakeupChain(void) {
    for (uint8_t ic = 0; ic < TOTAL_IC; ic++) {
        bms_csLow();
        HAL_Delay(1);
        bms_csHigh();
        HAL_Delay(1);
    }
}
```

### 🛠️ Tips for avoiding PEC errors:
- Verify SPI mode and timing match ADBMS6822 datasheet.  
- Ensure CS toggling meets wake-up timing specs.  
- Validate CRC10/CRC15 functions.  
- Confirm delay timers (`bms_delayUs`, `bms_delayMsActive`) are accurate.  

---

## ⏱️ Timers

| Timer | Purpose                 | Interval    |
|-------|-------------------------|-------------|
| TIM2  | Delay functions (µs)    | µs scale    |
| TIM5  | Delay / wake-up counter | µs scale    |
| TIM8  | Periodic task scheduler | 800 ms      |

Example configuration:

```c
htim8.Instance = TIM8;
htim8.Init.Prescaler = 63999;  // 64 MHz / 64000 = 1 kHz
htim8.Init.Period = 799;       // 1 kHz / 800 = 1.25 Hz (~800 ms)
HAL_TIM_Base_Start_IT(&htim8);
```

---

## 🧠 External Interrupt (User Button)

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_13) {
        bmsState = (bmsState == ACTIVE) ? INACTIVE : ACTIVE;
    }
}
```

➡ Make sure **EXTI15_10_IRQn** is enabled in `stm32f4xx_it.c`.

---

## 🗃️ EEPROM (24xx Series)

EEPROM driver initialization example:

```c
EE24_HandleTypeDef ee24;
EE24_Init(&ee24, &hi2c1, 0xA0);
```

Supports both bare-metal and RTOS modes, with built-in lock and delay handling.

---

## 📊 Node-RED Integration via UART

The firmware streams data to Node-RED over **USART1 (DMA)** at **230400 baud**.  
Data is sent as JSON-formatted strings and can be parsed directly in Node-RED dashboards.

Example payload:

```json
{
  "dieTemp": 24.5,
  "SegVoltage": 37.47,
  "rth_temps": { "ic": 1, "temps": [23.4, 24.1, 25.0] }
}
```

The included **`flows.json`** file provides a prebuilt Node-RED dashboard with:
- Real-time gauges for temperatures and voltages
- Per-cell voltage tables
- Line charts for temperature sensors
- Fault and CAN status indicators
- Debug console for raw UART logs

👉 To use: Import `flows.json` into Node-RED via **Import > Clipboard**.

---

## 🧪 Debugging PEC Errors

If you see:

```
WARNING! PEC ERROR - IC: 1, IC1: 0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, CC: 61
```

✔ Check SPI polarity (CPOL=1) and phase (CPHA=1)  
✔ Verify manual CS timing between transactions  
✔ Validate CRC10/CRC15 calculation in `bms_utility.c`  
✔ Ensure correct wake-up delay (use `bms_delayUs()`)  

---

## 📁 Project Structure

---

## 🧰 Build Notes

- Enable float support for `printf`: add **`-u _printf_float`** to linker flags.  
- Ensure SPI runs near 2 Mbps by adjusting APB2 clock and SPI prescaler.

---

## ✅ Status

- [x] SPI communication with ADBMS6822 verified  
- [x] PEC (CRC10/CRC15) verified  
- [x] CAN communication functional  
- [x] EEPROM read/write functional  
- [x] UART communication with Node-RED operational  
- [x] Timers and EXTI working  

---

## 📜 License

This project is intended for internal development and testing of BMS communication firmware using STM32F4 microcontrollers.
