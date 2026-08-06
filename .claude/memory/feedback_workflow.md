---
name: feedback-workflow
description: "Como trabalhar no lart_bms — quem faz commits, quando não mexer no código, verificação de build"
metadata:
  node_type: memory
  type: feedback
---

## Commits são do utilizador, no GitKraken
Nunca fazer `git commit`/`push` sem pedido explícito. Deixar as alterações no working tree e dizer o que mudou; ele revê e committa no GitKraken.

**Porquê:** é o fluxo dele e quer manter o controlo do que entra no repo.

## "Ajuda-me a encontrar" ≠ "corrige"
Quando ele pede para **diagnosticar** ("consegues ajudar-me a encontrar o que causa isto?"), responder só com análise. Não editar ficheiros até ele pedir a correção.

**Porquê:** já aconteceu eu ir logo alterar o `brain.c` num pedido de diagnóstico e ele ter de corrigir ("eu não te disse para corrigires o erro"). Diagnóstico e correção são passos separados.

**Como aplicar:** se a causa for clara, descrever a correção em palavras e perguntar se quer que a aplique.

## Cuidado com ficheiros que mudam no disco
Ele edita no CubeIDE em paralelo e muda de branch no GitKraken. Edições minhas já desapareceram várias vezes. Reler o ficheiro antes de editar quando o `Edit` avisa que mudou, e **confirmar com `grep` depois de editar** que ficou lá.

## Verificar sem build completo
Syntax-check com o gcc do CubeIDE, sem depender do IDE:

```
C:\ST\STM32CubeIDE_1.18.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin\arm-none-eabi-gcc.exe
  -mcpu=cortex-m4 -std=gnu11 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F412Rx -Wall -fsyntax-only
  -ICore/Inc -ICore/Inc/adbms -ICore/Inc/dbc -ICore/Src -ICore/Src/adbms
  -IDrivers/STM32F4xx_HAL_Driver/Inc -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include
```

Warnings pré-existentes a ignorar: `start_initiated` não usado (precharge.c), `cell_code_to_mV` implícito (live_debug.c), `-Wswitch` nos enums de estado.

Ficheiros novos exigem **Clean + Build** no CubeIDE (os makefiles gerados não os apanham sozinhos).

## Idioma e estilo
Ele escreve em português; responder em português. Comentários no código em português sem acentos (o estilo que já lá está). Manter o estilo dele: tabs, chavetas na mesma linha, `printfDebug` para tudo o que é consola.
