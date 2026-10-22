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

# the host build and the suite (the gate: 59/59 today)
meson compile -C build
meson test -C build

# the scenario suite - the library the MCP server speaks (504 today)
python -m unittest discover -s scripts\scenario\tests -t scripts

# the Windows targets (need the vcvars64 environment)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-injector"
```

> **Where the counts come from.** "59/59" is what `meson test -C build`
> prints at this commit — one Meson test per C++ host test — and "504" is what
> `python -m unittest discover -s scripts\scenario\tests -t scripts` prints
> (the scenario library's own suite). The counts move as the tree grows: if a
> count here does not match what the commands print, **the page is stale, not
> the tree** — fix the page.

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
| **the chams** | the chams' status: `hook=`, `seen=`, `chams=`, `skip=`, `rec=`, `pool=`, `tdn=`, `gu=` | the hook installs (`hook=installed`), the models are drawn with the flat material, and the colour is the one the operator picked. If the hook is refused, the status names the reason (no offset → the calibration is stale; prologue mismatch → not the build the RE measured). **The counters are the health:** `rec=` (the guard broke a recursion — it should stay **0**, and a climbing one means the hook's target is wrong), `pool=` (**the guard's pool was exhausted — the primitive was not drawn at all**, so a climbing `pool` means the chams are eating the scene), `tdn=` (a draw that arrived while the hook was torn down — one per teardown) and `gu=` (the desc cache's give-ups — the cache's health; it should stay **low**, because while it climbs the draw path pays two engine reads per *primitive* instead of per *model*). **The live run is still owed:** the detour has **never run against the real draw**, the tier0 render-job count is **unmeasured** (the 64-slot pool is headroom, not a measurement), and the fail-closed refusal's real-world frequency is unknown — so these counters are what the operator's first run is for. See `docs/known-issues.md` §14 |
| **the Monte-Carlo objective** | the `rage` status: `objective` (`"monte_carlo"`), `mc_hitchance`, `mc_expected_damage`, `mc_samples`, `mc_seed`, `mc_refusal`, `mc_fallback` | it is selected over the **control channel** (`{"command": "rage", "args": {"objective": "monte_carlo"}}`) or from the per-weapon profile — **not from the menu**. The numbers are the **measured** probability and the **expected** damage, so `min_hitchance` and `min_damage` now mean **different things**: the same `min_hitchance` passes **more** shots and the same `min_damage` blocks **more** — the two gates move in **opposite** directions (`docs/ragebot.md` §14 has the numbers). **Turn the backtracking off** while live-testing it (`{"command": "rage", "args": {"backtrack": false}}`): the Monte-Carlo solve and the backtracking pass do not fit the frame budget together. **The MCP `cs2_rage` tool refuses `"monte_carlo"` until the server is restarted** (it imported the schema at start-up); the repository's own runner does not have that problem (`python scripts\run-scenario.py rage --set objective=monte_carlo`), and `cs2_run_scenario`'s `args` are free-form, so the objective is live-testable today. §6 has the details |
| **the third person** | the third-person status line: `hook=`, `status=`, `updates=`, `writes=`, `cmds=` | the camera goes third person, `writes` climbs, and **the game does not die**. (It did, once: `0xc0000005` with a read of `0xFFFFFFFFFFFFFFFF` — §5.1b. The cause was a `thread_local` in the detour, which is **fixed**; the guard is now per owner.) |
| **the triggerbot** | the trigger status | it fires when the crosshair is on a target, and the shot is the game's own |

The scenarios do the same thing from the agent's side:

```powershell
cs2_run_scenario({name:"autostrafe", args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"animfix",   args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"anti-aim",  args:{map_name:"de_dust2"}})
cs2_run_scenario({name:"bot-match", args:{map_name:"de_dust2", per_team:5}})
cs2_run_scenario({name:"rage",      args:{objective:"monte_carlo"}})
```

> The names are the registry's own, so they are **refused** if you mistype one
> (`anti-aim`, not `anti_aim` — the module is `anti_aim.py`, the scenario is
> not).

A bot match is one call: it loads the map, **joins the team** (as early as
the screen allows), applies the playground convars (no warmup, no freeze,
infinite ammo, respawn on death), restarts the round so the convars take
effect, waits for the round to go live, adds the bots, freezes them
(`bot_stop 1` so the bots hold still) and **waits for the round to be live
again** before it returns — so nothing has to be waited for by hand.

The result says what happened: `screen` (the screen the game is in when the
scenario returns), `round` (the phase and the recent round events),
`waited_s` (how long the final wait took) and `team_joined` (`"joined"` — the
scenario's own `jointeam` — or `"auto"`, the server assigned the team).

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

## 6. The Monte-Carlo objective (the live check for T35)

> Added 2026-10-21 — task T35 (`docs/orchestration/spec-t35-montecarlo-live.md`).
> The Monte-Carlo hit-chance solver is in the live path as a **fifth
> objective**. The four existing ones are unchanged, so the comparison below is
> the point: the same scene, the same weapon, two solvers.
>
> **What the numbers mean is in `docs/ragebot.md` §14.2** — read that before
> comparing a number from one objective with a number from the other.

### 6.1 Switching the objective

The objective is selected over the **control channel**:

```json
{"command": "rage", "args": {"objective": "monte_carlo"}}
{"command": "rage", "args": {"objective": "hitchance"}}
```

The `rage` command's **answer** is the status. What to look at:

| field | what a good run looks like |
|---|---|
| `objective` | `"monte_carlo"` — the objective **by name**, not a number |
| `mc_samples` | `64` — the sample count in force |
| `mc_seed` | `"sequence"` (the default) or `"angle_grid"` |
| `mc_seed_fallback` | `true` under `sequence`: the tick stands in for the command's sequence number, which the service cannot reach |
| `mc_hitchance` | the measured probability — a number between 0 and 1 that moves with the target |
| `mc_expected_damage` | the **expected** damage (a miss contributes 0) |
| `mc_refusal` | `""` when the solver produced a number |
| `mc_fallback` | `false` while the solver is producing numbers |

The one-line HUD status carries the objective's name too (`obj monte_carlo`).

### 6.2 The comparison that matters

1. Point at a target with the **old** objective and note the status.
2. Switch to `monte_carlo` and note it again, **with the same target in the
   same place**.
3. The two `hitchance` numbers are **not** the same quantity: the old one is a
   Gaussian mass (too narrow a target, too wide a σ → it reports **0.221** where
   the truth is **0.777**), the new one is the measured probability. The new one
   is the number you can trust; the old one is the number the gates have been
   tuned against.
4. `mc_expected_damage` is the **expected** damage, so it is lower than the old
   objective's `value` (which is the damage of a shot that hits). A 50 % hit
   chance on 100 damage reports **50**.

### 6.3 The gates behave differently — and that is the point

`min_hitchance` and `min_damage` are compared against **different quantities**
under the new objective (`docs/ragebot.md` §14.2):

* the same `min_hitchance` **passes more shots** under `monte_carlo` (the old
  gate was stricter than its number suggested) — to keep the old strictness,
  raise it to about the true probability (≈ **0.78** for a head-sized target);
* the same `min_damage` **blocks more shots** under `monte_carlo` (the expected
  damage is lower than the hit damage) — lower it, or read it as "the damage I
  expect".

So: **a ragebot that fires under `hitchance` and does not fire under
`monte_carlo` with the same thresholds is not a bug — it is the semantics
change.** Re-tune with the new numbers in front of you.

### 6.4 The refusal is visible

Switch to a weapon whose pellet pattern is **not modelled** (a shotgun, a
machinegun) and switch the objective to `monte_carlo`:

```json
{"command": "rage", "args": {"objective": "monte_carlo"}}
```

What to look at: `mc_refusal` must name the reason
(`"unsupported_pattern"`) and `mc_fallback` must be **true**. The aim then comes
from the `damage` objective — **the fallback is not silent**, and the status
says so. If `mc_refusal` is empty and the aim is wrong, that is a bug: report
it.

### 6.5 The seed policy

```json
{"command": "rage", "args": {"seed": "angle_grid"}}
```

What to look at: `mc_seed` becomes `"angle_grid"`, `mc_seed_fallback` becomes
`false`, and `mc_seed_note` says the numbers are **indicative, not exact** (the
hash is a stand-in for the engine's unreverse-engineered
`get_tick_view_angles`). The two policies answer different questions —
`sequence` is the expectation over the next N shots, `angle_grid` the
probability over the shot that would be taken — so compare them, do not average
them.

### 6.6 The sample count and the cost

```json
{"command": "rage", "args": {"samples": 128}}
```

`mc_samples` must show the count in force. **Leave it at 64** unless you are
measuring: 128 is over the 12 ms frame guard and 256 is over the tick.

**Turn the backtracking off** while live-testing this objective
(`{"command": "rage", "args": {"backtrack": false}}`): the backtracking pass is
a full solve per pose, and the Monte-Carlo solve is ~4.3 ms at a live-sized
hitbox set, so the two together do not fit the frame budget.

### 6.7 The per-weapon profile

The objective is part of the per-weapon profile too:

```json
{"command": "rage", "args": {"objective": "monte_carlo", "profile_save": true, "weapon": "weapon_ak47"}}
{"command": "rage", "args": {"profile_load": true, "weapon": "weapon_ak47"}}
```

What to look at: after the load, `objective` is still `"monte_carlo"` — the
byte round-trips through `cs2-rage.cfg`. A **garbage** byte in the file loads
as the hitchance (the documented safe default), never as an arbitrary
objective.

### 6.8 Selecting it from the scenario harness

The harness's `rage()` helper takes the objective's **name**:

```python
game.rage(on=True, force=True, objective="monte_carlo")
```

> **The MCP tools' schema.** The harness's `AimObjective` type now carries
> `"monte_carlo"` (`scripts/scenario/protocol.py`), so the `cs2_aim` and
> `cs2_rage` tools **accept** it — but the **running server still advertises
> the old schema**, because it imported the module at start-up. Until it is
> restarted, use the scenario library's `game.rage(...)` (which passes the
> name through) or the raw control channel. See **§6.9**.

### 6.9 After the review round (2026-10-22)

**§6.8's warning is closed.** The harness's `AimObjective` type now carries
`"monte_carlo"`, so the MCP `cs2_aim` and `cs2_rage` tools accept it:

```json
{"name": "cs2_rage", "arguments": {"objective": "monte_carlo"}}
```

> **The running MCP server still advertises the old schema.** It imported
> `protocol.py` at start-up, so the two tools refuse `"monte_carlo"` until the
> server is restarted by hand. Until then the feature is live-testable through
> the scenario library (`game.rage(objective="monte_carlo")`) or through
> `cs2_run_scenario`, whose `args` are free-form:
>
> ```json
> {"name": "cs2_run_scenario", "arguments": {"name": "rage", "args": {"objective": "monte_carlo"}}}
> ```
>
> **The repository's own runner** (`python scripts\run-scenario.py <name>`) is
> a **fresh process**, so it picks the new schema up at once — no restart.

**What to look for in the HUD.** The one-line status carries the objective's
name and, under the Monte-Carlo objective, a marker: `mc83%` is the measured
probability, and `mc-refused` says the solver refused — the aim then came from
the `damage` objective, and §2.4 says a fallback must never be silent.

**The status's new keys.** `mc_can_kill`, `mc_hitgroup` and `mc_hits` are in
the `rage` status now; `objective` names the **effective** objective (the
per-weapon profile's, when there is one) and `objective_global` the setting;
`mc_samples_note` says what the sample count in force means; and
`backtrack_over_budget` says whether the last tick skipped the backtracking
because the frame had already blown the 12 ms guard.
