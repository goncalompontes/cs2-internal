@echo off
setlocal
pushd "%~dp0"
echo This registers this package's Vulkan overlay manifest. Windows may ask for administrator approval.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-overlay.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo Setup failed or the Windows prompt was cancelled. No game restart was performed.
pause
exit /b %RESULT%
