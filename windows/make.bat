@echo off
REM Builds bafc.exe. Run this once before using windows\baf.cmd.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
