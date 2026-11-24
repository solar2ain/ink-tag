@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\setup.ps1" %*
set "INKTAG_SETUP_RESULT=%errorlevel%"
if "%~1"=="" pause
exit /b %INKTAG_SETUP_RESULT%
