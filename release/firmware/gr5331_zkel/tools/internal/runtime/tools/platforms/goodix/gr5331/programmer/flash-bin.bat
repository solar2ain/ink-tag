@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\..\..\tools\hardware\goodix-clean-flash.ps1" -Action flash -Port gr5331 %*
exit /b %errorlevel%
