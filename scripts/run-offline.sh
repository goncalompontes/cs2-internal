#!/usr/bin/env bash
# Launch the target offline with a Windows (Proton) build.
#
# - Forced Proton (Windows depot) so a Windows payload DLL can be loaded.
# - -insecure disables VAC (offline/local testing only!).
# - prime-run renders on the NVIDIA GPU.
#
# The target must be configured in Steam to use Proton
# (Properties -> Compatibility).
set -euo pipefail

APP_ID=730
EXTRA_ARGS=("$@")

# Default offline flags; add more via arguments.
FLAGS=(-insecure -condebug -novid)
if [ "${1:-}" = "--" ]; then shift; FLAGS=("$@"); fi

launcher="steam"
command -v prime-run >/dev/null 2>&1 && launcher="prime-run steam"

echo "Launching target (appid $APP_ID) offline: ${launcher} -applaunch $APP_ID ${FLAGS[*]}"
exec $launcher -applaunch "$APP_ID" "${FLAGS[@]}"
