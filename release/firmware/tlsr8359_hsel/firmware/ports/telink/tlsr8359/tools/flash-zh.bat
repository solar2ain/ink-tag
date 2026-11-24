@echo off
setlocal
set "INKTAG_TLSR_FLASH_LANGUAGE=zh"
call "%~dp0flash.bat" %*
exit /b %errorlevel%
