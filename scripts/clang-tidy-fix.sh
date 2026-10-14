#!/usr/bin/env bash
# Thin POSIX wrapper around scripts/clang-tidy-fix.py (same pattern as
# scripts/gen-sdk.sh). The real implementation is cross-platform Python so the
# sweep runs from the native Windows Meson build and from Linux hosts alike.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$root/scripts/clang-tidy-fix.py" "$@"
