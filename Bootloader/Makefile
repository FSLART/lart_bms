# Bootloader CAN do BMS master (STM32F412RE), 0x08000000..0x08007FFF.
#
#   make                 build/bms_bootloader.{elf,hex,bin}
#   make flash           grava com o STM32_Programmer_CLI (ST-LINK, SWD)
#   make clean
#
# No Windows: o make e o arm-none-eabi-gcc que vem com o STM32CubeIDE
# (ver BOOTLOADER.md), ou "File > New > Makefile Project with Existing Code".
# O codigo partilhado com a aplicacao esta em ../Firmware/Core/boot.

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
PROGRAMMER ?= STM32_Programmer_CLI

BUILD   := build
TARGET  := $(BUILD)/bms_bootloader
FIRMWARE := ../Firmware

SOURCES := \
	Src/startup.c Src/main.c Src/bl_hw.c Src/bl_can.c Src/bl_flash.c \
	Src/bl_core.c Src/bl_protocol.c Src/bl_log.c \
	$(FIRMWARE)/Core/boot/boot_hash.c $(FIRMWARE)/Core/boot/boot_image.c \
	$(FIRMWARE)/Core/boot/boot_journal.c $(FIRMWARE)/Core/boot/boot_shared.c \
	$(FIRMWARE)/Core/boot/boot_names.c

INCLUDES := -ISrc -I$(FIRMWARE)/Core/boot \
	-I$(FIRMWARE)/Drivers/CMSIS/Device/ST/STM32F4xx/Include -I$(FIRMWARE)/Drivers/CMSIS/Include

# sem FPU: o bootloader nao faz contas em virgula flutuante e nao precisa de a ligar
CPU     := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
CFLAGS  := $(CPU) -std=gnu11 -Os -g3 -DSTM32F412Rx -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	-fno-common $(INCLUDES)
LDFLAGS := $(CPU) -TSTM32F412RE_BOOT.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
	-Wl,--gc-sections -Wl,-Map=$(TARGET).map -Wl,--print-memory-usage

OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(SOURCES)))

all: $(TARGET).elf $(TARGET).hex $(TARGET).bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS) STM32F412RE_BOOT.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# so os setores do bootloader (S0, S1): a aplicacao e o journal ficam
flash: $(TARGET).hex
	$(PROGRAMMER) -c port=SWD -d $(TARGET).hex -v -rst

clean:
	rm -rf $(BUILD)

.PHONY: all flash clean
