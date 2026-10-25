@echo off
setlocal
rem Vendored upstream libvpl 2.17.0; extraction instructions are in the report.
set "VPL=C:\src\tmp\s7\vendor\prtsd\vendor\libvpl"
if not "%~1"=="" set "VPL=%~1"
set "OUT=C:\src\tmp\s7\build"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd "%OUT%"
if exist vpl.lib goto probe
cl /nologo /c /O2 /EHsc /std:c++17 /MD /MP4 /DUNICODE /D_UNICODE /DNOMINMAX /FIobjbase.h /I"%VPL%\api" /I"%VPL%\libvpl" /I"%VPL%\libvpl\src" "%VPL%\libvpl\src\*.cpp" "%VPL%\libvpl\src\windows\*.cpp" "%VPL%\libvpl\src\mfx_config_interface\*.cpp"
if errorlevel 1 goto failed
lib /nologo /out:vpl.lib *.obj
if errorlevel 1 goto failed
:probe
cl /nologo /O2 /EHsc /std:c++17 /MD /W4 /DUNICODE /D_UNICODE /DNOMINMAX /I"%VPL%\api" "%~dp0s7probe.cpp" /Fe:s7probe.exe /link vpl.lib d3d11.lib dxgi.lib dxva2.lib advapi32.lib cfgmgr32.lib version.lib ole32.lib
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
