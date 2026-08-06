# Memória do projeto lart_bms

Vive no repositório (vai no `git push`). Um ficheiro por facto; editar o ficheiro existente em vez de duplicar.

- [lart_bms firmware overview](project_lart_bms_overview.md) — STM32F412, ADBMS6830 daisychain, dual CAN, IVT-S, live_debug
- [Arquitetura do AMS_ERROR](project_ams_error_architecture.md) — ams_error.c é o único dono do pino, todas as fontes clearable, como diagnosticar
- [Duas ISAs](project_two_isas.md) — pack no CAN1 vs handcart no CAN2, mesmos IDs, nunca misturar; corrente de carga é a do CAN2
- [Arquitetura de carregamento](project_charging_architecture.md) — EV Europe 650-6 em protocolo P1000, submáquina do charger, cortes
- [Defeitos de hardware conhecidos](project_hardware_quirks.md) — slave 3 NTC3/4, open-wire e clamp BAV70W, limiar 2,3 V, CAN1 na bancada
- [Design do balanceamento](project_balancing_design.md) — alvo mínimo congelado, ac_codes, pulso 1500 ms < tSLEEP; parece bug, é deliberado
- [Bugs conhecidos](project_lart_bms_known_bugs.md) — abertos e já corrigidos
- [Modo de trabalho](feedback_workflow.md) — commits são do utilizador no GitKraken; "encontrar" ≠ "corrigir"; syntax-check sem build completo
- [Regra do printfDebug](feedback_printfdebug_console.md) — todos os prints por printfDebug (uartDMA.h), nunca printf/printfUI
- [Regra de segurança do MCP23017](feedback_mcp23017_isr_safety.md) — nunca bloquear I2C em contexto de interrupção
- [Âmbito do gitignore](feedback_gitignore_scope.md) — graphify-out/ e .claude/ ficam tracked, não voltar a ignorar
- [Sync do DBC](reference_dbc_sync.md) — sync_dbc.sh puxa do FSLART/T26_DBC; artefactos de build fora do git
- [Jump para o bootloader](reference_bootloader_jump.md) — a sequência de deinit do JumpToBootloader é deliberada, não code smell
