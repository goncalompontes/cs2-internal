# T34 — the Monte-Carlo hit-chance solver (the core)

> **The one-line version:** the repo's current hit probability is a **Gaussian**
> over the damage field, and the research says the engine's real spread kernel is
> **not** Gaussian — its support is exactly `|off| ≤ S + I` and its radial law is
> **uniform in radius** (rim-heavy). This task builds the **community's**
> Monte-Carlo hit-chance estimator — sample the engine's own spread kernel over
> seed indices, test each sample against the target's hitboxes with a
> **nearest-hit** ray test — as a **new, host-tested solver alongside** the
> existing one. The existing implementation **stays**, untouched and comparable.

---

## 1. What is wrong today

The finding is **§11.2** of [`docs/known-issues.md`](../../known-issues.md) — the
operator's own list. Read it, plus `docs/research/aim-objective-math.md` (the
research) and `docs/re/aim.md` (the assembly-verified kernel). **This spec is
the contract; the ledger is the history.**

### 1.1 The kernel is not Gaussian, and the σ is wrong by √6

`docs/re/aim.md:18-38` — the engine's own sampler, `FX_FireBullets` →
`FUN_180d23460` (RVA `0xd23460`), **assembly-verified**:

```
RandomSeed(seed);
for each pellet:
  r1 = RandomFloat(0, 1);   a1 = RandomFloat(0, 2*pi);   // spread
  r2 = RandomFloat(0, 1);   a2 = RandomFloat(0, 2*pi);   // inaccuracy
  // weapon patterns: revolver r->1-r^2; negev r->1-r^(2^k); shotgun FUN_1807cd1b0
  off_x = r1*cos(a1) + r2*cos(a2);
  off_y = r1*sin(a1) + r2*sin(a2);
```

The offset is applied in the **`(right, up)` tangent frame** of the aim
direction.

Two facts the Gaussian gets wrong (`docs/research/aim-objective-math.md:173-192`):

* **Support:** `|off| ≤ S + I` **exactly**. A Gaussian assigns ~2.5e-3 of its
  mass beyond that radius where the truth assigns **exactly zero**.
* **Radial law:** a term `R·u` with `R ~ U[0,c]` has density `f(r) = 1/(2πcr)` on
  `r < c` — **uniform in radius**, i.e. **rim-heavy**, not area-uniform.

And the σ is wrong by a factor: the per-axis variance is `(S² + I²)/6`, so the
variance-matched σ is `sqrt(S²+I²)/√6` — the code's σ is **√6 too wide**
(`aim-objective-math.md:184-192`, `242-245`). Measured: for a head-sized target
the **true** hit probability is **0.777** where the current model reports
**0.221** — the `min_hitchance` gate is rejecting shots that hit ~78 % of the
time.

> **The `√6` fix is NOT this task.** It is blocked on an unresolved RE question
> (the `sqrt(r)` transform, `aim-objective-math.md:194-201`) and on the
> inaccuracy's units (`docs/ragebot-review.md:104-118`). A solver that uses the
> **true kernel** does not need the σ correction at all — that is the strongest
> argument for this approach, and it is why this task does **not** touch σ.

### 1.2 The cells are not disjoint, so the current probability double-counts

`payload/aim/src/field.cpp:542-544` says:

> *"The cells are disjoint by construction, so the probability the shot lands in
> the target is the sum of each cell's Gaussian mass (exact, no sampling)."*

**That is false.** `docs/next-session-ragebot-research.md:124-128`:
*"Overlapping hitbox polygons are not disjoint in `DamageField`; hit
probability/expected value can double-count and the pointwise max does not model
nearest-hit target order."* `docs/research/aim-solver-design-review.md:81`
agrees.

**Nearest-hit semantics fixes this.** A Monte-Carlo sample asks *"where does
**this one** bullet land?"* — the nearest hit wins. There is nothing to
double-count. **This is a correctness advantage of the new solver and the spec
claims it**, with the caveat that the two solvers are then **not comparable
cell-for-cell** (a claim the report must state).

### 1.3 `P(hit)` is not the pellet sum

`docs/research/aim-objective-math.md:580-586`: for a multi-pellet weapon,
`P(hit)` is the mass of the **union** of the shifted copies, not the sum. A
per-pellet Monte-Carlo must therefore score *"did **any** pellet hit?"* — a
sample's hit is the union over that sample's pellets.

### 1.4 The seed — the open question, and this task's answer

`docs/known-issues.md:259-262` names it: *"**The first decision for the next
session: the seed.** Is the new solver's hit chance the probability over the
shot that would be taken (the seed the engine would use for that pose) or the
expectation over seeds? The community does the first, and the research says
that is what works in practice."*

The repo holds **two conflicting models**:

| model | source | claim |
|---|---|---|
| **sequence** | `docs/re/aim.md:42-47` — the repo's own RE | `CBaseUserCmdPB::random_seed` (`+0x6c`) = `MD5_PseudoRandom(sequence)`; the seed is a pure function of the **command number** |
| **angle** | `docs/research/aim-objective-math.md:203-210` — the community / leak | the client's spread entry point is `get_tick_view_angles(angles, tick)`; the seed is a function of the **view angles and the tick**. *"the live behaviour (and every CS2 'no-spread' writeup) says the seed depends on the angles. This is the single most important thing to pin down, because it decides whether the objective is an integral at all."* |

**They cannot both be true, and the repo cannot settle it today**: the engine's
`get_tick_view_angles` is **not reverse-engineered**, and the RVAs in
`docs/re/aim.md:68-69` *"resolve to unrelated functions"* on the live build
`659940c1`.

**The answer this task implements, and it is a structured one:**

> **The seed is a parameter of the solver, not a baked assumption.** The solver
> takes a **seed source** — a policy object that maps a candidate aim and a
> sample index to a seed. Two policies are implemented and both are selectable:
>
> * **`sequence`** — `md5_pseudo_random(sequence + i)` for `i` in `[0, N)`. This
>   is the repo's own RE model (`docs/re/aim.md:42-47`), and it is **exactly the
>   community's own sampling**: velocity probes `seed+1 … seed+N`
>   (`aim-objective-math.md:128`). **This is the default.**
> * **`angle_grid`** — for each of `N` probe angles within the candidate's
>   neighbourhood, the seed is `md5_pseudo_random(hash(probe_yaw, probe_pitch,
>   tick))`, and that seed's **exact** offset is used. This models the
>   community's *angle-dependent* seed (velocity's `find_spread_correction`,
>   `aim-objective-math.md:145-149`).
>
> **The `angle_grid` hash is a STAND-IN.** The engine's `get_tick_view_angles`
> is not reverse-engineered, so `angle_grid`'s hash is *not* the engine's
> function. The code must say so, in the header and in a comment, and the
> report must state that the numbers under `angle_grid` are **indicative, not
> exact**. Do not claim otherwise, and do not "fix" it by guessing the engine's
> hash.

**Why this is the right shape:** the ledger's question — *"is it the probability
over the shot that would be taken, or the expectation over seeds?"* — has a
different answer under each model. Under **`sequence`** the honest answer is
*"the expectation over the next N shots"* (the seed advances with the command
number, so `seed+i` is a *different shot*). Under **`angle_grid`** it is
*"the probability over the shot that would be taken"* (the seed is a function of
the aim, so within a cell there are many seeds and the lottery is the
*choice*). **The solver must report which model produced a number**, and the
report must state the answer under each.

### 1.5 The pieces the repo already has

| piece | where | usable? |
|---|---|---|
| the engine's RNG + MD5 seed | `payload/aim/src/spread.cpp:82-167` (`UniformRandomStream`, `md5_pseudo_random`, `pellet_offsets`) | **yes** — but note the **draw order is load-bearing**: `r1, a1, r2, a2` per pellet, sequential across pellets (`spread.cpp:155-165`) |
| the hitbox geometry | `payload/aim/include/cs2/aim/hitbox.hpp` — `Hitbox { Obb box; Capsule capsule; bool is_capsule; float multiplier; int group; int target; }` | **yes** |
| the analytic ray/solid spans | `payload/aim/src/occlusion.cpp` — `ray_sphere_span` (L46), `ray_capsule_span` (L112), `ray_cylinder_span` (L65) | **yes** — these are for *occluders*, but the arithmetic is the same and is directly reusable |
| the weapon model | `payload/aim/include/cs2/aim/field.hpp:19-40` — `WeaponProfile` (`spread`, `inaccuracy`, `inaccuracy_*`) | **yes** |
| the chart | `payload/aim/include/cs2/aim/geometry.hpp` — `Basis`, `Vec2/Vec3`, `yaw_scale(pitch)` | **yes** |
| **a ray-vs-capsule test for hitboxes** | — | **NO — it does not exist.** The capsule is only ever *projected* into the chart (`project_capsule`, `geometry.cpp:428-457`), never intersected with a ray. **This is the main new primitive.** |

### 1.6 The cost

`docs/known-issues.md:256-257`: *"the current cone objective is **20–35 ms for
717 cells**, so a sample-based objective has to be counted carefully."*
`docs/research/aim-solver-design-review.md:197-199`: *"The existing 717-cell
saved-set measurement reports **34 ms** for `argmax_hit_probability`; that
already exceeds a **15.6 ms tick** before a certified global search."*

**The budget is hard: one 64 Hz tick is 15.6 ms.** The service's own guard is
`k_backtrack_budget_us = 12000` (`payload/control/src/service.cpp:2206`).

### 1.7 The benchmark is not in the repository

The published numbers come from an **ad-hoc, out-of-tree** harness in
`%TEMP%\opencode\bench\bench_live.cpp` — no Meson target, not versioned, not
reproducible by anyone else. The saved cell set **is** committed:
`artifacts/live-heatmap.json` (717 cells).

---

## 2. What to build

### 2.1 A new module: `payload/aim` — the Monte-Carlo estimator

New files:

* `payload/aim/include/cs2/aim/montecarlo.hpp`
* `payload/aim/src/montecarlo.cpp`

**Platform-independent**: no `<windows.h>`, no engine access, no allocation on
a hot path (the module is pure geometry + arithmetic). It must build into both
the Windows payload and the host tests.

The public surface, in the repo's style (`snake_case`, `PascalCase` types,
trailing return types, `[[nodiscard]]`, `noexcept` where it cannot fail):

```cpp
namespace cs2::aim {

// --- The seed policy ---------------------------------------------------------

/// How a candidate aim and a sample index become a seed.
enum class SeedSource : std::uint8_t {
  sequence,    ///< md5_pseudo_random(sequence + i) - the repo's RE model.
  angle_grid,  ///< md5_pseudo_random(hash(probe angle, tick)) - the community's
               ///< model, with a STAND-IN hash (see the header's warning).
};

/// The per-sample context the seed policies need.
struct SeedContext {
  std::uint32_t sequence{0};   ///< the current command's sequence number.
  std::uint32_t tick{0};       ///< the prediction tick.
  SeedSource source{SeedSource::sequence};
  float probe_step{0.0087F};   ///< the angle_grid probe step, radians (~0.5 deg).
};

// --- The sample --------------------------------------------------------------

/// One drawn pellet set and the outcome of the test.
struct Sample {
  Vec2 offset{};        ///< the chart-space offset (right, up).
  bool hit{false};       ///< did any pellet of this sample hit?
  float damage{0.0F};   ///< the damage of the nearest hit, 0 when it missed.
  int target{-1};        ///< the target index of the nearest hit, -1 when it missed.
  int hitgroup{0};      ///< the hitgroup of the nearest hit.
};

/// The estimator's verdict for one candidate aim.
struct HitChance {
  float probability{0.0F};  ///< the fraction of samples that hit.
  float expected_damage{0.0F};  ///< the mean damage over ALL samples (misses are 0).
  int samples{0};          ///< how many samples were drawn.
  int hits{0};             ///< how many of them hit.
};

}  // namespace cs2::aim
```

**You own the exact shape** — this is the *shape*, not the contract. What is
**contractual** is the behaviour in §2.2–§2.5 and the tests in §2.6.

### 2.2 The kernel must be the engine's, not a Gaussian

The sampler must reproduce `FX_FireBullets`'s draw **exactly**:

* the draw order is `r1, a1, r2, a2` **per pellet**, sequential across pellets
  (`payload/aim/src/spread.cpp:155-165`);
* the offset is `spread·r1·(cos a1, sin a1) + inaccuracy·r2·(cos a2, sin a2)`;
* the revolver pattern is `r1 → 1 − r1²` (`SpreadPattern::revolver`);
* the RNG is `CUniformRandomStream` seeded with the **negated-seed** quirk
  (`spread.cpp:82-88`).

**Reuse `payload/aim/src/spread.cpp`'s `UniformRandomStream` and
`md5_pseudo_random`** — do not write a second RNG. `pellet_offsets` returns a
`std::vector<Vec2>`, which allocates; for the hot path you need a **non-allocating**
form. Either add a span-based overload to `spread.hpp` (you own it — see §4) or
draw the samples directly with the stream.

**The known gaps in the kernel model**, which you must state and not paper over:

* the **`sqrt(r)` question** — `docs/research/aim-objective-math.md:194-201`:
  the RE listing has no `sqrt` between `RandomFloat` and the `cos/sin`, but
  *"the missing `sqrtss` … is exactly the kind of instruction a hand-written
  listing drops"*, and *"the difference is large"*. The repo's current model has
  **no** `sqrt`. **Do not add one** — the repo's RE is the authority and it says
  none. **State it in the report** as a known open item.
* the **shotgun / negev pattern table** `FUN_1807cd1b0` is **not modelled**
  (`spread.hpp:47-49`), and the negev's `r → 1 − r^{2^k}` is not either. The
  solver must **refuse** (or clearly report) for the weapons whose pattern it
  does not model — it must **not silently use the standard pattern** for a
  shotgun.

### 2.3 The hit test: a ray against the target's hitboxes, nearest-hit wins

For each sample:

1. Build the shot's **direction** from the eye: the aim direction, rotated by
   the sample's offset in the **`(right, up)` tangent frame**. The tangent
   frame's construction is the repo's `Basis` (`geometry.hpp:39`) — use it, do
   not re-derive the convention.
2. Cast the ray against **every** hitbox of **every** candidate target.
3. Take the **nearest** hit — **not** the sum, **not** the best-value hit. This
   is the nearest-hit semantics that fixes the double-count of §1.2.
4. The hit's `multiplier` and the weapon's damage model give the sample's
   damage; the hitbox's `group` is the hitgroup.

**The new primitive.** The repo has the analytic span arithmetic for occluders
(`ray_sphere_span`, `ray_capsule_span`, `ray_cylinder_span` in
`payload/aim/src/occlusion.cpp:46-195`) but **nothing that intersects a ray
with a *hitbox***. Write it.

* Reuse the existing span arithmetic rather than duplicating it — **but do not
  change the occluders' behaviour**: if you extract a helper, the occluder
  tests (`aim-occlusion`) must stay green and unchanged.
* The hitbox is either a **capsule** (`Hitbox::is_capsule`) or an **OBB**
  (`Hitbox::box`). Both must work. An OBB ray test is a slab test in the box's
  local frame — the repo has `Obb` and the quaternion `rotate` in
  `hitbox.hpp`/`hitbox.cpp`.
* The test must be **exact** for the shapes it claims, not an approximation.

**Penetration and occlusion:** the repo's design is Tier-2 — it does **not**
trace rays through the world; it projects the world solids into the chart
(`docs/ragebot.md:372-384`). **This task does not change that.** The Monte-
Carlo solver scores the **hitbox geometry only**. Whether the shot is
*visible* is the existing solver's job (the `only_visible` / `penetrated`
machinery). Say this in the header and the report — it is a real limitation and
the report must state it.

### 2.4 The estimator

For a candidate aim:

* draw `N` samples (`N` = 64 / 128 / 256, selectable — the community's counts);
* each sample's seed comes from the **seed policy** (§1.4);
* each sample's pellets come from the **engine's kernel** (§2.2);
* each sample is tested against the hitboxes (§2.3);
* `probability = hits / N`;
* `expected_damage = mean(damage over all N samples)` — **misses contribute 0**;
* for a multi-pellet weapon, a sample **hits** if **any** of its pellets hits
  (§1.3) — the union, not the sum.

### 2.5 The search: bounded, and the rule stated

The objective is **not** differentiable — no gradient ascent. The solver's
search is therefore a **bounded evaluation over a candidate set**.

* The candidate set comes from the existing machinery: the best cells'
  **Chebyshev centres** and **centroids** — `field_candidates`
  (`payload/aim/src/field.cpp:167-193`) and `chebyshev_center`
  (`geometry.hpp`). **Reuse them**; do not write a second candidate generator.
* The candidate count must be a **documented, bounded budget**, not a
  function of the field's size. Say what it is and why.
* **The selection rule must be stated in the code and in the report.** The
  community's rule is lexicographic (`docs/research/aim-objective-math.md:134-138`):
  *"can-kill first, then the highest probability of any hit, then a hitgroup
  preference."* The repo's own recommendation is different
  (`aim-objective-math.md:353-355`): `argmax E[damage]` subject to
  `P(hit) ≥ H_min` and `P(kill) ≥ K_min`.

  **This task implements the community's lexicographic rule**, because it is
  the reference the operator asked for, and it is the one the live test can
  compare against:

  1. prefer a candidate that **can kill** (its expected damage reaches the
     target's remaining health),
  2. then the **highest probability of any hit**,
  3. then the **hitgroup preference** (head before body).

  The **hitgroup preference order** is the repo's `Hitbox::group`; use the
  repo's existing preference if one exists (`test_aim_solver.cpp` has a
  "head-beats-body" test — find it and reuse its order), otherwise state the
  order you used.

### 2.6 The tests — and they must be able to fail

A new host test `payload/aim/tests/test_aim_montecarlo.cpp`, registered as
`test('aim-montecarlo', …)` in the root `meson.build` (the aim tests are
registered at `meson.build:88-220`; follow their pattern, including
`include_directories : [aim_inc, game_inc]`).

Use the aim tests' harness convention: an `expect(bool, const char*)` +
`failures` counter (the repo sets `b_ndebug=false` project-wide — *"a test that
only bites in the debug build is not a test"*, `meson.build:14`).

The tests must cover, at least:

1. **The kernel matches the engine's draw order.** A fixed seed produces the
   **exact** offsets `pellet_offsets` produces (`payload/aim/src/spread.cpp`).
   **This is the regression test for "the sampler is the engine's".**
2. **The support is exact.** Every sampled offset satisfies `|off| ≤ S + I`.
   This is the fact a Gaussian gets wrong — **pin it**.
3. **The radial law is rim-heavy, not area-uniform.** A statistical test over
   many samples: the **mean** radius of the samples is `≈ c/2` (uniform in
   radius), **not** `≈ 2c/3` (area-uniform). Use a tolerance you state.
4. **The hit test is exact.** A ray through the **centre** of a capsule hits;
   a ray that misses by more than the radius does not; a ray that hits an OBB
   at a known distance reports that distance; a ray behind the eye does not
   hit.
5. **Nearest-hit wins.** With two overlapping targets, the sample's damage and
   `target` come from the **nearest** hit, not the highest-multiplier one.
   **This is the test that pins §1.2's fix.**
6. **The union over pellets.** With two pellets where only one hits, the
   sample counts as **one hit**, and `probability` is the fraction of samples
   with **any** hit — not the sum of the pellets' hits.
7. **The seed policies differ and are deterministic.** The `sequence` policy
   gives the same seeds for the same context; the `angle_grid` policy varies
   with the probe index; **both** are reproducible run to run.
8. **The selection rule.** Given a field with a head and a body, the solver
   picks per the lexicographic rule; and a candidate that can kill wins over
   one with a higher hit probability.

**Prove each one can fail**: break the rule on purpose in a scratch copy,
watch the test fail, revert, and report the evidence. At least the support
test, the nearest-hit test and the union test **must** be shown failing.

### 2.7 A committed benchmark — and the cost must be published

The published numbers come from an **out-of-tree** harness in
`%TEMP%\opencode\bench\`. **Bring it into the repository.**

* New: `payload/aim/bench/bench_montecarlo.cpp` — a **Meson executable
  target** (not a `test()`; a benchmark must not slow the suite), e.g.
  `aim_bench`, registered in the root `meson.build` next to the test targets.
* It replays the **committed** saved cell set: `artifacts/live-heatmap.json`
  (717 cells). The path is **relative to the repository root** — the harness
  runs from the repo root (that is what the current one does).
* It prints **p50 / p95 / p99** (and the min/max) of the solver's cost, for
  the **sample counts 64 / 128 / 256**, over the saved set, and the cost of a
  single sample evaluation.
* **Also compare** against the existing objective's cost on the same set, so
  the two are comparable: the current cone objective is **20–35 ms for 717
  cells** (`docs/known-issues.md:256`) and `argmax_hit_probability` is
  **34 ms** at σ=0.002.
* The numbers go in the report **and** in `docs/ragebot.md` (appended).

**The budget is a hard constraint, and this is where the task can fail:** a
64 Hz tick is **15.6 ms**, and the service's own guard is
`k_backtrack_budget_us = 12000` (12 ms). If the Monte-Carlo solver cannot fit
inside the budget on the saved set, **say so plainly and report the numbers** —
do not quietly shrink the candidate set until it fits, and do not report a
number from a run that was not on the saved set.

### 2.8 The documentation

* `docs/ragebot.md` — **append only**: a section describing the new solver,
  the kernel it uses, the two seed policies (and the stand-in warning), the
  selection rule, and the measured cost.
* `docs/research/aim-objective-math.md` — **do not rewrite**. If your work
  resolves or sharpens a claim in it, **append** a dated note.

---

## 3. What is out of scope

* **The existing solver must not change.** `payload/aim/src/field.cpp`,
  `solver.cpp`, `damage.cpp`, `occlusion.cpp`, `penetration.cpp` and the
  `DamageField` API stay as they are. The new solver goes **alongside**
  (`known-issues.md:225-227`: *"The existing implementation stays — the new
  solver goes alongside it on a new branch (the operator's instruction), so the
  current damage-field solver remains available and comparable"*).
* **Do not wire it into the live path.** No `AimObjective` enumerator, no
  `service.cpp` dispatch, no `wire::Settings`, no menu, no per-weapon profile
  value, no status line. **That is the next task.** This task delivers the
  **core**, host-tested and benchmarked.
* **The `√6` σ correction** — blocked on the `sqrt(r)` RE question and the
  inaccuracy's units. Do not apply it. Do not change σ anywhere.
* **The engine's `get_tick_view_angles` / `weapon_calculate_spread`** — not
  reverse-engineered, and the RVAs in `docs/re/aim.md:68-69` are stale for the
  live build. Do not call into `client.dll` for the spread; do not guess the
  hash.
* **The engine's physics trace** (`CGamePhysicsQueryInterface`) — the payload
  does not call it today, and `docs/re/aim.md:154-159` says the ABI is not
  pinned. Do not start.
* **The shot's seed model** — see §1.4. You implement **both** policies; you do
  not decide which is the engine's truth. That needs RE.

---

## 4. File ownership

You own:

* `payload/aim/include/cs2/aim/montecarlo.hpp` (new)
* `payload/aim/src/montecarlo.cpp` (new)
* `payload/aim/tests/test_aim_montecarlo.cpp` (new)
* `payload/aim/bench/bench_montecarlo.cpp` (new)
* `payload/aim/include/cs2/aim/spread.hpp` and `payload/aim/src/spread.cpp` —
  **only** to add a non-allocating offset draw if you need one. The existing
  `pellet_offsets` behaviour must not change (`aim-spread` must stay green).
* `payload/aim/meson.build` (the new source) and the root `meson.build` (the
  new test and benchmark targets)
* `payload/aim/include/cs2/aim/geometry.hpp` / `src/geometry.cpp` — **only**
  if you need to expose an existing helper (e.g. `chebyshev_center`, `Basis`'s
  tangent frame). Existing behaviour must not change.
* `payload/aim/include/cs2/aim/hitbox.hpp` / `src/hitbox.cpp` — **only** to
  expose what the hit test needs. Existing behaviour must not change.
* `payload/aim/src/occlusion.cpp` — **only** to extract a shared span helper;
  the occluders' behaviour must not change.
* `docs/ragebot.md` — **append only**

You do **not** own `payload/aim/src/field.cpp`, `solver.cpp`, `damage.cpp`,
`penetration.cpp`, `records.cpp`, `heatmap.cpp`, `reader.cpp`, `bvh.cpp`,
`payload/control/**`, `payload/features/**`, `overlay/**`, `wire::Settings`, the
dumper, or anything else. **If you need a file you do not own, stop and report
it instead of editing it.**

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010).
* **New constants are `SCREAMING_SNAKE_CASE`** (the legacy `k_` spelling is not
  to be copied). `enum class` values are `snake_case`.
* `payload/aim` is **platform-independent**: no `<windows.h>`. It builds into
  the Windows payload **and** the host tests.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local** (`docs/next-session-ragebot-research.md:271-272`). No
  `thread_local` anywhere.
* **Purity** (`cpp-coding-standards`): the sampler must take its RNG and its
  inputs as **parameters** — no hidden global state, no global RNG.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 6. The verify loop

```powershell
# the project's environment FIRST - it keeps the cargo target directory and
# the tool caches out of the worktree
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

**The benchmark** runs from the **repository root** (its data path is
relative):

```powershell
cd C:\Users\gpmpo\cs2-wt\<task>
meson compile -C C:\Users\gpmpo\cs2-build\<task>\build
& C:\Users\gpmpo\cs2-build\<task>\build\aim_bench.exe
```

---

## 7. What the report must state

1. **The kernel** — the exact draw order you used, and the proof that it
   matches `pellet_offsets` for a fixed seed.
2. **The seed policies** — both, how each maps an aim and a sample index to a
   seed, and the explicit statement that `angle_grid`'s hash is a **stand-in**
   for the engine's unre-engineered `get_tick_view_angles`.
3. **The answer to the ledger's question** — *"is the hit chance the
   probability over the shot that would be taken, or the expectation over
   seeds?"* — **under each policy**, and which one you believe is right and
   why.
4. **The hit test** — how the ray is built, how the capsule and the OBB are
   intersected, and the proof that the **nearest** hit wins.
5. **The selection rule** — the lexicographic rule, and the hitgroup order.
6. **The tests** — each one, and how it fails when the defect is reintroduced.
   At least the support, the nearest-hit and the union tests **must** be shown
   failing.
7. **The cost** — p50/p95/p99 from the **committed benchmark** on the saved
   717-cell set, for 64/128/256 samples, next to the existing objective's cost.
   **If it does not fit the 15.6 ms tick, say so plainly.**
8. **The known open items** — the `sqrt(r)` question, the shotgun/negev pattern
   table, the penetration/visibility limitation, the stand-in hash.
9. The file list and the commit hashes.
