#!/usr/bin/env bash
# Live calibration entry point. See scripts/calibrate-live.py for details.
#
# Requires the target to be running and the dumper to hold CAP_SYS_PTRACE
# (yama ptrace_scope=1 denies a non-descendant otherwise). Grant it once:
#
#   sudo setcap cap_sys_ptrace+ep external/dumper/target/release/cs2-dumper
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$root/scripts/calibrate-live.py" "$@"
