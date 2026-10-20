#!/bin/sh
# Testes do firmware no PC (sem hardware). Correr a partir de qualquer pasta:
#   CC=<gcc ou clang do PC> Firmware/Tests/run_tests.sh
# Precisa de um compilador para o PC, nao o arm-none-eabi. Se CC nao estiver
# definido usa o gcc do PATH.

set -e
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_tests}"   # fora do repo: nada para ignorar no git
FLAGS="-std=gnu11 -Wall -Wextra -g"
INC="-ITests/stubs -ITests -ICore/Inc -ICore/adbms6830"

mkdir -p "$OUT"

"$CC" $FLAGS $INC -o "$OUT/test_adbms" Tests/test_adbms.c Tests/fake_chain.c \
	Core/Src/brain.c Core/adbms6830/adBms_Application.c Core/adbms6830/adbms_cell_read.c \
	Core/adbms6830/adbms_temp_read.c Core/adbms6830/adbms_open_wire.c Core/adbms6830/adbms_balancing.c -lm

"$CC" $FLAGS $INC -o "$OUT/test_wrapper" Tests/test_wrapper.c Core/adbms6830/mcuWrapper.c

status=0
echo "== test_adbms";   "$OUT/test_adbms"   || status=1
echo "== test_wrapper"; "$OUT/test_wrapper" || status=1
exit $status
