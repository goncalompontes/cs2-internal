# New-machine setup — Windows (agent guide)

Audience: an **OpenCode agent on a fresh Windows box** that has to bring this
repo up so it can be worked on exactly like the primary dev machine (build the
payload/injector/overlay, drive a live CS2 for the scenario harness, and do
static RE with REA + Ghidra).

**How to use this document.** Work top to bottom. After every step, run the
verification command shown for it and report the result to the user before
moving on. Do not skip verification: most failures here are "tool not on `PATH`"
or "ran in the wrong session", and they only surface at the first build or the
first live run otherwise.

> **Never launch `cs2.exe` / `steam.exe` directly.** An agent shell runs in
> Windows **session 0**, which has no GPU and no desktop. Always go through the
> `CS2Launch` scheduled task (§7), which runs in the interactive session.
> See `AGENTS.md`.

> **Docs contain the primary machine's absolute paths** (`C:\Users\gpmpo\...`,
> `F:\SteamLibrary\...`). Everything repo-relative is portable; map any
> absolute path you meet in `docs/re/*.md` and `docs/handoff.md` to your own
> locations.

**Kick-off prompt** (what the owner can hand to the agent on the new machine):

> Clone `https://github.com/goncalompontes/cs2-internal` to `C:\cs2-internal`,
> then read and execute `docs/setup-windows.md` top to bottom. After each
> numbered section run its verification and report the result. Finish with the
> §13 checklist and tell me anything that still fails.

---

## 0. Target state

When you are done, this repo has:

| Piece | Purpose | Where it lives |
|---|---|---|
| Host tools build | unit tests, SDK header generation | `build/` (Meson, clang-cl) |
| `payload.dll` | the injected DLL (state, overlay model, control channel, input hooks) | `build-payload/` |
| `injector.exe` | manual-map injector (no `LoadLibrary`) | `build-injector/` |
| `cs2_overlay_layer.dll` | Vulkan implicit layer that draws the overlay | `build-overlay/` |
| `cs2-dumper.exe` | external offset/interface/schema dumper + live memory backend | `external/dumper/target/release/` |
| `CS2Launch` task | launches CS2 in the interactive session | Windows Task Scheduler |
| Vulkan implicit-layer registration | makes the loader pick up the overlay | `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` |
| OpenCode MCP servers | `treesitter` (repo config) + `rea` (global config) | OpenCode config |
| REA + Ghidra + JDK 21 | static RE | outside the repo |
| Steam + CS2 (appid 730) | the live target | outside the repo |

`calibration/*.json` and `payload/sdk/*.hpp` are **git-ignored and
build-specific**: they do not travel with the repo and must be regenerated
against your installed game build (§11).

---

## 1. Preflight

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\preflight-windows.ps1
```

It prints `[PASS]/[WARN]/[FAIL]` per item. Every `FAIL` maps to a step below;
`WARN` items are live-game/RE-only. Re-run it after each section.

---

## 2. Core tools

Install what preflight reports missing. `winget` is the fastest path:

```powershell
winget install --id Git.Git                    -e
winget install --id Python.Python.3.12         -e
winget install --id OpenJS.NodeJS.LTS          -e
winget install --id Rustlang.Rustup            -e
winget install --id LLVM.LLVM                  -e
# Visual Studio Build Tools with the C++ workload (MSVC headers/libs + Windows SDK).
# clang-cl/lld-link do the compiling; the MSVC + SDK bits come from here.
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
  --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Then Meson + Ninja (the primary machine has them as Python user scripts):

```powershell
py -3 -m pip install --user meson ninja
```

**Verify** (open a *new* shell so `PATH` updates land):

```powershell
git --version; meson --version; ninja --version; python --version
cargo --version; node --version; clang-cl --version
Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat'
```

Notes:

- The payload builds with **clang-cl + lld-link** (MSVC ABI) but needs the MSVC
  and Windows-SDK headers/libs, so every `build-*` configure/compile runs from a
  **vcvars64 environment** (§5).
- Python for this repo is **stdlib only** — no `pip install` for the scripts.
- `cargo` is for `external/dumper`.
- `node` is for REA (§12) and the global OpenCode MCP config.

---

## 3. Clone

The private repo is `https://github.com/goncalompontes/cs2-internal` (ask the
owner to add your GitHub account as a **collaborator** first, then accept the
invitation).

```powershell
git clone https://github.com/goncalompontes/cs2-internal.git C:\cs2-internal
cd C:\cs2-internal
```

(GitHub over HTTPS prompts for credentials; `gh auth setup-git` wires up the
GitHub CLI as a credential helper, or use your own SSH key.)

- The repo carries a **symlink** (`overlay/third_party/imgui` →
  `../../payload/third_party/imgui`). A Windows checkout materialises it as a
  text file; `overlay/meson.build` already sidesteps this by referencing the
  payload copy directly, so **do not** "fix" it.
- Line endings: git may print `LF will be replaced by CRLF` warnings. Harmless.
- Nothing under `build*/`, `toolchain/msvc/`, `external/Vulkan-Headers/`,
  `calibration/*.json` or `payload/sdk/*.hpp` is in the repo — all are local.

---

## 4. Host build + tests

```powershell
meson setup build
meson compile -C build
meson test -C build
```

**Verify:** all four tests pass (`host-probe`, `game-matrix`, `control-core`,
`sdk-consumer`). `build/compile_commands.json` exists for the built-in clangd
LSP.

---

## 5. Native Windows builds (payload / injector / overlay)

```powershell
# 1. Vendored Vulkan headers (git-ignored; needed by the overlay).
#    The tag is pinned: the overlay's include path must be the same on every
#    machine (the CI clones the same tag -- see .github/workflows/ci.yml).
git clone --depth 1 --branch vulkan-sdk-1.4.363.0 `
  https://github.com/KhronosGroup/Vulkan-Headers external/Vulkan-Headers

# 2. vcvars64: MSVC + Windows SDK environment, LLVM already on PATH.
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
#   (use whatever version you installed; §2 verified the path exists)

# 3. clang-cl as the compiler.
set CC=clang-cl
set CXX=clang-cl

# 4. The three Windows targets.
#    The payload is configured with the cross file: it pins the *static* CRT
#    (b_vscrt = mt), which the manually mapped DLL needs (docs/architecture.md).
meson setup build-payload  payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload
meson setup build-injector injector
meson compile -C build-injector
meson setup build-overlay  overlay
meson compile -C build-overlay
```

If a `build-*/` directory already exists from a different compiler, delete it
(`Remove-Item build-payload -Recurse -Force`) and re-configure — do not mix.

**Verify:**

```powershell
Test-Path build-payload\payload.dll            # True
Test-Path build-injector\injector.exe          # True
Test-Path build-overlay\cs2_overlay_layer.dll  # True
Test-Path build-overlay\cs2_overlay_layer.json # True
```

`payload.dll` may build from an empty SDK (the generated headers are optional at
this stage); it becomes useful after §11.

Cross-compiling to Windows **from Linux** (msvc-wine) is a separate, older lane
(`toolchain/README.md`). This guide is the native-Windows lane; ignore
`toolchain/msvcenv-native.sh` here.

---

## 6. Steam + CS2

1. Install **Steam** and **Counter-Strike 2** (appid 730) from the interactive
   desktop. The game may live on any drive via a Steam library folder.
2. Launch CS2 once normally so its shader cache and config exist, then close it.
3. The harness runs the game **offline / insecure** via the launch flags
   `-insecure -novid -vulkan` (§7). That is the intended test mode — not for
   online play.

**Verify** (the path is discovered from `libraryfolders.vdf` in preflight):

```powershell
# should print an install dir whose game\bin\win64\engine2.dll exists
Get-ChildItem C:\,D:\,E:\,F:\ -ErrorAction SilentlyContinue -Filter 'engine2.dll' -Recurse -Depth 7 |
  Select-Object -First 5 FullName
```

The two RE/live targets are:

- `…\Counter-Strike Global Offensive\game\bin\win64\engine2.dll`
- `…\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`

---

## 7. The `CS2Launch` scheduled task

This is the **only** sanctioned way for an agent to start the game: the task runs
`scripts/launch-cs2.cmd` in the **interactive session**, where the GPU and
desktop exist.

1. Check the Steam path hard-coded in `scripts/launch-cs2.cmd`
   (`set "STEAM=…"`). Override it without editing the file by setting a user
   `STEAM` environment variable, or edit the default.

2. Register the task (elevated PowerShell is **not** required — it runs as you,
   interactively, on demand, with no trigger):

```powershell
$repo = 'C:\cs2-internal'
$action  = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c $repo\scripts\launch-cs2.cmd"
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0
Register-ScheduledTask -TaskName CS2Launch -Action $action -Principal $principal -Settings $settings -Force
```

The equivalent `schtasks` form:

```powershell
schtasks /create /tn CS2Launch /sc once /st 00:00 /it /f `
  /tr "cmd.exe /c C:\cs2-internal\scripts\launch-cs2.cmd"
```

(The trigger is unused; the task is always started by hand with
`schtasks /run /tn CS2Launch`. The primary machine's task is
`cmd.exe /c <repo>\scripts\launch-cs2.cmd`, principal = your own account,
`InteractiveToken`, no triggers.)

**Verify:**

```powershell
schtasks /query /tn CS2Launch
schtasks /run   /tn CS2Launch     # launches the game in session 1
```

To change flags (`-insecure -novid -vulkan`, `CS2_OVERLAY=1`,
`CS2_PAYLOAD_PUB_THREAD=1`, `CS2_PAYLOAD_MOVEMENT=…`), edit
`scripts/launch-cs2.cmd` — see `docs/handoff.md` for what each one does.

---

## 8. Register the Vulkan overlay layer (once, elevated)

The Windows Vulkan loader discovers implicit layers through the registry.

```powershell
# Run in an ELEVATED (Administrator) PowerShell.
reg add "HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers" `
  /v "C:\cs2-internal\build-overlay\cs2_overlay_layer.json" /t REG_DWORD /d 0 /f
```

The manifest's `enable_environment` still gates loading on `CS2_OVERLAY=1`, which
`launch-cs2.cmd` already sets for the game.

**Verify:** `scripts/preflight-windows.ps1` reports `[PASS] Vulkan layer`, or:

```powershell
(Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers').PSObject.Properties.Name -like '*cs2_overlay_layer*'
```

> On Windows, Ghidra is **read-only** for annotation. Record RE findings in
> `docs/re/*.md`, never in the Ghidra database.

---

## 9. Windows Defender exclusion

Defender quarantines `injector.exe` **after it runs** (manual-map injectors trip
the heuristic); the next run then fails with "injector not built". Exclude the
repo (elevated):

```powershell
Add-MpPreference -ExclusionPath 'C:\cs2-internal'
```

If you cannot add an exclusion, expect to rebuild `injector.exe` before each
injection.

---

## 10. OpenCode configuration

**Project config** — `opencode.jsonc` already ships in the repo (built-in LSP +
the `treesitter` MCP resolved from `PATH`). Nothing to change.

**Global config** — `~/.config/opencode/opencode.jsonc` (`%USERPROFILE%\.config\opencode\opencode.jsonc`).
It is per-user and does **not** travel with the repo; add the `rea` MCP entry
matching your machine (see §12 for what to point at):

```jsonc
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "rea": {
      "type": "local",
      "enabled": true,
      "command": ["C:\\Program Files\\nodejs\\node.exe",
                  "C:\\rea\\scripts\\rea.mjs", "mcp"],
      "environment": {
        "REA_ANALYSIS_PROVIDER": "ghidra",
        "GHIDRA_INSTALL_DIR": "C:\\ghidra\\ghidra_12.1.4_PUBLIC",
        "JAVA_HOME": "C:\\Program Files\\Java\\jdk-21",
        "REA_GHIDRA_STARTUP_TIMEOUT_MS": "1800000",
        "REA_GHIDRA_PROJECT_CACHE": "on"
      }
    }
  }
}
```

The running server **hot-reloads** config edits. **Do not run
`opencode service restart`** while an agent session is open — it kills the
session.

**Skills.** Project skills are discovered from `.agents/skills/<name>/SKILL.md`
(and `.opencode/skills/`); global ones from `~/.config/opencode/skills/` and
`~/.agents/skills/`. This repo now ships `re-toolkit` under
`.agents/skills/re-toolkit/`, so the agent gets it automatically. It needs

```powershell
py -3 -m pip install --user capstone keystone-engine
```

for the IDA/Cheat-Engine bridges; the local pattern/PE commands work without it.
(`retool.py` has no absolute paths and works on Windows.)

---

## 11. First live run (calibrate → build → inject → assert)

The payload's SDK must match the **running** game build. CS2 auto-updates, so
re-calibrate after every patch (the curated signatures usually still match —
only the RVAs move).

```powershell
# 1. Build the external dumper (native Windows live backend, no setcap needed).
cargo build --release --manifest-path external/dumper/Cargo.toml

# 2. Start the game in the interactive session (payload NOT needed for this).
schtasks /run /tn CS2Launch
#    wait for client.dll to load (preflight / the dumper can see it)

# 3. Live calibration writes calibration/*.json against the running process.
python scripts/calibrate-live.py

# 4. Render payload/sdk/*.hpp from those dumps.
python scripts/gen-sdk.py calibration payload/sdk

# 5. Rebuild the payload against the fresh SDK (in the vcvars64 + clang-cl env).
meson compile -C build-payload
```

Then inject and confirm the channel:

```powershell
# one launch + inject + connect + assert, then tear down
python scripts/run-scenario.py join-bot-match

# or attach to a game you already launched+injected (do NOT inject twice)
python scripts/run-scenario.py status --no-launch --no-inject --keep-running
python scripts/run-scenario.py --list
```

`status` is the fastest health check: `events_ready`, `input_ready`,
`state_ready` should be true. If they are false, the SDK is stale → redo
step 3–5.

Logs (Medium-integrity process writes here, not `C:\`):

- `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`
- `%LOCALAPPDATA%\cs2-internal\cs2-overlay-layer.log`

> **Never inject twice into one process.** Two payload instances install two pipe
> servers on the same name; clients then reach a random one. To load a new
> payload: kill + relaunch the game, then inject **once**.

---

## 12. Static RE: Ghidra + JDK + REA

Read `docs/re/README.md` first — it is the agent playbook (the
string → xref → decompile loop, vtables/RTTI, turning findings into dumper
signatures, gotchas, warm projects and the current RE map). This section only
covers **installation**.

### 12a. Ghidra + JDK 21

```powershell
# Extract Ghidra 12.1.4 (not into the repo) and install a JDK 21.
# Primary machine:
#   Ghidra  C:\ghidra\ghidra_12.1.4_PUBLIC
#   Java    C:\Program Files\Java\jdk-21
```

### 12b. REA — pick one

**(recommended) Published package.** Static RE works fully; you only lose the
fork-only *persistent Ghidra project cache* (so a first `client.dll` import is
slow, ~20 min instead of ~1 min on a warm project).

```powershell
npm install --global rea-agents@6.7.0
rea doctor          # expect the `ghidra` scope check healthy once env vars are set
```

Point the global `rea` MCP entry's `command` at the installed launcher, e.g.
`["<node.exe>", "<npm-global>\node_modules\rea-agents\scripts\rea.mjs", "mcp"]`.
The published package ships a prebuilt Windows native addon, so no compiler is
needed.

**(exact match) The primary machine's fork build.** The project cache is an
unmerged upstream PR (`morluto/rea#3531`); the primary machine runs a local
build of it. Its source is public — the user's fork, branch
`feat/ghidra-project-cache`, pinned at commit `d4b8afbba`:

```powershell
# The native addon only ships inside the npm tarball (the repo's own addon build
# lane is Linux + MinGW only), so take it from the published package:
npm install --global rea-agents@6.7.0
$addon = Join-Path (npm root -g) 'rea-agents\native\windows\build'

git clone https://github.com/goncalompontes/rea.git C:\rea
Set-Location C:\rea
git checkout d4b8afbba
npm install
Copy-Item "$addon\*" native\windows\build\ -Force
npm run compile          # tsc + bundle into dist/
npm link                 # puts `rea` on PATH pointing at this build

rea cache-list           # must be a known subcommand -> cache feature present
```

Then set `REA_GHIDRA_PROJECT_CACHE=on` in the MCP entry (§10). This is the
cache-enabled build; verify with `rea cache-list` / `rea doctor`. This recipe
reproduces the primary machine but is the fiddly path — prefer the published
package unless you specifically want warm analysis, and expect to adapt the
native-addon step if the pinned commit no longer matches the shipped addon.

Notes for either path:

- Set `GHIDRA_INSTALL_DIR` and `JAVA_HOME` (the MCP entry does this for the
  server; set them in your shell too for CLI use).
- The cache lives under `~/.rea/cache/ghidra-projects`; inspect it with
  `rea cache-list`, bound it with `REA_GHIDRA_PROJECT_CACHE_MAX_BYTES`.
- **The cache root must be owner-only on Windows** (current user + SYSTEM, with a
  protected DACL). A root that inherited the profile's DACL is refused by REA;
  the fork build (`d4b8afbba`+) rebuilds such a root as a private directory
  instead and reports the rebuild in the session Evidence.
- To **seed** the cache from an already-analyzed project, use
  `scripts/seed-rea-project-cache.mjs` (see `docs/re/README.md` §8).
- Ghidra targets: copy `engine2.dll` and `client.dll` out of the CS2 install
  into your own analysis folder before importing.
- Prefer the integrated `tools.rea.*` MCP tools over shelling out to the `rea`
  shim (they reuse the bound session and return normalized results), per
  `docs/re/README.md` §1.

---

## 13. End-to-end verification checklist

Run these in order and report each line:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\preflight-windows.ps1   # no FAIL
meson test -C build                                                                 # 4/4
Test-Path build-payload\payload.dll, build-injector\injector.exe, build-overlay\cs2_overlay_layer.dll
cargo test --manifest-path external/dumper/Cargo.toml                               # dumper fixtures
schtasks /query /tn CS2Launch                                                       # exists
rea doctor                                                                          # ghidra healthy
python scripts/run-scenario.py --list                                               # scenarios enumerate
python scripts/run-scenario.py join-bot-match                                       # live: launch+inject+assert
```

`join-bot-match` passing means the whole live lane works: task launch → session 1
GPU → inject → named-pipe control channel → `exec`/`state` → assertions.

Optional visual check (needs the operator watching the screen): enable the
overlay and confirm the HUD/ESP renders — the Vulkan layer's log should show
`drew frame … draws=…`.

---

## 14. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Game starts but no window / "no GPU available" | launched from session 0. Start it via `CS2Launch` (§7). |
| `injector.exe` "not built" on second run | Defender quarantined it (§9). |
| `ping` shows `events_ready/input_ready/state_ready = false`, or an `exec` crashes the game | SDK is stale after a CS2 update → re-run §11 steps 3–5. |
| `run-scenario.py` "dumper not built" | `cargo build --release --manifest-path external/dumper/Cargo.toml`. |
| Client reaches a random/foreign pipe server | you injected twice → kill + relaunch, inject once. |
| `meson setup` picks the wrong compiler | configure inside a **vcvars64** shell with `CC=CXX=clang-cl`; delete the `build-*/` dir first. |
| Overlay never draws | manifest not registered (§8), or `CS2_OVERLAY` not set for the game process. |
| `rea doctor` says ghidra `missing_analysis_engine` | `GHIDRA_INSTALL_DIR` / `JAVA_HOME` wrong (§12). |
| First `client.dll` RE session takes ~20 min | expected without the project cache (§12b); subsequent sessions reuse the warm project. |
| `rea` CLI hangs / agent session dies after config edit | do **not** `opencode service restart`; the MCP server hot-reloads. |

---

## Appendix — what does NOT travel with the repo

Every fresh machine must recreate these; they are machine-local by design:

- global OpenCode config (`~/.config/opencode/opencode.jsonc`) — the `rea` MCP
  entry;
- Ghidra 12.1.4 + JDK 21 + REA install, and the REA project cache;
- `external/Vulkan-Headers/`, `toolchain/msvc/` (Linux lane);
- all `build-*/` directories, `calibration/*.json`, `payload/sdk/*.hpp`;
- the `CS2Launch` task, the Vulkan implicit-layer registry value, the Defender
  exclusion;
- your Steam + CS2 install.
