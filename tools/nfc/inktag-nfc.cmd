@echo off
python -X utf8 -u "%~dp0..\hardware\inktag_nfc.py" %*
exit /b %errorlevel%
