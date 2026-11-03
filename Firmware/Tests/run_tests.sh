#!/bin/sh
# Testes do firmware no PC (sem hardware). Correr a partir de qualquer pasta:
#   CC=<gcc ou clang do PC> Firmware/Tests/run_tests.sh
# Precisa de um compilador para o PC, nao o arm-none-eabi. Se CC nao estiver
# definido usa o gcc do PATH.
#
# O codigo do firmware entra todo tal como esta, menos o que fala com o HAL:
# o driver da ADI e o mcuWrapper (fake_chain.c), o bsp.c, o can_bus.c e o
# uart_link.c (fake_board.c, que tambem faz de RN4871 e de ISA). O test_boot
# corre a logica do bootloader CAN (Bootloader/Src) numa flash simulada.

set -e
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_tests}"   # fora do repo: nada para ignorar no git
FLAGS="-std=gnu11 -Wall -Wextra -g -Wno-unused-parameter"
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
"$CC" $FLAGS $INC -o "$OUT/test_wrapper" Tests/test_wrapper.c Core/adbms6830/mcuWrapper.c
# o bootloader (Bootloader/Src) com a flash simulada; -O1 porque corta a energia em cada escrita
"$CC" $FLAGS -O1 $DEFS -ICore/boot -I../Bootloader/Src -o "$OUT/test_boot" Tests/test_boot.c $BOOT $BOOTLOADER

status=0
echo "== test_adbms";   "$OUT/test_adbms"   || status=1
echo "== test_system";  "$OUT/test_system"  || status=1
echo "== test_wrapper"; "$OUT/test_wrapper" || status=1
echo "== test_boot";    "$OUT/test_boot"    || status=1
# o uploader (Python) contra o bootloader num bus CAN virtual, se houver python-can e cantools
if python3 -c "import can, cantools" 2>/dev/null; then
	echo "== test_can_upload"
	CC="$CC" python3 ../Bootloader/tools/test_can_upload.py > "$OUT/test_can_upload.log" 2>&1 || { tail -30 "$OUT/test_can_upload.log"; status=1; }
	tail -3 "$OUT/test_can_upload.log"
else
	echo "== test_can_upload: sem python-can/cantools (pip install -r Bootloader/tools/requirements.txt)"
fi
exit $status
