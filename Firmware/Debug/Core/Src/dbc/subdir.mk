################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/dbc/powertrain_t26.c 

OBJS += \
./Core/Src/dbc/powertrain_t26.o 

C_DEPS += \
./Core/Src/dbc/powertrain_t26.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/dbc/%.o Core/Src/dbc/%.su Core/Src/dbc/%.cyclo: ../Core/Src/dbc/%.c Core/Src/dbc/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -c -I../Core/Inc -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc/dbc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Src" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Inc/adbms" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Firmware/Core/Src/adbms" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-dbc

clean-Core-2f-Src-2f-dbc:
	-$(RM) ./Core/Src/dbc/powertrain_t26.cyclo ./Core/Src/dbc/powertrain_t26.d ./Core/Src/dbc/powertrain_t26.o ./Core/Src/dbc/powertrain_t26.su

.PHONY: clean-Core-2f-Src-2f-dbc

