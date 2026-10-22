#!/usr/bin/env bash
# Launch the live-analysis MCP server for OpenCode.
#
# Wraps `cs2-dumper --mcp`, building the release binary on first use so the
# server is always available to the agent. Talks the MCP stdio protocol.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/external/dumper/target/release/cs2-dumper"

if [[ ! -x "$bin" ]]; then
  echo "mcp-live: building cs2-dumper (release)" >&2
  cargo build --release --manifest-path "$root/external/dumper/Cargo.toml" >&2
fi

exec "$bin" --mcp
