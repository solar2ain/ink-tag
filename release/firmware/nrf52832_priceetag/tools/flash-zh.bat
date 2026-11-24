@echo off
setlocal DisableDelayedExpansion
for /f "tokens=2 delims=:" %%p in ('chcp') do set "inktagCodePage=%%p"
chcp 65001 >nul
if not "%~1"=="" goto unattended
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0initialization.ps1" -Action preserve -Language zh -InteractiveLaunch
set "inktagExit=%errorlevel%"
echo.
echo 按任意键关闭...
pause >nul
goto finish
:unattended
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0initialization.ps1" -Action preserve -Language zh %*
set "inktagExit=%errorlevel%"
:finish
chcp %inktagCodePage% >nul
exit /b %inktagExit%
