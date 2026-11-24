@echo off
setlocal
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
if "%~2"=="" goto guided
python -u -B "%~dp0..\hardware\da14585-clean-bin.py" --action flash --language %*
exit /b %errorlevel%
:guided
chcp 65001 >nul
python -u -B "%~dp0..\hardware\da14585-clean-bin.py" --action flash --guided --language "%~1"
set "inktag_result=%errorlevel%"
echo.
if /I "%~1"=="zh" (echo 操作已结束，退出码 %inktag_result%。需要 Python 3.12 或更新版本。按任意键关闭窗口。) else (echo Finished with exit code %inktag_result%. Python 3.12 or later is required. Press any key to close.)
pause >nul
exit /b %inktag_result%
