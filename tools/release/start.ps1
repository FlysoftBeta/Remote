[CmdletBinding()]
param(
    [ValidateRange(1, 65535)][int]$Port = 8080,
    [string]$BindAddress = '127.0.0.1',
    [string[]]$EndpointArgs = @(),
    [switch]$ControlPlaneOnly
)
$ErrorActionPreference = 'Stop'
$node = Join-Path $PSScriptRoot 'node\node.exe'
$endpoint = Join-Path $PSScriptRoot 're\re.exe'
$browser = Join-Path $PSScriptRoot 'rb\chrome.exe'
# The Chromium sandbox needs read/execute for AppContainer principals on the rb
# folder; an extracted zip lacks them. Grant once (marker file).
$rbDir = Join-Path $PSScriptRoot 'rb'
$aclMarker = Join-Path $rbDir '.acl-granted'
if (!$ControlPlaneOnly -and (Test-Path -LiteralPath $browser) -and !(Test-Path -LiteralPath $aclMarker)) {
    Write-Host 'First run: granting ALL (RESTRICTED) APPLICATION PACKAGES read/execute on rb ...'
    # S-1-15-2-1 = ALL APPLICATION PACKAGES, S-1-15-2-2 = ALL RESTRICTED APPLICATION PACKAGES
    & icacls.exe $rbDir /grant:r '*S-1-15-2-1:(OI)(CI)(RX)' '*S-1-15-2-2:(OI)(CI)(RX)' /T /C /Q | Out-Null
    if ($LASTEXITCODE -eq 0) { Set-Content -LiteralPath $aclMarker -Value (Get-Date -Format o) }
    else { Write-Warning 'icacls failed; the browser sandbox may report "cannot access executable". Run once from an elevated prompt.' }
}
$profileDir = Join-Path $PSScriptRoot 'data\profile'
New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
$env:HOST = $BindAddress
$env:ENDPOINT_PATH = $null
$env:ENDPOINT_ARGS = $null
$env:ENDPOINT_ARGS_JSON = $null
if (!$ControlPlaneOnly -and (Test-Path -LiteralPath $endpoint)) {
    $env:ENDPOINT_PATH = $endpoint
    $env:ENDPOINT_ARGS_JSON = ConvertTo-Json -Compress -InputObject (@("--chrome=$browser", "--user-data-dir=$profileDir") + $EndpointArgs)
} elseif (!$ControlPlaneOnly) {
    Write-Warning 're.exe missing; starting control plane only. Streaming is unavailable.'
}
Write-Host "PRTS viewer: http://localhost:$Port/"
Write-Host "Listening on $BindAddress; press Ctrl+C to stop."
& $node (Join-Path $PSScriptRoot 'control-plane\index.js') --port $Port
exit $LASTEXITCODE
