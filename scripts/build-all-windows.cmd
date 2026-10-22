@echo off
setlocal
pushd "%~dp0.."
where cl >nul 2>&1
if errorlevel 1 (
  if not exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    echo Run from an x64 Visual Studio developer prompt.
    exit /b 1
  )
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not exist build-tools\Scripts\meson.exe (
  py -3 -m venv build-tools
  if errorlevel 1 exit /b 1
  build-tools\Scripts\python.exe -m pip install meson ninja
  if errorlevel 1 exit /b 1
)
set "PATH=%CD%\build-tools\llvm\bin;%CD%\build-tools\Scripts;%PATH%"
set CC=clang-cl
set CXX=clang-cl
where clang-cl >nul 2>&1
if errorlevel 1 (
  echo clang-cl is required on PATH or under build-tools\llvm\bin.
  exit /b 1
)
if not exist external\Vulkan-Headers\include\vulkan\vulkan.h (
  git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers external/Vulkan-Headers
  if errorlevel 1 exit /b 1
)
cargo build --release --manifest-path external/dumper/Cargo.toml
if errorlevel 1 exit /b 1
if not exist calibration\offsets.json (
  echo Missing calibration\offsets.json. Generate calibration before building a game test.
  exit /b 1
)
call :build build .
if errorlevel 1 exit /b 1
call :build build-payload payload
if errorlevel 1 exit /b 1
call :build build-injector injector
if errorlevel 1 exit /b 1
call :build build-overlay overlay
if errorlevel 1 exit /b 1
meson test -C build --print-errorlogs
if errorlevel 1 exit /b 1
meson test -C build-payload --print-errorlogs
if errorlevel 1 exit /b 1
meson test -C build-overlay --print-errorlogs
exit /b %errorlevel%

:build
if not exist %1\build.ninja (
  meson setup %1 %2
  if errorlevel 1 exit /b 1
)
meson compile -C %1
exit /b %errorlevel%
