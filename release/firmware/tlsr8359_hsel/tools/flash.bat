@echo off
setlocal
set "INKTAG_TLSR_RELEASE_TOOLS=%~dp0"
set "INKTAG_TLSR_FLASH_RETRY=1"
set "INKTAG_TLSR_FLASH_GUIDED="
if "%~1"=="" set "INKTAG_TLSR_FLASH_GUIDED=1"
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0internal\serial\flash.ps1" -AllowMissingUid %*
set "inktag_exit=%errorlevel%"
if defined INKTAG_TLSR_FLASH_GUIDED pause
exit /b %inktag_exit%
