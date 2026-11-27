@echo off
if exist "%~dp0internal\flash-launch.bat" goto run
chcp 65001 >nul
echo 错误：缺少启动器 "%~dp0internal\flash-launch.bat"。
if "%~1"=="" (
  echo 按任意键关闭窗口。
  pause >nul
)
exit /b 1
:run
call "%~dp0internal\flash-launch.bat" zh %*
exit /b %errorlevel%
