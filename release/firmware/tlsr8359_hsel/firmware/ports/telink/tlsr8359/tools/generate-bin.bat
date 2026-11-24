@echo off
setlocal
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0generate-bin.ps1" %*
set "inktag_exit=%errorlevel%"
if "%~1"=="" pause
exit /b %inktag_exit%
