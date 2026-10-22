#!/usr/bin/env bash
# Sets up MSVC + Windows SDK headers/libs on Linux via mstorsjo/msvc-wine.
# Not redistributable: the result lives in toolchain/msvc/ (gitignored).
#
# Requires: git, python3, msitools, libgcab, ca-certificates, wine (for the
# full install) and clang/llvm (clang-cl, lld).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/msvc-wine"
DEST="$ROOT/msvc"

if [ ! -d "$SRC" ]; then
  git clone --depth 1 https://github.com/mstorsjo/msvc-wine "$SRC"
fi

# Download MSVC + WinSDK, then wire up wrapper scripts.
"$SRC/vsdownload.py" --accept-license --dest "$DEST"
"$SRC/install.sh" "$DEST"

echo
echo "Done. Source the environment before configuring the payload:"
echo "  BIN=$ROOT/msvc/bin/x64 . $ROOT/msvcenv-native.sh"
