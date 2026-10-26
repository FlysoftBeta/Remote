<#
.SYNOPSIS
  Print the latest Codex rate-limit snapshot (5h primary window, 7d secondary window).
  Source: token_count events in the newest rollout files under ~/.codex/sessions (written by every codex run,
  including `codex exec`). `codex exec --json` stdout does NOT carry rate_limits in CLI 0.163.
#>
$files = Get-ChildItem "$env:USERPROFILE\.codex\sessions" -Recurse -Filter '*.jsonl' |
  Sort-Object LastWriteTime -Descending | Select-Object -First 30
foreach ($f in $files) {
  $m = Select-String -Path $f.FullName -Pattern '"rate_limits":\{.*?"credits"' | Select-Object -Last 1
  if (-not $m) { continue }
  $j = ($m.Line | ConvertFrom-Json)
  $rl = $j.payload.rate_limits
  if (-not $rl -or -not $rl.primary) { continue }  # snapshots written at the limit can lack the windows
  $epoch = [DateTime]'1970-01-01Z'
  foreach ($w in 'primary','secondary') {
    $x = $rl.$w
    if ($x) {
      $reset = $epoch.AddSeconds($x.resets_at).ToLocalTime()
      "{0,-9} used={1,5}%  window={2}min  resets={3:yyyy-MM-dd HH:mm}" -f $w, $x.used_percent, $x.window_minutes, $reset
    }
  }
  "(from $($f.Name), $($f.LastWriteTime))"
  exit 0
}
"no rate_limits snapshot found"; exit 1
