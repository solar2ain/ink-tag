@echo off
setlocal
set "INKTAG_TLSR_RELEASE_TOOLS=%~dp0"
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0internal\serial\generate-bin.ps1" %*
set "inktag_exit=%errorlevel%"
if "%~1"=="" pause
exit /b %inktag_exit%
