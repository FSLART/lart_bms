#!/usr/bin/env bash
#
# sync_dbc.sh - puxa os .c/.h DBC gerados mais recentes do repo
# FSLART/T26_DBC para dentro do lart_bms (layout split Src/dbc + Inc/dbc).
#
# O T26_DBC gera os .c/.h por CI a cada push do .dbc, por isso isto puxa
# direto o codigo gerado - nao precisas de correr o cantools localmente.
#
# Uso:
#   ./sync_dbc.sh            puxa e escreve os ficheiros (ref default = main)
#   ./sync_dbc.sh --check    so mostra o que MUDARIA, nao escreve nada
#   REF=v1.2 ./sync_dbc.sh   puxa de uma tag/commit especifico
#
# Depois de escrever: COMPILA e confirma que ainda builda. Se os sinais
# mudaram la em cima, as chamadas podem precisar de ajuste. Commita tu
# no GitKraken (este script nunca faz commit).

set -euo pipefail

# ferramentas necessarias (Git Bash no Windows ja tras todas)
for tool in git curl mktemp cmp; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "ERRO: falta '${tool}' no PATH. Corre num Git Bash / shell com estas tools." >&2
		exit 1
	fi
done

# ref do T26_DBC a puxar. main = ultimo sempre; mete uma tag/commit para
# builds reprodutiveis (DBC = sinais CAN, convem controlar a versao)
REF="${REF:-main}"

REPO="FSLART/T26_DBC"
RAW="https://raw.githubusercontent.com/${REPO}/${REF}"

# --check = dry run (nao escreve, so reporta)
CHECK=0
if [ "${1:-}" = "--check" ]; then
	CHECK=1
fi

# raiz do lart_bms (funciona corras de onde correres)
ROOT="$(git rev-parse --show-toplevel)"
SRC_DIR="${ROOT}/Firmware/Core/Src/dbc"
INC_DIR="${ROOT}/Firmware/Core/Inc/dbc"

# ficheiros a sincronizar: "caminho_upstream|destino_local"
# so os que o BMS usa (NAO puxar eveurope - foi removido de proposito)
FILES=(
	"generated/powertrain_t26/powertrain_t26.c|${SRC_DIR}/powertrain_t26.c"
	"generated/powertrain_t26/powertrain_t26.h|${INC_DIR}/powertrain_t26.h"
	"generated/handcart_t26/handcart_t26.c|${SRC_DIR}/handcart_t26.c"
	"generated/handcart_t26/handcart_t26.h|${INC_DIR}/handcart_t26.h"
)

if [ "${CHECK}" -eq 1 ]; then
	echo "sync DBC (CHECK) de ${REPO}@${REF}"
else
	echo "sync DBC de ${REPO}@${REF}"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

# 1) baixar tudo para temp primeiro - so se TUDO vier bem e que mexemos
#    nos ficheiros locais (evita ficar com a chain meio-atualizada)
i=0
for entry in "${FILES[@]}"; do
	up="${entry%%|*}"
	if ! curl -fsSL "${RAW}/${up}" -o "${TMP}/f${i}"; then
		echo "ERRO: falhou baixar ${up} (ref '${REF}' e o ficheiro existem?)" >&2
		exit 1
	fi
	if [ ! -s "${TMP}/f${i}" ]; then
		echo "ERRO: ${up} veio vazio" >&2
		exit 1
	fi
	i=$((i + 1))
done

# conteudo igual IGNORANDO line endings (CRLF Windows vs LF Unix)? Assim o
# script e idempotente em qualquer PC - nao reescreve so por causa do \r
same_content() {
	# $1 = novo (temp, LF do curl)   $2 = destino
	[ -f "$2" ] || return 1
	diff -q <(tr -d '\r' <"$1") <(tr -d '\r' <"$2") >/dev/null 2>&1
}

# 2) comparar / escrever
i=0
changed=0
for entry in "${FILES[@]}"; do
	dst="${entry##*|}"
	base="$(basename "${dst}")"

	if same_content "${TMP}/f${i}" "${dst}"; then
		echo "  = ${base} (igual)"
	else
		changed=$((changed + 1))
		if [ "${CHECK}" -eq 1 ]; then
			echo "  ~ ${base} MUDARIA"
		else
			mkdir -p "$(dirname "${dst}")"
			# preservar o line ending do destino para nao criar churn no git:
			# se o ficheiro atual ja e CRLF (Windows/autocrlf) escreve CRLF,
			# senao LF. Ficheiro novo -> LF.
			if [ -f "${dst}" ] && [ "$(tr -cd '\r' <"${dst}" | wc -c)" -gt 0 ]; then
				sed 's/$/\r/' "${TMP}/f${i}" >"${dst}"
			else
				cp "${TMP}/f${i}" "${dst}"
			fi
			echo "  + ${base} atualizado"
		fi
	fi
	i=$((i + 1))
done

if [ "${CHECK}" -eq 1 ]; then
	echo "check: ${changed} ficheiro(s) mudariam. Corre sem --check para aplicar."
else
	echo "feito: ${changed} ficheiro(s) mudaram. Compila e reve no GitKraken."
fi
