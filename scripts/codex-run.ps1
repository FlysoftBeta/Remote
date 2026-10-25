<#
.SYNOPSIS
  Run one bounded Codex task non-interactively with a hard timeout.
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts/codex-run.ps1 -Name research-b -PromptFile prompts/b.md -OutFile research/b.final.md -TimeoutMin 40 -Effort high
  The prompt is fed via stdin (no quoting problems). JSONL events go to .codex-logs/<name>-<ts>.jsonl,
  stderr to .codex-logs/<name>-<ts>.err. On timeout the whole process tree is killed (taskkill /T /F).
  Exit code: codex exit code, or 124 on timeout.
#>
param(
  [Parameter(Mandatory)][string]$Name,
  [string]$PromptFile,
  [string]$Prompt,
  [string]$OutFile,
  [string]$Dir = "",
  [ValidateSet('low','medium','high','xhigh')][string]$Effort = 'medium',
  [string]$Model = 'gpt-6.1-sol',
  [int]$TimeoutMin = 40
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Dir) { $Dir = $root }
$Dir = (Resolve-Path $Dir).Path.TrimEnd('\')
$logDir = Join-Path $root '.codex-logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
# runs.log is shared by concurrent runs: append with retries and never fail the run because of logging.
function Log { process {
  $_
  $line = "$_`r`n"
  for ($i = 0; $i -lt 20; $i++) {
    try { [IO.File]::AppendAllText((Join-Path $logDir "runs.log"), $line, (New-Object Text.UTF8Encoding $false)); break }
    catch { Start-Sleep -Milliseconds (50 + (Get-Random -Maximum 200)) }
  }
} }
$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
$base = Join-Path $logDir "$Name-$ts"

if ($Prompt) { $PromptFile = "$base.prompt.md"; [IO.File]::WriteAllText($PromptFile, $Prompt, (New-Object Text.UTF8Encoding $false)) }
if (-not $PromptFile -or -not (Test-Path $PromptFile)) { throw "need -Prompt or existing -PromptFile" }
$PromptFile = (Resolve-Path $PromptFile).Path
if (-not $OutFile) { $OutFile = "$base.last.md" }
$outDir = Split-Path -Parent $OutFile
if ($outDir) { New-Item -ItemType Directory -Force $outDir | Out-Null }

$codex = (Get-Command codex).Source
# codex on Windows may be a .cmd/.ps1 shim; resolve to exe if present
$exe = Join-Path (Split-Path $codex) 'codex.exe'
if (Test-Path $exe) { $codex = $exe }

$argList = @('exec','-m',$Model,'-c',"model_reasoning_effort=$Effort",'--skip-git-repo-check',
  '-s','danger-full-access','-C',"`"$Dir`"",'-o',"`"$OutFile`"",'--json','--color','never','-')

"[$(Get-Date -Format s)] start $Name effort=$Effort timeout=${TimeoutMin}m out=$OutFile" | Log
$p = Start-Process -FilePath $codex -ArgumentList $argList -NoNewWindow -PassThru `
  -RedirectStandardInput $PromptFile -RedirectStandardOutput "$base.jsonl" -RedirectStandardError "$base.err"
$null = $p.Handle  # keep handle so ExitCode is available
if (-not $p.WaitForExit($TimeoutMin * 60 * 1000)) {
  & taskkill.exe /PID $p.Id /T /F | Out-Null
  "[$(Get-Date -Format s)] TIMEOUT $Name (killed tree)" | Log
  exit 124
}
$code = $p.ExitCode
"[$(Get-Date -Format s)] done $Name exit=$code" | Log
(& (Join-Path $PSScriptRoot "codex-limits.ps1") | Select-Object -First 2) -join " | " | Log
exit $code
