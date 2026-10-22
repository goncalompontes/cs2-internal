#!/usr/bin/env bash
# Unix-path INCLUDE/LIB for cross-compiling with clang-cl against the msvc-wine
# headers/libs (no Wine needed at build time).
#
# Run from the repository root:
#   BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
#
# Override the toolchain location with CS2_MSVC_DIR if it lives elsewhere.
BASE_UNIX="$(realpath "${CS2_MSVC_DIR:-toolchain/msvc}" 2>/dev/null)"
if [ ! -d "$BASE_UNIX/vc" ]; then
  echo "msvcenv-native: MSVC not found at '$BASE_UNIX'. Run from the repo root" \
       "or set CS2_MSVC_DIR. Run toolchain/setup-msvc-wine.sh first." >&2
  return 1 2>/dev/null || exit 1
fi

MSVCVER="$(ls -1 "$BASE_UNIX/vc/tools/msvc" 2>/dev/null | sort -V | tail -1)"
SDKVER="$(ls -1 "$BASE_UNIX/kits/10/include" 2>/dev/null | sort -V | tail -1)"
ARCH=x64

MSVCDIR="$BASE_UNIX/vc/tools/msvc/$MSVCVER"
SDKINC="$BASE_UNIX/kits/10/include/$SDKVER"
SDKLIB="$BASE_UNIX/kits/10/lib/$SDKVER"

export INCLUDE="$MSVCDIR/atlmfc/include;$MSVCDIR/include;$SDKINC/shared;$SDKINC/ucrt;$SDKINC/um;$SDKINC/winrt;$SDKINC/km"
export LIB="$MSVCDIR/atlmfc/lib/$ARCH;$MSVCDIR/lib/$ARCH;$SDKLIB/ucrt/$ARCH;$SDKLIB/um/$ARCH;$SDKLIB/km/$ARCH"
export LIBPATH="$LIB"
