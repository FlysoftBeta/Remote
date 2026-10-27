# Grant sandboxed Chromium processes read/execute access to deployed binaries.
# Run elevated after copying a component build; optionally include the directory
# that will contain dedicated user-data directories. Profiles themselves must
# remain package-specific; never inherit shared package RX into tenant data.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [string]$ProfileParent
)

$ErrorActionPreference = 'Stop'
foreach ($directory in @($BuildDirectory)) {
    if ([string]::IsNullOrWhiteSpace($directory)) { continue }
    $resolved = (Resolve-Path -LiteralPath $directory).ProviderPath
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "Not a directory: $resolved"
    }
    # Numeric SIDs work on Windows installations in any display language.
    # /grant:r replaces this principal's explicit grant, making reruns idempotent.
    & icacls.exe $resolved /grant:r '*S-1-15-2-1:(OI)(CI)(RX)' '*S-1-15-2-2:(OI)(CI)(RX)'
    if ($LASTEXITCODE -ne 0) { throw "icacls failed for $resolved ($LASTEXITCODE)" }
}
if (-not [string]::IsNullOrWhiteSpace($ProfileParent)) {
    & "$PSScriptRoot\grant-traversal.ps1" -Directory $ProfileParent
}
