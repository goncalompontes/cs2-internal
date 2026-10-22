# T37 — the fire path: semi-automatic weapons, and "can we actually shoot"

> **The one-line version:** the ragebot holds `IN_ATTACK` down on **every**
> command while a solve is active, so a **semi-automatic** weapon (a pistol, the
> R8, the scout) fires **once** and never again — the engine needs a
> release→re-press **edge**. And nothing in the fire path asks whether the shot
> can be taken at all: the clip, the fire interval, the weapon's state and the
> fire mode are all unread. This task makes the fire path correct for
> semi-automatic weapons and gates it on what the weapon can actually do.

---

## 1. What is wrong today

Both findings were reported by the operator on 2026-10-22 and **verified in the
code**.

### 1.1 The attack is held, so a semi-automatic weapon fires once

`payload/control/src/input.cpp:2627-2640`:

```cpp
  // The features' buttons: the triggerbot's own hold, plus the ragebot's
  // assists. The ragebot's attack is a **persistent** press while a solve is
  // active ...
  const bool rage_solved = rage_active_.load();
  const bool rage_firing = rage_solved && rage_fire_.load();
  std::uint64_t feature_bits = feature_mask_.load();
  if (rage_firing) {
    feature_bits |= button::k_attack;
  }
```

And the feature bits are **only ever set, never cleared** —
`payload/control/include/cs2/control/strafe_geometry.hpp:201-216`:

> *"The features' bits are only ever **set**, never cleared: a feature that
> releases the attack button must not take the player's own press of the same
> button away."*

The ragebot **never releases** the attack: `release_feature(button::k_attack)`
appears only in the **triggerbot** (`payload/control/src/trigger.cpp:188`,
`:330`). The ragebot's `Input::clear_rage_aim()` (`input.cpp:298-303`) clears
`rage_active_/stop/scope/reload` and touches no button.

**Consequence:** the engine sees a continuously-held trigger. A
semi-automatic weapon fires **once**; the shot rate is the *weapon's*, not the
ragebot's. The payload asserts the bit and the engine ignores it.

### 1.2 Nothing asks whether the shot can be taken

Exhaustive audit of the fire path:

| what | in the schema? | read by the payload? |
|---|---|---|
| `m_bIsFullAuto` (`CCSWeaponBaseVData`) | **yes** | **no** |
| `m_flFireInterval` | **yes** | **no** |
| `m_flRefireTime` | **yes** | **no** |
| `m_nNextPrimaryAttackTick` | **yes** | **no** |
| `m_flPostponeFireReadyFrac` | **yes** | **no** |
| `m_iClip1` | yes | **only** the ESP ammo bar (`state_reader.cpp:107`, `:260`) and the **auto-reload assist** (`service.cpp:2019`) — **never** as a fire gate |
| the weapon's state (reloading / deploying / `m_bFiring`) | yes | **no** |
| the fire mode | yes | **no** |

`is_rage_weapon` (`service.cpp:1439-1461`) distinguishes only **firearm vs
non-firearm** by `m_WeaponType` 1–6. There is **no semi/full-auto
distinction anywhere in the payload**.

The solve itself is **unconditional**: `aim::solve` is pure geometry and has no
knowledge of ammo, the fire rate, the weapon's state or the fire mode. The only
gates are *after* the solve, and they are about the **aim**, not the shot
(`service.cpp:2406-2411`, `:2428`):

| gate | effect |
|---|---|
| `result.value < tuning.min_damage` | `clear_rage_aim()` — no aim and **no fire** |
| `result.hitchance < tuning.min_hitchance` | `clear_rage_aim()` — no aim and **no fire** |
| `tuning.only_visible && result.penetrated` | the aim is written, but `firing` is **false** |
| `!tuning.fire && !forced` | the aim is written, but `firing` is **false** |

### 1.3 The repo does not know about this

A grep of `docs/` for `semi`, `full-auto`, `m_flFireRate`, `m_iClip1` in the
fire path returns **nothing**. The only related line is
`docs/ragebot-review.md:142` (*"**Auto-reload** (`rage_autoreload`) | bool |
holds `IN_RELOAD` when the clip is empty"*). **This is an undocumented,
unhandled defect** — and the operator found it by playing.

---

## 2. What to build

### 2.1 Read the weapon's fire capability

The payload must learn, per weapon:

* **is it full automatic?** (`m_bIsFullAuto` on the weapon's VData, or whatever
  the current build's field is — **find it in the schema**, do not guess an
  offset);
* **how fast can it fire?** (`m_flFireInterval` / `m_flRefireTime` /
  `m_nNextPrimaryAttackTick` — pick the one the engine actually uses and say
  why; the schema has all three);
* **how much ammo is left?** (`m_iClip1` — already read; make it available to
  the fire path);
* **is the weapon ready?** (not reloading, not deploying, `m_bFiring`).

**Read them through the existing pattern** — the generated SDK
(`payload/sdk/offsets.hpp`) and the state's reader
(`payload/game/src/state_reader.cpp`) — with a **dumper signature** where the
field is not already in the schema (ADR-0012). **A field that is in the schema
must be read through the schema**, not through a hard-coded offset.

**A field the payload cannot read must degrade safely**, not silently: if the
fire mode is unknown, the **conservative** behaviour is to treat the weapon as
**semi-automatic** (the re-press edge costs nothing and is always correct) and
to report `"unknown"` in the status. Say which way you went and why.

### 2.2 A semi-automatic weapon needs a real edge

**The requirement:** for a semi-automatic weapon, the ragebot's shots must be
**separate shots** — the engine must see the attack **released** between them.

**The constraint that makes this hard**, from
`payload/control/include/cs2/control/strafe_geometry.hpp:201-216`: the
features' bits are **only ever set, never cleared**, *"a feature that releases
the attack button must not take the player's own press of the same button
away"*.

So the release must be **scoped to the ragebot's own contribution** and must
**not** clear the player's own press. The design must satisfy both:

* the player holds attack → the ragebot must not steal it;
* the ragebot's own shot on a semi-auto weapon must be followed by a
  **release** it caused, so the next shot is a new edge.

**The decision you must make and state:** when does the ragebot release?
Options, each with a cost — pick one and justify it:

* **release on the tick after the shot** (the simplest: press, then release for
  one command, then press again);
* **release until the weapon is ready again** (the engine's own rate);
* **hold the press but toggle it** on the commands where a re-press is needed.

**The requirement whichever you choose:** the engine must see a
**release→re-press edge** between two ragebot shots on a semi-automatic
weapon, and the **player's own** press must survive the whole thing.

**The player's own press is a real case**: the operator holds attack while the
ragebot is on. Then the ragebot's press is *not* an edge either — the engine
sees one continuous press. **Say what happens in that case** and make it the
documented behaviour.

### 2.3 A "can we shoot" gate

**The requirement:** the ragebot must not assert the attack when the shot
**cannot** be taken. The gate is evaluated on the game thread (where the
weapon's state is readable) and must be cheap.

**The conditions to gate on** — the spec's list is a minimum, not a maximum:

| condition | why |
|---|---|
| the clip is **empty** | the shot cannot be fired; the auto-reload assist already holds `IN_RELOAD` in that case (`service.cpp:2019`) |
| the weapon is **reloading or deploying** | the engine ignores the trigger |
| the weapon's **fire interval** has not elapsed | the shot would be dropped by the engine anyway — and on a semi-auto weapon, a press inside the interval is **the shot being wasted** |
| the weapon is **not ready** (`m_bFiring`, the deploy flag) | same |

**A gate that is wrong in the "don't fire" direction is safe; a gate that is
wrong in the "fire" direction is not.** When in doubt, do not fire.

**The gate must not break the existing behaviour**: the triggerbot has its own
hold (`trigger.cpp`) and is **out of scope** — do not change it.

### 2.4 The status must say what the fire path decided

The operator is looking at a game, not at a diff. The rage status must report:

* the weapon's **fire mode** as the payload understands it (`"auto"`,
  `"semi"`, `"unknown"`);
* whether the fire path is **currently able to shoot**, and if not, **why**
  (which condition blocked it) — the same discipline the chams' `refused=`
  counter follows;
* the number of **shots the ragebot actually caused** (the payload already
  counts `rage_shots_`, `input.cpp:701-703` — check it counts the *engine's*
  shots and not the ragebot's attempts; if it counts attempts, say so and
  report both).

**The refusal must never be silent** — a silent "did not fire" is the defect
this task is fixing.

---

## 3. What is out of scope

* The **triggerbot** (`payload/control/src/trigger.cpp`) — its own hold, out of
  scope.
* The **third-person camera**, the **anti-aim**, the **ESP** and the **animfix**
  — untouched.
* The **occlusion / world-geometry** defect — a separate task (T39 is its
  instrument).
* The **spread patterns** — a separate task.
* The **`wire::Settings` block** — if the fire mode or the gate needs a new
  setting, it goes in the **control channel** and the per-weapon profile, not
  in the wire. (The wire is versioned and shared between two binaries; the
  feature brief's §0.5 says so.)
* **Changing the engine's behaviour.** The payload asks; the engine decides.

---

## 4. The tests

### 4.1 Host tests

* **The fire-mode classification** — a weapon profile that is full-auto, one
  that is semi, and one whose mode is **unknown**: each must classify
  correctly, and the unknown one must take the conservative path.
* **The edge logic** — the pure decision *"should the ragebot assert the
  attack on this command, given the weapon's mode, the ammo, the weapon's
  state and the elapsed fire interval?"* must be a **pure function** with a
  host test. It must be **pure** — no game state, no globals — so it can be
  tested exhaustively.
* **The player's press survives** — the pure function must show that the
  player's own held attack is never cleared by the ragebot.
* **The gate** — each blocking condition must block, and the empty-conditions
  case must not.
* **The unknown-mode degradation** — the status must say `"unknown"`.

### 4.2 Prove each test can fail

Break the rule on purpose in a scratch copy, watch the test fail, revert, and
report the evidence.

---

## 5. File ownership

You own:

* `payload/control/src/input.cpp` and
  `payload/control/include/cs2/control/input.hpp` — the fire path
* `payload/control/include/cs2/control/strafe_geometry.hpp` — the button-mask
  composition, **only** if the scoped release requires it
* `payload/control/src/service.cpp` and
  `payload/control/include/cs2/control/service.hpp` — the gate's call site, the
  status, the profile
* `payload/control/include/cs2/control/rage_config.hpp` and
  `payload/control/src/rage_config.cpp` — the per-weapon profile, if a new
  setting belongs there
* `payload/game/src/state_reader.cpp` and
  `payload/game/include/cs2/game/snapshot.hpp` — the new weapon fields
* `payload/control/tests/**` (the new host tests)
* `external/dumper/src/analysis.rs` and `scripts/calibrate-live.py` — **only**
  if a signature is needed for a field that is not in the schema (ADR-0012)
* `docs/ragebot.md` — **append only**: the fire path's rules and the new
  status fields

You do **not** own `payload/aim/**` (the solver), `payload/features/**`,
`overlay/**`, `wire::Settings`, `payload/control/src/trigger.cpp`, or the
occlusion query. If you need a file you do not own, **stop and report it**
instead of editing it.

---

## 6. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors are values,
  **no exceptions**, trailing return types on declarations **and**
  definitions, fully qualified `std::`, no new global mutable state,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, 2-space indent, 100 columns, K&R braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
* **No allocation on the render thread** and none on the command path.
* The command path runs on the **game's** thread; the gate reads the weapon's
  state there.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 7. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\<task>\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\<task>\build
meson test   -C C:\Users\gpmpo\cs2-build\<task>\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories live **outside** the worktree; do not run `meson setup`
inside it.)

**The live run is the operator's.** The agent cannot see the screen. Say plainly
what you could not verify — in particular, whether the engine really needs a
release→re-press edge for a semi-automatic weapon is **an engine behaviour the
host tests cannot prove**: the host test can prove the payload *emits* the edge,
and the operator's run proves the engine *honours* it. **State that split
explicitly.**

---

## 8. What the report must state

1. **The fields** — which fields you read, from where (the schema or a new
   signature), and what each one means.
2. **The edge rule** — when the ragebot releases, why, and what happens when
   the **player** is holding attack.
3. **The gate** — every condition, and the "unknown" degradation.
4. **The status** — exactly what the operator will see.
5. **The tests** — each one, and how it fails when the defect is reintroduced.
6. **The split** — what the host tests prove, and what only the operator's live
   run can prove.
7. What is still open, and what you could not verify.
