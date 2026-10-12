# Tooling setup (reproducible notes)

This project uses a small toolset. These are the exact steps used to set it up
on Arch, for reproducibility.

## Code intelligence (OpenCode built-in)

Enabled in `opencode.jsonc` with `"lsp": true`. OpenCode auto-installs clangd
for C/C++. Meson writes `compile_commands.json`, which clangd consumes.

## MCP servers

### treesitter (installed)
```sh
git clone --depth 1 https://github.com/Christoph/treesitter-mcp /tmp/ts
cd /tmp/ts && cargo build --release
cp target/release/treesitter-mcp ~/.local/bin/
```

### Static RE: REA + Ghidra (global)
Static RE is provided by **REA** (`rea-agents`, MCP server `rea`) driving a
**global** Ghidra install — this project keeps no local Ghidra copy. The server
is registered in the global `~/.config/opencode/opencode.jsonc`.

```sh
# CLI + MCP server
npm install --global rea-agents

# Ghidra 12.1.x, extracted outside the repo
#   ~/.local/share/ghidra/ghidra_12.1.4_PUBLIC
export GHIDRA_INSTALL_DIR="$HOME/.local/share/ghidra/ghidra_12.1.4_PUBLIC"
export JAVA_HOME=/usr/lib/jvm/java-27-openjdk
```

Verify: `rea doctor` reports the `ghidra` scope check as `healthy`.

For large targets, apply the local **Ghidra project cache** (one project per
target digest, reused on later opens) — see `docs/rea-project-cache.md`.

> After editing `opencode.jsonc` MCP entries, run `opencode service restart`
> so the running service reloads configuration.

## Build toolchain (MSVC ABI)

See `toolchain/README.md` (`msvc-wine` + `msvcenv-native.sh`).
