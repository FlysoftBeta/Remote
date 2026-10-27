# Ops: running Codex jobs

## Runner
`scripts/codex-run.ps1` (Windows PowerShell 5.1 compatible):

```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex-run.ps1 `
  -Name <job> -PromptFile <file.md> [-OutFile <final.md>] [-Dir <workdir>] [-Effort medium|high] [-TimeoutMin 40]
# or -Prompt "<short text>" (written to a file, then fed via stdin)
```

- Invokes `codex exec -m gpt-6.1-sol -c model_reasoning_effort=<e> --skip-git-repo-check -s danger-full-access -C <dir> -o <out> --json -`
  with the prompt on **stdin** (no quoting issues).
- Logs: `.codex-logs/<job>-<ts>.jsonl` (event stream), `.err` (stderr), `runs.log` (start/done/timeout lines).
  `.codex-logs/` is gitignored.
- Hard timeout: `WaitForExit`, then `taskkill /PID <pid> /T /F` (kills the whole tree). Exit code 124 = timeout.
- Pitfall found: in PS 5.1 `$PSScriptRoot` is empty inside `param()` defaults; a trailing `\` before a closing quote
  breaks argv parsing (`"C:\"`). Both handled.
- Pattern used by the orchestrator: shared preamble `research/prompts/_common.md` + per-task prompt, concatenated into
  `.codex-logs/p/<task>.md`; run from Claude Code with `run_in_background` and wait for the completion notification.
  Tasks write their report incrementally so partial output survives a timeout.

## Bounded-task helper (Stage 1, preferred)
`bash scripts/codex-task.sh <task> [timeout-min=30] [effort=high]` concatenates `research/prompts2/_common.md` +
`research/prompts2/<task>.md` into `.codex-logs/p/<task>.md` and calls `codex-run.ps1`; final message goes to
`.codex-logs/<task>.final.md`, the report to the path named in the prompt. Launch from Claude Code with
`run_in_background` and wait for the completion notification.
- Fixed 2026-10-25: concurrent runs crashed at start because `Add-Content` on the shared `runs.log` threw a sharing
  violation under `$ErrorActionPreference='Stop'`. `Log` now appends via `[IO.File]::AppendAllText` with retries and never
  fails the run. (Older lines in `runs.log` are UTF-16; newer ones UTF-8.)
- Observed cost: one high-effort 10-minute verification task used ~8-15% of the 5-hour window. Stage 1 used 6 tasks
  (41% -> 91%). Budget accordingly; switch to Sonnet subagents above ~85%.
- Prompt style that worked: one numbered list of concrete questions, explicit file paths to start from, [V]/[I] evidence
  labels, "create the file in the first 5 minutes", 25-minute limit. All six runs finished in 8-12 minutes.

## Reading the remaining Codex rate limit
`scripts/codex-limits.ps1` prints e.g.

```
primary   used=  1.0%  window=300min    resets=2026-10-25 23:39     # 5-hour window
secondary used=  0.0%  window=10080min  resets=2026-11-01 18:39     # weekly window
```

Method: every Codex run (including `codex exec`) writes a rollout file under `~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl`
containing `token_count` events with `payload.rate_limits = {primary:{used_percent,window_minutes,resets_at}, secondary:{...}, credits}`.
The script takes the newest snapshot. Note: `codex exec --json` stdout (CLI 0.163) does **not** include `rate_limits`; the
app-server protocol has an account rate-limit read method too, but the rollout file is the simplest source.
The snapshot is only as fresh as the last run; run a trivial `-Effort low` job to refresh if needed.

## Pacing policy
- At most 3 concurrent Codex jobs; prefer 2 for heavy (high effort) work.
- Check `codex-limits.ps1` between batches; if primary > 70%, drop to 1 concurrent job / medium effort; if a run reports
  exhaustion, fall back to Claude Sonnet subagents.
- Every job gets a timeout (research: 40-45 min; build/grunt: set explicitly).

## Local resources
- Partial chromium/src clone for grepping: `C:\src\cr-partial` (see `research/cr-partial.md`).
- Prior art (read-only): `C:\Users\Flysoft\Desktop\chromium`.
