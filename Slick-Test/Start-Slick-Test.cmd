@echo off
setlocal
cd /d "%~dp0..\handling" || exit /b 1
call "Start-Handling.cmd" %*
