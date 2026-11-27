@echo off
setlocal
set "INKTAG_TLSR_RELEASE_TOOLS=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\serial\setup.ps1" %*
set "INKTAG_SETUP_RESULT=%errorlevel%"
if "%~1"=="" pause
exit /b %INKTAG_SETUP_RESULT%
