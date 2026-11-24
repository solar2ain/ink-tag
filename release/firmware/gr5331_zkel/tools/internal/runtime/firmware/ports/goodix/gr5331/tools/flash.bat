@echo off
setlocal
if "%~1"=="" goto interactive
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\..\..\tools\hardware\goodix-clean-flash.ps1" -Action preserve -Port gr5331 %*
exit /b %errorlevel%

:interactive
if not "%INKTAG_GR_FLASH_LANGUAGE%"=="zh" set "INKTAG_GR_FLASH_LANGUAGE=en"
for /f "tokens=2 delims=:" %%p in ('chcp') do set "_inktagPreviousCodePage=%%p"
chcp 65001 >nul
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0..\..\..\..\..\tools\hardware\goodix-clean-flash.ps1" -Action preserve -Port gr5331 -InteractiveMenu -Language %INKTAG_GR_FLASH_LANGUAGE% -ToolDirectory "%~dp0."
set "_inktagFlashStatus=%errorlevel%"
if "%INKTAG_GR_FLASH_LANGUAGE%"=="zh" (echo 按任意键关闭窗口。) else (echo Press any key to close this window.)
pause >nul
if defined _inktagPreviousCodePage chcp %_inktagPreviousCodePage% >nul
exit /b %_inktagFlashStatus%
