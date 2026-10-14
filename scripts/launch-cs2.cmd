@echo off
rem launch-cs2.cmd — launch the target game (CS2, Steam appid 730) with the
rem offline test flags, via the Steam that lives on the interactive desktop.
rem
rem IMPORTANT: run this through the CS2Launch scheduled task, which executes in
rem the interactive session (session 1) where the GPU and desktop exist:
rem
rem     schtasks /run /tn CS2Launch
rem     powershell -c "Start-ScheduledTask -Name CS2Launch"
rem
rem Launching cs2.exe / steam.exe straight from an agent/SSH/service shell runs
rem in session 0, which has no GPU, so the game cannot initialise Direct3D.
rem
rem Extra arguments passed to this script are appended after the default flags.
setlocal
set "STEAM=C:\Program Files (x86)\Steam\steam.exe"
set "APPID=730"
set "FLAGS=-insecure -novid -vulkan"

rem Payload + Vulkan overlay layer configuration (see docs/handoff.md).
rem - PUB_THREAD: the payload's safe dedicated publish thread (native Windows).
rem - CS2_OVERLAY: enables the implicit Vulkan layer (registered under
rem   HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers; see overlay/README.md).
rem - Outputs go under %LOCALAPPDATA%\cs2-internal (a Medium-integrity game
rem   process cannot write C:\ root).
set "CS2_PAYLOAD_PUB_THREAD=1"
set "CS2_OVERLAY=1"
set "CS2_OVERLAY_LOG=%LOCALAPPDATA%\cs2-internal\cs2-overlay-layer.log"
if not exist "%LOCALAPPDATA%\cs2-internal" mkdir "%LOCALAPPDATA%\cs2-internal"

"%STEAM%" -applaunch %APPID% %FLAGS% %*
endlocal
