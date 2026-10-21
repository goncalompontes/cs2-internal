# T28 — the curated offsets, resolved **offline**

> **The one-line version:** the payload's offsets come from
> `calibration/offsets.json`, and refreshing it today needs a **running game**
> (`scripts/calibrate-live.py` attaches to `cs2.exe`). But the dumper's
> `--file` mode already resolves the very same curated signatures **from the
> module's file on disk** — no process needed. This task makes that a
> first-class path, so the animfix's and the chams' new signatures exist
> without the operator having to run the game.

---

## 1. Why this matters

The animfix's review (T20, round 5) found that the **shipped** calibration
dump has **none** of the animfix's signatures:

> "the repository's own calibration dump (`calibration/offsets.json`, 23
> entries) contains **none** of `animfix_graph_codec` / `animfix_anim_tick` …
> That is the *shipped* state of the tree. So the missing-offset path is taken
> on every tick of every session."

The same will be true of the chams' new `scene_draw_primitive` signature
(T26): a signature that the dump does not carry is an offset the payload
cannot resolve, and the feature is dead until the dump is refreshed — and
refreshing it today means the operator has to launch CS2 and run a live
calibration.

**It does not have to be that way.** The dumper has two modes:

* **live**: attach to the process, read the modules' memory;
* **file**: `--file <path>` — parse the PE, map it to RVA addressing, and
  analyse it (`external/dumper/src/main.rs:154-171`). The same
  `analysis::resolve_known(module, bytes, base)` runs in both
  (`main.rs:337`), so the offsets are the same.

I have already measured that the offline path reproduces the shipped dump
**exactly** on this machine:

```
cs2-dumper.exe --file "<game>\game\csgo\bin\win64\client.dll" \
                --resolve-offsets --json --module client.dll
```

| offset | the shipped `calibration/offsets.json` | the offline resolution |
|---|---|---|
| `game_entity_system` | `0x271a908` | `0x271a908` |
| `entity_list` | `0x236fa70` | `0x236fa70` |
| `game_rules` | `0x25651d0` | `0x25651d0` |
| `local_player_controllers` | `0x253cb78` | `0x253cb78` |
| `view_matrix` | `0x256b020` | `0x256b020` |
| `game_event_manager` | `0x254bc08` | `0x254bc08` |
| `event_key_hash` | `0x166050` | `0x166050` |
| `create_move` | `0xd02130` | `0xd02130` |
| `create_move_input` | `0xb65de0` | `0xb65de0` |
| `ccsgo_input` | `0x257b640` | `0x257b640` |
| `user_cmd_array` | `0x2223d00` | `0x2223d00` |
| `get_user_cmd_tick` | `0x15df9f0` | `0x15df9f0` |
| `get_user_cmd_array` | `0x9446c0` | `0x9446c0` |
| `get_user_cmd_by_sequence` | `0x9444a0` | `0x9444a0` |
| `repeated_ptr_add` | `0x1233010` | `0x1233010` |
| `subtick_step_alloc` | `0x4f74a0` | `0x4f74a0` |
| `physics_query_interface` | `0x2227058` | `0x2227058` |
| `physics_query_filter` | `0x2227070` | `0x2227070` |
| `entities_in_box` | `0x9dbd20` | `0x9dbd20` |
| `entities_along_linear_sweep` | `0x9db9a0` | `0x9db9a0` |
| `surface_properties_list` | `0x254e100` | `0x254e100` |

**All twenty-one match.** (The dump's other two entries are `engine2.dll`'s
`input_service` and `cbuf_add_text`.)

So the offsets do not need a running game. What *does* need one is the
**interfaces** and the **schema** (they come from the live process's
`CreateInterface` registry and its schema system) — this task is only about
the **offsets**.

## 2. What to build

### 2.1 The dumper learns to name its modules

`external/dumper/src/main.rs` gains a small flag:

```
--list-signature-modules
```

which prints the modules that have curated signatures — the distinct
`module` values in `analysis::known_signatures()` — as a JSON array. The
**point** is that the list comes from the signature registry itself: when
someone adds a signature for a new module (the chams add `scenesystem.dll`),
the offline path picks it up with no edit here.

The flag must not disturb the existing modes: it prints its list and exits
(like `--list-interfaces` does, if such a flag exists — follow the file's own
conventions).

### 2.2 The script

`scripts/resolve-offsets-offline.py` (new):

```
usage: resolve-offsets-offline.py [--game-dir DIR] [--dump-dir DIR] [--check]
```

* `--game-dir` — the game's root (`...\Counter-Strike Global Offensive`).
  The default is the Steam install path on this machine; make the default
  **discoverable** (the Steam library path is not fixed — the game lives in
  `F:\SteamLibrary\…` here) and say so in `--help`.
* `--dump-dir` — where `calibration/offsets.json` lives. The default is the
  repository's `calibration/` directory.
* `--check` — resolve and **compare** with the existing dump; write nothing;
  exit non-zero when they differ. This is the mode CI and the reviewer can
  run.

The script:

1. asks the dumper for the modules (`--list-signature-modules`);
2. finds each module's file: the game's `game/bin/win64` and
   `game/csgo/bin/win64` (the layout differs per module — `client.dll` is in
   the `csgo` tree, `scenesystem.dll` and `materialsystem2.dll` are in the
   `game` tree). Search both; say which one it used;
3. runs the dumper's offline resolution per module
   (`--file <path> --resolve-offsets --json --module <module>`);
4. **merges** the results and writes `offsets.json` in the **same shape** the
   live calibration writes (`{"name", "module", "rva", "source"}` — check
   `scripts/calibrate-live.py` and the dumper's JSON);
5. reports, per module, how many signatures resolved and **which ones did
   not** — a signature that no longer matches on this build is the whole
   point of the exercise, and it must be **loud**;
6. **never writes a partial file**: resolve everything first, then write
   atomically (write to a temporary file and replace);
7. leaves `interfaces.json`, `schema.json` and `schema_system.json`
   **untouched** — say so in the output, because they still need the live
   calibration.

### 2.3 The dumper must be found

The script resolves the dumper the way `scripts/gen-sdk.py` and
`scripts/calibrate-live.py` do — including the `CARGO_TARGET_DIR` case, which
is the trap the T21 reviewer and the T20 reviewer both hit: the environment
redirects cargo's output, and the scripts that look only in
`external/dumper/target/release/` cannot find the binary. **Follow the
resolution `scripts/gen-sdk.py` already uses** (it was fixed for exactly this
in T24) — do not invent a third convention.

### 2.4 The documentation

* `scripts/resolve-offsets-offline.py --help` explains the difference
  between the **offsets** (offline is enough) and the **interfaces/schema**
  (the live calibration is still needed).
* `docs/setup-windows.md` gains the step: "refresh the offsets without the
  game".
* The AGENTS note (or `docs/handoff-wave2.md` — whichever the file's own
  conventions favour) says that a new signature only needs the **offline**
  refresh to become usable, and that the live calibration is only needed for
  the interfaces and the schema.

## 3. What is out of scope

* **`scripts/calibrate-live.py`** — another task (T26, the chams) owns it.
  Do not edit it; if you think it needs a change, say so in your report.
* The **interfaces** and the **schema** — they need the live process.
* The payload's own code (`payload/**`) — this task is a toolchain change.
* The dumper's *analysis* logic — the signatures themselves are not this
  task's business.

## 4. File ownership

You own:

* `external/dumper/src/main.rs` (the `--list-signature-modules` flag) and
  `external/dumper/src/analysis.rs` **only if** you need a public accessor
  for the module list (prefer a small, well-named one over duplicating the
  list).
* `scripts/resolve-offsets-offline.py` (new)
* `scripts/scenario/tests/test_resolve_offsets_offline.py` (new — the
  script's logic, tested)
* `docs/setup-windows.md` (the new step)
* `docs/re/README.md` or a new `docs/re/offsets.md` — **your choice**, but
  record the method and the measurement (the table in §1 is the evidence).

You do **not** own `scripts/calibrate-live.py`, `scripts/gen-sdk.py`, the
payload, the overlay or the injector. If you need something outside this
list, say so in your report instead of editing it.

## 5. The rules

- The Python is **mypy strict** (`python scripts/typecheck.py` is the gate):
  every function is typed, no bare `dict`/`list` without a parameter type,
  no `Any` unless it is unavoidable and explained.
- The repository's Python style: 4-space indent, `snake_case`, module-level
  constants in `SCREAMING_SNAKE_CASE`, no `print` in library code (the
  scripts print — they are programs), `from __future__ import annotations` if
  the file needs it, and the error handling the other scripts use (a
  non-zero exit and a message on stderr, not a traceback).
- The dumper is Rust: `cargo clippy`-clean style, `thiserror`/`anyhow` as
  the file already uses, no unwraps in a new code path.
- Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

## 6. The test that must be able to fail

The script's **decision logic** must be in importable, testable functions
(the repository's convention — `scripts/scenario/tests/` holds unit tests for
the scripts):

* the **module list** comes from the dumper, not from a hard-coded list —
  test it with a fake dumper answer;
* the **module file** is found in either game tree — test both layouts and
  the "not found" case;
* the **merge** keeps every module's entries and does not drop a module
  whose resolution came back empty — test that;
* the **unresolved** signatures are reported — test that a signature that
  does not resolve on the build is **named** in the output (this is the
  whole point of the script: a signature that silently stops matching must be
  visible);
* the write is **atomic** and **complete**: a failure half-way leaves the
  previous dump untouched.

And a **host test for the flag**: the dumper's `--list-signature-modules`
prints the modules that have curated signatures — and the list contains
`client.dll` and `engine2.dll` today, and `scenesystem.dll` once the chams'
signature lands. If the list is empty, or does not contain the modules the
signature registry names, the test fails.

## 7. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t28-offline-offsets\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\t28-offline-offsets\build
meson test   -C C:\Users\gpmpo\cs2-build\t28-offline-offsets\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t28-offline-offsets\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t28-offline-offsets\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t28-offline-offsets\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

**The proof of the task**, which you must run and report: the script, run
against this machine's game, produces an `offsets.json` whose entries **match
the shipped `calibration/offsets.json` exactly** for the twenty-one
`client.dll` offsets and the two `engine2.dll` ones — and it says so. The
`--check` mode must report "no difference" against the current dump (apart
from the offsets that only exist once the animfix and the chams land).

## 8. What the report must state

1. **The flag** — what it prints and how it is derived from the registry.
2. **The script** — what it does, step by step, and what it refuses to do.
3. **The measurement** — the command you ran and the output, including the
   comparison with the shipped dump.
4. **The test** — the hostile test and the proof that it fails without the
   fix.
5. **What is still open** — the interfaces and the schema still need the
   live calibration; the game is still needed for the live verification.
6. The file list and the commit hashes.
