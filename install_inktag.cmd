@echo off
setlocal
where py >nul 2>nul
if errorlevel 1 (
    python "%~dp0install.py" %*
) else (
    py -3 "%~dp0install.py" %*
)
set "installExit=%ERRORLEVEL%"
if not "%installExit%"=="0" echo Install failed. Check Python 3.11+, full 7-Zip and the error above.
exit /b %installExit%
