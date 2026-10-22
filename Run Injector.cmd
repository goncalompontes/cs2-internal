@echo off
setlocal
pushd "%~dp0"
set "INJECTOR=%~dp0injector.exe"
set "PAYLOAD=%~dp0payload.dll"
if not exist "%INJECTOR%" set "INJECTOR=%~dp0build-injector\injector.exe"
if not exist "%PAYLOAD%" set "PAYLOAD=%~dp0build-payload\payload.dll"
set "LOG=%~dp0injector-last-run.log"
if not exist "%INJECTOR%" (
  echo Injector not found. Extract the entire package before running this launcher.
  pause
  exit /b 2
)
if "%~1"=="" (
  if not exist "%PAYLOAD%" (
    echo Payload not found. Keep payload.dll beside this launcher or build the payload.
    pause
    exit /b 2
  )
  echo Running injector for cs2.exe with the bundled payload. Please wait...
  "%INJECTOR%" cs2.exe "%PAYLOAD%" --load-library >"%LOG%" 2>&1
) else (
  "%INJECTOR%" %* >"%LOG%" 2>&1
)
set "RESULT=%ERRORLEVEL%"
type "%LOG%"
echo.
echo Injector exit code: %RESULT%
echo Saved output: "%LOG%"
if not "%RESULT%"=="0" echo If OpenProcess reports error 5, check that the injector has the same Windows permission level as the game.
echo Press any key to close this window.
pause >nul
exit /b %RESULT%
