# T35 — the Monte-Carlo solver in the live path

> **The one-line version:** T34 built the Monte-Carlo hit-chance solver as a
> **host-tested core**. This task puts it **in the game**: a new
> `AimObjective::monte_carlo`, reachable from the control channel and the
> per-weapon profile, so the operator can live-test it against the existing
> solver. The existing objectives **stay** and remain selectable.

---

## 1. What this task is, and what it is not

T34 (`docs/orchestration/spec-t34-montecarlo.md`, the spec on the branch) built
`payload/aim/include/cs2/aim/montecarlo.hpp` and
`payload/aim/src/montecarlo.cpp`:

| symbol | what it is |
|---|---|
| `SeedSource` | `sequence` (the repo's RE model) or `angle_grid` (the community's, with a **stand-in** hash) |
| `SeedContext` | `{ sequence, tick, source, probe_step }` |
| `SpreadKernel` / `spread_kernel(weapon, pellets, …)` / `pattern_supported(weapon)` | the engine's kernel and the refusal for the unmodelled patterns |
| `MonteCarloInput` | `{ eye, basis, hitboxes, targets, weapon, kernel, seed, samples }` |
| `TargetHealth` | `{ index, health }` — the health the lexicographic rule's "can kill" needs |
| `CandidateBudget` | `{ max_cells, max_candidates }` |
| `MonteCarloChoice` | `{ valid, aim, chance, target, hitgroup, can_kill }` |
| `HitChance` | `{ probability, expected_damage, samples, hits, target, hitgroup }` |
| `solve_montecarlo(input, cells, budget) -> std::expected<MonteCarloChoice, MonteCarloError>` | the bounded search |
| `evaluate_hit_chance(input, aim) -> HitChance` | one candidate |
| `hitgroup_preference(group) -> int` | the preference order |

**This task does not change the Monte-Carlo core.** It wires it up.

**Read T34's spec and T34's report first.** The core's limits are stated there
and they constrain this task — the two most important:

* the MC solver scores the **hitbox geometry only**. The repo is Tier-2: the
  world is projected into the chart, not traced. **Whether the shot is visible
  or penetrates a wall is still the existing machinery's job.**
* `angle_grid`'s hash is a **stand-in** for the engine's unreverse-engineered
  `get_tick_view_angles`. Its numbers are **indicative, not exact**.

---

## 2. What to build

### 2.1 The objective reaches the solver

**`payload/aim/include/cs2/aim/solver.hpp`** — the `AimObjective` enum
(`solver.hpp:24-29`) gains `monte_carlo`. It is a **value**, not a behaviour
change: the existing four keep their exact behaviour.

**`payload/aim/include/cs2/aim/solver.hpp`** — `SolveInput`
(`solver.hpp:31-55`) gains the Monte-Carlo's parameters. They must be
**optional** — a `SolveInput` that does not set them must behave exactly as it
does today:

* the sample count (default **64** — see §2.1a: it is the **only** count that
  fits the budget at a live-sized hitbox set);
* the `SeedContext`;
* the `CandidateBudget`;
* the targets' health — `std::span<const TargetHealth>`.

### 2.1a The sample count is **64**, and the reason is a measurement

The review of T34 re-measured the solver at a **live-sized** hitbox set — the
repo's own RE documents `m_nHitBoxes (19 on a player)` (`docs/re/aim.md:240`),
so a live 3-target solve hands the solver **57** hitboxes, not the 24 the
benchmark's reconstruction uses:

| N | p50 | p99 | verdict |
|---|---|---|---|
| **64** | **4.26 ms** | **6.56 ms** | **fits** — inside the 12 ms guard and the 15.6 ms tick |
| 128 | 9.42 ms | 13.88 ms | **over** the 12 ms guard |
| 256 | 17.25 ms | 18.70 ms | **over** the 15.6 ms tick |

So the live path's default must be **64**, and the status must show the count.
**Do not let the live path pick 128 or 256.** If the operator wants a different
count, it must be an explicit choice, and the status must show it.

**`payload/aim/src/solver.cpp`** — the objective switch (`solver.cpp:119-136`)
gains the `monte_carlo` case. It must:

1. build the `MonteCarloInput` from the `SolveInput` — the eye, the basis, the
   hitboxes, the targets, the weapon, the kernel (`spread_kernel(...)`), the
   seed context;
2. call `solve_montecarlo(input, field.cells, budget)` — **`field.cells`** is
   the candidate set (the field's cells are already the **occlusion-filtered**
   fragments, so the aim is always in a visible region);
3. set `result.aim` from the choice;
4. set **`result.hitchance = chance.probability`** — the MC's probability, not
   the field's Gaussian (`solver.cpp:147` currently sets the Gaussian one — the
   MC objective must **not** overwrite it with the Gaussian's);
5. set **`result.value = chance.expected_damage`**;
6. keep **`result.penetrated`** computed from the field exactly as today
   (`solver.cpp:150`) — the MC has no penetration model, and the
   `only_visible` gate still needs this flag.

**The targets' health must be the LIVE enemies' health.** The MC's
lexicographic rule's first key is "can this candidate kill the target", and
the review of T34 found the key was computed from an aggregate and attributed
to one target — a candidate could claim a kill that cannot happen. The fix
makes the decision **per target**, from that target's own damage, which means
the live path must supply the **real** health of each enemy (the snapshot
carries it) and must supply it for **every** target the solve considered —
not just the one that was finally chosen.

**If `solve_montecarlo` fails** (an error from `MonteCarloError` — e.g. the
weapon's pattern is not modelled), the objective must **fall back
predictably** and **say so**: the choice is invalid, and the solver must fall
back to the `damage` objective (the cheapest, always-available one) **and
report the refusal**. A silent fallback to a *different* objective is exactly
what the spec forbids — the operator must be able to see that the MC solver
refused and why. `MonteCarloError::to_string` exists for the message.

### 2.2 The objective reaches the service

**`payload/control/src/service.cpp`**:

* the rage path's objective switch (`service.cpp:2143-2153`) gains the new
  value;
* the `aim` control command's objective names (`service.cpp:2675-2703`) gain
  `"monte_carlo"`;
* the `rage` control command's objective name → value mapping
  (`service.cpp:2828-2830`) gains the new value, so the operator can select
  it live.

**`payload/control/include/cs2/control/rage_config.hpp`** — the per-weapon
profile's `objective` byte must be able to carry the new value, and it must
**round-trip** through the profile's save/load
(`payload/control/src/rage_config.cpp`). An **unknown** objective value read
from a config file must fall back to a **documented, safe** default — never to
an arbitrary objective.

### 2.3 The seed context must come from the live game

The MC solver needs a **sequence** number and a **tick**.

* The **tick** is the prediction tick the service already works with.
* The **sequence** is the command's sequence number — the thing
  `MD5_PseudoRandom(sequence)` is derived from (`docs/re/aim.md:42-47`,
  `docs/re/subtick.md:96`).

Find where the service can get them. **If the sequence number is not reachable
from where the service sits, the spec's requirement is explicit: use a
documented fallback, and the status must say the seed is a fallback.** Do not
invent a sequence number and present it as the game's.

### 2.4 The status must say what it is doing

This is the part the operator reads. The status must report:

* the **objective by name** (not a number — today it is a bare number at
  `service.cpp:2932`);
* the **sample count** — and it must be **64** by default (§2.1a);
* the **seed policy** in force (`sequence` or `angle_grid`), and — when it is
  `angle_grid` — that the number is **indicative, not exact**;
* the **hit chance** and the **expected damage** the MC produced;
* when the MC **refused** (the unmodelled patterns) or fell back, the reason —
  a fallback to a different objective must never be silent.

**The seed policy must be selectable.** The two models answer the ledger's
question differently (spec §1.4 of T34): under `sequence` the number is the
*expectation over the next N shots*; under `angle_grid` it is the *probability
over the shot that would be taken*. The operator is the one who can judge
which matches the game they are in, so the policy must be reachable the same
way the objective is — over the control channel — and the status must say
which is in force.

### 2.5 The operator must be told that the numbers changed meaning

**This is the most important sentence in this spec.**

The existing `hitchance` objective reports a **Gaussian mass** whose σ is
**√6 too wide** (`docs/research/aim-objective-math.md:184-192`, `242-245`): for
a head-sized target the truth is **0.777** and the current model reports
**0.221**. So:

* `min_hitchance` under the old objective is **far stricter** than it looks —
> the gate is *"the chance my (too-wide) cone covers the head"*, not *"the
  chance I hit"*. Under the MC the same number is the **real** probability.
* **`min_hitchance` will behave very differently** between the two objectives.
  The same operator value that blocks every shot under the old objective will
  pass under the MC.
* `min_damage` also changes meaning: the MC's `result.value` is the
  **expected** damage (misses contribute 0), so a 50 % hit chance on 100
  damage reports 50, not 100.

The status must therefore report **enough for the operator to tell the two
apart** (§2.4). The docs must say the same thing — see §2.6.

### 2.6 The documentation

* `docs/ragebot.md` — **append only**: a section on the live objective, how
  to select it, what the numbers mean, and the two limitations (hitbox
  geometry only; the stand-in hash).
* `docs/operator-live-check.md` — **append only**: the operator's steps to
  switch the objective and what to look for.
* `docs/known-issues.md` — **append only**, in the ledger's own style: the
  new objective, its limits, and the `min_hitchance` semantics change. The
  ledger's rule is that a finding is removed only when it is **merged and
  re-reviewed**; a new entry is added when it is **reported**.

### 2.7 `wire::Settings` is **not** touched

The objective is **not** in the wire block. It lives in the control channel and
in the per-weapon profile (a POD config file), and the menu and the overlay do
not show it at all — a grep for `objective` in `payload/features/src/menu.cpp`,
`overlay/src/*.cpp` and `payload/features/src/overlay.cpp` finds **nothing**.

So this task needs **no** `wire::k_version` bump, and must not touch
`overlay/include/cs2/overlay/wire.hpp`,
`payload/features/include/cs2/features/esp.hpp`,
`payload/features/src/settings.cpp` or `overlay/src/renderer.cpp`.

**Do not add the objective to the menu or the overlay.** The operator selects
it over the control channel. Adding a menu entry would drag the wire block in
and blow the scope up — the spec's whole point is that it does not have to.

### 2.8 The core's two remaining gaps — close them here

T34's re-review left **one minor and one latent minor** in the core, and said of
the second: *"Fix before the live wiring."* **This task is the live wiring**, so
they are in scope here.

**G1 — a NaN damage multiplier is not validated.** `hitbox_valid` checks the
**geometry** (finite components, a positive radius, positive half-extents) but
not the hitbox's `multiplier`. A hitbox with `multiplier = NaN` therefore slips
past the "the solver refuses a malformed hitbox" guarantee. Measured: one
capsule with `multiplier = NaN` → `expected_damage = NaN` → in the selection
rule **every comparison against NaN is false**, so the search keeps the *first*
candidate regardless of quality and returns a NaN verdict **with no error**.
The fix is one line (`std::isfinite(hitbox.multiplier)`) — add it to
`hitbox_valid`, and add the test.

**G2 — the per-target attribution is per *sample*, not per *pellet*.** A
sample's whole damage (the sum over the pellets that hit) is added to the slot
of `sample->target`. If two pellets hit two different targets, the second's
damage is attributed to the first — **the M2 bug one level down**. It is latent
today (the multi-pellet weapons are exactly the ones `pattern_supported`
refuses), but **the live path may clear the weapon type**, which would make it
reachable. Either attribute per pellet, or **refuse** the case explicitly and
say so.

**And the pattern argument.** T34's fix round **removed** `spread_kernel`'s
pattern **default**, because `WeaponProfile` carries only the weapon *type* and
a default silently dropped the revolver's `1 − r²`. So the live path must now
supply the pattern **explicitly** — and it must get it right, or the R8's
revolver pattern is lost. The weapon's **designer name** is available to the
service (`local.weapon` is the designer name, e.g. `"weapon_glock"`). Say how
you decide, and what happens for a weapon you cannot classify.

### 2.9 The core's public API changed — read it, do not assume

T34's fix round changed three public signatures in
`payload/aim/include/cs2/aim/montecarlo.hpp`:

* `evaluate_sample` now returns `std::expected<Sample, MonteCarloError>` (it
  had no error channel, so it gained one);
* `draw_pellet_offsets` now returns `std::expected<int, MonteCarloError>` and
  **refuses** a buffer that is too small (`buffer_too_small`);
* `spread_kernel`'s pattern argument is now **required** — see §2.8.

Read the header before you write the dispatch.

---

## 3. The tests

### 3.1 The aim module

`payload/aim/tests/test_aim_solver.cpp` (the `aim-solver` target) gains a
case for the new objective:

* the `monte_carlo` objective returns a **valid** result on a simple field;
* `result.hitchance` is the MC's **probability** — and it is **not** the
  field's Gaussian (construct a case where the two differ and assert the MC's
  value is the one reported);
* `result.value` is the **expected damage**;
* a `SolveInput` that does **not** set the MC's parameters behaves **exactly**
  as before — the other objectives are unchanged (a regression test);
* the **fallback**: a weapon whose pattern is not modelled (the shotgun) makes
  the MC refuse, and the result falls back **predictably** — and the refusal is
  **observable**, not silent.

### 3.1a The core's two gaps (§2.8)

In `payload/aim/tests/test_aim_montecarlo.cpp`:

* **G1** — a hitbox with a **NaN multiplier** is rejected; the evaluation
  refuses the scene rather than returning a NaN verdict.
* **G2** — the per-pellet attribution: either two pellets hitting two
  different targets attribute their damage to the right targets, or the case is
  refused. Say which, and prove it.

### 3.2 The objective's names and values — a pure, testable mapping

`scripts/scenario/tests/test_control.py` is a **protocol-level** test: it drives a
`FakePipe` and checks the request envelope and the response matching. **It
never runs `service.cpp`**, and there is no host harness that does. So the
"the `rage` command is accepted and the status reports the objective by name"
requirement **cannot** be tested at that level, and a test written there would
be testing the fake pipe.

→ **Extract the mapping into pure functions** and test those. The repo already
has the precedent: the chams' target rules live in
`payload/features/include/cs2/features/hook_target.hpp` and are tested by
`payload/features/tests/test_chams_target.cpp`.

The pure surface must cover, at minimum:

* the **objective name → value** mapping (`"damage"` → 0, `"hitchance"` → 1,
  `"expected"` → 2, `"monte_carlo"` → the new value) — the one the `rage`
  command uses;
* the **value → name** mapping the **status** uses — so the operator sees
  `"monte_carlo"` and not `3`;
* the **objective name → `AimObjective`** mapping the `aim` command uses;
* an **unknown name** → the documented safe default. It must **not** select the
  new objective by accident.

Where these functions live is yours (a new `payload/control/include/…`
header, or beside the service), but they must be **pure**, **host-testable**,
and the `service.cpp` call sites must use them rather than re-implementing the
mapping inline.

### 3.3 The profile round-trip

The per-weapon profile's `objective` byte must survive a **save → load** with
the new value, and a config file with a **garbage** objective byte must load
into the documented safe default.

### 3.4 Prove each test can fail

Break the rule on purpose in a scratch copy, watch the test fail, revert, and
report the evidence.

---

## 4. What is out of scope

* **The Monte-Carlo core** (`montecarlo.hpp`, `montecarlo.cpp`) — T34's. If
  you find a defect in it, **report it**; do not fix it here.
* **The `√6` σ correction** — still blocked on the `sqrt(r)` RE question and
  the inaccuracy's units. Do not change σ anywhere.
* **The engine's `get_tick_view_angles` / `weapon_calculate_spread`** — not
  reverse-engineered. Do not call into `client.dll` for the spread; do not
  guess the hash.
* **The engine's physics trace** — the payload does not call it today.
* **The menu and the overlay** — see §2.7.
* **`wire::Settings`** — see §2.7.
* **The other objectives** — `damage`, `hitchance`, `expected` and `pellets`
  must behave **exactly** as they do today.
* **Penetration and visibility modelling in the MC solver** — the MC scores
  hitbox geometry only, by design.

---

## 5. File ownership

You own:

* `payload/aim/include/cs2/aim/solver.hpp` (the enum and `SolveInput`)
* `payload/aim/src/solver.cpp` (the dispatch)
* `payload/aim/tests/test_aim_solver.cpp`
* `payload/aim/include/cs2/aim/montecarlo.hpp` and
  `payload/aim/src/montecarlo.cpp` — **only** for the two gaps in §2.8 (the
  NaN multiplier, the per-pellet attribution) and their tests. **Nothing else
  in the core.** If you find another defect in it, **report it**; do not fix it
  here.
* `payload/aim/tests/test_aim_montecarlo.cpp` — **only** for the §2.8 tests
* `payload/control/src/service.cpp` (the objective switch and the two control
  command mappings)
* `payload/control/include/cs2/control/rage_config.hpp` and
  `payload/control/src/rage_config.cpp` (the profile's objective byte)
* the **new** pure-function header for the objective's names and values, and
  its host test under `payload/control/tests/`
* `payload/control/meson.build` and the root `meson.build` (registering the
  new test)
* `scripts/scenario/tests/**` (if you find a level at which the control
  command's acceptance can be tested — see §3.2)
* `docs/ragebot.md`, `docs/operator-live-check.md`, `docs/known-issues.md` —
  **append only**

You do **not** own `payload/aim/src/montecarlo.cpp`,
`payload/aim/include/cs2/aim/montecarlo.hpp`, `payload/aim/src/field.cpp`,
`payload/aim/src/occlusion.cpp`, `payload/aim/src/spread.cpp`,
`payload/features/**`, `overlay/**`, `wire::Settings`, or the dumper. **If you
need a file you do not own, stop and report it instead of editing it.**

---

## 6. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010).
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
* `payload/aim` is **platform-independent**: no `<windows.h>`.
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

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

**You cannot launch CS2 from here** — the agent runs in Windows session 0 with
no GPU. Say plainly what is unverified. The live test is the operator's, and
`docs/operator-live-check.md` is where the steps go.

---

## 8. What the report must state

1. **The dispatch** — how the `monte_carlo` objective is wired, and the proof
   that the other four objectives are **unchanged** (a test, not a claim).
2. **The seed context** — where the **sequence** and the **tick** came from.
   If the sequence number was not reachable, say so plainly and say what the
   fallback is.
3. **The refusal** — what happens when the MC refuses (the unmodelled
   patterns), and the proof that the fallback is **observable** and not
   silent.
4. **The status** — exactly what the operator will see, and the **sample
   count** and the **seed policy** in it.
5. **The semantics change** — how `min_hitchance` and `min_damage` behave
   differently under the new objective, with the numbers.
6. **The tests** — each one, and how it fails when the defect is reintroduced.
7. **What is still open** — the live run, the two limitations (hitbox geometry
   only; the stand-in hash), and anything you could not verify.
8. The file list and the commit hashes.
