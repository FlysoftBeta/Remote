# Requires Windows PowerShell 5.1+, pnpm build and a completed Windows x64 release build.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')][string]$Version,
    [Parameter(Mandatory)][string]$Out,
    [string]$ChromeOut = 'C:\src\chromium\src\out\rel',
    [string]$ChromiumSrc = 'C:\src\chromium\src',
    [string]$ReExe = '',
    [string[]]$ReRuntimeDll = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$ReExe) { $ReExe = Join-Path $ChromeOut 're.exe' }
$Out = [IO.Path]::GetFullPath($Out)
$ChromeOut = (Resolve-Path -LiteralPath $ChromeOut).Path
$name = "remotekit-$Version-win-x64"
$stage = Join-Path $Out $name
$archive = Join-Path $Out "$name.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive) -or
    (Test-Path -LiteralPath "$archive.001")) {
    throw "Output already exists for $Version; use a new -Out or version."
}
foreach ($built in @('control-plane\dist\server.mjs', 'viewer\dist\index.html')) {
    if (!(Test-Path -LiteralPath (Join-Path $repo $built))) { throw "Missing $built; run corepack pnpm install and corepack pnpm build." }
}
foreach ($package in @('package.json', 'control-plane\package.json', 'viewer\package.json')) {
    if ((Get-Content -Raw -LiteralPath (Join-Path $repo $package) | ConvertFrom-Json).version -ne $Version) { throw "Version mismatch: $package" }
}
foreach ($required in @('chrome.exe', 'chrome.dll', 'chrome_elf.dll', 'resources.pak',
    'chrome_100_percent.pak', 'chrome_200_percent.pak', 'icudtl.dat', 'locales\en-US.pak',
    'WidevineCdm\manifest.json', 'WidevineCdm\_platform_specific\win_x64\widevinecdm.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $ChromeOut $required))) { throw "Missing browser runtime: $required" }
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($dir in @('rb', 're', 'control-plane', 'viewer', 'node', 'LICENSES')) {
    New-Item -ItemType Directory -Path (Join-Path $stage $dir) | Out-Null
}
function Copy-RuntimeFile([string]$Source, [string]$Destination) {
    if ([IO.Path]::GetExtension($Source) -in @('.pdb', '.lib', '.obj', '.exp')) { return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
}

# mini_installer's input maps application and version directories. A portable
# tree flattens both next to chrome.exe. Optional features are included if built.
$manifest = Join-Path $ChromiumSrc 'chrome\installer\mini_installer\chrome.release'
$section = ''
$patterns = @()
foreach ($line in Get-Content -LiteralPath $manifest) {
    if ($line -match '^\[(.+)\]') { $section = $Matches[1]; continue }
    if ($section -in @('GENERAL', 'HIDPI', 'FFMPEG', 'SNAPSHOTBLOB', 'DXC') -and
        $line -match '^([^#\s][^:]*):') { $patterns += $Matches[1] }
}
# Widevine is under GOOGLE_CHROME in the upstream manifest, but our Chromium
# build explicitly enables bundle_widevine_cdm. Keep its complete runtime tree.
$patterns += @('*.pak', '*.manifest', '*snapshot*.bin', 'WidevineCdm\*',
    'msvcp140*.dll', 'vcruntime140*.dll', 'vccorlib140.dll')
foreach ($pattern in $patterns | Select-Object -Unique) {
    foreach ($file in Get-ChildItem -Path (Join-Path $ChromeOut $pattern) -File -Recurse:$($pattern -eq 'WidevineCdm\*') -ErrorAction SilentlyContinue) {
        $relative = $file.FullName.Substring($ChromeOut.Length + 1)
        Copy-RuntimeFile $file.FullName (Join-Path $stage "rb\$relative")
    }
}

$endpointPresent = Test-Path -LiteralPath $ReExe -PathType Leaf
if ($endpointPresent) {
    Copy-RuntimeFile $ReExe (Join-Path $stage 're\re.exe')
} else {
    Write-Warning "Endpoint incomplete; skipping re.exe. Input: $ReExe"
}
# re.exe from the non-component release build links libwebrtc, oneVPL and OpenH264
# statically; it needs only the MSVC runtime. A component build (out\dev) also needs
# its component DLLs: pass them with -ReRuntimeDll. Do not bundle graphics-driver DLLs.
foreach ($pattern in @('msvcp140*.dll', 'vcruntime140*.dll', 'vccorlib140.dll')) {
    foreach ($dll in Get-ChildItem -Path (Join-Path $ChromeOut $pattern) -File) {
        Copy-RuntimeFile $dll.FullName (Join-Path $stage "re\$($dll.Name)")
    }
}
foreach ($dll in $ReRuntimeDll) {
    if ([IO.Path]::GetExtension($dll) -ne '.dll') { throw "Expected runtime DLL: $dll" }
    Copy-RuntimeFile (Resolve-Path -LiteralPath $dll).Path (Join-Path $stage "re\$([IO.Path]::GetFileName($dll))")
}

# Pin both version and digest, from nodejs.org's release SHASUMS256.txt.
$nodeVersion = '24.22.1'
$nodeSha256 = 'd2046d28c2c3dcd211ac5145c0bf30e2047ef9dfb306693c8c6e2eccd8b95fa5'
$nodeName = "node-v$nodeVersion-win-x64"
$nodeUrl = "https://nodejs.org/dist/v$nodeVersion/$nodeName.zip"
$cache = 'C:\src\tmp\remotekit-release-cache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$nodeZip = Join-Path $cache "$nodeName.zip"
if (!(Test-Path -LiteralPath $nodeZip)) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $download = "$nodeZip.download"
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe --fail --silent --show-error --location --retry 3 --max-time 300 --output $download $nodeUrl
        if ($LASTEXITCODE -ne 0) { throw "Node download failed: $LASTEXITCODE" }
    } else {
        $previousProgress = $ProgressPreference
        try {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -UseBasicParsing -Uri $nodeUrl -OutFile $download -TimeoutSec 300
        } finally { $ProgressPreference = $previousProgress }
    }
    Move-Item -LiteralPath $download -Destination $nodeZip
}
if ((Get-FileHash -LiteralPath $nodeZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $nodeSha256) {
    throw "Node archive SHA-256 mismatch: $nodeZip"
}
# Fresh extraction after verification; never trust previously extracted cache files.
$nodeExtract = Join-Path $cache ([Guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $nodeZip -DestinationPath $nodeExtract
$nodeRoot = Join-Path $nodeExtract $nodeName
Copy-Item -LiteralPath (Join-Path $nodeRoot 'node.exe') -Destination (Join-Path $stage 'node\node.exe')
Copy-Item -LiteralPath (Join-Path $nodeRoot 'LICENSE') -Destination (Join-Path $stage 'LICENSES\Node-LICENSE.txt')

# esbuild bundles the locked ws runtime into this ESM server; no dev dependencies or npm at runtime.
Copy-RuntimeFile (Join-Path $repo 'control-plane\dist\server.mjs') (Join-Path $stage 'control-plane\dist\server.mjs')
@{ name = 'remotekit-control-plane'; version = $Version; type = 'module'; main = 'dist/server.mjs' } |
    ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $stage 'control-plane\package.json')
"import './dist/server.mjs';" | Set-Content -Encoding ASCII -LiteralPath (Join-Path $stage 'control-plane\index.js')
Copy-Item -Path (Join-Path $repo 'viewer\dist') -Destination (Join-Path $stage 'viewer\dist') -Recurse
# Keep all harness pages and measurement helpers reachable at their existing /tools URLs.
Copy-Item -LiteralPath (Join-Path $repo 'tools') -Destination (Join-Path $stage 'tools') -Recurse
foreach ($entry in @(
    @('control-plane\node_modules\ws\LICENSE', 'ws.txt'),
    @('viewer\node_modules\react\LICENSE', 'React.txt'),
    @('viewer\node_modules\react-dom\LICENSE', 'React-DOM.txt')
)) { Copy-RuntimeFile (Join-Path $repo $entry[0]) (Join-Path $stage ('LICENSES\' + $entry[1])) }
foreach ($file in @('start.ps1', 'start.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.release.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSES.md') -Destination (Join-Path $stage 'LICENSES\README.md')
$licenses = @{
    'Chromium.txt' = (Join-Path $ChromiumSrc 'LICENSE')
    'WebRTC.txt' = (Join-Path $ChromiumSrc 'third_party\webrtc\LICENSE')
    'Chromium-Credits.html' = (Join-Path $ChromeOut 'gen\components\resources\about_credits.html')
    'nv-codec-headers.txt' = (Join-Path $repo 're\third_party\nv-codec-headers\LICENSE')
    'oneVPL.txt' = (Join-Path $repo 're\third_party\libvpl\LICENSE')
    'OpenH264.txt' = (Join-Path $ChromiumSrc 'third_party\openh264\src\LICENSE')
}
foreach ($entry in $licenses.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $stage "LICENSES\$($entry.Key)")
}
$widevineLicense = Join-Path $stage 'rb\WidevineCdm\LICENSE'
if (Test-Path -LiteralPath $widevineLicense) {
    Copy-Item -LiteralPath $widevineLicense -Destination (Join-Path $stage 'LICENSES\Widevine.txt')
}
@{
    version = $Version; platform = 'win-x64'; endpointPresent = $endpointPresent
    commit = (& git -C $repo rev-parse HEAD); controlPlane = 'TypeScript ESM bundle (ws included)'; viewer = 'React + TypeScript / Vite'
    chromeOut = $ChromeOut; browserManifest = $manifest; reExe = $ReExe
    nodeVersion = $nodeVersion; nodeUrl = $nodeUrl; nodeSha256 = $nodeSha256
} | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $stage 'BUILD-INFO.json')
$fileHashes = foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
}
$fileHashes | Set-Content -Encoding ASCII -LiteralPath (Join-Path $stage 'FILES.sha256')

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $true)
$assets = @($archive)
# Use binary chunks only if needed; 1.9 GB leaves margin below GitHub's 2 GB limit.
$maxAssetBytes = 1900000000L
if ((Get-Item -LiteralPath $archive).Length -ge $maxAssetBytes) {
    $assets = @()
    $inputStream = [IO.File]::OpenRead($archive)
    try {
        $buffer = New-Object byte[] (4MB)
        $part = 1
        while ($inputStream.Position -lt $inputStream.Length) {
            $partPath = '{0}.{1:d3}' -f $archive, $part
            $outputStream = [IO.File]::Create($partPath)
            try {
                $remaining = $maxAssetBytes
                while ($remaining -gt 0 -and ($read = $inputStream.Read($buffer, 0, [int][Math]::Min($buffer.Length, $remaining))) -gt 0) {
                    $outputStream.Write($buffer, 0, $read); $remaining -= $read
                }
            } finally { $outputStream.Dispose() }
            $assets += $partPath; $part++
        }
    } finally { $inputStream.Dispose() }
    # This archive was created above; the chunks are the publishable assets.
    Remove-Item -LiteralPath $archive
    $instructions = Join-Path $Out "$name-REASSEMBLE.txt"
    "In cmd.exe, run: copy /b $name.zip.??? $name.zip`r`nThen extract the zip normally. SHA256SUMS.txt covers each part." |
        Set-Content -Encoding ASCII -LiteralPath $instructions
    $assets += $instructions
}
$sumFile = Join-Path $Out 'SHA256SUMS.txt'
$sums = foreach ($asset in $assets) {
    if ((Get-Item -LiteralPath $asset).Length -ge 2000000000L) { throw "Asset exceeds GitHub size limit: $asset" }
    '{0}  {1}' -f (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($asset)
}
$sums | Add-Content -Encoding ASCII -LiteralPath $sumFile
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
[PSCustomObject]@{
    Folder = $stage; FileCount = $files.Count
    UncompressedBytes = ($files | Measure-Object Length -Sum).Sum
    ArchiveBytes = ($assets | ForEach-Object { (Get-Item -LiteralPath $_).Length } | Measure-Object -Sum).Sum
    Assets = $assets; Checksums = $sumFile; EndpointPresent = $endpointPresent
}
