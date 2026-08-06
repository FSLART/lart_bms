---
name: reference-dbc-sync
description: "sync_dbc.sh — puxa os DBC gerados do FSLART/T26_DBC para dentro do lart_bms"
metadata:
  node_type: memory
  type: reference
---

**`sync_dbc.sh`** na raiz do repo puxa os `.c/.h` DBC gerados do repositório https://github.com/FSLART/T26_DBC.

```bash
./sync_dbc.sh            # puxa e escreve (ref = main)
./sync_dbc.sh --check    # dry-run, só mostra o que mudaria
REF=<tag> ./sync_dbc.sh  # fixa uma versão
```

- O T26_DBC gera os `.c/.h` por CI (`convert_to_c.yml`) a cada push do `.dbc`, em `generated/<nome>/` — não é preciso correr o cantools.
- Sincroniza só **`powertrain_t26`** e **`handcart_t26`** para o split `Firmware/Core/Src/dbc` + `Inc/dbc`. Não re-introduz o `eveurope_charger` (removido de propósito).
- Idempotente: compara ignorando line endings e escreve preservando o EOL do destino (senão o CRLF do Windows criava churn a cada corrida).
- `.gitattributes` força `*.sh` em LF para o script não partir noutra máquina.
- **Nunca faz commit** — isso é no GitKraken, que é como o utilizador trabalha.

⚠️ Puxar o `main` pode partir o build se renomearem sinais lá em cima. Fluxo: sync → compilar → corrigir. Para builds reprodutíveis, fixar uma tag com `REF=`.

## Build artifacts fora do git
`Firmware/Debug/**` está ignorado **exceto** `lart_bms.elf` e `lart_bms.bin`, que continuam a ir no push. Os 237 ficheiros `.o/.d/.su/.cyclo/.mk/.list/.map` foram destracked (estavam commitados antes das regras existirem — gitignore não destracka sozinho).

Ver [[feedback-gitignore-scope]].
