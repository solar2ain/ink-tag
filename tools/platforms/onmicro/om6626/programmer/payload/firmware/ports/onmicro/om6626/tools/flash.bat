@echo off
setlocal DisableDelayedExpansion
set "om_interactive="
set "om_language="
if "%~1"=="" set "om_interactive=-InteractiveFlash"
if "%INKTAG_OM_FLASH_LANGUAGE%"=="zh" set "om_language=-Chinese"
set "om_flash_status=1"
if not exist "%~dp0internal\entry.ps1" (
    >&2 echo OM flash support files are missing. Keep the complete tools directory.
    goto finish
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0internal\entry.ps1" -Action preserve %om_interactive% %om_language% %*
set "om_flash_status=%errorlevel%"
:finish
if not defined om_interactive exit /b %om_flash_status%
if "%om_flash_status%"=="2" echo Canceled.
if not "%om_flash_status%"=="0" if not "%om_flash_status%"=="2" echo Flash failed ^(exit code %om_flash_status%^).
echo Press any key to close this window.
pause >nul
exit /b %om_flash_status%
