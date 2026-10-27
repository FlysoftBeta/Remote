# Windows PowerShell 5.1+ / PowerShell 7. Run from any directory; Ctrl+C stops this run's process tree.
[CmdletBinding()]
param(
    [string]$BinDir,
    [ValidateRange(1, 65535)][int]$Port = 8300,
    [ValidateSet('marker', 'S', 'V', 'G')][string]$Scenario = 'S',
    [ValidateRange(5, 3600)][int]$Seconds = 60,
    [string]$OutDir = 'C:\src\tmp\m4\field-kit\runs',
    [string[]]$EndpointArgs = @()
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$nodeExe = (Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$nodeVersion = & $nodeExe --version
if ($LASTEXITCODE -ne 0 -or [int]($nodeVersion.TrimStart('v').Split('.')[0]) -lt 22) {
    throw 'Node.js 22+ is required (control-plane/package.json).'
}
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'control-plane\node_modules\ws\package.json'))) {
    throw "Install dependencies first: npm --prefix `"$repoRoot\control-plane`" ci"
}
if (-not $BinDir) {
    foreach ($candidate in @('C:\src\chromium\src\out\rel', 'C:\src\chromium\src\out\dev')) {
        if ((Test-Path -LiteralPath (Join-Path $candidate 'chrome.exe')) -and
            (Test-Path -LiteralPath (Join-Path $candidate 're.exe'))) { $BinDir = $candidate; break }
    }
    if (-not $BinDir) { throw 'No binary pair found in out\rel or out\dev. Specify -BinDir.' }
}
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
$chromeExe = Join-Path $BinDir 'chrome.exe'
$endpointExe = Join-Path $BinDir 're.exe'
foreach ($exe in @($chromeExe, $endpointExe)) {
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing binary: $exe" }
}
foreach ($extra in $EndpointArgs) {
    if ($extra -eq '--no-hires-timer') { throw 'm3-combined requires the default high-resolution timer.' }
    if ($extra -match '^--(synthetic|cr-user-data-dir)(=|$)') {
        throw "Reserved field-kit argument: $extra. Use the standard capture path and dedicated profile."
    }
}
# Read-only port check. Never stop another run or any build process.
$listeners = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
if ($listeners.Count) { throw "Port $Port already has a listener (PID $($listeners[0].OwningProcess)). Choose another port or stop that run yourself." }
$addresses = @(Get-NetIPAddress -AddressFamily IPv4 | Where-Object {
    $_.AddressState -eq 'Preferred' -and $_.IPAddress -ne '127.0.0.1' -and
    $_.IPAddress -notlike '169.254.*' -and $_.IPAddress -ne '0.0.0.0'
} | Select-Object -ExpandProperty IPAddress -Unique)
if (-not $addresses.Count) { throw 'No usable IPv4 LAN address found. Connect the server to the LAN.' }

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$tcpName = "remotekit field TCP $Port"
$udpName = 'remotekit field endpoint UDP ' + $BinDir
function Show-FieldFirewallRule($Name, $Protocol, $Program) {
    $existing = @(Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue)
    if (-not $existing.Count -and $elevated) {
        $ruleParams = @{ DisplayName = $Name; Direction = 'Inbound'; Action = 'Allow'; Enabled = 'True';
            Profile = @('Private', 'Domain'); RemoteAddress = 'LocalSubnet'; Protocol = $Protocol }
        if ($Protocol -eq 'TCP') { $ruleParams.LocalPort = $Port }
        if ($Program) { $ruleParams.Program = $Program }
        New-NetFirewallRule @ruleParams | Out-Null
        $existing = @(Get-NetFirewallRule -DisplayName $Name)
    }
    if ($existing.Count) {
        $existing | Format-Table DisplayName, Enabled, Direction, Action, Profile | Out-Host
        $existing | Get-NetFirewallPortFilter | Format-Table Protocol, LocalPort | Out-Host
        $existing | Get-NetFirewallAddressFilter | Format-Table RemoteAddress | Out-Host
        $existing | Get-NetFirewallApplicationFilter | Format-Table Program | Out-Host
    } else {
        Write-Warning "Firewall rule missing. In an elevated terminal run:"
        $command = 'netsh advfirewall firewall add rule name="' + $Name + '" dir=in action=allow protocol=' + $Protocol + ' profile=private,domain remoteip=localsubnet'
        if ($Protocol -eq 'TCP') { $command += " localport=$Port" }
        if ($Program) { $command += ' program="' + $Program + '"' }
        Write-Host $command
    }
}
Show-FieldFirewallRule $tcpName 'TCP' $null
Show-FieldFirewallRule $udpName 'UDP' $endpointExe
Get-NetConnectionProfile | Format-Table InterfaceAlias, NetworkCategory, IPv4Connectivity | Out-Host
Write-Host 'Rules allow Private/Domain LANs. A Public network profile needs explicit firewall configuration.'

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDir = [IO.Path]::GetFullPath((Join-Path $OutDir ($stamp + '-' + $PID)))
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$profileDir = Join-Path $runDir 'chrome-profile'
$trial = 'WebRTC-Video-Pacing/factor:2.5/WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate/Enabled/'
# Preset settings come last, as in tools/latency/run.mjs. Chromium's last switch wins.
$arguments = @($EndpointArgs) + @(
    "--chrome=$chromeExe", "--user-data-dir=$profileDir", '--cr-enable-logging',
    '--encoder=vpl', '--fps=60', '--cr-fake-vsync-rate=60', "--webrtc-field-trials=$trial",
    "--url=http://127.0.0.1:$Port/tools/latency/remote-page.html?scenario=$Scenario"
)
$config = [ordered]@{
    preset = 'm3-combined'; scenario = $Scenario; seconds = $Seconds; port = $Port;
    startedAt = (Get-Date).ToUniversalTime().ToString('o'); binaries = $BinDir;
    endpointArgs = $arguments; requestedEncoder = 'vpl'; captureFps = 60; fakeVsyncHz = 60;
    hiresTimer = 'default enabled'; fieldTrials = $trial; network = 'direct LAN; no netem';
    note = 'Requested settings only; confirm actual encoder/fallback in stderr.log and sender stats.'
}
$configJson = $config | ConvertTo-Json -Depth 8
$configJson | Set-Content -LiteralPath (Join-Path $runDir 'run.json') -Encoding UTF8
$urls = @($addresses | ForEach-Object {
    $viewerUrl = "http://$($_):$Port/?measure=$Seconds&scenario=$Scenario"
    if ($env:CP_TOKEN) { $viewerUrl += '&token=' + [Uri]::EscapeDataString($env:CP_TOKEN) }
    $viewerUrl
})
$urls | Set-Content -LiteralPath (Join-Path $runDir 'viewer-urls.txt') -Encoding UTF8
$childEnv = @{
    ENDPOINT_PATH = $endpointExe; ENDPOINT_ARGS_JSON = (ConvertTo-Json -InputObject $arguments -Compress);
    FIELD_REPORT_DIR = $runDir; FIELD_CONFIG_JSON = $configJson; HOST = '0.0.0.0'
}
$previousEnv = @{}
$child = $null
try {
    foreach ($key in $childEnv.Keys) {
        $previousEnv[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $childEnv[$key], 'Process')
    }
    $cpScript = Join-Path $repoRoot 'control-plane\index.js'
    $child = Start-Process -FilePath $nodeExe -ArgumentList @(('"' + $cpScript + '"'), '--port', "$Port") `
        -WorkingDirectory $repoRoot -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $runDir 'stdout.log') -RedirectStandardError (Join-Path $runDir 'stderr.log')
    @{ controlPlanePid = $child.Id; launcherPid = $PID } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'pids.json') -Encoding UTF8
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($child.HasExited) { throw "Control plane exited ($($child.ExitCode)); see $runDir\stderr.log" }
        try {
            $health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 1
            if ($health.ok) { $healthy = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 200
    }
    if (-not $healthy) { throw "Control plane did not become healthy; see $runDir" }
    Write-Host "Logs and reports: $runDir"
    Write-Host 'Open ONE of these URLs in Chrome 89+ on the viewer device, then tap Start:'
    $urls | ForEach-Object { Write-Host "  $_" }
    Write-Host 'Keep this terminal open. Ctrl+C stops only this run. No CDP or TURN is required.'
    while (-not $child.HasExited) { Start-Sleep -Milliseconds 500 }
    throw "Control plane exited ($($child.ExitCode)); see $runDir"
} finally {
    foreach ($key in $previousEnv.Keys) { [Environment]::SetEnvironmentVariable($key, $previousEnv[$key], 'Process') }
    if ($child -and -not $child.HasExited) {
        # Exact launched PID tree only. Never process-name kills (out\rel may be building).
        & taskkill.exe /PID $child.Id /T /F | Out-Null
    }
}
