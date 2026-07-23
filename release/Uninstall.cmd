@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\Launch-Elevated.ps1" -ScriptName Uninstall.ps1
set "result=%errorlevel%"
echo.
if not "%result%"=="0" echo Uninstallation failed with code %result%.
pause
exit /b %result%
