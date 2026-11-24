@echo off
setlocal
python -B "%~dp0..\hardware\da14585-clean-bin.py" --action flash-bin %*
exit /b %errorlevel%
