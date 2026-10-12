#!/usr/bin/env bash
# REA MCP entrypoint that keeps the local Ghidra "project cache" patch applied.
#
# The patch edits the *compiled* rea-agents dist, which `rea update` (npm)
# overwrites. This wrapper is the MCP `command`, so on every server start it
# (re)applies the patch when it is missing, and stays out of the way otherwise.
#
# Conflict handling: if the patch no longer applies (upstream changed those
# files), it leaves the installed files untouched, records the conflict, and
# still runs REA unpatched so the server keeps working. Resolve it by
# regenerating scripts/rea-project-cache.patch against the new version.
#
# stdout is the MCP stdio protocol: all diagnostics go to stderr only.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
patch_file="$here/rea-project-cache.patch"
rea_dir="${REA_DIR:-$HOME/.local/lib/node_modules/rea-agents}"
rea_bin="${REA_BIN:-$(command -v rea || true)}"
target="$rea_dir/dist/ghidra/GhidraLauncher.js"
marker="Persistent, content-addressed Ghidra project cache"

ensure_patch() {
    [ -f "$target" ] || { echo "rea-project-cache: $target not found; running unpatched" >&2; return 0; }
    [ -f "$patch_file" ] || { echo "rea-project-cache: $patch_file missing; running unpatched" >&2; return 0; }
    grep -qF "$marker" "$target" && return 0

    # Serialize concurrent MCP server starts.
    if command -v flock >/dev/null 2>&1; then
        exec 9>"/tmp/rea-project-cache.lock" || true
        flock -w 30 9 2>/dev/null || true
    fi
    grep -qF "$marker" "$target" && return 0

    if patch -p1 --forward --dry-run -d "$rea_dir" <"$patch_file" >/dev/null 2>&1; then
        if patch -p1 --forward -d "$rea_dir" <"$patch_file" >&2; then
            echo "rea-project-cache: applied to $rea_dir" >&2
            return 0
        fi
    fi

    echo "rea-project-cache: patch does not apply to $rea_dir (rea-agents changed?)." >&2
    echo "rea-project-cache: running unpatched; regenerate $patch_file to resolve." >&2
    mkdir -p "$here/../artifacts" 2>/dev/null || true
    {
        printf 'conflict at %s\n' "$(date -Is 2>/dev/null || date)"
        patch -p1 --forward --dry-run -d "$rea_dir" <"$patch_file" 2>&1 | sed -n '1,80p'
    } >>"$here/../artifacts/rea-project-cache-conflict.log" 2>/dev/null || true
    return 0
}

ensure_patch

if [ -z "$rea_bin" ]; then
    echo "rea-project-cache: 'rea' executable not found on PATH" >&2
    exit 1
fi
exec "$rea_bin" mcp
