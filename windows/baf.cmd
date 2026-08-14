@echo off
REM Shim so `baf` works from cmd.exe exactly like it does from a shell.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0baf.ps1" %*
exit /b %ERRORLEVEL%
