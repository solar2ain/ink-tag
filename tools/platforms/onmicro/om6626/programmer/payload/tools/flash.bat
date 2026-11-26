@echo off
setlocal DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0check.ps1" -OfflinePackageOnly
set "om_package_status=%errorlevel%"
if not "%om_package_status%"=="0" (
    if "%~1"=="" (
        echo Press any key to close this window.
        pause >nul
    )
    exit /b %om_package_status%
)
call "%~dp0..\firmware\ports\onmicro\om6626\tools\flash.bat" %*
exit /b %errorlevel%
