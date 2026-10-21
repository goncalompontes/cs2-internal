# Aim selection under uncertainty — the mathematics of the objective

> Research note, 2026-10-19. Companion to [`docs/ragebot.md`](../ragebot.md) §2/§3/§7,
> [`docs/ragebot-perf.md`](../ragebot-perf.md) §3.2/§3.9,
> [`docs/re/aim.md`](../re/aim.md) §"The spread cone" and
> [`docs/research/autowall-implementations.md`](autowall-implementations.md).
>
> **Scope.** What the objective *should* be, how to compute it fast, and how to maximise
> it — not the geometry/occlusion pipeline (that is the autowall note).
>
> **Method.** Engine claims are from this repo's RE (`docs/re/aim.md`, build 25815307).
> Public-code claims were read in the repository at the linked path, not from a README.
> Numeric claims marked **[measured]** were produced by the scripts in §9 and can be
> reproduced. Claims I could not verify are marked **unverified**.

---

## 0. TL;DR

1. **The integral is right; the uncertainty model is wrong in three ways.** (a) The spread
   is not Gaussian — the engine draws it from a *compactly supported* polar kernel
   (`r = RandomFloat(0,1)`, `θ = RandomFloat(0,2π)`, one for spread and one for
   inaccuracy). (b) The Gaussian's `σ` is set to the cone *scale*
   `sqrt(S² + I²)` (`service.cpp`), but the variance-matched σ of that kernel is
   `sqrt(S² + I²)/√6` — the code's σ is **√6 = 2.449× too wide**, and the resulting
   probability is 1.6–7× too low. (c) Pose uncertainty is a *world-space, per-hitbox*
   blur, collapsed into one isotropic chart σ.
2. **Consequence of (b), measured:** for a head-sized target (angular radius = 0.5 ×
   cone radius) the true hit probability is **0.777**; the current model reports **0.221**
   and the variance-matched Gaussian reports **0.777**. The `min_hitchance` gate is
   therefore rejecting shots that hit ~78 % of the time. (The live run's `hitchance
   0.125` is in the same range — the model is behaving as coded.)
3. **Spread is deterministic given (aim, tick).** In CS2 the seed is derived from the
   view angles and the tick (velocity calls the engine's `get_tick_view_angles(angles,
   tick)` and `weapon_calculate_spread(seed, …)`), so `o_k = o_k(a)`. The "random cone" is
   a *prior over a seed you can actually compute*. The correct treatment is deterministic
   evaluation, plus a **seed lottery** inside the chosen cell (the aim has 0.1–1° of
   freedom → many seeds → pick one whose offset lands centrally). One public cheat
   already brute-forces this.
4. **Numerics: keep Green's theorem, change the integrand.** The divergence form
   `∮ g(r) ((x−a)·n)/r ds` needs no `erfc`, is one Gauss rule per edge (no 1σ
   segmentation), and is *kernel-agnostic*: for the true engine kernel only the 1-D
   function `g` changes (tabulated radial CDF). **[measured]** GL-8: 4e-10 worst-case
   absolute error vs 1e-16 for the current φΦ form at 2–4× the node count.
5. **Do not adopt a bivariate-normal library.** Every candidate (Drezner, Genz, BIVNOR,
   `mvtnorm`, SciPy) computes *rectangle* probabilities; you need *polygon* masses, which
   are cheaper to get directly. If you ever want one, it is `GPL-2` (`mvtnorm`) or
   `BSD-3` (SciPy, QMC, default tolerance 1e-5) — neither is a fit for a `noexcept`
   payload.
6. **Optimisation: the objective is genuinely multimodal.** A single cell's mass is
   log-concave (Prékopa–Leindler ⇒ unimodal), but a *weighted sum* of cells is not.
   **[measured]** 459-cell field: two basins, the ascent from the centre lands 8.9 %
   below the global optimum; better
   local optimisation did not close the gap, a better *seed set* did. Use value-ranked
   multi-start + Newton (exact Hessian, one extra boundary pass), and prune with the
   certified bound `f(a) ≤ v_max · P_hit(a)`.
7. **Multi-pellet:** `E[damage]` is additive, so summing the shifted copies is exact.
   `P(hit)` is **not** additive — shifted copies overlap and the sum double-counts;
   use the union mass.
8. **Keep:** the cell/field representation, the exact per-cell mass, the candidate set,
   the deterministic spread/pellet replication, the 8σ tail cutoff, and the
   occlusion/penetration model. None of them are the problem.

---

## 1. What the field does today

`DamageField` is a set of disjoint convex chart cells, each with a damage value
`v_i = weapon_damage · hitgroup_multiplier · range_modifier^(d/500)`, clipped by the
projected occluders (`payload/aim/src/field.cpp`). The chart is absolute world
yaw/pitch; a shot fired along `a` hits cell `i` iff `a + noise ∈ Ω_i`, so the two
objects of interest are

```
hit probability   P(a) = Σ_i  mass_i(a)
expected damage   E(a) = Σ_i  v_i · mass_i(a)
```

`mass_i` is the bivariate-Normal mass of the cell, `hit_probability_exact` /
`expected_exact`. It is computed by Green's theorem as the boundary integral
`−∮ φ(x)Φ(y) dx` in coordinates scaled by `(σ_u, σ_v)`, with each edge split into
~1σ segments and 8-point Gauss–Legendre per segment (`polygon_normal_mass`, lines
53–113). The gradient is the analytic boundary flux `∮ φφ` from the same nodes
(`mass_gradient`, lines 213–237). Cells whose scaled AABB lies outside ±8σ are dropped,
with a proven `Φ(−8) = 6.2e-16` per-cell bound (lines 121–129). The optimisers are:
`argmax_damage` (max value, then max margin, over a candidate set of Chebyshev centres,
centroids, vertices and the nearest point — `field_candidates`), `argmax_hitchance`
(the same candidate set, max margin among cells with `v ≥ 0.5·v_max`),
`argmax_hit_probability` and `argmax_expected` (steepest ascent with backtracking,
`mass_ascent`, seeded from the four highest-value cells, ≤32 iterations, 8 halvings per
line search, stop at 1e-4 relative improvement), and `argmax_pellets` (sum of the field
at `K` fixed offsets over a candidate set built from each cell's optima shifted by
`−o_k`).

The live ragebot path (`payload/control/src/service.cpp:1567–1573`, `solver.cpp:113–141`)
sets `pose_sigma = rage_sigma` and `spread_sigma = sqrt(inaccuracy² + spread²)`; the
`hitchance` objective maximises `argmax_hit_probability(spread_sigma)`, the `expected`
objective maximises `argmax_expected(pose_sigma)`, and `result.hitchance` is always
`hit_probability(aim, spread_sigma)`. So the code already has the right *shape*: one
kernel for "where will the bullet go" and one for "where will the target be". The
problem is entirely in what those two kernels are and what σ means.

There is also a semantic inconsistency between the design and the code. `docs/ragebot.md`
§2 correctly states that the spread is deterministic and that "hitchance over 256 spread
samples is integrating a delta" — yet `hit_probability` integrates a Gaussian over the
spread as if it were random. Both views can be defended (see §3.2), but they must be
reconciled, because they disagree by 1.6–7× on exactly the number the fire gate uses. Note also that `mass_ascent` maximises `hit_probability(a, sigma)` where `sigma`
is the *spread* σ, whereas `argmax_expected` maximises a *pose*-blurred value: the
reported `result.value` is the unblurred `value_at(aim)`, so the two objectives are not
reporting the same functional.

Measured cost (from `docs/ragebot-perf.md` §3.9): `argmax_damage` 0.15 ms, cone
objective 20–35 ms for 717 cells. The cone cost is an *evaluation count* problem: the
budget is 32 iterations × 8 line-search probes = up to 256 full passes, so at 20–35 ms
a full pass is ≤0.14 ms and the code is using a large fraction of that budget. Any
change that gets the evaluation count to ~5 (Newton + 2–3 line-search probes) is a 5–10×
win before touching the per-cell math.

---

## 2. What real aimbots optimise

| Project | Year / target | What is maximised | "Hitchance" | Damage | Licence |
|---|---|---|---|---|---|
| **Osiris** [`csgo-end-of-life`](https://raw.githubusercontent.com/danielkrupinski/Osiris/csgo-end-of-life/Source/Hacks/Aimbot.cpp) | CS:GO | **Min FOV** over 6 bones, gated by visibility or a simulated-penetration `canScan ≥ minDamage` (killshot ⇒ target HP) | **None.** Gates on `getInaccuracy() ≤ maxAimInaccuracy` / `maxShotInaccuracy` (a magnitude threshold) | `TraceRay` + `HandleBulletPenetration`, 4 walls | **MIT** |
| **Osiris** `master` | CS2 | *No aimbot at all* (visuals/HUD only) | — | — | MIT |
| **UC "1-line hitchance"** ([thread 687656](https://www.unknowncheats.me/forum/counter-strike-2-a/687656-1-line-hitchance-challenge.html)) | CS2, 2025 | aim at the best hitbox | `clamp( atan(r_hitbox / dist) / (atan(spread) + atan(inaccuracy)), 0, 1 )` — **angular-radius ratio** | none | forum post (no grant) |
| **velocity** [`shared.cpp`](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/combat/impl/shared.cpp) `calculate_hitchance` | CS2 leak, 2026 | **Kill first**, then hitchance (5 % tie band), then hitgroup priority (`rage.cpp:1293–1360`) | **Monte-Carlo over 64/128/256 seed indices**, calling the engine's own `weapon_calculate_spread(seed+1, inacc, spread, recoil, …)` and a ray-vs-capsule test | engine `trace_bullet` (real penetration) | **none — all rights reserved** |
| **velocity** `calculate_hitchance_fast` | CS2 | same | closed-form fudge: `1.8·sqrt( (r_a·r_b) / (1 + 8·r_offset²) )`, where `r_a = semi_major/total`, `r_b = semi_minor/total` | — | none |
| **Aimware / Gamesense / Neverlose** | CS:GO/CS2 | UI exposes "hit chance %"; kill-first ordering is community consensus | **unverified** (closed source). Leaked-source-era descriptions and the Aimware Lua API (`rbot.hitscan.accuracy…`) suggest seed/trace sampling, but I could not read the implementation | unverified | closed |

What this table says:

- **Nobody maximises expected utility.** The state of the art is lexicographic:
  can-kill first, then the highest probability of *any* hit, then a hitgroup preference.
  velocity even adds a two-shot kill heuristic `kill_prob = 1 − (1 − hc)²` rather than
  modelling the damage distribution. That is a decision-theoretically crude objective,
  but it is cheap and it matches how players think ("kill shots first").
- **Hitchance is either a size ratio or a seed-sampled trace loop.** The 1-line formula
  is the ratio of the target's angular radius to the cone's angular radius — the
  *uniform-disc* CDF in its linear-radius limit, not a probability under the engine's
  actual kernel. It is popular because it is monotone in the right things and costs a
  handful of flops. It is also conservative: at `R = 0.5·R_cone` it returns 0.50 where the
  true probability is 0.78 (§3.3), so it behaves like a stricter `min_hitchance`.
- **The 256-sample loop is not a Monte-Carlo over the *actual* shot.** It averages over
  hypothetical seeds. velocity's own `find_spread_correction` (720 yaw half-degree
  probes) shows the authors know the seed is a function of the angles: they search for an
  angle whose seed produces a *better* offset. That is the deterministic view winning in
  practice, packaged as a hack.
- **Licensing.** Osiris is MIT. velocity has no `LICENSE` file → all rights reserved:
  read it for ideas, do not copy code. UC snippets are forum posts, not licences.

---

## 3. The probability model

### 3.1 What the engine actually draws

`FX_FireBullets` → `FUN_180d23460` (RVA `0xd23460`), assembly-verified in
[`docs/re/aim.md`](../re/aim.md) §"The spread cone":

```
RandomSeed(seed);
for each pellet:
  r1 = RandomFloat(0, 1);  a1 = RandomFloat(0, 2π);   // spread
  r2 = RandomFloat(0, 1);  a2 = RandomFloat(0, 2π);   // inaccuracy
  // patterns: revolver r→1−r²; negev r→1−r^{2^k}; shotgun table FUN_1807cd1b0
  off = (r1·cos a1 + r2·cos a2,  r1·sin a1 + r2·sin a2)
```

applied in the `(right, up)` tangent frame. Two facts follow directly:

- **Support.** `|off| ≤ S + I` exactly (each term has radius ≤ its magnitude). With
  `S = I`, the support radius is `2S = 3.46σ` for the variance-matched σ. A Gaussian
  assigns 2.5e-3 of its mass beyond that radius where the truth assigns **exactly zero**.
- **Radial law.** A single term `R·u`, `R ~ U[0,c]`, has density
  `f(r) = 1/(2πcr)` on `r < c` — uniform in *radius*, i.e. rim-heavy. The sum of the two
  terms is the 2-fold convolution of two such discs.

The support fact is worth using: with the variance-matched σ the support is `3.46σ`, so
the existing 8σ cutoff remains valid (nothing is wrongly dropped) and could be tightened
to the support for a small extra win.

**Variance.** For one term, `E[x²] = E[r²]·E[cos²] = (c²/3)(1/2) = c²/6`. So the sum has
per-axis variance `(S² + I²)/6` and

```
σ_matched = sqrt(S² + I²) / √6
```

For `S = I` this is `0.289·R_cone` (and the code's `σ = sqrt(S²+I²)` is `0.707·R_cone`,
i.e. `√6` times too wide).

**Open RE question (worth one hour).** A rim-heavy `r ~ U[0,c]` is an odd choice for a
game; an area-uniform disc would be `sqrt(RandomFloat(0,1))`. The doc's pseudo-code has
no `sqrt`, but the missing `sqrtss` between the `RandomFloat` call and the `cos/sin` is
exactly the kind of instruction a hand-written listing drops. **Check the instruction
stream**; the difference is large: with a `sqrt`, a single-term kernel becomes
`F(r) = (r/c)²` (much *more* concentrated than the Gaussian) instead of `F(r) = r/c`
(much *less* concentrated near the centre). Everything below still holds with the
appropriate `f`; only the numbers change.

**The seed is a function of the aim.** `docs/re/aim.md` derives
`random_seed = MD5_PseudoRandom(sequence)` from the command, but the CS2 client's spread
entry point is `get_tick_view_angles(angles, tick)` (velocity's `get_spread_seed`,
[shared.cpp:912](https://github.com/wubly/velocity/blob/main/cs2/velocity-cs2/project/core/features/combat/impl/shared.cpp)),
which is passed the **view angles and the tick**. The two models conflict; the live
behaviour (and every CS2 "no-spread" writeup) says the seed depends on the angles.
**This is the single most important thing to pin down**, because it decides whether the
objective is an integral at all.

### 3.2 Three uncertainties, only some of them real

| Source | Scale | Real? | Right model |
|---|---|---|---|
| **Seed choice** (which offset the RNG gives) | ≤ S+I | **No** if the seed is computable from (angles, tick); a *prior* otherwise | deterministic offsets; if unknown, the exact engine kernel (not a Gaussian) |
| **Pose** (rewind/interp/extrapolation) | world units → angle ∝ 1/depth | **Yes** | per-hitbox, world-space covariance; mixture over lag records |
| **Seed grid** (how many distinct seeds one cell contains) | a cell is ~0.1–1° wide; the seed hash may quantise the angles | Yes | the *budget* of the seed lottery; measure it (**unverified** — see the no-spread thread) |

The current `spread_sigma` conflates (1) and (2) and models both as one isotropic
Gaussian in the chart. The `expected` objective already uses a separate `pose_sigma`, so
the split exists — it is just that the *hit probability* gate uses the spread σ. Note that
(3) is not a nuisance parameter: it is the *budget* of the seed lottery, and it should be
measured (how many distinct `(angles, tick) → seed` values does one cell contain?).

### 3.3 How wrong is the Gaussian? [measured]

Disc target of angular radius `R = f·R_cone` centred on the aim, `S = I = 1` so
`R_cone = 2` (MC, 4M samples; the Gaussian column via the noncentral χ²):

| `f = R/R_cone` | `R` | true kernel | σ = sqrt(S²+I²) (**current**) | σ = sqrt(S²+I²)/√6 (**matched**) |
|---|---|---|---|---|
| 0.10 | 0.20 | 0.069 | 0.010 | 0.058 |
| 0.20 | 0.40 | 0.216 | 0.039 | 0.213 |
| 0.35 | 0.70 | 0.502 | 0.115 | 0.521 |
| 0.50 | 1.00 | **0.777** | **0.221** | **0.777** |
| 0.70 | 1.40 | 0.947 | 0.387 | 0.947 |
| 1.00 | 2.00 | 1.000 | 0.632 | 0.998 |

Two separate conclusions:

1. **The σ is the bug, and it is a factor of √6.** The current column is the matched
   column evaluated at 2.449× the σ; the reported probability is 1.6–7× too low (3.5× at
   the head-sized case). The live `hitchance 0.125` (review §2.4) is in exactly this
   range, which is what the model predicts — the code is behaving as written.
2. **The variance-matched isotropic Gaussian is *excellent* when both terms are present.**
   Against the true kernel the error is ≤ 2 percentage points over the whole range
   (0.777 vs 0.777; 0.947 vs 0.947; 0.213 vs 0.216). So for `S ≈ I` you do **not** need
   the exact kernel — the matched Gaussian *is* the kernel to that accuracy. The
   approximation degrades only when one term dominates:

| single term (`I = 0`), `f = R/c` | 0.25 | 0.50 | 0.75 | 0.90 | 1.00 |
|---|---|---|---|---|---|
| true (linear radial law) | .250 | .500 | .750 | .900 | 1.000 |
| matched Gaussian | .171 | .528 | .815 | .912 | .950 |

So: up to ~8 points error, a support cut at `c` vs a 5 % Gaussian tail, and the wrong
shape near the centre. Worth fixing only if the "spread-only" regime matters (a standing
rifle has `I ≪ S`? — see §3.1 and the calibration in §8.2).

Offset targets (the realistic case — the aim is not at the hitbox centre), `R = 0.5·R_cone`:

| centre offset `d` (same units as `R`) | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 | 1.25 | 1.50 |
|---|---|---|---|---|---|---|---|
| true | .777 | .739 | .651 | .528 | .379 | .239 | .136 |
| σ = sqrt(S²+I²)/√6 (**matched**) | .777 | .746 | .658 | .527 | .378 | .240 | .133 |

The matched Gaussian tracks the truth to ≤ 1.5 points even two cone radii off-centre.
Good enough for a gate, and good enough for the objective.

### 3.4 The exact kernel integral is the same algorithm

For **any** isotropic radial density `f`, the mass over a polygon is still a boundary
integral:

```
mass(a) = ∮ g(r) · ((x − a)·n) / r  ds ,      r g(r) = (1/2π) ∫₀^r s f(s) ds
```

(divergence theorem with `G = g(r)(x−a)/r`, `div G = f`). Only the 1-D profile `g`
changes:

| kernel | `g(r)` |
|---|---|
| Gaussian, σ | `(1 − e^{−r²/2σ²}) / (2π r)` — **no `erfc`** |
| one linear-radial disc, radius `c` | `1/(2πc)` for `r < c`, `1/(2πr)` for `r ≥ c` — piecewise, exact |
| sum of two discs (the engine) | `F(r)/(2πr)`, with `F` the radial CDF of the sum — **tabulate once per (S,I)** |

So the engine kernel is a *drop-in*: compute a 256-entry table of `F(r)` once per (S,I)
pair, then the per-edge cost is unchanged — one `exp`-free lookup per node, and the
support cutoff is exact rather than an 8σ argument. For the table itself, the robust route
is a 1-D numerical convolution of the two radial laws (or the analytic Bessel form
`F'(r) = (r/SI)∫ J0(kr)J1(Sk)J1(Ik)/k dk`, which is exact but oscillatory — prefer the
convolution). The single-disc case needs no table at all: `g` is piecewise and
transcendental-free.

For multi-pellet, the kernel is the convolution of the per-pellet kernels with the
offset deltas, i.e. a mixture of shifted copies — see §6.

### 3.5 Pose blur

Pose error is a *translation in world space*, so its angular covariance depends on the
hitbox's depth and on the direction of motion:

```
Σ_angle ≈ (1/d²) · J_perp · Σ_world · J_perpᵀ        (J_perp projects onto the tangent plane)
```

It is therefore (a) anisotropic, (b) different for the head and the chest of the same
player, (c) different per lag record. The field currently applies one isotropic σ about
the aim, which is only correct for a stationary target at a fixed depth.

Magnitudes (**estimate**, to be calibrated): a target at 1000 units moving 250 u/s, with
one tick (15.6 ms) of extrapolation error, moves 3.9 units → 3.9 mrad = 0.22°. A head
capsule of radius ~2.5 units at 1000 units subtends 2.5 mrad = 0.14°. So for a running
target, **pose uncertainty is ~1.5 head radii — larger than the spread cone** — while for
a standing target it is ~0.1 unit and negligible. That is the opposite of what the
current single-σ model implies.

Recommended model: a **mixture over the lag records** (which the design already has,
`docs/ragebot.md` §8), each with a small Gaussian residual for interp/extrapolation:

```
E(a) = Σ_records w_r · Σ_i v_i · mass_i(a; Σ_r)
```

with `w_r` from the record-age/velocity model, and `Σ_r` the whitened per-hitbox
covariance. This replaces "a Gaussian with a guessed σ" with "the poses we actually
observed, plus a small blur" — strictly more information, and it is the same integral.

Note that a Gaussian is the right family only for the *residual*: the pose distribution
over records is discrete and often multimodal (a strafing target reverses), and a
Gaussian blur of a strafing target's silhouette is a poor description of "he is at one of
these two poses". For the *continuous* residual between records — extrapolating the last
two records to the shot time — a constant-velocity α-β filter or a Kalman predictor is
the standard tool (Bar-Shalom, Li & Kirubarajan, *Estimation with Applications to
Tracking and Navigation*, Wiley 2001, ch. 6), and its process-noise covariance is exactly
the `Σ_world` that gets projected above. The filter is not a replacement for the record
mixture; it produces the residual that the mixture's Gaussian term describes.

### 3.6 What the objective should be

Three defensible objectives, in increasing order of correctness:

1. `E[damage]` — linear in the cell values, cheap, indifferent to overkill.
2. `P(hit)` — the fire gate's quantity; useful as a constraint, not as the objective
   (it prefers a fat limb over a thin head).
3. `P(kill)` — the decision-theoretic one. With deterministic per-pellet damage `d_k`
   and target HP `H`, `P(kill)` is the mass of the set of poses where
   `Σ_{hits} d_k ≥ H` — a *threshold* probability, not `E[damage]/H`. It is a union of
   shifted cell sets, so it needs the union mass (below), not a sum.

The industry's lexicographic rule is a cheap approximation to (3). A principled middle
ground is `argmax E[damage]` subject to `P(hit) ≥ H_min` and `P(kill) ≥ K_min` — which is
what the menu already exposes — provided `P` is computed with the right kernel.

---

## 4. Numerics

### 4.1 The problem is a polygon mass, not a rectangle CDF

The bivariate-normal literature is about rectangles/orthants:

- **Di Donato, Jarnagin & Hageman** —
  *Computation of the Integral of the Bivariate Normal Distribution Over Arbitrary
  Polygons*, NSWC/DL TR-3866, DTIC
  [ADA102466](https://apps.dtic.mil/sti/citations/ADA102466) (1978): bivariate normal over
  convex/arbitrary (even self-intersecting) polygons, by extracting exterior angular
  regions; the Fortran programs are set to 3/6/9 decimal digits. A journal version with a
  *convex polygon* title appeared in SIAM J. Sci. Stat. Comput.
  ([Semantic Scholar](https://www.semanticscholar.org/paper/bf7e3dd354a68d88e480a54d6b195d1df64e1a51)).
  This is the closest prior art to `polygon_normal_mass`, and it validates the
  boundary-integral approach.
- **Drezner (1978)**, *Computation of the bivariate normal integral*, Math. Comp. 32,
  277–279; **Drezner & Wesolowsky (1990)**, J. Stat. Comput. Simul. 35, 101–107 — fast
  rectangle CDF (double precision).
- **Genz (1992)**, JCGS 1, 141–149; **Genz (2004)**, Stat. Comput. 14, 251–260;
  **Genz & Bretz (2009)**, *Computation of Multivariate Normal and t Probabilities*
  (Springer LNCSE 195) — the standard `MVNDST`/`bvnu` algorithms, rectangle/orthant,
  ~1e-6…1e-15 depending on dimension and settings.
- **TOMS 462 / `BIVNOR`** ([Burkardt's C++ port](https://people.math.sc.edu/Burkardt/cpp_src/toms462/toms462.html)) —
  rectangle CDF, the reference implementation of the Di Donato algorithm.

To use any of them for a *polygon* you must triangulate from some origin, which needs
`O(edges)` CDF evaluations plus care when the origin is outside the polygon (the
decomposition in the DTIC report exists precisely to avoid that). That is strictly more
work than the boundary integral you already have.

For **polygon** integration the right citation is **Sommariva & Vianello (2007)**,
*Product Gauss cubature over polygons based on Green's integration formula*, BIT Numer.
Math. 47, 883–896, [doi:10.1007/s10543-007-0131-2](https://doi.org/10.1007/s10543-007-0131-2):
one univariate Gauss rule per edge, exact for polynomials of degree `2n−1`, no
triangulation. That is exactly the method in `polygon_normal_mass`, applied to a smooth
integrand instead of a polynomial.

### 4.2 The divergence form [measured]

Replace `−∮ φ(x)Φ(y) dx` by

```
mass = ∮ (1 − e^{−r²/2σ²}) · ((x−a)·n) / (2π r²)  ds
```

i.e. `g` instead of `φΦ`, no coordinate scaling by σ (σ is in the exponent), no `erfc`,
no per-edge segmentation. Over 10 random convex cells with circumradius 0.4–2.5σ and
centre within 0.5σ of the aim:

| method | nodes | worst absolute error vs adaptive quadrature |
|---|---|---|
| current (1σ segments × GL8) | 40–128 | **1.1e-16** |
| divergence, GL4 | 3–8 per edge | 4.0e-05 |
| divergence, GL6 | 3–8 per edge | 1.6e-07 |
| divergence, GL8 | 3–8 per edge | 3.9e-10 |

So: the current code is *more* accurate than it needs to be, at 2–4× the node count and
with an `erfc` per node. GL6 in the divergence form is already 1e-7 (far below the 1e-4
tie-break resolution); GL8 matches the current accuracy class for practical purposes.
Two caveats:

- **Do not switch the gradient to the divergence form.** Its derivative contains a
  `1/r³` term that cancels analytically over a closed polygon but not in floating point;
  an ascent using it diverged in my test (most plausibly when the aim stepped onto an
  edge, where that term blows up). The existing `φΦ` flux gradient is bounded and
  correct — keep it, or derive the gradient from the `φΦ` form and the *mass* from the
  divergence form.
- The `φΦ` form loses relative accuracy for cells many σ away (catastrophic cancellation
  between edges). The 8σ cutoff already hides that; the divergence form behaves better,
  another reason to switch the mass.

### 4.3 Precomputation and the far-field shortcut

Per evaluation, the edge geometry (`n`, `L`, midpoint, half-vector) is fixed — precompute
it once per cell, as `field_candidates` already effectively does. The *integral itself*
cannot be precomputed, because the integrand depends on `a`; what can be precomputed is
the edge frame, the cell moments, and (for the engine kernel) the profile `g`. Then:

- **Far cells.** For a cell whose centroid is `> 1.5σ` from the aim, the mass is
  `area·φ(c) + O((diam/σ)²·mass)` (midpoint rule on a smooth kernel). That is an `O(1)`
  evaluation with a computable error term, replacing `edges × nodes`. In a 717-cell field
  at a small cone σ, most cells are far; this is the single biggest per-pass win
  available and it composes with the 8σ cutoff. Use exact integration only for cells
  within ~1.5σ of the aim (typically a handful).
- **Kernel table.** For the engine kernel, precompute `F(r)` on a 256-point grid per
  (S,I) pair once per tick, and linearly interpolate (error `O(h²F'')`, negligible at
  256 points over `[0, S+I]`).

### 4.4 Libraries: recommendation

**Do not add one.** The mass is 20 lines of `exp`/multiply-add and needs no exception
machinery. If you ever do want a library:

| Candidate | What it gives | Licence | Header-only | Fit |
|---|---|---|---|---|
| Genz `bvnu` / [`mvtnorm`](https://cran.r-project.org/web/packages/mvtnorm/index.html) | rectangle CDF, Fortran | **GPL-2** | no | licence-incompatible with a closed payload; rectangle only |
| [`scipy.stats.multivariate_normal.cdf`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.multivariate_normal.html) | Genz QMC, default `abseps=releps=1e-5`, randomised | BSD-3 | no (Python) | not deterministic at your resolution; not embeddable |
| Boost.Math | **no bivariate normal** in the distribution list ([docs](https://www.boost.org/doc/libs/latest/libs/math/doc/html/dist.html)); has `gauss_kronrod`/`tanh_sinh` quadrature | BSL-1.0 | yes | would only replace your GL nodes; not worth it |
| TOMS 462 `BIVNOR` | rectangle CDF | ACM/Di Donato (US Gov work for the original) | C port available | rectangle only |

If a library is ever justified, the honest choice is a **vendored copy of Genz's `bvnu`**
(rectangle) only if you move to a rectangle decomposition — which would be a regression.

---

## 5. Optimisation

### 5.1 Structure

`f(a) = Σ_i v_i m_i(a)`. Each `m_i` is the Gaussian measure of a convex set as a function
of the mean. That function is **log-concave**: `F(x,t) = γ(x)·1(x ∈ K+tv)` is a product
of log-concave functions, so Prékopa–Leindler gives log-concavity of the marginal
([Pinelis, MO 446756](https://mathoverflow.net/questions/446739/sliding-a-convex-body-over-a-gaussian-measure);
Prékopa 1971/1973; Brascamp–Lieb 1976; Borell 1975). Hence **a single cell has one
maximum and no other local maxima** — `argmax_hit_probability` for one cell is
well-posed, and Newton on it is globally safe with a line search.

A weighted **sum** of log-concave functions is not log-concave (the class is closed under
products and convolution, not addition), so `f` can be multimodal. It becomes multimodal
when two valued regions are separated by more than ~2σ.

**[measured]** 459 jittered rectangular cells, σ = 0.18 (≈2 cell widths), random values:
starting from the field centre, steepest ascent converges to 22.999; the global optimum
is 25.234 — the found point is 8.9 % below the global value, in a different basin.
Increasing the local solver's quality did not close it. Two equal cells separated by a
gap: the second local maximum appears at `gap ≈ 0.8` (4σ) and the peak ratio grows
2.6 → 7.2 → 50 → 3.6e4 as the gap goes 0.4 → 0.8 → 1.2 → 2.0.

So the practical rule is: **seed set first, local solver second**. `mass_ascent` already
seeds from the four highest-value cells, which is why it works on the real field (the
head cluster outranks the limb fragments). Keep that, and add:

- one seed per **connected component** of cells (cells whose polygons are within `2σ` of
  each other), so a genuinely separate second target/record is not missed;
- the **hitchance argmax** as an extra seed (it is the maximiser of `Σ m_i`, the
  upper-bound field; see below).

### 5.2 The gradient, the Hessian, and the right number of evaluations

The gradient is already exact and cheap (`mass_gradient`, one boundary pass). The
**Hessian is also a boundary integral**: differentiate the flux once more; the integrand
is `φ` times polynomials in `x, y` (for the Gaussian), evaluated at the same nodes. Cost:
one more pass, not four finite differences. Then:

```
Newton step:  a ← a − H⁻¹ ∇f ,  trust region radius ≤ 0.3·σ,  backtracking on f
```

with `argmax_damage` as the fallback if `H` is singular or the line search fails (at a
strict local maximum `H` is negative definite by definition, so the failure mode is
degeneracy, not sign).

Expected: 3–5 iterations (quadratic convergence) × 2–3 line-search probes ≈ 6–10 passes,
vs the current ≤256. At ~0.1 ms/pass that is 20–35 ms → **~1–3 ms**, before the far-field
shortcut. I did **not** measure the exact-Hessian Newton (my finite-difference Hessian was
too noisy to be a fair test), so treat the iteration count as an estimate, not a result.

### 5.3 A certified bound, and how to prune with it

Because the cells are disjoint and `Σ_i m_i(a) = P(a) ≤ 1`,

```
f(a) = Σ_i v_i m_i(a) ≤ v_max · Σ_i m_i(a) = v_max · P(a)          (*)
```

with equality iff every cell with non-zero mass has `v_i = v_max`. This is a *rigorous*
upper bound computable with the same machinery (it is just `hit_probability`). It gives
two things:

1. **Pruning.** Once a candidate with value `v*` exists, every aim with
   `P(a) < v*/v_max` is provably suboptimal. Evaluate `P` on a coarse grid of chart
   points, drop the points below the threshold, and refine only the surviving region. On a
   real field `v*/v_max` is often close to 1 (the head fragment), so the bound prunes
   little; when the best cell is a thin sliver and the chest is 0.6×, it prunes a lot. It
   is free, so use it as a cheap pre-filter before the ascent.
2. **A second seed.** `argmax P` is exactly `argmax_hit_probability`; evaluating it is
   already done for the gate. Feed it to the ascent.

A tighter per-cell bound `m_i(a) ≤ m_i*` (each cell's own maximum, computed once) gives a
branch-and-bound over cells if you ever need a *certified* global optimum: prune a region
`R` when `Σ_{i: dist(R, Ω_i) < 8σ} v_i m_i* + n·ε < v*`. With 717 cells that is
affordable offline; it is overkill per tick.

### 5.4 Is the objective unimodal in practice?

Per target: usually yes (the cells are adjacent fragments of one silhouette, and the
value varies by hitgroup only). Across records, targets, or a head + a detached limb
fragment: no. The measured 8.9 % gap is a realistic worst case for a dense field. The
cheap defence is the component-based seed set; the expensive defence is branch-and-bound.

---

## 6. Pellet patterns

For `K` pellets with offsets `o_k` (deterministic — `spread.cpp` replicates
`CUniformRandomStream` + `MD5_PseudoRandom`; `docs/re/aim.md` §"The seed"),

```
E[damage](a) = Σ_k Σ_i v_i · 1(a + o_k ∈ Ω_i) = Σ_k D(a + o_k)
```

which is exactly `value_at_pellets`, and it is **correct**: expectation is linear, the
cells are disjoint, and the offsets are known. `argmax_pellets` builds candidates from
each cell's optima *shifted by −o_k*. The target is piecewise constant on the arrangement
of the `K` shifted copies, so the only requirement is that the candidate set **hits every
face** of that arrangement; the current set can miss a face, because the faces are bounded
by intersections of two *shifted edges*, not only by shifted cell vertices. Two fixes, in
order of cost:

1. evaluate the **blurred** pellet objective
   `Σ_k Σ_i v_i · mass_i(a + o_k)` (a tiny σ, e.g. 1/10 of the smallest cell) and ascend
   on it — it is differentiable, it reuses `mass_ascent` (whose `evaluate` functor is
   already generic), and its maximiser is within `O(σ)` of the true one;
2. add the pairwise edge intersections of the shifted copies (exact, `O(K²·edges²)` per
   cell pair, only worth it if you need the certified optimum).

(The unblurred sum is piecewise *constant*, so there is no gradient to ascend: either
enumerate the arrangement's faces or ascend a smoothed surrogate. Do not try to run
`mass_ascent` on `value_at_pellets` directly.)

**`P(hit)` is not the pellet sum.** `Σ_k P(a + o_k ∈ field)` double-counts whenever two
pellets can hit the *same* cell (they usually can). The correct quantity is the mass of
the union `∪_k (field − o_k)`, which is not a sum over disjoint cells. If a pellet
hitchance is ever exposed, compute it as `1 − mass(field complement)` over the union, or
by inclusion–exclusion over the shifted copies (2^K terms — fine for K ≤ 4, hopeless for
a shotgun). For shotguns, skip the probability and use `E[damage]` plus the per-pellet
deterministic pattern — which is what the deterministic model is for.

What the others do: velocity evaluates each multipoint independently (per-point hitchance
and per-point damage from a real trace) and picks **one** point — it never sums over
pellets, and for shotguns it relies on the engine's own pellet spread at the chosen point.
The public bases I read (Osiris CS:GO, `TKazer/CS2_External`, `MitilcC/CS2-Internal-Cheat`)
have no pellet model at all; **unverified** for the closed-source cheats. So the
shifted-copy arrangement is, as far as I can tell, unique to this codebase — and the
`E[damage]` version of it is already correct.

The shotgun/negev pattern table (`FUN_1807cd1b0`) is not modelled yet; until it is, the
pellet offsets for those weapons are wrong, and no amount of probability modelling fixes
that.

---

## 7. What to keep

- The **cell representation** and the piecewise-constant value model. It is the right
  compression of a piecewise-smooth field, and it is what makes the mass cheap.
- The **exact mass** (no sampling) and the **analytic gradient**. Both are correct and
  fast; only the integrand and the σ need changing.
- The **candidate set** (Chebyshev centre, centroid, vertices, nearest point). It is the
  right finite superset for the piecewise objectives, and it is cheap.
- The **deterministic spread/pellet replication** (`spread.cpp`). It is more information
  than any competitor has; do not throw it away for a Gaussian.
- The **8σ cutoff** and the **occlusion/penetration** model.
- The **lexicographic kill-first option**. It is what real cheats do, it is cheap, and it
  is a better match to player intent than a pure expectation.

---

## 8. Concrete recommendations, ranked by value / effort

| # | Change | Effort | Expected gain | Risk / note |
|---|---|---|---|---|
| 1 | **Fix σ**: `sigma = sqrt(S²+I²)/√6`, i.e. `0.408·` the current value; keep the name `cone` for the radius and introduce `spread_sigma` separately | **S** (2 lines in `service.cpp`, 1 in `solver.cpp`) | Hitchance 0.22 → 0.78 for a head-sized target; removes a 1.6–7× understatement; unblocks `min_hitchance` | Changes every gate/threshold; recalibrate the menu defaults |
| 2 | **Calibrate the cone**: stand still, fire 200 shots at a wall at 1024 u, measure the decal radius distribution | **S** (one scenario) | Settles the unit (deg vs rad), the `sqrt` question and the kernel shape at once | Needs a live session; the only way to end the unit debate |
| 3 | **Divergence-form mass** (`g` instead of `φΦ`), GL6/GL8, no segmentation | **S/M** (rewrite `polygon_normal_mass`, keep the flux gradient) | 2–4× fewer nodes, no `erfc`; better conditioning far from the aim | **[measured]** 1.6e-7 / 3.9e-10; do not change the gradient |
| 4 | **Far-field midpoint shortcut** for cells `> 1.5σ` from the aim | **M** | Removes most cells from most passes; the biggest per-pass win at small σ | Needs an error term; validate against exact on saved cell sets |
| 5 | **Newton with the analytic Hessian** + trust region + component seeds | **M** | 20–35 ms → ~1–3 ms (estimate, 6–10 passes) | Keep gradient ascent as the fallback; the Hessian is a second boundary pass |
| 6 | **Exact kernel via a tabulated `g`** (radial CDF of the two-disc sum) | **M** | Only matters when `I ≪ S` (or `S ≪ I`): ≤2 pt error otherwise, up to 8 pt there, plus the correct support | Also needs (2); the table is per (S,I). Skip it if the calibration shows `S ≈ I` |
| 7 | **Pose as a record mixture** with per-hitbox world-space covariance (whitening) | **M/L** | Replaces a guessed σ with the observed poses; fixes the "one σ for head and chest" error | Needs the record ring wired into the solve (already a listed gap) |
| 8 | **Certified pruning** with `f ≤ v_max·P` | **S** | Free pre-filter; sometimes large | Bound is loose when the best cell is not dominant |
| 9 | **Pellet union mass** (only if a pellet hitchance is exposed) | **M** | Fixes double counting | Otherwise leave `E[damage]`, which is exact |
| 10 | **Seed lottery**: inside the chosen cell, probe 8–32 aim angles, compute the exact offset for each seed, pick the best `value_at_pellets` | **M** | Up to "no-spread" quality on a known seed; the largest upside of anything here | **Unverified** seed model; if the seed is not angle-dependent this does nothing; also the most likely to be patched/detected |

Order of work: **1 → 2 → 3 → 5**, then 4/6/7 depending on the profile. Recommendation 1
is a two-line change that fixes a 1.6–7× error; everything else is refinement. After (2)
you will know whether (6) is needed at all.

---

## 9. Measurements and how to reproduce

Scripts (temporary, not part of the repo):
`%TEMP%\opencode\aimmath\exp1_kernel.py` (radial law), `exp2b.py`/`exp3.py`
(boundary integral), `exp8_sigma.py` (the corrected σ tables), `exp4f.py`/`exp5.py`/
`exp7.py` (field + optimiser). All are Python 3.12 + NumPy/SciPy, MC seeded, results
deterministic.

- Kernel table: 1.5–4M samples of `R₁u₁ + R₂u₂`; the Gaussian columns use the
  noncentral χ² CDF, which is exact.
- Integral: reference is adaptive `scipy.integrate.quad` per edge at `epsabs=1e-14`;
  the divergence form was validated against the analytic rectangle CDF and the current
  implementation to 1e-16.
- Field: 459 jittered rectangles (24×30 grid, 6 value classes), σ = 0.18, grid reference
  101×101 + local refinement.
- Timings in §1 are from `docs/ragebot-perf.md` §3.9, not re-measured here.

Two measurement caveats: (a) the "current" implementation is *more accurate* than my
random-polygon test required, so the 1.1e-16 is not a fair "current is sloppy" claim — it
is a "current is paying 2–4× for accuracy you do not need" claim; (b) the Newton iteration
count in §5.2 is an estimate; only the gradient-ascent counts (6–11 iterations, 39–67
evaluations) were measured.

---

## 10. References

**Engine / local RE**

1. `docs/re/aim.md` — `FX_FireBullets` (`FUN_180d22c50`), the spread cone
   (`FUN_180d23460`, RVA `0xd23460`), the seed, inaccuracy, VData offsets.
2. `docs/re/lagcomp.md` — rewind tick, `sv_maxunlag`, interp.
3. `docs/ragebot.md` §2/§3/§7, `docs/ragebot-perf.md` §3.9, `docs/ragebot-review.md` §2.4.
4. `payload/aim/src/field.cpp`, `payload/aim/src/spread.cpp`,
   `payload/control/src/service.cpp:1567`.

**Public code**

5. Osiris, CS:GO `csgo-end-of-life`, `Source/Hacks/Aimbot.cpp` (MIT):
   <https://raw.githubusercontent.com/danielkrupinski/Osiris/csgo-end-of-life/Source/Hacks/Aimbot.cpp>
6. Osiris, CS2 `master` (MIT, no aimbot): <https://github.com/danielkrupinski/Osiris>
7. velocity (leaked CS2 cheat, **no licence**), `shared.cpp` `calculate_hitchance`,
   `calculate_hitchance_fast`, `find_spread_correction`;
   `rage.cpp` scoring and `generate_multipoints`:
   <https://github.com/wubly/velocity>
8. UC "1 Line Hitchance Challenge" (2025):
   <https://www.unknowncheats.me/forum/counter-strike-2-a/687656-1-line-hitchance-challenge.html>
9. UC "Taming randomness: an analysis of the new no spread" (CS2 seed):
   <https://www.unknowncheats.me/forum/counter-strike-2-a/611553-taming-randomness-analysis-spread.html>

**Numerics**

10. Di Donato, Jarnagin & Hageman, *Computation of the Integral of the Bivariate Normal
    Distribution Over Arbitrary Polygons*, NSWC/DL TR-3866, DTIC ADA102466 (1978):
    <https://apps.dtic.mil/sti/citations/ADA102466>; journal version, *…Over Convex
    Polygons*, SIAM J. Sci. Stat. Comput.:
    <https://www.semanticscholar.org/paper/bf7e3dd354a68d88e480a54d6b195d1df64e1a51>
11. Drezner, *Computation of the bivariate normal integral*, Math. Comp. 32 (1978)
    277–279.
12. Drezner & Wesolowsky, J. Stat. Comput. Simul. 35 (1990) 101–107.
13. Genz, *Numerical computation of multivariate normal probabilities*, JCGS 1 (1992)
    141–149; Genz, *Numerical computation of rectangular bivariate and trivariate normal
    and t probabilities*, Stat. Comput. 14 (2004) 251–260.
14. Genz & Bretz, *Computation of Multivariate Normal and t Probabilities*, Springer
    LNCSE 195 (2009).
15. Sommariva & Vianello, *Product Gauss cubature over polygons based on Green's
    integration formula*, BIT Numer. Math. 47 (2007) 883–896,
    <https://doi.org/10.1007/s10543-007-0131-2>
16. TOMS 462 / `BIVNOR`, C++ port:
    <https://people.math.sc.edu/Burkardt/cpp_src/toms462/toms462.html>
17. `mvtnorm` (GPL-2): <https://cran.r-project.org/web/packages/mvtnorm/index.html>
18. SciPy `multivariate_normal.cdf` (Genz QMC, default 1e-5):
    <https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.multivariate_normal.html>
19. Boost.Math distribution list (no bivariate normal, BSL-1.0):
    <https://www.boost.org/doc/libs/latest/libs/math/doc/html/dist.html>

**Convexity / optimisation**

20. Prékopa, *Logarithmic concave measures with application to stochastic programming*,
    Acta Sci. Math. Szeged 32 (1971) 301–316; 34 (1973) 335–343.
21. Brascamp & Lieb, *On extensions of the Brunn–Minkowski and Prékopa–Leindler
    theorems*, J. Funct. Anal. 22 (1976) 366–389.
22. Borell, *Convex set functions in d-space*, Period. Math. Hungar. 6 (1975) 111–136.
23. Pinelis, *Sliding a convex body over a Gaussian measure* (log-concavity via
    Prékopa–Leindler): <https://mathoverflow.net/questions/446739>

**Tracking**

24. Bar-Shalom, Li & Kirubarajan, *Estimation with Applications to Tracking and
    Navigation: Theory, Algorithms, and Software*, Wiley (2001), ch. 6 (Kalman/α-β
    prediction and process noise).

---

## 11. Addendum (2026-10-21) — the Monte-Carlo estimator, measured

> Task T34 (`docs/orchestration/spec-t34-montecarlo.md`) built the estimator this
> note argues for: `cs2/aim/montecarlo.hpp`, host-tested (`aim-montecarlo`) and
> benchmarked (`aim_bench`, replaying the committed 717-cell set). The write-up is
> [`docs/ragebot.md`](../ragebot.md) §13. **Nothing above changes** — this note
> records what the implementation *sharpens*, with the numbers.

1. **§3.1's variance, measured — 2.7 % low, not "to within 1 %"** (corrected
   2026-10-21; the review's M4). The sampler draws the engine's kernel
   (`r1, a1, r2, a2` per pellet) and the measured second moments **nearly**
   reproduce the derivation: one term (`S = 1`, `I = 0`) gives `E[r] = 0.486220`
   (theory `c/2`) and `rms(r) = 0.569462` (theory `c/√3 = 0.577350`), so the
   per-axis variance is `rms²/2 = 0.162144` against the derivation's
   `c²/6 = 0.166667` — **2.7 % low**. Two equal terms (`S = I = 1`) give
   `rms(r) = 0.812712`, so the per-axis variance is `0.330 c²` against `0.333 c²`
   (0.9 % low) and the variance-matched `σ = 0.573 c` against the derivation's
   `sqrt(S² + I²)/√6 = 0.577 c`. The `√6` claim of §3.1/§0 therefore holds **for
   the repo's own kernel model, to within 3 %** — not to within 1 %; the
   correction itself is still blocked (the `sqrt(r)` question, the inaccuracy's
   units) and is **not** applied anywhere.

   **The gap is not noise — it is the repo RNG's *first* draw.** The sampler uses
   the stream's **first** output for `r1` (the spread magnitude) on *every*
   sample, and that first output is biased low: over 200 000 MD5 seeds
   `E = 0.486220` (**−21.3 σ**, where σ = 1/√(12·200 000) = 0.000645), and over
   200 000 *sequential* seeds `E = 0.484602` (−23.9 σ). Draws **2–8** of the same
   streams are unbiased (|E − 0.5| ≤ 2.3 σ), so the defect is the stream's first
   output, not the seed distribution. The repo's own radial-law test measures
   **0.485476** over its 40 000 draws and used to pass only because its tolerance
   was 0.02 — 73 % of that budget was the bias.

   **What it means.** The *model* of the kernel is systematically **≈3 % tighter**
   than the ideal uniform-in-radius law, so every hit chance the solver publishes
   is slightly optimistic. Whether the **live engine** has the same bias depends
   on whether its `CUniformRandomStream` is this implementation: the repo models
   it as exactly this generator (`payload/aim/src/spread.cpp`), and `spread.hpp`
   still says the RNG is "unvalidated against the live engine", so the honest
   statement is *"the model is 2.7 % tighter than the ideal law; if the engine's
   generator is this one, the engine's cone is too"*.

   **The fix is deliberately out of scope for this task.** `UniformRandomStream`
   and `pellet_offsets` must stay bit-identical (the spec §4: "the existing
   `pellet_offsets` behaviour must not change"), and the first draw is a property
   of the engine's own generator, not of this module: changing it would change the
   offsets the *existing* solver computes. The bias is therefore **documented, not
   fixed** — the host test now pins the *measured* law (a band around 0.4855)
   instead of a ±0.02 window around 0.5, so it can no longer hide the bias, and
   the `sqrt(r)` question is still caught loudly.

2. **§3.1's radial law, measured.** The single term is **uniform in radius** (the
   ideal `E[r] = c/2`; the repo's RNG measures **0.486220**, the deficit being the
   first-draw bias of item 1, and the rim is reached: the largest radius over
   200 000 draws is 0.99999 c), so §3.3's linear-radial row is the right limit and
   the area-uniform `2c/3` is wrong. The **sum** of two terms is much more
   concentrated than its support suggests: with `S = I = c` the support is `2c` but
   the measured mean radius is only **0.720568 c** and the largest sampled radius
   1.99 c. A "half the cone radius" target is therefore *easier* than the
   support-based estimate of §3.3 suggests, and the `sqrt(r)` variant would move
   the single term to `c/3` — the difference the note calls "large" is exactly
   `c/2` against `c/3`.

3. **§3.1's `sqrt(r)` question is now a test, not a comment.** The host test pins
   the linear law — the mean radius in the **measured** band around 0.4855 (the
   repo RNG's first-draw bias; the ideal is 0.5), and *not* the area-uniform
   `2c/3`; reintroducing the `sqrt` fails it (the mean radius becomes `c/3`). The
   repo's RE stays the authority (no `sqrt`).

4. **§3.2's seed question has a structured answer.** The seed is a **parameter of
   the solver**, not an assumption: the `sequence` policy
   (`md5_pseudo_random(sequence + i)`) makes the number the **expectation over the
   next N shots**, and the `angle_grid` policy (the seed is a function of the aim
   and the tick) makes it the **probability over the shot that would be taken**. The
   `angle_grid` hash is a **stand-in** — the engine's `get_tick_view_angles` is not
   reverse-engineered — so its numbers are indicative, not exact.

5. **§6's "P(hit) is not the pellet sum" is implemented as a union.** A sample hits
   when **any** of its pellets hits; the sample's damage is the sum over the pellets
   that hit. §1.2's double-count is removed by the **nearest-hit** rule, and the
   price is that the two solvers are **not comparable cell-for-cell** any more: the
   Monte-Carlo solver scores the hitbox geometry, the field solver scores the chart
   cells.

6. **§1.6's cost, measured on the saved 717-cell set — at a live hitbox count.**
   The estimator's whole search (32 candidates) costs p50/p95/p99 = **4.3 / 5.0 /
   5.1 ms** at 64 samples, **8.9 / 10.1 / 10.7 ms** at 128 and **19.2 / 23.2 /
   23.8 ms** at 256 — against the **15.6 ms** tick and the service's **12 ms**
   guard (the median of six runs; the six runs' p99 ranged 4.9–7.4 ms, 9.2–12.8 ms
   and 22.9–29.3 ms).

   **These numbers are at a live-sized hitbox set: 57 hitboxes** (19 per player ×
   3 players, `docs/re/aim.md`: `m_nHitBoxes (19 on a player)`). The saved set
   stores *chart cells*, so the benchmark's first reconstruction rebuilt only **24**
   hitboxes (8 per target, one per hitgroup) — 2.4× fewer than a live solve — and
   the cost was **understated by the same factor** (2.4 / 4.8 / 9.7 ms). The
   benchmark now measures both, and the **live-sized** one is the one the budget
   verdict is taken from: **64 samples fit with room to spare, 128 samples are at
   the guard** (the median p99 is 10.7 ms, but the run-to-run spread crosses the
   12 ms guard), and **256 samples do not fit** (the p50 is already over the tick).

   The existing cone objective on the same field measures **60 / 65 / 67 ms** here
   (the published 34 ms is the same order). The sample-based objective is
   therefore **3–14× cheaper** than the one it would replace — not the 4–20× the
   24-hitbox reconstruction suggested.

   > **The cost ratio is not a quality-for-quality comparison.** The Monte-Carlo
   > search evaluates 32 **fixed** candidates (the top-16 cells' Chebyshev centres
   > and centroids) with no refinement inside a cell, while `argmax_hit_probability`
   > runs `mass_ascent` (8 seed candidates plus up to 32 compass iterations refining
   > to a local optimum over the 717 cells). And the two do **not** model the same
   > dispersion: the Monte-Carlo kernel's support is `S + I = 0.00283` rad and its
   > variance-matched σ is `0.00082` rad — **2.4× tighter** than the Gaussian's
   > `σ = 0.002` — so their hit chances are not comparable either.
