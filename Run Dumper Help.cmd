@echo off
setlocal
pushd "%~dp0"
set "DUMPER=%~dp0cs2-dumper.exe"
if not exist "%DUMPER%" set "DUMPER=%~dp0external\dumper\target\release\cs2-dumper.exe"
if not exist "%DUMPER%" (
  echo Dumper not found. Extract the entire package first.
  pause
  exit /b 2
)
"%DUMPER%" --help
set "RESULT=%ERRORLEVEL%"
echo.
echo The dumper is a command-line calibration tool. payload.dll and the overlay DLL are libraries, not applications.
pause
exit /b %RESULT%
