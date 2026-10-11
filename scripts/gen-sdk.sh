#!/usr/bin/env bash
# Render the C++ SDK headers from the calibration JSON dumps.
#
# Usage: gen-sdk.sh [DUMP_DIR] [OUT_DIR] [STAMP]
#
#   DUMP_DIR   directory holding interfaces.json / schema.json / offsets.json
#              (default: <repo>/calibration)
#   OUT_DIR    where the headers are written (default: <repo>/payload/sdk)
#   STAMP      optional file to touch on success (used by the Meson target)
#
# Any subset of the dumps may be missing; missing inputs simply produce empty
# sections. The dumper is built (release) on demand if no binary is present.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

live=0
if [[ "${1:-}" == "--live" ]]; then
  live=1
  shift
fi

dump_dir="${1:-$root/calibration}"
out_dir="${2:-$root/payload/sdk}"
stamp="${3:-}"

if [[ $live -eq 1 ]]; then
  "$root/scripts/calibrate-live.sh" "$dump_dir"
fi

args=(--gen "$out_dir")
found=0
for name in interfaces schema offsets; do
  file="$dump_dir/$name.json"
  if [[ -f "$file" ]]; then
    args+=("--$name-json" "$file")
    found=$((found + 1))
  fi
done

bin="$root/external/dumper/target/release/cs2-dumper"
if [[ ! -x "$bin" ]]; then
  echo "gen-sdk: building cs2-dumper (release)" >&2
  cargo build --release --manifest-path "$root/external/dumper/Cargo.toml" >&2
fi

if [[ $found -eq 0 ]]; then
  echo "gen-sdk: no dumps in $dump_dir — writing empty SDK headers" >&2
fi

"$bin" "${args[@]}"

if [[ -n "$stamp" ]]; then
  mkdir -p "$(dirname "$stamp")"
  : >"$stamp"
fi
