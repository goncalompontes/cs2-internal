# REA Ghidra project cache (local patch)

## Why

REA's Ghidra provider imports each target into a **fresh temporary project** and
passes `-deleteProject`, so every cold open re-runs Ghidra auto-analysis. Its
documented behaviour is explicit: *"Each new provider session still imports the
target into a fresh temporary project."*

The supported "analysis snapshot" (`open_binary(snapshot_path=…)`) caches exact
query results, but REA still starts the provider (and re-analyzes) before
serving them, so it does not avoid the cost. For a large target (`client.dll`,
39 MB) the cold analysis also exceeds REA's default `REA_GHIDRA_STARTUP_TIMEOUT_MS`
(330 s).

There is no upstream provider-project reuse, so this patch adds one.

## What the patch changes

Two files in the installed package
(`~/.local/lib/node_modules/rea-agents`):

- **`dist/ghidra/GhidraLauncher.js`** — keep one Ghidra project per target
  SHA-256 under a dot-free cache root, and reuse it:
  - cache root: `/var/tmp/rea-ghidra-projects/<sha256>` (override
    `REA_GHIDRA_PROJECT_CACHE`). It must not contain a `.`-prefixed path
    element: Ghidra aborts on those.
  - **cold** open: `-import` the target and *save* the project (no
    `-readOnly`, no `-deleteProject`).
  - **warm** open: `-process <program> -noanalysis -readOnly` — load the saved
    analysis without re-running it.
  - a `<project>/rea-analysis-complete` marker is written by the launcher only
    after the headless runner exits **cleanly (code 0)**, i.e. after the project
    has been saved. A killed/timed-out/cancelled run leaves no marker, so the
    next open re-imports.
- **`dist/ghidra/GhidraClient.js`** — raise the post-shutdown grace from 500 ms
  to 120 s (override `REA_GHIDRA_SHUTDOWN_GRACE_MS`). Ghidra holds the program
  read-only during post-scripts and only **saves it after the bridge returns**;
  REA's stock 500 ms grace force-stopped the JVM before the save completed.

The Java bridge (`bridge/ghidra/ReaGhidraBridge.java`) is **not** modified.

## Use

- **Cold** (first open of a target): imports + analyzes + saves, as before.
- **Warm** (later opens): loads the saved project — JVM start + project load
  only, no analysis.
- Clear one target: `rm -rf /var/tmp/rea-ghidra-projects/<sha256>`
- Clear everything: `rm -rf /var/tmp/rea-ghidra-projects`

For a large target the **cold** run still has to fit the startup deadline, so
raise `REA_GHIDRA_STARTUP_TIMEOUT_MS` in the `rea` MCP server environment
(global `~/.config/opencode/opencode.jsonc`), e.g. `1800000`.

## Apply / re-apply

`rea update` (npm) overwrites the patched files. Re-apply and restart the MCP
server so the running process loads it:

```sh
scripts/apply-rea-project-cache.sh          # idempotent; optional REA_DIR / arg
opencode service restart                    # the MCP server only reloads on restart
```

The patch is version-pinned to `rea-agents@6.4.0`
(`scripts/rea-project-cache.patch`); if an upgrade changes those files, the
apply step fails loudly and the patch must be regenerated.

## Status

- Validated via the **CLI** on `schemasystem.dll`: cold **27.7 s** → warm
  **6.3 s**, with the program saved into the project and the marker written.
- The MCP server must be restarted before `open_binary` uses the cache.
