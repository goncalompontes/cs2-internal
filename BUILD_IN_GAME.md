# Directional strafe counter-limit build

Restart CS2, then run Run Injector.cmd from this folder.

WASD strafe assistance is restored. The manual A/D/S bypass from the previous package is removed. The native gain strafer now prohibits an opposing velocity component at every speed-gain blend. The original recovered helper remains available with its original behavior.

The command adapter also reconciles previous movement impulses to current input once when no physical analog timeline exists. This prevents steering the previous input in the wrong direction. Physical movement/button events remain intact, repeated commands replace owned corrections, and releasing travel removes them.

Movement diagnostics now show Strafe reverse wish and whether ragebot autostop actually applied, including its path. Intentional ragebot air-autostop remains a separate braking feature. If braking remains, these distinguish its owner. Planning diagnostics do not prove what the server processed.

Use Movement -> Auto-strafe, start with 16 subticks and 100% Speed gain. Bunnyhop remains separate. The assist still skips grounded launches, zero travel input and speeds below 80 units/s.

Validation: host 18/18 and payload 15/15; actual DLL rendering in the synthetic WARP host. The new tests check every slice for a nonnegative velocity projection across four speeds, four blends and four slice counts, and verify prior-back/current-forward reconstruction, repeated-command safety, input release, WASD and silent-aim integration. In-game behavior is pending user confirmation.

Full private NL landing prediction remains unavailable. Air physics defaults remain 12 acceleration / 30 cap. See NL_MOVEMENT.md for source evidence and native adaptations.

Runtime log: %LOCALAPPDATA%\cs2-internal\cs2-payload.log
Use Direct3D 11; launch options: -insecure -novid.
