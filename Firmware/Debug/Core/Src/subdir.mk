################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/adBms_Application.c \
../Core/Src/adbms_to_CAN.c \
../Core/Src/analog_readings.c \
../Core/Src/bms_eeprom_config.c \
../Core/Src/bootloader_jumper.c \
../Core/Src/brain.c \
../Core/Src/can.c \
../Core/Src/cell_balancing.c \
../Core/Src/charger.c \
../Core/Src/contactors.c \
../Core/Src/ee24.c \
../Core/Src/fan_management.c \
../Core/Src/fault_manager.c \
../Core/Src/gpio_expander.c \
../Core/Src/isa_ivt-s.c \
../Core/Src/main.c \
../Core/Src/master_to_CAN.c \
../Core/Src/mcuWrapper.c \
../Core/Src/precharge.c \
../Core/Src/serialPrintResult.c \
../Core/Src/soc.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c \
../Core/Src/uartDMA.c 

OBJS += \
./Core/Src/adBms_Application.o \
./Core/Src/adbms_to_CAN.o \
./Core/Src/analog_readings.o \
./Core/Src/bms_eeprom_config.o \
./Core/Src/bootloader_jumper.o \
./Core/Src/brain.o \
./Core/Src/can.o \
./Core/Src/cell_balancing.o \
./Core/Src/charger.o \
./Core/Src/contactors.o \
./Core/Src/ee24.o \
./Core/Src/fan_management.o \
./Core/Src/fault_manager.o \
./Core/Src/gpio_expander.o \
./Core/Src/isa_ivt-s.o \
./Core/Src/main.o \
./Core/Src/master_to_CAN.o \
./Core/Src/mcuWrapper.o \
./Core/Src/precharge.o \
./Core/Src/serialPrintResult.o \
./Core/Src/soc.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o \
./Core/Src/uartDMA.o 

C_DEPS += \
./Core/Src/adBms_Application.d \
./Core/Src/adbms_to_CAN.d \
./Core/Src/analog_readings.d \
./Core/Src/bms_eeprom_config.d \
./Core/Src/bootloader_jumper.d \
./Core/Src/brain.d \
./Core/Src/can.d \
./Core/Src/cell_balancing.d \
./Core/Src/charger.d \
./Core/Src/contactors.d \
./Core/Src/ee24.d \
./Core/Src/fan_management.d \
./Core/Src/fault_manager.d \
./Core/Src/gpio_expander.d \
./Core/Src/isa_ivt-s.d \
./Core/Src/main.d \
./Core/Src/master_to_CAN.d \
./Core/Src/mcuWrapper.d \
./Core/Src/precharge.d \
./Core/Src/serialPrintResult.d \
./Core/Src/soc.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d \
./Core/Src/uartDMA.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -c -I../Core/Inc -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc/dbc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Src" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc/adbms" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Src/adbms" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/adBms_Application.cyclo ./Core/Src/adBms_Application.d ./Core/Src/adBms_Application.o ./Core/Src/adBms_Application.su ./Core/Src/adbms_to_CAN.cyclo ./Core/Src/adbms_to_CAN.d ./Core/Src/adbms_to_CAN.o ./Core/Src/adbms_to_CAN.su ./Core/Src/analog_readings.cyclo ./Core/Src/analog_readings.d ./Core/Src/analog_readings.o ./Core/Src/analog_readings.su ./Core/Src/bms_eeprom_config.cyclo ./Core/Src/bms_eeprom_config.d ./Core/Src/bms_eeprom_config.o ./Core/Src/bms_eeprom_config.su ./Core/Src/bootloader_jumper.cyclo ./Core/Src/bootloader_jumper.d ./Core/Src/bootloader_jumper.o ./Core/Src/bootloader_jumper.su ./Core/Src/brain.cyclo ./Core/Src/brain.d ./Core/Src/brain.o ./Core/Src/brain.su ./Core/Src/can.cyclo ./Core/Src/can.d ./Core/Src/can.o ./Core/Src/can.su ./Core/Src/cell_balancing.cyclo ./Core/Src/cell_balancing.d ./Core/Src/cell_balancing.o ./Core/Src/cell_balancing.su ./Core/Src/charger.cyclo ./Core/Src/charger.d ./Core/Src/charger.o ./Core/Src/charger.su ./Core/Src/contactors.cyclo ./Core/Src/contactors.d ./Core/Src/contactors.o ./Core/Src/contactors.su ./Core/Src/ee24.cyclo ./Core/Src/ee24.d ./Core/Src/ee24.o ./Core/Src/ee24.su ./Core/Src/fan_management.cyclo ./Core/Src/fan_management.d ./Core/Src/fan_management.o ./Core/Src/fan_management.su ./Core/Src/fault_manager.cyclo ./Core/Src/fault_manager.d ./Core/Src/fault_manager.o ./Core/Src/fault_manager.su ./Core/Src/gpio_expander.cyclo ./Core/Src/gpio_expander.d ./Core/Src/gpio_expander.o ./Core/Src/gpio_expander.su ./Core/Src/isa_ivt-s.cyclo ./Core/Src/isa_ivt-s.d ./Core/Src/isa_ivt-s.o ./Core/Src/isa_ivt-s.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/master_to_CAN.cyclo ./Core/Src/master_to_CAN.d ./Core/Src/master_to_CAN.o ./Core/Src/master_to_CAN.su ./Core/Src/mcuWrapper.cyclo ./Core/Src/mcuWrapper.d ./Core/Src/mcuWrapper.o ./Core/Src/mcuWrapper.su ./Core/Src/precharge.cyclo ./Core/Src/precharge.d ./Core/Src/precharge.o ./Core/Src/precharge.su ./Core/Src/serialPrintResult.cyclo ./Core/Src/serialPrintResult.d ./Core/Src/serialPrintResult.o ./Core/Src/serialPrintResult.su ./Core/Src/soc.cyclo ./Core/Src/soc.d ./Core/Src/soc.o ./Core/Src/soc.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su ./Core/Src/uartDMA.cyclo ./Core/Src/uartDMA.d ./Core/Src/uartDMA.o ./Core/Src/uartDMA.su

.PHONY: clean-Core-2f-Src

