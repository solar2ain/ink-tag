@echo off
setlocal DisableDelayedExpansion
if not exist "%~dp0flash.bat" (
    >&2 echo OM flash.bat is missing. Keep the complete tools directory.
    if "%~1"=="" pause
    exit /b 1
)
set "INKTAG_OM_FLASH_LANGUAGE=zh"
call "%~dp0flash.bat" %*
exit /b %errorlevel%
