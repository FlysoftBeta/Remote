$ErrorActionPreference = 'Stop'
$taskRoot = 'C:/src/tmp/t1-encoder-tuning'
$env:TEMP = $taskRoot
$env:TMP = $taskRoot
$setupProcess=Get-Process -Id $PID
$setupProcess.ProcessorAffinity=255
$setupProcess.PriorityClass='BelowNormal'
New-Item -ItemType Directory -Force "$taskRoot/media","$taskRoot/results","$taskRoot/bin","$taskRoot/vendor/openh264","$taskRoot/build" | Out-Null
if (!(Test-Path "$taskRoot/bin/ffmpeg")) {
  curl.exe -L --fail --retry 3 --retry-all-errors -o "$taskRoot/ffmpeg.zip" https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip
  if ($LASTEXITCODE) { throw 'ffmpeg download failed' }
  Expand-Archive "$taskRoot/ffmpeg.zip" "$taskRoot/bin/ffmpeg"
}
if (!(Test-Path "$taskRoot/bin/openh264.dll")) {
  curl.exe -L --fail --retry 3 --retry-all-errors -o "$taskRoot/bin/openh264.dll.bz2" https://ciscobinary.openh264.org/openh264-2.6.0-win64.dll.bz2
  if ($LASTEXITCODE) { throw 'OpenH264 download failed' }
  & 'C:/Program Files/Git/usr/bin/bzip2.exe' -dk "$taskRoot/bin/openh264.dll.bz2"
}
foreach ($file in 'codec_api.h','codec_app_def.h','codec_def.h','codec_ver.h') {
  if (!(Test-Path "$taskRoot/vendor/openh264/$file")) {
    curl.exe -sS -L --fail --retry 3 --retry-all-errors -o "$taskRoot/vendor/openh264/$file" "https://raw.githubusercontent.com/cisco/openh264/v2.6.0/codec/api/wels/$file"
    if ($LASTEXITCODE) { throw "Header download failed: $file" }
  }
}
# Reuse only the S7 upstream dispatcher and headers, in this task's own directory.
if (!(Test-Path "$taskRoot/vendor/libvpl")) { Copy-Item "$PSScriptRoot/../../endpoint/third_party/libvpl" "$taskRoot/vendor/libvpl" -Recurse }
if (!(Test-Path "$taskRoot/build/vpl.lib")) { Copy-Item C:/src/tmp/s7/build/vpl.lib "$taskRoot/build/vpl.lib" }
$ffmpeg = (Get-ChildItem "$taskRoot/bin/ffmpeg/*/bin/ffmpeg.exe").FullName
& $ffmpeg -hide_banner -y -threads 4 -ss 30 -i https://download.blender.org/demo/movies/ToS/tears_of_steel_720p.mov -t 11 -an -c:v copy -avoid_negative_ts make_zero "$taskRoot/media/natural-source.mkv"
if ($LASTEXITCODE) { throw 'Natural source extraction failed' }
& $ffmpeg -hide_banner -y -threads 4 -filter_threads 4 -i "$taskRoot/media/natural-source.mkv" -vf 'scale=1920:802:flags=bicubic:out_color_matrix=bt709:out_range=tv,pad=1920:1080:0:138:color=black,fps=60,format=nv12' -frames:v 600 -an -f rawvideo "$taskRoot/media/natural-60.nv12"
if ($LASTEXITCODE) { throw 'Natural reference generation failed' }
node tools/encoder-tuning/media.mjs
cmd /c tools\encoder-tuning\build.bat
Get-FileHash "$taskRoot/ffmpeg.zip","$taskRoot/bin/openh264.dll" -Algorithm SHA256
