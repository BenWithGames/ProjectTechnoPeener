@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Network-Test.ps1" -Observe
echo.
pause
