@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\runtime\tools\hardware\goodix-clean-flash.ps1" -Action flash -Port gr551x %*
exit /b %errorlevel%
