@echo off
setlocal
set "TASK=C:\src\tmp\t1-encoder-tuning"
set "TEMP=%TASK%"
set "TMP=%TASK%"
set "OUTNAME=%~1"
if "%OUTNAME%"=="" set "OUTNAME=tune"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd "%TASK%\build"
cl /nologo /O2 /EHsc /std:c++17 /MD /W4 /DUNICODE /D_UNICODE /DNOMINMAX /I"%TASK%\vendor\libvpl\api" /I"%TASK%\vendor\openh264" "%~dp0tune.cpp" /Fe:%OUTNAME%.exe /link vpl.lib d3d11.lib dxgi.lib dxva2.lib advapi32.lib cfgmgr32.lib version.lib ole32.lib
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
