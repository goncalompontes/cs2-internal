# T40 — the control channel's settings must survive the menu's

> **The one-line version:** the overlay's settings callback re-applies the **wire
> block's** rage settings on every publication, so **anything the control
> channel sets is overwritten within a second**. Reproduced live: `rage
> {objective: "expected"}` answers `"expected"` and reads back
> `"hitchance"` a second later. The control channel is the **only** way to
> reach the Monte-Carlo objective (the menu has no such option), so today the
> new objective is **unreachable in practice** — the feature T35 shipped is
> inert.

---

## 1. What is wrong today

### 1.1 The reproduction (live, 2026-10-22, on the merged tree)

```
cs2_rage {objective: "expected"}   ->  the answer says  objective: "expected"
cs2_rage {}          (a second)   ->  the answer says  objective: "hitchance"
```

The setting is accepted, acknowledged, and then **reverted**.

### 1.2 The mechanism

`payload/src/payload_main.cpp:294-331` — the overlay's settings callback:

```cpp
void on_movement_settings(void* context, const cs2::overlay::wire::Settings& settings) noexcept {
  ...
  runtime->control.set_rage_features(
      config.rage.enabled, config.rage.key, config.rage.fire, config.rage.objective,
      config.rage.hitbox, config.rage.fov, config.rage.min_damage, config.rage.min_hitchance,
      config.rage.autostop, config.rage.autoscope, config.rage.autoreload != 0,
      config.rage.only_visible != 0, config.rage.mode, config.rage.per_weapon != 0,
      config.rage.weapon.data(), config.rage.config_op, config.rage.config_token,
      config.rage.recoil != 0, config.rage.stop_speed, config.rage.backtrack != 0);
```

`config` is `cs2::features::from_wire_settings(live)` — the **wire block's**
values. And the wire block's rage fields are:

`overlay/include/cs2/overlay/wire.hpp:119-120`:

```cpp
// the aim/fire; `rage_objective` selects the solve objective; `rage_hitbox` filters it.
uint8_t rage_objective;  // 0 = damage, 1 = hitchance, 2 = expected
```

with the default at `wire.hpp:488`: `settings.rage_objective = 1;  // hitchance`.

So **every** settings publication writes `rage_objective = 1` into the service,
and the control channel's value is gone on the next publication.

**This is not specific to the objective.** Every rage field the wire owns
(`enabled`, `key`, `fire`, `objective`, `hitbox`, `fov`, `min_damage`,
`min_hitchance`, `autostop`, `autoscope`, `autoreload`, `only_visible`, `mode`,
`per_weapon`, `recoil`, `stop_speed`, `backtrack`) is clobbered the same way.
The control channel is the **harness's only way** to drive those, and the
scenarios use it — so the harness's control of the ragebot is, for those
fields, **transient**.

### 1.3 Why it matters more now than before

T35 made the **Monte-Carlo objective** reachable *only* over the control
channel — the menu has no option for it, by design (T35's spec §2.7: *"the
objective is not exposed there, by design"*, and the wire block is versioned
and shared between two separately compiled binaries).

**So the feature T35 shipped cannot be used.** The operator selects it, and a
frame later it is gone.

### 1.4 The repo does not know about this

A grep of `docs/known-issues.md` for `clobber`, `settings sink`,
`on_movement_settings` and the wire/objective pairing returns **nothing**. It
was found by the operator trying to use the feature in a live game on
2026-10-22, and by the orchestrator chasing why the objective would not stick.

---

## 2. What to build

### 2.1 The rule

> **The overlay's settings apply a rage field only when the operator has
> actually changed that field in the menu.**

The callback must **not** re-apply a value it has already applied and that
has not changed. A value the **control channel** set is therefore **not**
clobbered by a menu that has not been touched.

**Both halves matter:**

* the control channel's value **survives** while the menu is idle;
* when the operator **does** change the field in the menu, the menu's value
  **wins** — that is what the operator asked for.

### 2.2 How to decide "has it changed"

The wire's `Settings` is a **POD** (`overlay/include/cs2/overlay/wire.hpp`), and
the rage fields are a contiguous group in it. The decision is therefore a
**comparison of the rage group** against the **last applied** copy.

**The decision must be a pure function** over the wire's rage values, so it
can be **host-tested** — the repo's pattern for exactly this kind of rule is
`cs2/control/include/cs2/control/rage_objective.hpp` (T35's pure rules, with
`test_control_rage_objective.cpp`).

**Requirements:**

* the comparison must cover **every** rage field the wire owns — a field left
  out of the comparison is a field that keeps clobbering the control channel;
* it must be **exact** — a float field compared with `!=` is fine for "did the
  operator change it", but say so and be careful about a field the menu
  recomputes every frame (if any, it would make the comparison always-true and
  the fix would not work — **find out** whether the menu republishes
  recomputed values);
* the **last applied** copy must be updated **after** the apply, so the next
  publication compares against what was actually applied;
* the state must be **initialised** to the first settings the callback sees
  (or to the wire's defaults), so the **first** publication applies.

### 2.3 The two channels must both keep working

| the operator does | the result |
|---|---|
| changes a rage field in the **menu** | the menu's value wins, immediately |
| sets a rage field over the **control channel** | it sticks, until the menu's field for it changes |
| changes a **non-rage** field in the menu (e.g. the ESP) | the rage settings are **not** re-applied, so a control-channel rage value survives |

**A subtlety to decide and state:** when the operator changes a rage field in
the menu, the callback applies **the whole** rage group (the operator's current
menu values) or only the changed field? The first is simpler and matches the
operator's intent (the menu is the source of truth for what it shows); the
second preserves a control-channel value for the *other* fields. **Pick one,
say which, and make the status able to show the operator which channel owns
each field** — at minimum, whether the objective came from the menu or the
control channel.

### 2.4 The status must say who owns the objective

The rage status already reports the objective **by name** (`objective`,
`objective_value`, `objective_global`). Add whether the objective in force
came from the **menu** or the **control channel**, so the operator can tell
"the control channel set it and it stuck" from "the menu set it and it stuck".

**A `monte_carlo` objective in force must be visible in the status even
though the menu cannot select it** — the operator must be able to confirm it
took.

### 2.5 The wire block is **not** changed

* **No `wire::k_version` bump.** The wire's `rage_objective` keeps its three
  documented values (0 = damage, 1 = hitchance, 2 = expected) and its default
  of 1. The menu keeps its three options.
* The **fourth** objective (`monte_carlo`, value 3) is reachable **only** over
  the control channel — which is where T35's spec put it.
* **`wire.hpp`, `esp.hpp`, `settings.cpp` and `renderer.cpp` are not touched.**
  The feature brief's §0.5 explains why: the block is shared between two
  separately compiled binaries, and a version bump is a silent-corruption risk.

---

## 3. What is out of scope

* The **wire block** and the **menu** — not touched (§2.5).
* The **Monte-Carlo core** — T34's, and it is not changed.
* The **per-weapon profile** — its own `objective` byte, and its own
  save/load, are untouched.
* The **occlusion / world-geometry** defect, the **fire path**, and the
  **spread patterns** — separate tasks.
* **Any other feature's settings** (the ESP, the anti-aim, the animfix) — the
  rule is about the rage group. If the same clobbering affects another
  feature's settings, **report it**; do not fix it here.

---

## 4. The tests

### 4.1 Host tests — the rule is pure and must be tested

* **The comparison**: given a wire's rage values and the last applied copy,
  the rule must say "changed" for a field the operator moved and "unchanged"
  for one they did not — for **every** rage field.
* **The first publication applies**: the very first callback must apply
  (there is no "last applied" yet).
* **The update**: after an apply, the last-applied copy must be the one that
  was applied.
* **A field the menu recomputes** — if the menu republishes a recomputed
  value for some field, the comparison must not report it as changed on every
  publication, or the fix does not work. **Find out whether the menu does
  this** (it may recompute, e.g. a "default" button) and handle it.
* **The end-to-end shape**: a **source check** that the callback consults the
  rule before applying — the repo's established pattern (T35's fix round used
  one for the rage command's mappings).

### 4.2 Prove each test can fail

Break the rule on purpose in a scratch copy, watch the test fail, revert, and
report the evidence.

---

## 5. File ownership

You own:

* `payload/src/payload_main.cpp` — the callback and the last-applied state
* the **new** pure-function header for the rule, and its host test under
  `payload/control/tests/`
* `payload/control/include/cs2/control/service.hpp` and
  `payload/control/src/service.cpp` — **only** if the rule's home or the
  status's "which channel owns it" field requires it
* `payload/control/meson.build` and the root `meson.build` (the new test)
* `docs/ragebot.md` — **append only**: the rule and the new status field
* `docs/known-issues.md` — **append only**: the finding, the fix, and the
  merge

You do **not** own `overlay/include/cs2/overlay/wire.hpp`,
`payload/features/include/cs2/features/esp.hpp`,
`payload/features/src/settings.cpp`, `overlay/src/renderer.cpp`,
`payload/aim/**`, `payload/control/src/trigger.cpp`, or the dumper. **If you
need a file you do not own, stop and report it** instead of editing it.

---

## 6. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and** definitions,
  fully qualified `std::`, no new global mutable state, `[[nodiscard]]` /
  `noexcept`, value-init with `{}`, `static_cast`/`reinterpret_cast` only,
  2-space indent, 100 columns, K&R braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
* **No allocation on the render thread** — the settings callback runs there.
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

**The live check is the operator's**, and it is the whole point of this task:
after the fix, `rage {objective: "monte_carlo"}` must **stick** — the status
must still say `"monte_carlo"` seconds later. **Say plainly that you could not
run it**, and leave the exact invocation for the operator.

---

## 8. What the report must state

1. **The rule** — how "has the operator changed this field" is decided, and the
   pure function that decides it.
2. **The fields** — every rage field the comparison covers, and **any field it
   does not** (and why).
3. **The recomputed-field question** — whether the menu republishes a
   recomputed value for any field, and how the rule handles it.
4. **The status** — what the operator sees about which channel owns the
   objective.
5. **The tests** — each one, and how it fails when the defect is reintroduced.
6. **The live check** — the exact invocation, and that it is the operator's to
   run.
7. What is still open, and what you could not verify.
