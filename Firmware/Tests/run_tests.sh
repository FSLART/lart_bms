#!/bin/sh
# Testes do firmware no PC (sem hardware). Correr a partir de qualquer pasta:
#   CC=<gcc ou clang do PC> Firmware/Tests/run_tests.sh
# Precisa de um compilador para o PC, nao o arm-none-eabi. Se CC nao estiver
# definido usa o gcc do PATH.
#
# O codigo do firmware entra todo tal como esta, menos o que fala com o HAL:
# o driver da ADI e o mcuWrapper (fake_chain.c), o bsp.c, o can_bus.c e o
# uart_link.c (fake_board.c, que tambem faz de RN4871 e de ISA).

set -e
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_tests}"   # fora do repo: nada para ignorar no git
FLAGS="-std=gnu11 -Wall -Wextra -g -Wno-unused-parameter"
INC="-ITests/stubs -ITests -ICore/Inc -ICore/adbms6830 -ICore/DBC -ICore/config -ICore/comms -ICore/app"

APP="Core/Src/brain.c \
	Core/adbms6830/adBms_Application.c Core/adbms6830/adbms_cell_read.c Core/adbms6830/adbms_temp_read.c \
	Core/adbms6830/adbms_open_wire.c Core/adbms6830/adbms_balancing.c \
	Core/app/faults.c Core/app/ams_error.c Core/app/contactors.c Core/app/precharge.c Core/app/charger.c \
	Core/app/isa.c Core/app/soc.c Core/app/fans.c Core/app/board.c \
	Core/config/bms_config.c Core/config/config_store.c \
	Core/comms/can_messages.c Core/comms/rn4871.c Core/comms/console.c Core/comms/live_json.c \
	Core/DBC/powertrain_t26.c Core/DBC/handcart_t26.c \
	Tests/fake_chain.c Tests/fake_board.c"

mkdir -p "$OUT"

"$CC" $FLAGS $INC -o "$OUT/test_adbms" Tests/test_adbms.c $APP -lm
"$CC" $FLAGS $INC -o "$OUT/test_system" Tests/test_system.c $APP -lm
"$CC" $FLAGS $INC -o "$OUT/test_wrapper" Tests/test_wrapper.c Core/adbms6830/mcuWrapper.c

status=0
echo "== test_adbms";   "$OUT/test_adbms"   || status=1
echo "== test_system";  "$OUT/test_system"  || status=1
echo "== test_wrapper"; "$OUT/test_wrapper" || status=1
exit $status
