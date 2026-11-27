@echo off
if exist "%~dp0internal\flash-launch.bat" goto run
chcp 65001 >nul
echo ERROR: Missing launcher "%~dp0internal\flash-launch.bat".
if "%~1"=="" (
  echo Press any key to close.
  pause >nul
)
exit /b 1
:run
call "%~dp0internal\flash-launch.bat" en %*
exit /b %errorlevel%
