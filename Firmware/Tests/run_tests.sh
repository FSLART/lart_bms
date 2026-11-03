#!/bin/sh
# Testes do firmware no PC (sem hardware). Correr a partir de qualquer pasta:
#   CC=<gcc ou clang do PC> Firmware/Tests/run_tests.sh
# Precisa de um compilador para o PC, nao o arm-none-eabi. Se CC nao estiver
# definido usa o gcc do PATH.
#
# O codigo do firmware entra todo tal como esta, menos o que fala com o HAL:
# o driver da ADI e o mcuWrapper (fake_chain.c), o bsp.c, o can_bus.c e o
# uart_link.c (fake_board.c, que tambem faz de RN4871 e de ISA). Esses tem
# testes proprios: test_adi (driver da ADI contra uma cadeia ao nivel do SPI),
# test_wrapper (mcuWrapper) e test_drivers (HAL falso em Tests/hal). O
# test_boot corre a logica do bootloader CAN (Bootloader/Src) numa flash
# simulada; o test_boot_hw corre o bootloader inteiro, registos incluidos,
# num STM32F412 simulado (Tests/stm32_model.c).
#
# Com sanitizers: EXTRA_FLAGS="-fsanitize=address,undefined" Tests/run_tests.sh

set -e
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_tests}"   # fora do repo: nada para ignorar no git
FLAGS="-std=gnu11 -Wall -Wextra -g -Wno-unused-parameter ${EXTRA_FLAGS:-}"
INC="-ITests/stubs -ITests -ICore/Inc -ICore/adbms6830 -ICore/DBC -ICore/config -ICore/comms -ICore/app -ICore/boot"
DEFS="-DBOOT_HOST_TEST"   # a flash e a RAM do bootloader sao arrays (boot_layout.h)
BOOT="Core/boot/boot_hash.c Core/boot/boot_image.c Core/boot/boot_journal.c Core/boot/boot_shared.c Core/boot/boot_names.c"
BOOTLOADER="../Bootloader/Src/bl_core.c ../Bootloader/Src/bl_protocol.c ../Bootloader/Src/bl_log.c"

APP="Core/Src/brain.c \
	Core/adbms6830/adBms_Application.c Core/adbms6830/adbms_cell_read.c Core/adbms6830/adbms_temp_read.c \
	Core/adbms6830/adbms_open_wire.c Core/adbms6830/adbms_balancing.c \
	Core/app/faults.c Core/app/ams_error.c Core/app/contactors.c Core/app/precharge.c Core/app/charger.c \
	Core/app/isa.c Core/app/soc.c Core/app/fans.c Core/app/board.c Core/app/boot_ctrl.c \
	Core/config/bms_config.c Core/config/config_store.c \
	Core/comms/can_messages.c Core/comms/rn4871.c Core/comms/console.c Core/comms/live_json.c \
	Core/DBC/powertrain_t26.c Core/DBC/handcart_t26.c \
	Tests/fake_chain.c Tests/fake_board.c $BOOT"

mkdir -p "$OUT"

"$CC" $FLAGS $DEFS $INC -o "$OUT/test_adbms" Tests/test_adbms.c $APP -lm
"$CC" $FLAGS $DEFS $INC -o "$OUT/test_system" Tests/test_system.c $APP -lm
"$CC" $FLAGS $DEFS $INC -o "$OUT/test_config" Tests/test_config.c $APP -lm
"$CC" $FLAGS $DEFS $INC -o "$OUT/test_comms" Tests/test_comms.c $APP -lm
"$CC" $FLAGS $DEFS $INC -o "$OUT/test_modules" Tests/test_modules.c $APP -lm
"$CC" $FLAGS $INC -o "$OUT/test_wrapper" Tests/test_wrapper.c Core/adbms6830/mcuWrapper.c
# o driver da ADI (PEC, registos, comandos) contra uma cadeia de ADBMS6830 ao nivel do SPI
"$CC" $FLAGS $INC -o "$OUT/test_adi" Tests/test_adi.c Tests/fake_spi_chain.c \
	Core/adbms6830/adBms6830GenericType.c Core/adbms6830/adBms6830ParseCreate.c
# o bootloader (Bootloader/Src) com a flash simulada; -O1 porque corta a energia em cada escrita (antes das
# FLAGS: o -O0 do coverage.sh ganha, senao o gcov conta mal as linhas)
"$CC" -O1 $FLAGS $DEFS -ICore/boot -I../Bootloader/Src -o "$OUT/test_boot" Tests/test_boot.c $BOOT $BOOTLOADER
# o bootloader inteiro (startup, main, bl_hw, bl_can, bl_flash) num STM32F412 simulado (Tests/stm32_model.c,
# so x86-64 Linux; nos outros salta). O _edata do PC e do linker: o startup.c usa outro nome
"$CC" $FLAGS -DSTM32F412Rx -Dmain=bootloader_main -D_edata=stm32_edata -ITests/stm32 -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
	-ICore/boot -I../Bootloader/Src -ITests -o "$OUT/test_boot_hw" Tests/test_boot_hw.c Tests/stm32_model.c \
	../Bootloader/Src/startup.c ../Bootloader/Src/main.c ../Bootloader/Src/bl_hw.c ../Bootloader/Src/bl_can.c ../Bootloader/Src/bl_flash.c \
	$BOOT $BOOTLOADER
# os drivers do HAL (can_bus, uart_link, bsp) com o main.h verdadeiro e um HAL falso (Tests/hal)
"$CC" $FLAGS -ITests/hal -ITests -ICore/Inc -ICore/comms -ICore/app -ICore/boot -ICore/config -ICore/DBC -ICore/adbms6830 \
	-o "$OUT/test_drivers" Tests/test_drivers.c Tests/fake_hal.c Core/comms/can_bus.c Core/comms/uart_link.c Core/app/bsp.c

status=0
echo "== test_adbms";   "$OUT/test_adbms"   || status=1
echo "== test_system";  "$OUT/test_system"  || status=1
echo "== test_config";  "$OUT/test_config"  || status=1
echo "== test_comms";   "$OUT/test_comms"   || status=1
echo "== test_modules"; "$OUT/test_modules" || status=1
echo "== test_wrapper"; "$OUT/test_wrapper" || status=1
echo "== test_adi";     "$OUT/test_adi"     || status=1
echo "== test_boot";    "$OUT/test_boot"    || status=1
echo "== test_boot_hw"; "$OUT/test_boot_hw" > "$OUT/test_boot_hw.log" 2>&1 || { cat "$OUT/test_boot_hw.log"; status=1; }
tail -1 "$OUT/test_boot_hw.log"
echo "== test_drivers"; "$OUT/test_drivers" || status=1
# o uploader (Python) contra o bootloader num bus CAN virtual, se houver python-can e cantools
if python3 -c "import can, cantools" 2>/dev/null; then
	echo "== test_can_upload"
	CC="$CC" python3 ../Bootloader/tools/test_can_upload.py > "$OUT/test_can_upload.log" 2>&1 || { tail -30 "$OUT/test_can_upload.log"; status=1; }
	tail -3 "$OUT/test_can_upload.log"
else
	echo "== test_can_upload: sem python-can/cantools (pip install -r Bootloader/tools/requirements.txt)"
fi
exit $status
