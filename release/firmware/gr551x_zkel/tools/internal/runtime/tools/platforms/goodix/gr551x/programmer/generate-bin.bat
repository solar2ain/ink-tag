@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\..\..\tools\hardware\goodix-clean-flash.ps1" -Action generate -Port gr551x %*
exit /b %errorlevel%
