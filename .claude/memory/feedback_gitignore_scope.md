---
name: feedback-gitignore-scope
description: graphify-out/ and .claude/settings.local.json are intentionally tracked in the lart_bms repo - do not re-ignore
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 71f7fca9-d43b-42fe-8a9f-fcbd53bf5ad9
---

`graphify-out/` (graphify knowledge-graph output, ~180 files/20MB) and `.claude/settings.local.json` must stay tracked/committed in lart_bms repo. Don't re-add to `.gitignore` or suggest untracking again.

Why: user wants this "vital information for claude code to work even in a different machine" — graph output + local Claude settings should travel with repo so fresh clone/machine has them immediately, no rebuild/reconfigure.

Context: initially untracked both (standard practice for generated/machine-local files) after noticing accidental commit. User explicitly reversed. Re-added via `git add -f`, removed corresponding `.gitignore` entries.

Separately, `Firmware/Debug/**` (STM32CubeIDE build artifacts: .o/.d/.su/.cyclo/subdir.mk) stays ignored normally — only graphify-out and .claude were the exception. `Firmware/dump_to_binary/output.bin` also explicitly kept tracked (via `.gitignore` negation `!Firmware/dump_to_binary/output.bin`) — generated tool output user wants in-repo, not a build artifact.

How to apply: if `.gitignore` touched again here, preserve negation for `Firmware/dump_to_binary/output.bin`, don't add `graphify-out/` or `.claude/` ignore rules.
