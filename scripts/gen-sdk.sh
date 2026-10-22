#!/usr/bin/env bash
# Thin POSIX wrapper around scripts/gen-sdk.py (kept for muscle memory / docs).
#
# The real implementation is cross-platform Python so the same generator runs
# from the native Windows Meson build. See scripts/gen-sdk.py.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$root/scripts/gen-sdk.py" "$@"
