@echo off
if exist "%~dp0flash-launch.bat" goto run
chcp 65001 >nul
echo ERROR: Missing launcher "%~dp0flash-launch.bat".
if "%~1"=="" (
  echo Press any key to close.
  pause >nul
)
exit /b 1
:run
call "%~dp0flash-launch.bat" en %*
exit /b %errorlevel%
