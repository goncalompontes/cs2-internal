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

The global `rea` MCP server runs **`scripts/rea-mcp.sh`**, which (re)applies the
patch on every server start and then `exec`s `rea mcp`:

- patch already present → exec immediately;
- patch missing and applies cleanly → apply, then exec;
- patch does **not** apply (upstream changed the files) → leave the install
  untouched, record the conflict to `artifacts/rea-project-cache-conflict.log`,
  print it to stderr, and run REA **unpatched** so the server still works.

A whole-patch `--dry-run` guards against partial application. To resolve a
conflict, regenerate `scripts/rea-project-cache.patch` against the new
`rea-agents` version (see below), then restart the server.

Manual (re)apply, without the wrapper:

```sh
scripts/apply-rea-project-cache.sh          # idempotent; optional REA_DIR / arg
opencode service restart                    # the MCP server only reloads on restart
```

Regenerate the patch after an upstream change:

```sh
cd /tmp && npm pack rea-agents@<version> && tar xzf rea-agents-<version>.tgz
# re-apply your edits to package/dist/ghidra/{GhidraLauncher,GhidraClient}.js,
# then diff them against the installed copy:
for f in dist/ghidra/GhidraLauncher.js dist/ghidra/GhidraClient.js; do
  diff -u --label "a/$f" --label "b/$f" "package/$f" "$HOME/.local/lib/node_modules/rea-agents/$f"
done > scripts/rea-project-cache.patch
```

## Status

- Validated via the **CLI** on `schemasystem.dll`: cold **27.7 s** → warm
  **6.3 s**, with the program saved into the project and the marker written.
- The MCP server must be restarted before `open_binary` uses the cache.

## Upstream

This local patch is a stopgap for the upstream design:

- **Issue #1048** — "Open a pre-imported Ghidra project…" (open).
- **PR #2150** — launcher `-process` branch for a caller-supplied project (open).
- **Issue #2147** — follow-up: product wiring, digest binding, docs, real-provider verification.
- **PR #2358** — our wiring PR (draft, stacked on #2150): adds
  `REA_GHIDRA_EXISTING_PROJECT` / `REA_GHIDRA_EXISTING_PROGRAM`.

Once an upstream release exposes the opt-in existing-project path, drop this
patch and use those environment variables instead.
