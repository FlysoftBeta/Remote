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
$native = Join-Path $PSScriptRoot 're\re_native.dll'
$browser = Join-Path $PSScriptRoot 'rb\chrome.exe'
$env:HOST = $BindAddress
$env:ENDPOINT_PATH = $null
$env:ENDPOINT_ARGS = $null
$env:ENDPOINT_ARGS_JSON = $null
if (!$ControlPlaneOnly -and (Test-Path -LiteralPath $endpoint) -and (Test-Path -LiteralPath $native)) {
    $env:ENDPOINT_PATH = $endpoint
    $env:RE_NATIVE_DLL = $native
    $env:ENDPOINT_ARGS_JSON = ConvertTo-Json -Compress -InputObject (@("--chrome=$browser") + $EndpointArgs)
} elseif (!$ControlPlaneOnly) {
    Write-Warning 're.exe / re_native.dll missing; starting control plane only. Streaming is unavailable.'
}
Write-Host "PRTS viewer: http://localhost:$Port/"
Write-Host "Listening on $BindAddress; press Ctrl+C to stop."
& $node (Join-Path $PSScriptRoot 'control-plane\index.js') --port $Port
exit $LASTEXITCODE
