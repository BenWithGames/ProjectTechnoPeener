@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-RollbackReport.ps1"
pause
