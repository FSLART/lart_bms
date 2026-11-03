#!/bin/sh
# Compila o firmware para o STM32F412 sem o CubeIDE, com as mesmas flags do
# Debug do CubeIDE. Serve para verificar que tudo compila e liga (CI, outra
# maquina); o binario para gravar continua a sair do CubeIDE.
#   ARM_CC=<arm-none-eabi-gcc> Firmware/Tests/build_arm.sh
# Sources: tudo o que esta em Core/ e Drivers/ (como o CubeIDE, que compila as
# pastas inteiras).

set -e
cd "$(dirname "$0")/.."

CC="${ARM_CC:-arm-none-eabi-gcc}"
SIZE="$(dirname "$(command -v "$CC")")/arm-none-eabi-size"
OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_arm}"
CPU="-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb --specs=nano.specs"
DEFS="-DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx"
WARN="-Wall ${EXTRA_WARN:-}"
INC=""
for dir in Core/Inc Core/adbms6830 Core/DBC Core/Src Core/config Core/comms Core/app Core/boot \
	Drivers/STM32F4xx_HAL_Driver/Inc Drivers/STM32F4xx_HAL_Driver/Inc/Legacy \
	Drivers/CMSIS/Device/ST/STM32F4xx/Include Drivers/CMSIS/Include; do
	[ -d "$dir" ] && INC="$INC -I$dir"
done

rm -rf "$OUT"
mkdir -p "$OUT"
objects=""

for src in $(find Core Drivers -name '*.c' | sort); do
	obj="$OUT/$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/')"
	"$CC" "$src" $CPU -std=gnu11 -g3 $DEFS -c $INC -O0 -ffunction-sections -fdata-sections $WARN -o "$obj"
	objects="$objects $obj"
done

for src in $(find Core -name '*.s' | sort); do
	obj="$OUT/$(echo "$src" | tr '/' '_' | sed 's/\.s$/.o/')"
	"$CC" $CPU -g3 -DDEBUG -c -x assembler-with-cpp -o "$obj" "$src"
	objects="$objects $obj"
done

"$CC" -o "$OUT/lart_bms.elf" $objects $CPU -T STM32F412RETX_FLASH.ld --specs=nosys.specs \
	-Wl,-Map="$OUT/lart_bms.map" -Wl,--gc-sections -static -u _printf_float \
	-Wl,--print-memory-usage -Wl,--start-group -lc -lm -Wl,--end-group

"$SIZE" "$OUT/lart_bms.elf"
