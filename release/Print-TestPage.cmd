@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\Print-TestPage.ps1"
set "result=%errorlevel%"
echo.
pause
exit /b %result%
