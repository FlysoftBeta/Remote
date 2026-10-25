#!/usr/bin/env bash
# Usage: scripts/codex-task.sh <task-name> [timeout-min] [effort]
# Concatenates research/prompts2/_common.md + research/prompts2/<task>.md into .codex-logs/p/<task>.md
# and runs scripts/codex-run.ps1 (hard timeout, logs in .codex-logs/). Final message -> .codex-logs/<task>.final.md
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
task="$1"; tmo="${2:-30}"; eff="${3:-high}"
mkdir -p "$root/.codex-logs/p"
cat "$root/research/prompts2/_common.md" "$root/research/prompts2/$task.md" > "$root/.codex-logs/p/$task.md"
cd "$root"
exec powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex-run.ps1 \
  -Name "$task" -PromptFile ".codex-logs/p/$task.md" -OutFile ".codex-logs/$task.final.md" \
  -Effort "$eff" -TimeoutMin "$tmo"
