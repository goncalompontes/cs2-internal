@echo off
setlocal
pushd "%~dp0.."
where cl >nul 2>&1
if errorlevel 1 (
  if not exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo Open an x64 Native Tools Command Prompt for Visual Studio, then rerun this script.
    exit /b 1
  )
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not exist build-autostop mkdir build-autostop
cl /nologo /std:c++latest /EHsc /fp:strict /W4 /I payload\aim\include payload\aim\src\autostop.cpp payload\aim\tests\test_autostop.cpp /Fo:build-autostop\ /Fe:build-autostop\autostop_test.exe
if errorlevel 1 exit /b 1
build-autostop\autostop_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /fp:strict /W4 /I payload\aim\include /I payload\control\include /I payload\game\include payload\aim\src\autostop.cpp payload\control\src\autostop_input.cpp payload\aim\tests\test_autostop_input.cpp /Fo:build-autostop\ /Fe:build-autostop\autostop_input_test.exe
if errorlevel 1 exit /b 1
build-autostop\autostop_input_test.exe
exit /b %errorlevel%
