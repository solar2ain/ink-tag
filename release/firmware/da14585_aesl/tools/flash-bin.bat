@echo off
setlocal
python -B "%~dp0internal\da14585-clean-bin.py" --action flash-bin %*
exit /b %errorlevel%
