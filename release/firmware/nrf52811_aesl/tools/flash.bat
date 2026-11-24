@echo off
setlocal
chcp 65001 >nul
set "inktag_interactive="
if "%~1"=="" set "inktag_interactive=-Interactive"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0initialization.ps1" -Action Preserve -Language en %inktag_interactive% %*
set "inktag_exit=%errorlevel%"
if "%~1"=="" (
    echo.
    echo Exit code: %inktag_exit%. Press any key to close.
    pause >nul
)
exit /b %inktag_exit%
