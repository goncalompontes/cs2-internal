#!/usr/bin/env bash
# Release build.
#
# 1. Build the dumper (the calibration + generation tool).
# 2. Live-calibrate the running target into calibration/*.json.
# 3. Host build with -Dlive_calibration=true, so the sdk-headers target
#    re-calibrates and renders payload/sdk/*.hpp as part of the build.
# 4. Cross-build the payload against the msvc-wine toolchain (if set up).
#
# Requirements:
#   - the target running, and cs2-dumper holding CAP_SYS_PTRACE:
#       sudo setcap cap_sys_ptrace+ep external/dumper/target/release/cs2-dumper
#     (a release rebuild replaces the binary and drops the capability, so
#      re-apply it after each cargo build --release)
#   - toolchain/msvc/msvcenv-native.sh for the payload step.
#
# Env:
#   CS2_SKIP_CALIBRATION=1   build without touching the target (uses existing
#                             dumps; sdk-headers falls back to them)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

bin="$root/external/dumper/target/release/cs2-dumper"

# Calibrate BEFORE rebuilding: `cargo build --release` replaces the binary and
# drops its file capability, so the (already capped) binary is the one that can
# read the target.
calibrate="${CS2_SKIP_CALIBRATION:-0}"
live_flag=false
if [[ "$calibrate" != "1" ]]; then
  if [[ ! -x "$bin" ]]; then
    echo "==> building dumper (release)"
    cargo build --release --manifest-path external/dumper/Cargo.toml
  fi
  if ! getcap "$bin" 2>/dev/null | grep -q cap_sys_ptrace; then
    echo "warning: $bin lacks cap_sys_ptrace; live calibration may fail" >&2
    echo "         grant it with: sudo setcap cap_sys_ptrace+ep $bin" >&2
  fi
  echo "==> live calibration"
  if ./scripts/calibrate-live.sh; then
    live_flag=true
  else
    echo "warning: live calibration failed; continuing without it" >&2
  fi
fi

echo "==> building dumper (release)"
cargo build --release --manifest-path external/dumper/Cargo.toml

setup() { # dir, source, extra...
  local dir="$1" src="$2"; shift 2
  if [[ -d "$dir" ]]; then
    meson setup --reconfigure "$dir" "$src" "$@"
  else
    meson setup "$dir" "$src" "$@"
  fi
}
# Map the boolean to the Meson option value.
if [[ "$live_flag" == true ]]; then live_opt=true; else live_opt=false; fi

echo "==> host build (buildtype=release, live_calibration=$live_opt)"
setup build-release . --buildtype=release -Dlive_calibration="$live_opt"
meson compile -C build-release
meson test -C build-release

if [[ -f toolchain/msvcenv-native.sh ]]; then
  echo "==> payload + injector (cross, Windows)"
  # shellcheck disable=SC1091
  BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
  setup build-payload payload --cross-file toolchain/windows-msvc.ini --buildtype=release
  meson compile -C build-payload
  setup build-injector injector --cross-file toolchain/windows-msvc.ini --buildtype=release
  meson compile -C build-injector
else
  echo "==> payload skipped (toolchain/msvc not set up; run toolchain/setup-msvc-wine.sh)"
fi

echo "==> release build complete"
