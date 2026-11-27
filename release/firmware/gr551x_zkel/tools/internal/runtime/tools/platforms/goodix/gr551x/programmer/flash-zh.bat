@echo off
setlocal
set "INKTAG_GR_FLASH_LANGUAGE=zh"
call "%~dp0flash.bat" %*
exit /b %errorlevel%
