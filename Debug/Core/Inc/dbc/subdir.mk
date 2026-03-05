################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Inc/dbc/ams.c 

OBJS += \
./Core/Inc/dbc/ams.o 

C_DEPS += \
./Core/Inc/dbc/ams.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Inc/dbc/%.o Core/Inc/dbc/%.su Core/Inc/dbc/%.cyclo: ../Core/Inc/dbc/%.c Core/Inc/dbc/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -c -I../Core/Inc -I"C:/Users/jpser/Documents/GitHub/lart_bms/Core/Inc/dbc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Core/Inc" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Core/Src" -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jpser/Documents/GitHub/lart_bms/Core/Inc/adbms" -I"C:/Users/jpser/Documents/GitHub/lart_bms/Core/Src/adbms" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Inc-2f-dbc

clean-Core-2f-Inc-2f-dbc:
	-$(RM) ./Core/Inc/dbc/ams.cyclo ./Core/Inc/dbc/ams.d ./Core/Inc/dbc/ams.o ./Core/Inc/dbc/ams.su

.PHONY: clean-Core-2f-Inc-2f-dbc

