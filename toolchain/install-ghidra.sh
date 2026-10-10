#!/usr/bin/env bash
# Installs Ghidra (from the zip downloaded into toolchain/) and the ReVa
# extension. ReVa requires Ghidra 12.0+ and JDK 21+ (JDK 27 is fine).
#
# Usage:
#   ./toolchain/install-ghidra.sh [ZIP_PATH]
#
# Defaults to toolchain/ghidra_12.1.4.zip, extracts to toolchain/ghidra/, and
# installs ReVa via the Ghidra Extension Manager (GUI) or the PyGhidra/headless
# route documented by ReVa. See docs/decisions.md (ADR-0004).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZIP="${1:-$ROOT/ghidra_12.1.4.zip}"
DEST="$ROOT/ghidra"

if [ ! -f "$ZIP" ]; then
  echo "Ghidra zip not found: $ZIP" >&2
  exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"
unzip -q "$ZIP" -d "$DEST"
GHDIR="$(find "$DEST" -maxdepth 1 -mindepth 1 -type d -name 'ghidra_*' | head -1)"
echo "Ghidra extracted to: $GHDIR"
echo
echo "Next: install the ReVa extension (Ghidra 12.0+). Options:"
echo "  GUI:      $GHDIR/ghidraRun  -> File > Install Extensions > + > ReVa zip"
echo "  Headless: see https://github.com/cgtudor/reverse-engineering-assistant"
