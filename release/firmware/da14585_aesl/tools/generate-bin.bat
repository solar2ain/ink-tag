@echo off
setlocal
python -B "%~dp0internal\da14585-clean-bin.py" --action generate %*
exit /b %errorlevel%
