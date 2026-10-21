# The live run — the operator's checklist

> Everything in this repository is verified statically. **This is the one thing
> that is not**: no feature in this tree has ever been observed in a running
> CS2 (the agent runs in Windows session 0, with no GPU and no desktop). This
> page is the checklist for the person who can press the keys.
>
> Read it once before the first run; it is deliberately short.

---

## 0. Before anything: the build and the calibration

```powershell
cd C:\Users\gpmpo\cs2-internal

# the host build and the suite (the gate: 45/45 today)
meson compile -C build
meson test -C build

# the Windows targets (need the vcvars64 environment)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-injector"
```

> **The payload is injected with the STATIC CRT (`/MT`).** A build that
> defaults to the dynamic CRT (`b_vscrt=from_buildtype` with a release-ish
> buildtype gives `/MD`) makes `payload.dll` import `MSVCP140.dll`,
> `VCRUNTIME140.dll` and the `api-ms-win-crt-*` DLLs — the injector would
> then have to resolve them **inside the game process**. The configuration
> that is actually injected is `/MT`, so build it that way and **check**:
>
> ```powershell
> meson configure build-payload -Db_vscrt=mt
> cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
>
> # the imports must be exactly these three
> & 'F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin\llvm-readobj.exe' `
>      --coff-imports build-payload\payload.dll | Select-String 'Name:'
> ```
>
> `KERNEL32.dll`, `USER32.dll` and `D3DCOMPILER_47.dll` — anything else means
> the build is not the injected configuration.

**The calibration dump.** The payload's engine offsets come from
`calibration/offsets.json`. The **offsets** can be refreshed **without the
game**:

```powershell
python scripts\resolve-offsets-offline.py --check     # compare, write nothing
python scripts\resolve-offsets-offline.py             # refresh the offsets
```

(That script is T28; it resolves the same curated signatures from the
module's file on disk and reproduces the shipped dump byte for byte. It names
every signature that did **not** resolve — that list is the thing to read.)

The **interfaces and the schema** still need the live calibration
(`scripts/calibrate-live.py`, with the game running).

A feature whose signature is missing from the dump says so **in its status** —
it is not a silent failure.

## 1. Launching the game

The agent cannot launch it (session 0, no GPU). From an **interactive**
session:

```powershell
schtasks /run /tn CS2Launch
```

That task runs `scripts/launch-cs2.cmd` in session 1 with
`-applaunch 730 -insecure -novid`. Wait for the menu.

## 2. Injecting — **once**

```powershell
build-injector\injector.exe cs2.exe build-payload\payload.dll --load-missing
```

> **Never inject twice into the same game.** A second injection double-hooks
> the game and the result is not a bug in the payload.

The payload's log is `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`.

## 3. The probes, one feature at a time

Each feature reports its own evidence. The control channel is the window into
the payload; the MCP tools (`tools.cs2.*`) drive it from an agent session.

| feature | what to look at | what a good run looks like |
|---|---|---|
| **the auto-strafe** | the `input` status: `strafe_gain`, `strafe_steered`, `strafe_steered_ticks`, `strafe_ignored` | the speed **gains** while airborne; `strafe_steered` climbs. If `strafe_ignored` climbs instead, the server ignores the sub-tick view angles (§5.1 of `docs/handoff-wave2.md`) — that is the server's policy, not the payload's |
| **the animfix** | `animfix_duty_cycle` (≈ 1.0), `animfix_client_tick_seen` (must be **true**), `animfix_corrected_ticks` vs `animfix_client_ticks`, and the server's aim yaw next to the client's predicted one | the duty cycle is high, the flag is seen, and the **server's** value is the one in the graph. If `animfix_client_tick_seen` is false, the anim tick's flag polarity is wrong for this build and the feature cannot work |
| **the anti-aim** | the camera yaw next to the fake yaw | the **camera does not follow** the fake yaw. If it does, the split is not working — the scenario fails on it |
| **the chams** | the chams' status: whether the draw hook is installed, and the reason when it is not | the hook installs, the models are drawn with the flat material, and the colour is the one the operator picked. If the hook is refused, the status names the reason (no offset → the calibration is stale; prologue mismatch → not the build the RE measured) |
| **the third person** | the third-person status line: `hook=`, `writes=`, `cmds=` | the camera goes third person, `writes` climbs, and **the game does not die**. (It did, once: `0xc0000005` with a read of `0xFFFFFFFFFFFFFFFF` — §5.1b) |
| **the triggerbot** | the trigger status | it fires when the crosshair is on a target, and the shot is the game's own |

The scenarios do the same thing from the agent's side:

```powershell
cs2_run_scenario({name:"autostrafe", args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"animfix",   args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"anti_aim",  args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"bot-match", args:{map_name:"de_dust2", per_team:5}})
```

A bot match is one call: it loads the map, applies the playground convars
(no warmup, no freeze, infinite ammo, respawn on death, `bot_stop 1` so the
bots hold still), joins a team and adds the bots.

## 4. What to do when a probe fails

1. **Read the log first**: `%LOCALAPPDATA%\cs2-internal\cs2-payload.log`. Most
   failures say why ("the calibration dump has no '…' signature (re-run
   scripts/calibrate-live.py to refresh it)").
2. **The status, not the log**: the features report their state through the
   control channel (`animfix {"on": true}`, `strafe`, `chams`,
   `thirdperson`); the MCP tools reach them (`cs2_settings_get`,
   `cs2_settings_set`, `cs2_state`).
3. **A crash is evidence**: the payload's crash handler logs the faulting
   address and the access. Copy the two lines above it into the report — the
   address tells whether the fault is in the payload (`0x183…`, the manually
   mapped image) or in the game, and the access tells which read went wrong.
4. **Never "fix" it by injecting again.** Restart the game.

## 5. The rules that exist because of a past crash

- **Inject once.**
- **A feature that is off must not patch the game's code.** Every hook in
  this tree installs when the operator turns the feature on and is removed
  when it is turned off. If a hook is installed while the feature is off, that
  is a bug — report it.
- **No `thread_local` in a detour.** The payload is manually mapped: only the
  injector's entry thread has a TLS block, and a `thread_local` on a game's
  thread faults (that is what killed the game once — §5.1b).
- **A stale address is worse than no feature.** Every hook target in this
  tree is either re-resolved per build (a dumper signature) or verified before
  it is patched (the prologue bytes are read and compared).
