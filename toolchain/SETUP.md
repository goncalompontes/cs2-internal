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

### ReVa (Ghidra headless MCP)
ReVa needs four things: the Python CLI, the Ghidra extension (provides the
`reva` module), PyGhidra from the *local* Ghidra, and noo `mcp` 2.x (ReVa 7.3.1's
bridge imports the mcp 1.x symbol name).

```sh
# 1. Ghidra 12.x (see toolchain/install-ghidra.sh)
# 2. Python CLI
curl -LsSf https://astral.sh/uv/install.sh | sh
uv tool install reverse-engineering-assistant --with 'mcp<2'

# 3. Ghidra extension (matches your Ghidra version; 12.1.x build used here)
curl -fL -o reva-ext.zip \
  https://github.com/cyberkaida/reverse-engineering-assistant/releases/download/v7.3.1/ghidra_12.1.3_PUBLIC_20260903_reverse-engineering-assistant.zip
unzip -q reva-ext.zip -d "$GHIDRA_INSTALL_DIR/Ghidra/Extensions/"

# 4. PyGhidra from the local Ghidra, then re-pin mcp
TP="$HOME/.local/share/uv/tools/reverse-engineering-assistant"
uv pip install --python "$TP/bin/python" "$GHIDRA_INSTALL_DIR/Ghidra/Features/PyGhidra/pypkg"
uv pip install --python "$TP/bin/python" 'mcp==1.30.0'
```

Verify: `GHIDRA_INSTALL_DIR=... mcp-reva </dev/null` should print
`ReVa server ready ...` then `Bridge ready - stdio transport active`.

> After editing `opencode.jsonc` MCP entries, run `opencode service restart`
> so the running service reloads configuration.

## Build toolchain (MSVC ABI)

See `toolchain/README.md` (`msvc-wine` + `msvcenv-native.sh`).
