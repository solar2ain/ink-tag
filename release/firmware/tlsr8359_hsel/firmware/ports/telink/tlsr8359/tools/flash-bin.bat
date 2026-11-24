@echo off
setlocal
set "INKTAG_TLSR_FLASH_RETRY=1"
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0flash-bin.ps1" %*
set "inktag_exit=%errorlevel%"
if "%~1"=="" pause
exit /b %inktag_exit%
