################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/adbms6830/adBms6830GenericType.c \
../Core/adbms6830/adBms6830ParseCreate.c \
../Core/adbms6830/adBms_Application.c \
../Core/adbms6830/mcuWrapper.c \
../Core/adbms6830/serialPrintResult.c 

OBJS += \
./Core/adbms6830/adBms6830GenericType.o \
./Core/adbms6830/adBms6830ParseCreate.o \
./Core/adbms6830/adBms_Application.o \
./Core/adbms6830/mcuWrapper.o \
./Core/adbms6830/serialPrintResult.o 

C_DEPS += \
./Core/adbms6830/adBms6830GenericType.d \
./Core/adbms6830/adBms6830ParseCreate.d \
./Core/adbms6830/adBms_Application.d \
./Core/adbms6830/mcuWrapper.d \
./Core/adbms6830/serialPrintResult.d 


# Each subdirectory must supply rules for building sources it contributes
Core/adbms6830/%.o Core/adbms6830/%.su Core/adbms6830/%.cyclo: ../Core/adbms6830/%.c Core/adbms6830/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -c -I../Core/Inc -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/DBC" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Src" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-adbms6830

clean-Core-2f-adbms6830:
	-$(RM) ./Core/adbms6830/adBms6830GenericType.cyclo ./Core/adbms6830/adBms6830GenericType.d ./Core/adbms6830/adBms6830GenericType.o ./Core/adbms6830/adBms6830GenericType.su ./Core/adbms6830/adBms6830ParseCreate.cyclo ./Core/adbms6830/adBms6830ParseCreate.d ./Core/adbms6830/adBms6830ParseCreate.o ./Core/adbms6830/adBms6830ParseCreate.su ./Core/adbms6830/adBms_Application.cyclo ./Core/adbms6830/adBms_Application.d ./Core/adbms6830/adBms_Application.o ./Core/adbms6830/adBms_Application.su ./Core/adbms6830/mcuWrapper.cyclo ./Core/adbms6830/mcuWrapper.d ./Core/adbms6830/mcuWrapper.o ./Core/adbms6830/mcuWrapper.su ./Core/adbms6830/serialPrintResult.cyclo ./Core/adbms6830/serialPrintResult.d ./Core/adbms6830/serialPrintResult.o ./Core/adbms6830/serialPrintResult.su

.PHONY: clean-Core-2f-adbms6830

