@echo off
setlocal
pushd "%~dp0.."
where cl >nul 2>&1
if errorlevel 1 (
  if not exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo Open an x64 Native Tools Command Prompt for Visual Studio, then rerun.
    exit /b 1
  )
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not exist build-rage-field mkdir build-rage-field
set INC=/I payload\aim\include /I payload\game\include /I payload\control\include
cl /nologo /O2 /std:c++latest /EHsc /fp:strict /W4 /c %INC% payload\aim\src\geometry.cpp payload\aim\src\field.cpp payload\aim\src\hitbox.cpp payload\aim\src\ballistics.cpp payload\aim\src\solver.cpp /Fo:build-rage-field\
if errorlevel 1 exit /b 1
set CORE=build-rage-field\geometry.obj build-rage-field\field.obj build-rage-field\hitbox.obj build-rage-field\ballistics.obj build-rage-field\solver.obj
for %%T in (test_aim_geometry test_aim_field test_aim_solver test_heatmap) do (
  cl /nologo /O2 /std:c++latest /EHsc /fp:strict /W4 %INC% payload\aim\tests\%%T.cpp %CORE% /Fo:build-rage-field\ /Fe:build-rage-field\%%T.exe
  if errorlevel 1 exit /b 1
  build-rage-field\%%T.exe
  if errorlevel 1 exit /b 1
)
cl /nologo /O2 /std:c++latest /EHsc /fp:strict /W4 %INC% payload\aim\src\reader.cpp payload\aim\tests\test_target_charts.cpp %CORE% /Fo:build-rage-field\ /Fe:build-rage-field\test_target_charts.exe
if errorlevel 1 exit /b 1
build-rage-field\test_target_charts.exe
if errorlevel 1 exit /b 1
cl /nologo /O2 /std:c++latest /EHsc /fp:strict /W4 %INC% payload\control\src\json.cpp payload\control\src\aim_options.cpp payload\control\tests\test_aim_options.cpp %CORE% /Fo:build-rage-field\ /Fe:build-rage-field\test_aim_options.exe
if errorlevel 1 exit /b 1
build-rage-field\test_aim_options.exe
exit /b %errorlevel%
