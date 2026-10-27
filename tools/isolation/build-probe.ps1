[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutDirectory)
$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetFullPath($OutDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vs = & "$installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$source = Join-Path $PSScriptRoot 'probe.cc'
$script = Join-Path $out 'build.cmd'
$commands = @(
    '@echo off',
    'chcp 65001 >nul',
    "set `"PATH=$installer;%PATH%`"",
    "call `"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul",
    'if errorlevel 1 exit /b 1',
    "cl /nologo /std:c++20 /W4 /WX /MT /EHsc /Fe:`"$out\probe.exe`" /Fo:`"$out\probe.obj`" `"$source`" user32.lib advapi32.lib",
    'if errorlevel 1 exit /b 1',
    "cl /nologo /std:c++20 /W4 /WX /MT /EHsc /DRB_PROBE_HOOK /LD /Fe:`"$out\probe-hook.dll`" /Fo:`"$out\probe-hook.obj`" `"$source`" user32.lib /link /IMPLIB:`"$out\probe-hook.lib`""
)
[IO.File]::WriteAllLines($script, $commands, [Text.UTF8Encoding]::new($false))
Push-Location $out
try {
    & $env:ComSpec /d /c "`"$script`""
    if ($LASTEXITCODE -ne 0) { throw "probe build failed ($LASTEXITCODE)" }
} finally { Pop-Location }
