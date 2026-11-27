@echo off
setlocal DisableDelayedExpansion
set "INKTAG_TLSR_RELEASE_TOOLS=%~dp0"
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0internal\serial\backup.ps1" %*
exit /b %errorlevel%
