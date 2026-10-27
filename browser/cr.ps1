<#
remotekit Chromium workflow (design/02-browser-patches.md section 4).

  Infra (this repo):  chromium\PIN.json, chromium\series, chromium\patches\*.patch, chromium\args\{dev,rel}.gn
  Checkout:           $env:CR_ROOT (default C:\src\chromium)\src ; depot_tools: C:\src\depot_tools
  Branches in src:    upstream = PIN commit ; prts = upstream + series ; backups prts-backup/<ts>

Commands:
  init                 write .gclient, gclient sync src@PIN (shallow, no history), create upstream, apply
  apply                recreate prts from upstream + series (git am --3way); refuses on uncommitted changes
  export               upstream..prts commits -> patches (name = Patch-Name: trailer or subject slug), rewrite series
  check [base]         dry-run the series on a temporary index based on upstream (or <base>); no side effects
  stat                 per-patch upstream/new file line counts, with warnings
  rebase <tag> [-Continue]  move upstream + prts to a new Stable tag, sync deps, update PIN.json, export
  gen <dev|rel> [-Force]    copy args\<cfg>.gn to out\<cfg>\args.gn (diff check) and gn gen
  build <dev|rel> [target]  autoninja under a build lock; log + .exit in out\<cfg>\prts-build-<ts>.*
  run <dev|rel> [args]      launch out\<cfg>\chrome.exe --headless=new --prts-remote with a scratch profile
  status               PIN vs checkout, branch, series count
Commit on prts (one commit per maintenance unit, Patch-Name: trailer), then 'cr export' and commit chromium\ here.
#>
param(
  [Parameter(Position = 0)][string]$Cmd = 'help',
  [switch]$Force,
  [switch]$Continue,
  [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = 'Stop'
$env:Path = "C:\src\depot_tools;" + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ";" + [Environment]::GetEnvironmentVariable('Path', 'User')
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
if (-not $env:vs2026_install) { $env:vs2026_install = 'C:\Program Files\Microsoft Visual Studio\18\Community' }

$Repo    = Split-Path $PSScriptRoot
$CrDir   = Join-Path $Repo 'chromium'
$Patches = Join-Path $CrDir 'patches'
$Series  = Join-Path $CrDir 'series'
$PinFile = Join-Path $CrDir 'PIN.json'
$Root    = if ($env:CR_ROOT) { $env:CR_ROOT } else { 'C:\src\chromium' }
$Src     = Join-Path $Root 'src'
$Url     = 'https://chromium.googlesource.com/chromium/src.git'
$TmpRoot = 'C:\src\tmp\cr'   # all temporary files live under C:\src\tmp
$Utf8    = New-Object System.Text.UTF8Encoding($false)
# Real git.exe: depot_tools git.bat runs through cmd, which eats "^" in revisions like x^{commit}.
$Git     = (Get-Command git.exe -CommandType Application | Select-Object -First 1).Source

function G { & $Git -C $Src @args; if ($LASTEXITCODE) { throw "git $args failed ($LASTEXITCODE)" } }
function Gq { $o = & $Git -C $Src @args; if ($LASTEXITCODE) { throw "git $args failed ($LASTEXITCODE)" }; $o }
function Has-Ref($r) { & $Git -C $Src rev-parse --verify --quiet "$r^{commit}" > $null; -not $LASTEXITCODE }
function Native($line, $dir = $Src) {
  Push-Location $dir
  try { cmd /d /c "$line 2>&1"; if ($LASTEXITCODE) { throw "'$line' failed ($LASTEXITCODE)" } } finally { Pop-Location }
}
function Get-Pin { if (-not (Test-Path $PinFile)) { throw "Missing $PinFile" }; Get-Content $PinFile -Raw | ConvertFrom-Json }
function Save-Pin($p) {
  $p.updated = (Get-Date).ToString('s')
  [IO.File]::WriteAllText($PinFile, ($p | ConvertTo-Json) + "`n", $Utf8)
}
function Get-Series {
  if (-not (Test-Path $Series)) { return @() }
  @(Get-Content $Series | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}
function Assert-Clean {
  $d = Gq status --porcelain --untracked-files=no
  if ($d) { throw "Uncommitted changes in $Src; commit (or stash) first." }
}
function Ensure-Upstream {
  if (Has-Ref 'refs/heads/upstream') { return }
  $pin = Get-Pin
  if (-not (Has-Ref $pin.commit)) { throw "PIN commit $($pin.commit) not present in $Src; run 'cr init'" }
  G branch -f upstream $pin.commit
}
function Backup-Prts {
  if ((Has-Ref 'refs/heads/prts') -and ((Gq rev-parse prts) -ne (Gq rev-parse upstream))) {
    $b = "prts-backup/" + (Get-Date -Format 'yyyyMMdd-HHmmss')
    G branch $b prts
    Write-Host "backup: $b"
  }
}
function Cfg-Out([string]$cfg) {
  if ($cfg -notin @('dev', 'rel')) { throw "config must be dev or rel (got '$cfg')" }
  "out\$cfg"
}

function Init {
  $pin = Get-Pin
  New-Item -ItemType Directory -Force $Root | Out-Null
  if (-not (Test-Path "$Root\.gclient")) {
    $g = @"
solutions = [
  {
    "name": "src",
    "url": "$Url",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {"checkout_pgo_profiles": False},
  },
]
"@
    [IO.File]::WriteAllText("$Root\.gclient", $g, $Utf8)
  }
  if (Test-Path "$Src\.git") { Assert-Clean; & $Git -C $Src checkout -q --detach }
  Native "gclient sync --revision src@$($pin.ref) --no-history --shallow -D" $Root
  $head = Gq rev-parse HEAD
  if ($head -ne $pin.commit) { throw "checkout HEAD $head != PIN $($pin.commit)" }
  G branch -f upstream $pin.commit
  Apply
}

function Apply {
  Ensure-Upstream
  Assert-Clean
  Backup-Prts
  G checkout -q -f -B prts upstream
  $n = 0
  foreach ($p in Get-Series) {
    $f = Join-Path $Patches $p
    if (-not (Test-Path $f)) { throw "series lists missing patch $p" }
    & $Git -C $Src am -q --3way --keep-cr $f
    if ($LASTEXITCODE) { throw "git am failed at $p : resolve in $Src, 'git am --continue', then apply the rest by hand (or fix and 'cr export')" }
    $n++
  }
  Write-Host "prts = upstream + $n patch(es)"
}

function Slug([string]$s) {
  $s = ($s.ToLower() -replace '[^a-z0-9]+', '-').Trim('-')
  if ($s.Length -gt 60) { $s = $s.Substring(0, 60).Trim('-') }
  $s
}

function Export {
  Ensure-Upstream
  if (-not (Has-Ref 'refs/heads/prts')) { throw 'no prts branch' }
  New-Item -ItemType Directory -Force $Patches | Out-Null
  $commits = @(Gq rev-list --reverse upstream..prts)
  $names = @(); $changed = 0
  $work = Join-Path $TmpRoot ("export-" + [guid]::NewGuid()); New-Item -ItemType Directory -Force $work | Out-Null
  try {
    foreach ($c in $commits) {
      $name = ((Gq log -1 "--format=%(trailers:key=Patch-Name,valueonly)" $c) -join '').Trim()
      if (-not $name) { $name = Slug ((Gq log -1 --format=%s $c) -join '') }
      if (-not $name.EndsWith('.patch')) { $name += '.patch' }
      if ($names -contains $name) { throw "duplicate Patch-Name $name (commit $c)" }
      $names += $name
      $pf = @(& $Git -C $Src format-patch -1 --zero-commit --no-signature --no-stat -o $work $c)[0]
      if ($LASTEXITCODE) { throw "format-patch $c failed" }
      $dst = Join-Path $Patches $name
      $new = [IO.File]::ReadAllBytes($pf); Remove-Item $pf
      $same = (Test-Path $dst) -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($dst)) -eq [Convert]::ToBase64String($new))
      if (-not $same) { [IO.File]::WriteAllBytes($dst, $new); $changed++; Write-Host "  wrote $name" }
    }
  } finally { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
  foreach ($f in Get-ChildItem $Patches -Filter *.patch) {
    if ($names -notcontains $f.Name) { Remove-Item $f.FullName; $changed++; Write-Host "  removed $($f.Name)" }
  }
  $text = "# Patch order (only source of ordering). Generated by 'cr export'.`n" + (($names | ForEach-Object { "$_`n" }) -join '')
  [IO.File]::WriteAllText($Series, $text, $Utf8)
  Write-Host "exported $($names.Count) patch(es), $changed file change(s)"
}

function Check([string]$base) {
  if (-not $base) { Ensure-Upstream; $base = 'upstream' }
  $ErrorActionPreference = 'Continue'   # native stderr is redirected below
  New-Item -ItemType Directory -Force $TmpRoot | Out-Null
  $idx = Join-Path $TmpRoot ("index-" + [guid]::NewGuid())
  $old = $env:GIT_INDEX_FILE
  $bad = 0
  try {
    $env:GIT_INDEX_FILE = $idx
    G read-tree $base
    foreach ($p in Get-Series) {
      $f = Join-Path $Patches $p
      & $Git -C $Src apply --cached --check $f 2>&1 | Out-Null
      if ($LASTEXITCODE) {
        $bad++
        Write-Host "CONFLICT $p"
        & $Git -C $Src apply --cached --check $f 2>&1 | Select-Object -First 8 | ForEach-Object { "    $_" }
      } else {
        G apply --cached $f
        Write-Host "ok       $p"
      }
    }
  } finally {
    $env:GIT_INDEX_FILE = $old
    Remove-Item $idx -ErrorAction SilentlyContinue
  }
  Write-Host "check vs $base : $bad conflicting patch(es) of $(@(Get-Series).Count)"
  if ($bad) { exit 1 }
}

function Stat {
  Ensure-Upstream
  $ErrorActionPreference = 'Continue'
  $total = 0
  foreach ($p in Get-Series) {
    $f = Join-Path $Patches $p
    $up = 0; $nw = 0
    foreach ($l in (& $Git -C $Src apply --numstat $f)) {
      $a, $d, $path = $l -split "`t", 3
      $n = ($a -as [int]) + ($d -as [int])   # "-" for binary
      & $Git -C $Src cat-file -e "upstream:$path" 2>$null
      if ($LASTEXITCODE) { $nw += $n } else {
        $up += $n
        if ($n -gt 30) { Write-Host "  WARN $p touches upstream $path with $n lines (> 30)" }
      }
    }
    $total += $up
    '{0,-40} upstream {1,6}  new {2,6}' -f $p, $up, $nw
  }
  "upstream total: $total"
  if ($total -gt 800) { Write-Host "WARN upstream total $total > 800" }
}

function Finish-Rebase($tag, $new) {
  G branch -f upstream $new
  G checkout -q --detach $new
  Native "gclient sync --revision src@$new --no-history --shallow -D" $Root
  G checkout -q prts
  $pin = Get-Pin
  $pin.version = $tag; $pin.commit = $new; $pin.ref = "refs/tags/$tag"
  $pin.depot_tools = ((& $Git -C C:\src\depot_tools rev-parse HEAD) -join '').Trim()
  Save-Pin $pin
  Export
  Write-Host "rebased onto $tag ($new); rebuild and commit chromium\"
}

function Rebase([string]$tag) {
  if (-not $tag) { throw 'usage: cr rebase <tag> [-Continue]' }
  if (-not $Continue) {
    Ensure-Upstream; Assert-Clean
    G fetch --depth=1 origin "refs/tags/${tag}:refs/tags/$tag"
    $new = Gq rev-parse "$tag^{commit}"
    G branch -f "prts-backup/pre-$tag" prts
    & $Git -C $Src rebase --onto $new upstream prts
    if ($LASTEXITCODE) { throw "rebase stopped: resolve, 'git rebase --continue' until done, then 'cr rebase $tag -Continue'" }
  }
  $new = Gq rev-parse "$tag^{commit}"
  Finish-Rebase $tag $new
}

function Gen([string]$cfg) {
  $out = Cfg-Out $cfg
  $dir = Join-Path $Src $out
  $srcArgs = Join-Path $CrDir "args\$cfg.gn"
  New-Item -ItemType Directory -Force $dir | Out-Null
  $dstArgs = Join-Path $dir 'args.gn'
  if ((Test-Path $dstArgs) -and ((Get-Content $dstArgs -Raw) -ne (Get-Content $srcArgs -Raw))) {
    Write-Host "out\$cfg\args.gn differs from chromium\args\$cfg.gn:"
    Compare-Object (Get-Content $srcArgs) (Get-Content $dstArgs) | ForEach-Object { "  $($_.SideIndicator) $($_.InputObject)" }
    if (-not $Force) { Write-Host 'keeping existing args.gn (use -Force to overwrite)' } else { Copy-Item $srcArgs $dstArgs -Force }
  } elseif (-not (Test-Path $dstArgs)) { Copy-Item $srcArgs $dstArgs }
  Native "gn gen $out"
}

function Build([string]$cfg, [string]$target) {
  $out = Cfg-Out $cfg
  if (-not $target) { $target = 'chrome' }
  $dir = Join-Path $Src $out
  if (-not (Test-Path "$dir\build.ninja")) { Gen $cfg }
  $lock = Join-Path $dir 'prts-build.lock'
  if (Test-Path $lock) {
    $lp = (Get-Content $lock -TotalCount 1) -as [int]
    if ($lp -and (Get-Process -Id $lp -ErrorAction SilentlyContinue)) { throw "build already running in $out (pid $lp)" }
    Remove-Item $lock -Force
  }
  try { $fs = [IO.File]::Open($lock, 'CreateNew', 'Write', 'Read') } catch { throw "could not take build lock $lock" }
  $b = $Utf8.GetBytes("$PID`n"); $fs.Write($b, 0, $b.Length); $fs.Flush()
  try {
    # Running chrome.exe from this out dir locks DLLs and breaks linking.
    Get-Process chrome -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($dir, 'OrdinalIgnoreCase') } | Stop-Process -Force
    $ts = Get-Date -Format 'yyyyMMdd-HHmmss'
    $log = Join-Path $dir "prts-build-$ts.log"
    $t0 = Get-Date
    Push-Location $Src
    try { cmd /d /c "autoninja -C $out $target > `"$log`" 2>&1"; $code = $LASTEXITCODE } finally { Pop-Location }
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    [IO.File]::WriteAllText((Join-Path $dir "prts-build-$ts.exit"), "exit=$code target=$target seconds=$sec`n", $Utf8)
    Copy-Item (Join-Path $dir "prts-build-$ts.exit") (Join-Path $dir 'prts-build-last.exit') -Force
    Get-Content $log -Tail 15
    Write-Host "build $out $target exit=$code in ${sec}s (log $log)"
    if ($code) { exit $code }
  } finally { $fs.Close(); Remove-Item $lock -ErrorAction SilentlyContinue }
}

function Run([string]$cfg, [string[]]$extra) {
  $out = Cfg-Out $cfg
  $exe = Join-Path $Src "$out\chrome.exe"
  $prof = Join-Path $Src "$out\prts-profile"
  & $exe --headless=new --prts-remote "--user-data-dir=$prof" @extra
}

$A1 = if ($Rest) { $Rest[0] } else { $null }
$More = if ($Rest -and $Rest.Count -gt 1) { $Rest[1..($Rest.Count - 1)] } else { @() }
switch ($Cmd) {
  'init'   { Init }
  'apply'  { Apply }
  'export' { Export }
  'check'  { Check $A1 }
  'stat'   { Stat }
  'rebase' { Rebase $A1 }
  'gen'    { Gen $A1 }
  'build'  { Build $A1 ($More | Select-Object -First 1) }
  'run'    { Run $A1 $More }
  'status' {
    $pin = Get-Pin
    $up = if (Has-Ref 'refs/heads/upstream') { Gq rev-parse upstream } else { '(none)' }
    $br = (Gq rev-parse --abbrev-ref HEAD)
    Write-Host "PIN      $($pin.version) $($pin.commit)"
    Write-Host "upstream $up $(if ($up -eq $pin.commit) {'(in sync)'} else {'(OUT OF SYNC)'})"
    Write-Host "HEAD     $br ; series: $(@(Get-Series).Count) patch(es)"
  }
  default  { (Get-Content $PSCommandPath -TotalCount 21) -join "`n" }
}
