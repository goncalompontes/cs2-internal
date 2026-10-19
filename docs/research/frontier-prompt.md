# Frontier-model prompt — find the optimal ragebot occlusion/aim formulation

> Paste everything below the line into a frontier reasoning model (or hand it the repo and
> the three briefs in `docs/research/`). It is written to be self-contained: if the model
> cannot read files, the numbers it needs are inline.

---

You are a senior graphics/computational-geometry engineer and applied mathematician. I
need you to **find the optimal formulation** for a real, measured performance problem in a
game-overlay aim solver, and to tell me where my current design is not just slow but
*the wrong shape*. Challenge my assumptions; if the right answer is "delete most of this",
say so and show the math.

## 1. The system

An in-process cheat payload (C++23, injected into a running Counter-Strike 2 client). On
each game tick (64 Hz, so the whole per-tick budget is ~15.6 ms, and it must run on the
game thread inside the `CreateMove` hook) it must choose an aim direction that maximises
expected damage against enemy players.

The world model available to us:
- ~10,000 static world solids (convex brush hulls: a vertex cloud + inward face planes),
  plus a handful of dynamic props (spheres/capsules) and large collision **meshes**
  (triangle soups, pre-split into spatial clusters of ≤2048 triangles).
- Each enemy is a set of ~19 studio hitboxes (capsules: two world-space endpoints +
  radius). Multiple enemies can be candidates at once (up to ~6 in FOV).
- We replicate the engine's penetration model exactly: a shot can cross at most 4–5 walls,
  a ~90-unit accumulated-thickness cap, a 3000-unit trace-length gate, per-surface
  penetration/damage modifiers, fractional loss terms, and a stop when damage drops < 1.

## 2. The current design, and why it is the wrong shape

1. Project every enemy hitbox silhouette from the eye into a **2-D yaw/pitch chart**
   (an absolute-world angular map; convex polygons).
2. Enumerate every world solid that could lie between the eye and any hitbox (a per-enemy
   cone/pyramid query over an AABB BVH; ~14–320 solids in practice, 1000+ in dense spots).
3. For each hitbox silhouette, compute the **full arrangement** of
   `silhouette − (every occluder silhouette)` using a specialised convex clipper
   (`subtract_convex`), producing one convex cell per region.
4. Each cell carries the wall stack along its direction, hence a damage value.
5. Optimise: pick the aimpoint maximising damage (`argmax_damage`) or the probability of
   hitting under a Gaussian angular spread (`argmax_hit_probability`, a boundary-integral
   Gaussian mass with an analytic gradient ascent).

Step 3 is the problem. Measured on a real scene: **57 hitboxes × 312 solids → 15,670 convex
cells, ~0.6 s for one solve.** One silhouette clipped by 4→64 overlapping occluders goes
8 → 2,487 fragments and 0.03 → 36 ms: superlinear, and it is the only stage that can stall
the game (one tick took 11.6 s).

## 3. Measured facts (use these; they are from benchmarks on this machine)

- `subtract_convex` (our convex-vs-convex difference, also returns the intersection):
  **0.55 µs** per op. Clipper2 on the same pair: **3.26 µs**. **Our primitive is 6× faster
  than the library** — so the problem is not the clipper.
- `ray_span` (exact ray vs 6-plane convex hull, half-space clipping): **51.5 ns**.
- `AabbBvh` (our median-split AABB BVH): 2.6 ms build / **0.57 µs** query at 10k boxes.
- `query_pyramid` (eye→proxy pyramid vs BVH, per-node SAT): 9.67 µs per call.
- Mesh clusters: projecting the **8 AABB corners** instead of the 6443-point soup is
  772 µs → 0.69 µs, because the 2-D silhouette is the convex hull of the projected points.
- After my fixes the stages are: enumeration ~0.1–0.5 ms, projection 0.6–0.9 ms, the
  arrangement **6–70 ms** (spot-dependent), objective ~5 ms. The arrangement is the whole
  problem.
- A stopgap cap (examine only the nearest 24 solids per hitbox) brings it to ~6–10 ms but
  is **not exactness-preserving** (a far solid can be the first wall on a ray that misses
  the nearer ones), so it can under-report a wallbang. I want it gone.

## 4. What the industry does (and what no one does)

Every public CS2/CS:GO implementation I surveyed uses **engine ray traces**, never a
projected field. In CS2 the engine call returns the **entire sorted surface stack** for a
ray in one call, and the penetration step is pure arithmetic per stack entry — so the
canonical shape is *one trace per candidate direction + ≤5 arithmetic steps*. **No public
implementation builds a projected damage field**; mine appears novel, and that novelty is
the cost. (Sources: the Source/CS:GO autowall lineage, `Read1dno/ExternalAutoWallCS2`
which builds a BVH from VPK geometry and ray-traces it, and the UC CS2 EngineTrace
releases.)

## 5. Hard constraints (these make library answers non-trivial)

- The payload is **manually mapped** into the game process: no CRT dynamic
  initialisation, no `thread_local` caches, no process-wide thread pools, no exceptions or
  RTTI machinery that adds imports, and every CRT import must be resolvable by the mapper.
  Header-only / static-CRT / `-fno-exceptions` is the safe shape.
- Everything must run **on the game thread** inside `CreateMove` (engine calls that
  allocate fault outside it).
- The per-tick budget is ~15.6 ms, and this is one of several subsystems.
- Correctness requirement from the operator: **do not drop geometry that could still
  attenuate a shot.** Prefer exact-but-slower over approximate; if you propose an
  approximation, give a *proven* error bound.

## 6. What I want from you

1. **Reformulate the problem.** What is the right formal statement of "choose an aim
   direction maximising expected damage under a spread distribution, with wallbang"? Name
   the known problems/literature it corresponds to (I suspect: visibility decomposition,
   the visibility complex, arrangement complexity, optimal aiming under uncertainty,
   maximum-weight region queries, decision theory). Is the projected field the right
   object, or is the right object something else (e.g. a set of traces, a depth buffer, a
   spherical/von-Mises formulation, a stochastic-optimisation problem)?

2. **Complexity analysis.** Give the true asymptotic cost of my current pipeline as a
   function of (hitboxes H, solids S, hitbox edges E, chart resolution). Then give the
   asymptotic cost of every alternative you think is competitive. Be concrete about which
   term dominates at H≈50, S≈300.

3. **The optimal design.** Propose the architecture you would build, with pseudocode and
   complexity. Consider at least:
   - a bounded candidate set scored by exact ray casts (we already own `ray_span` at 51.5 ns
     and the engine's sorted-stack trace; a candidate solve should be ~2–5 ms);
   - rasterising occluders into a low-resolution **masked hierarchical depth buffer**
     (Intel's MaskedOcclusionCulling / Hi-Z) and answering visibility with no per-solid
     work;
   - a hybrid: exact arrangement only for the final few candidates or for the heatmap
     render, rays everywhere else;
   - anything better that I have not thought of.
   Justify the choice against the constraints in §5.

4. **The probability model.** My spread model is a 2-D Gaussian in angle; I have measured
   that the engine's actual kernel (`r = RandomFloat(0,1)`, `θ = RandomFloat(0,2π)`, once
   for spread and once for inaccuracy, summed) has per-axis variance `(S²+I²)/6`, not
   `S²+I²`, so my σ is √6 too wide (reported hitchance 0.221 where the truth is 0.777 for a
   head-sized target). I also model pose/lag uncertainty as one isotropic blur, and
   multi-pellet weapons as a sum of shifted copies (where `P(hit)` is **not** additive
   because copies overlap). Give me the correct objective and the correct estimator,
   including the right treatment of the deterministic seed (the spread is a deterministic
   function of aim and tick, so it is a seed lottery, not a distribution — and one public
   cheat already brute-forces the seed).

5. **Numerics.** I currently compute the bivariate-Normal mass over each convex cell by
   Green's theorem with 8-point Gauss–Legendre per ~1σ edge segment. I have been told the
   divergence form `∮ g(r)((x−a)·n)/r ds` is cheaper, `erfc`-free and kernel-agnostic
   (GL-8: 4e-10 error at 3–8 nodes/edge). Confirm or refute, give the error bound, and tell
   me whether there is an exact/precomputed formulation for *convex* polygons specifically.

6. **Optimisation.** The objective is a weighted sum of Gaussian masses over disjoint
   convex cells. Each cell's mass is log-concave (Prékopa–Leindler), but the weighted sum
   is not, and I measured two basins 8.9% apart in a 459-cell field. What is the right
   global maximiser given the budget (branch and bound with the certified bound
   `f(a) ≤ v_max·P_hit(a)`? weighted Voronoi of the cells? Newton with the analytic
   Hessian? quadrature on the sphere?). Give the expected number of function evaluations.

7. **Migration plan.** A staged path from where I am to your design, where each stage is
   independently shippable and measurable, and where I can delete the stopgap caps.

8. **Falsifiable predictions.** For each recommendation, state the predicted cost on my
   workload (H≈50 hitboxes, S≈300 solids, 64 Hz) and the experiment that would falsify it.

## 7. Deliverable format

A technical memo: problem formalisation → complexity table (current vs alternatives) →
recommended architecture with pseudocode → probability/numerics fixes → optimisation
algorithm → migration stages → risks and falsifiable predictions → references. Cite
primary sources (papers, library docs, engine code) with URLs. Where you are unsure, say
"unverified" rather than guessing. Assume I am competent: skip tutorial material, and
push back hard if the honest answer is that I should abandon the field and trace like
everyone else.
