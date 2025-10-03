################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/IVT_CAN.c \
../Core/Src/bms_cmdlist.c \
../Core/Src/bms_libWrapper.c \
../Core/Src/bms_mcuWrapper.c \
../Core/Src/bms_utility.c \
../Core/Src/ee24.c \
../Core/Src/eeprom_utils.c \
../Core/Src/main.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c \
../Core/Src/uartDMA.c \
../Core/Src/version.c 

OBJS += \
./Core/Src/IVT_CAN.o \
./Core/Src/bms_cmdlist.o \
./Core/Src/bms_libWrapper.o \
./Core/Src/bms_mcuWrapper.o \
./Core/Src/bms_utility.o \
./Core/Src/ee24.o \
./Core/Src/eeprom_utils.o \
./Core/Src/main.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o \
./Core/Src/uartDMA.o \
./Core/Src/version.o 

C_DEPS += \
./Core/Src/IVT_CAN.d \
./Core/Src/bms_cmdlist.d \
./Core/Src/bms_libWrapper.d \
./Core/Src/bms_mcuWrapper.d \
./Core/Src/bms_utility.d \
./Core/Src/ee24.d \
./Core/Src/eeprom_utils.d \
./Core/Src/main.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d \
./Core/Src/uartDMA.d \
./Core/Src/version.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/IVT_CAN.cyclo ./Core/Src/IVT_CAN.d ./Core/Src/IVT_CAN.o ./Core/Src/IVT_CAN.su ./Core/Src/bms_cmdlist.cyclo ./Core/Src/bms_cmdlist.d ./Core/Src/bms_cmdlist.o ./Core/Src/bms_cmdlist.su ./Core/Src/bms_libWrapper.cyclo ./Core/Src/bms_libWrapper.d ./Core/Src/bms_libWrapper.o ./Core/Src/bms_libWrapper.su ./Core/Src/bms_mcuWrapper.cyclo ./Core/Src/bms_mcuWrapper.d ./Core/Src/bms_mcuWrapper.o ./Core/Src/bms_mcuWrapper.su ./Core/Src/bms_utility.cyclo ./Core/Src/bms_utility.d ./Core/Src/bms_utility.o ./Core/Src/bms_utility.su ./Core/Src/ee24.cyclo ./Core/Src/ee24.d ./Core/Src/ee24.o ./Core/Src/ee24.su ./Core/Src/eeprom_utils.cyclo ./Core/Src/eeprom_utils.d ./Core/Src/eeprom_utils.o ./Core/Src/eeprom_utils.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su ./Core/Src/uartDMA.cyclo ./Core/Src/uartDMA.d ./Core/Src/uartDMA.o ./Core/Src/uartDMA.su ./Core/Src/version.cyclo ./Core/Src/version.d ./Core/Src/version.o ./Core/Src/version.su

.PHONY: clean-Core-2f-Src

