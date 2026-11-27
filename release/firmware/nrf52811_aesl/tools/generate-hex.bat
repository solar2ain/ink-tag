@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0internal\initialization.ps1" -Action Generate %*
exit /b %errorlevel%
