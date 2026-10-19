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
rem Steam path: defaults to the standard install, but an existing STEAM
rem environment variable wins (set it if Steam lives elsewhere, e.g. another
rem drive). See docs/setup-windows.md.
if not defined STEAM set "STEAM=C:\Program Files (x86)\Steam\steam.exe"
set "APPID=730"

rem Render path (workstream A). `present` (the default) renders the ESP + menu
rem **in-process** from the game's D3D11/DXGI Present: no Vulkan layer, no -vulkan.
rem `layer` keeps the external Vulkan present layer (`overlay/`), which needs the
rem Vulkan backend and the implicit-layer registration.
if not defined CS2_PAYLOAD_RENDER set "CS2_PAYLOAD_RENDER=present"
set "FLAGS=-insecure -novid"

rem Payload + Vulkan overlay layer configuration (see docs/handoff.md).
rem - CS2_PAYLOAD_RENDER: `present` (in-process DXGI) or `layer` (Vulkan layer).
rem - CS2_OVERLAY: enables the implicit Vulkan layer (registered under
rem   HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers; see overlay/README.md).
rem - MOVEMENT: the payload's movement features (docs/re/subtick.md §"The movement
rem   yaw channel"): a comma-separated subset of `strafe` (silent airborne
rem   auto-strafe) and `bhop` (auto-jump while grounded). Unset = both off; they
rem   seed the overlay menu's "Movement" tab, which owns them afterwards.
rem   NOTE: the game inherits **Steam's** environment, not this script's (Steam
rem   launches the game), so setting it here only works when this script also starts
rem   Steam. For an always-on seed use the overlay menu, a *user* environment
rem   variable (`setx CS2_PAYLOAD_MOVEMENT strafe,bhop` + restart Steam), or the
rem   control commands. Leave it unset for movement *tests*.
rem - Outputs go under %LOCALAPPDATA%\cs2-internal (a Medium-integrity game
rem   process cannot write C:\ root).
if /I "%CS2_PAYLOAD_RENDER%"=="layer" (
  set "FLAGS=-insecure -novid -vulkan"
  set "CS2_OVERLAY=1"
)
set "CS2_OVERLAY_LOG=%LOCALAPPDATA%\cs2-internal\cs2-overlay-layer.log"
if not exist "%LOCALAPPDATA%\cs2-internal" mkdir "%LOCALAPPDATA%\cs2-internal"

"%STEAM%" -applaunch %APPID% %FLAGS% %*
endlocal
