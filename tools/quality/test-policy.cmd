@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "QUALITY_TEST_OUT=%TEMP%\remotekit-quality-policy"
if not exist "%QUALITY_TEST_OUT%" mkdir "%QUALITY_TEST_OUT%"
cl /nologo /EHsc /std:c++17 /W4 /WX /I"%~dp0..\.." "%~dp0frame-refresh-test.cc" /Fo"%QUALITY_TEST_OUT%\test.obj" /Fe"%QUALITY_TEST_OUT%\test.exe"
if errorlevel 1 exit /b 1
"%QUALITY_TEST_OUT%\test.exe"
