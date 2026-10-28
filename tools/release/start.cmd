@echo off
rem Portable TypeScript/React build; all arguments are forwarded to start.ps1.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
exit /b %errorlevel%
