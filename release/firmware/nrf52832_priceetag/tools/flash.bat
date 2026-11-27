@echo off
setlocal DisableDelayedExpansion
if not "%~1"=="" goto unattended
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0internal\initialization.ps1" -Action preserve -InteractiveLaunch
set "inktagExit=%errorlevel%"
echo.
echo Press any key to close...
pause >nul
exit /b %inktagExit%
:unattended
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\initialization.ps1" -Action preserve %*
exit /b %errorlevel%
