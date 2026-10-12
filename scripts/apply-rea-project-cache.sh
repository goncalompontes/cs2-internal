#!/usr/bin/env bash
# Apply the local Ghidra "project cache" patch to the installed rea-agents
# package. REA ships an ephemeral Ghidra project per session (it imports the
# target into a temp project and passes -deleteProject), so every cold open
# re-runs auto-analysis. This patch keeps one project per target digest and
# reuses it with `-process -noanalysis`.
#
# `rea update` (npm) overwrites the patched files, so re-run this after any
# upgrade, then restart the REA MCP server (`opencode service restart`) so the
# running server loads it.
#
# Usage:
#   scripts/apply-rea-project-cache.sh [REA_PACKAGE_DIR]
# Env:
#   REA_DIR   override the package directory
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
patch_file="$here/rea-project-cache.patch"
rea_dir="${1:-${REA_DIR:-$HOME/.local/lib/node_modules/rea-agents}}"

if [ ! -f "$rea_dir/dist/ghidra/GhidraLauncher.js" ]; then
  echo "rea-agents not found at: $rea_dir" >&2
  echo "pass the package directory or set REA_DIR" >&2
  exit 1
fi

if grep -q "Persistent, content-addressed Ghidra project cache" \
     "$rea_dir/dist/ghidra/GhidraLauncher.js"; then
  echo "project-cache patch already applied to $rea_dir"
  exit 0
fi

patch -p1 -d "$rea_dir" < "$patch_file"
echo "applied project-cache patch to $rea_dir"
echo "restart the REA MCP server to load it:  opencode service restart"
