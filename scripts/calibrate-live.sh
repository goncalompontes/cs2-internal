#!/usr/bin/env bash
# Live calibration: dump interfaces (and schema, when an address is supplied)
# from a running target into JSON for the SDK generator.
#
# Requires the target to be running and the dumper to be able to read its
# memory. With yama ptrace_scope=1 a non-descendant process is denied; grant
# the binary the capability once, or relax the policy:
#
#   sudo setcap cap_sys_ptrace+ep external/dumper/target/release/cs2-dumper
#   # or: sudo sysctl kernel.yama.ptrace_scope=0
#
# Schema layout offsets are still placeholders (see docs/re/schema-system.md),
# so schema dumping is opt-in and only attempted when CS2_SCHEMA_SYSTEM is set.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dump_dir="${1:-$root/calibration}"
process="${CS2_PROCESS:-cs2.exe}"
module="${CS2_MODULE:-client.dll}"

mkdir -p "$dump_dir"

bin="$root/external/dumper/target/release/cs2-dumper"
if [[ ! -x "$bin" ]]; then
  echo "calibrate-live: building cs2-dumper (release)" >&2
  cargo build --release --manifest-path "$root/external/dumper/Cargo.toml" >&2
fi

if ! "$bin" --process "$process" --json >"$dump_dir/_manifest.json" 2>/dev/null; then
  echo "calibrate-live: target '$process' not running (or memory unreadable)" >&2
  rm -f "$dump_dir/_manifest.json"
  exit 1
fi

echo "calibrate-live: dumping interfaces from $module" >&2
"$bin" --process "$process" --interfaces --json --module "$module" >"$dump_dir/interfaces.json"
echo "calibrate-live: wrote $dump_dir/interfaces.json" >&2

if [[ -n "${CS2_SCHEMA_SYSTEM:-}" ]]; then
  echo "calibrate-live: dumping schema at $CS2_SCHEMA_SYSTEM" >&2
  "$bin" --process "$process" --schema --json --schema-system "$CS2_SCHEMA_SYSTEM" >"$dump_dir/schema.json"
  echo "calibrate-live: wrote $dump_dir/schema.json" >&2
else
  echo "calibrate-live: skipping schema (set CS2_SCHEMA_SYSTEM to enable)" >&2
fi
