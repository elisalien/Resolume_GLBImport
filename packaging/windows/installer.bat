@echo off
REM Lance le script d installation PowerShell qui se trouve a cote.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
