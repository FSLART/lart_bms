#!/bin/sh
# Cobertura dos testes no PC (linhas e ramos), com o gcov e o gcovr, e do
# uploader em Python com o coverage.py:
#   pip install gcovr coverage
#   Firmware/Tests/coverage.sh            resumo por ficheiro
#   Firmware/Tests/coverage.sh --html     tambem coverage/index.html
# Corre o run_tests.sh com --coverage numa pasta temporaria.

set -e
cd "$(dirname "$0")/.."

OUT="${OUT:-${TMPDIR:-/tmp}/lart_bms_coverage}"
rm -rf "$OUT"
mkdir -p "$OUT"

EXTRA_FLAGS="--coverage -fprofile-abs-path -O0" OUT="$OUT" Tests/run_tests.sh > "$OUT/run.log" 2>&1 || { tail -40 "$OUT/run.log"; exit 1; }

cd ..
FILTER="--filter Firmware/Core/ --filter Bootloader/Src/ --exclude .*/DBC/.* --exclude .*Tests/.*"
gcovr --root . $FILTER "$OUT" --print-summary --sort uncovered-percent
if [ "$1" = "--html" ]; then
	mkdir -p coverage
	gcovr --root . $FILTER "$OUT" --html-details coverage/index.html
	echo "coverage/index.html"
fi

# o uploader (Python), se houver o coverage.py (pip install coverage)
if python3 -c "import coverage, can, cantools" 2>/dev/null; then
	COVERAGE_FILE="$OUT/.coverage" CC="${CC:-gcc}" python3 -m coverage run --include="*/Bootloader/tools/can_upload.py" \
		Bootloader/tools/test_can_upload.py > "$OUT/python.log" 2>&1 || { tail -30 "$OUT/python.log"; exit 1; }
	COVERAGE_FILE="$OUT/.coverage" python3 -m coverage report
fi
