@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\runtime\tools\hardware\goodix-clean-flash.ps1" -Action generate -Port gr5331 %*
exit /b %errorlevel%
