#pragma once

// The Monte-Carlo hit-chance solver (docs/orchestration/spec-t34-montecarlo.md).
//
// **Why.** The damage-field solver scores a shot with a **Gaussian** over the field
// (`payload/aim/src/field.cpp`), but the engine's spread kernel is not Gaussian: its
// support is exactly `|off| <= S + I` and its radial law is uniform in radius
// (`docs/re/aim.md` §"The spread cone", `docs/research/aim-objective-math.md` §3.1).
// This module samples the engine's **own** kernel over seed indices and tests each
// sample against the target's hitboxes with a **nearest-hit** ray test — the
// community's estimator (velocity's 64/128/256 seed loop,
// `docs/research/aim-objective-math.md` §2).
//
// **It sits alongside the existing solver, not inside it.** Nothing here changes
// `DamageField`; the existing implementation stays untouched and comparable. This
// module is the **core** only: it is not wired into the live path (no `AimObjective`,
// no dispatch, no setting).
//
// **The seed is a parameter, not an assumption.** The two policies below model the two
// conflicting accounts of the engine's seed (`docs/known-issues.md` §11.2): the repo's
// own RE (`MD5_PseudoRandom(sequence)`) and the community's (a function of the view
// angles and the tick). The solver reports which one produced a number.
//
// **The `angle_grid` hash is a STAND-IN.** The engine's `get_tick_view_angles` is not
// reverse-engineered (`docs/re/aim.md` §"The seed"), so the hash below is *ours*. The
// numbers that policy produces are **indicative, not exact**.
//
// **What this module does NOT model: the world.** The repo's design is Tier-2 — it
// projects the world solids into the chart instead of tracing rays
// (`docs/ragebot.md` §3). This solver scores the **hitbox geometry only**: whether
// the shot is *visible* (or penetrates a wall) remains the existing solver's job
// (`only_visible` / `penetrated`).

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "cs2/aim/field.hpp"
#include "cs2/aim/hitbox.hpp"
#include "cs2/aim/spread.hpp"

namespace cs2::aim {

// --- The errors ----------------------------------------------------------------

// Why an evaluation could not be made. Errors are values: the solver never throws.
enum class MonteCarloError : std::uint8_t {
  no_hitboxes,          ///< nothing to test the samples against
  invalid_samples,      ///< the sample count or the pellet count is not positive
  too_many_pellets,     ///< more pellets than `MAX_PELLETS`
  unsupported_pattern,  ///< the weapon's pellet pattern is not modelled
  no_candidates,        ///< the candidate set is empty
  too_many_targets,     ///< more candidate targets than `MAX_TRACKED_TARGETS`
  invalid_geometry,     ///< a hitbox is not a well-formed solid (NaN, inf, degenerate)
  buffer_too_small,     ///< the caller's buffer is smaller than the pellet count
};

[[nodiscard]] auto to_string(MonteCarloError error) noexcept -> const char*;

// --- The kernel ----------------------------------------------------------------

// The engine's spread kernel for one shot: the magnitudes `FX_FireBullets` is called
// with (`docs/re/aim.md` §"The spread cone") and the pellet count.
struct SpreadKernel {
  float spread = 0.0F;      ///< the spread magnitude (radians)
  float inaccuracy = 0.0F;  ///< the inaccuracy magnitude (radians)
  int pellets = 1;
  SpreadPattern pattern = SpreadPattern::standard;
  // The engine's **per-shot rules** (the profile's): the revolver's and the
  // negev's transforms, and the shotgun-pattern convar.
  //
  // **They are a parameter, not a constant.** The spread radius and angle are
  // drawn **once** for the shot and again per pellet only when the convar is on,
  // and the transforms depend on the attack type and the recoil counter, which
  // are per-shot state the engine keeps on the weapon.
  //
  // **The revolver's transform is keyed on the *pending* shot's attack type**,
  // which the payload cannot know (`cs2/aim/spread.hpp`, `SpreadRules`), so it
  // is never applied: the observed value the payload read travels with the
  // kernel as **evidence** and nothing else.
  SpreadRules rules{};
};

// The largest pellet count the sampler accepts. The engine's shotguns and the negev
// fire 10; the bound keeps the sampler's scratch buffer on the stack (the payload is
// manually mapped — no heap on the hot path).
//
// **It is a defensive cap, not a reachable one.** The weapons that fire more than
// 16 pellets (the shotguns) are refused by `pattern_supported` anyway, so the bound
// is not reachable through `spread_kernel`; it matters only to a caller that clears
// the weapon type and states a pellet count of its own.
inline constexpr int MAX_PELLETS = 16;

// The engine's weapon types (`m_WeaponType`, `cs2/aim/field.hpp`).
inline constexpr int WEAPON_TYPE_SHOTGUN = 4;
inline constexpr int WEAPON_TYPE_MACHINEGUN = 6;

// The engine's hitgroup ids (`m_nGroupId`, `docs/re/aim.md`).
inline constexpr int HITGROUP_HEAD = 1;
inline constexpr int HITGROUP_CHEST = 2;
inline constexpr int HITGROUP_STOMACH = 3;
inline constexpr int HITGROUP_LEFT_ARM = 4;
inline constexpr int HITGROUP_RIGHT_ARM = 5;
inline constexpr int HITGROUP_LEFT_LEG = 6;
inline constexpr int HITGROUP_RIGHT_LEG = 7;
inline constexpr int HITGROUP_GENERIC = 10;

// Whether the engine's pellet pattern for `weapon` is modelled by the kernel.
//
// **The shotgun is refused — a policy choice, not a modelling limit.** The
// engine's pattern table **is** reproducible (the values come from the VData
// with the repo's own `UniformRandomStream`, `cs2/aim/spread.hpp`), so the
// refusal is **not** "the table is unknown". It is this:
//
//   * the table is only used when the convar
//     `weapon_accuracy_shotgun_spread_patterns` is **on**, and it is **off by
//     default** — so the payload *could* model the **default** (the random
//     cone) and be right unless a server enables the convar;
//   * the payload **cannot read the convar**: it is a `ConVar` the payload has
//     no address for, and reading it would need a **dumper signature** the repo
//     does not have (ADR-0012) — which this task is not allowed to add.
//
// The refusal therefore states a **policy**: "a wrong number is worse than no
// number". The payload cannot tell which cone the server enabled, so it refuses
// rather than guess — even though the *default* is modellable.
//
// **The machineguns are refused** because the payload cannot tell the negev from
// the M249: both are weapon type 6 and the VData carries no flag for the negev
// (the engine compares the weapon **id**). The negev's transform itself is
// modelled (`SpreadPattern::negev`); a caller that knows its weapon is the negev
// clears the type (or passes -1) and takes the responsibility.
//
// **Every entry point that can produce a number checks this** — `spread_kernel`,
// `evaluate_sample`, `evaluate_hit_chance` and `solve_montecarlo` all refuse an
// unmodelled weapon with `MonteCarloError::unsupported_pattern`. There is no path
// through the module on which a shotgun is silently scored with the standard
// pattern (`docs/orchestration/spec-t34-montecarlo.md` §2.2).
[[nodiscard]] auto pattern_supported(const WeaponProfile& weapon) noexcept -> bool;

// The kernel of a weapon: its spread and its effective inaccuracy. Fails for the
// weapons whose pellet pattern is not modelled (`pattern_supported`).
//
// **The pattern is a required argument — there is no silent default.** The
// pattern names the weapon's *class*; the engine's own conditions (the VData's
// `m_bIsRevolver` and the attack type) come from the **profile** and are applied
// by the draw, so the caller's class can never put a transform on that the engine
// would not apply.
//
// **A caller may pass the class or the pattern in force** — the core applies
// the transform only when the engine's own conditions hold, so the two differ
// only in what the *status* reports. The live path passes the pattern **in
// force** (`cs2::control::applied_spread_pattern`), so the status cannot claim
// a transform the core does not apply.
[[nodiscard]] auto spread_kernel(const WeaponProfile& weapon, int pellets,
                                 SpreadPattern pattern) noexcept
    -> std::expected<SpreadKernel, MonteCarloError>;

// The engine's `K` pellet offsets for `seed` — the same draw `pellet_offsets`
// makes, in the engine's order (`r1, a1` once for the shot, `r2, a2` per pellet).
// The number of offsets written is returned; `out` is the caller's buffer (no
// allocation).
//
// **A buffer that is too small is an error, not a silent truncation** (the review's
// m5): the caller asked for `kernel.pellets` offsets and did not get them, and a
// caller that ignores the error would otherwise score a shot with fewer pellets
// than the weapon fires.
[[nodiscard]] auto draw_pellet_offsets(std::int32_t seed, const SpreadKernel& kernel,
                                       std::span<Vec2> out) noexcept
    -> std::expected<int, MonteCarloError>;

// --- The seed policies ----------------------------------------------------------

// How a candidate aim and a sample index become a seed.
//
// The two policies model the two **conflicting** accounts of the engine's seed
// (`docs/known-issues.md` §11.2, `docs/research/aim-objective-math.md` §3.1):
//
//   sequence    the repo's own RE: `CBaseUserCmdPB::random_seed` is
//               `MD5_PseudoRandom(sequence)` — a pure function of the command number
//               (`docs/re/aim.md` §"The seed").
//   angle_grid  the community's model: the seed is a function of the **view angles
//               and the tick** (the client's `get_tick_view_angles(angles, tick)`).
//               **The hash is a STAND-IN** — the engine's function is not
//               reverse-engineered.
enum class SeedSource : std::uint8_t {
  sequence,
  angle_grid,
};

// The per-sample context the seed policies need.
struct SeedContext {
  std::uint32_t sequence = 0;  ///< the current command's sequence number
  std::uint32_t tick = 0;      ///< the prediction tick
  SeedSource source = SeedSource::sequence;
  // The `angle_grid` probe step (radians, ~0.5 deg): the angular distance between two
  // neighbouring probe angles in the candidate's neighbourhood.
  float probe_step = 0.0087F;
};

// The seed of the `index`-th sample of the candidate aim `aim`, under the context's
// policy. `sample_count` is the number of samples the caller draws (the `angle_grid`
// policy centres its probe sweep on the candidate).
//
// `sequence`: `md5_pseudo_random(sequence + index)` — the samples are the next
// `index` **commands**, so the resulting hit chance is the **expectation over the
// next shots**.
// `angle_grid`: `md5_pseudo_random(hash(probe yaw, probe pitch, tick))`, where the
// probe angles sweep the candidate's neighbourhood at `probe_step` — the samples are
// the seeds that neighbourhood would produce, so the resulting hit chance is the
// **probability over the shot that would be taken**.
//
// **The candidate's own angle is one of the probes**: the sweep's centre index is
// integral (`sample_count / 2`), so the candidate's own seed is in the sample set.
// The sweep reaches `floor(sample_count / 2)` steps either side of the candidate
// (asymmetric by one step for an even count) — at the default `probe_step` of
// 0.0087 rad, a half-width of 15.95° at N = 64, 31.9° at N = 128 and 63.8° at
// N = 256.
//
// The hash is a **stand-in** for the engine's unre-engineered `get_tick_view_angles`.
[[nodiscard]] auto sample_seed(const SeedContext& context, const Vec2& aim, int index,
                               int sample_count) noexcept -> std::int32_t;

// The STAND-IN seed hash of the `angle_grid` policy.
//
// **This is not the engine's function.** The client computes its spread seed through
// `get_tick_view_angles(angles, tick)`, which the repo has **not** reverse-engineered
// (`docs/re/aim.md` §"The seed"; the RVAs there resolve to unrelated functions on the
// live build). This hash is *ours*: the probe angles are quantised onto a fixed grid
// and mixed with the tick. It reproduces the **shape** of the community's model — the
// seed is a function of the aim and the tick — and nothing more, so the numbers the
// `angle_grid` policy produces are indicative, not exact.
//
// **The hash is injective on the grid, not on the angles.** The quantisation is
// deliberate — the stand-in models "the same aim gives the same seed" — so two
// probe angles within one quantum (1e-4 rad) share a seed. The probe step
// (0.0087 rad by default) is 87 quanta, so two neighbouring probes never collide.
[[nodiscard]] auto angle_seed_hash(float yaw, float pitch, std::uint32_t tick) noexcept
    -> std::uint32_t;

// --- The sample -------------------------------------------------------------------

// One drawn pellet set and the outcome of the test.
//
// The **nearest** hit wins. An exact tie is broken by **the rule** — the hitgroup
// preference, then the damage multiplier, then the target index — so the outcome
// never depends on the order the caller listed the hitboxes in.
struct Sample {
  // The chart-space offset of the sample's **first** pellet: the `(right, up)`
  // tangent-plane displacement of the shot's direction, in radians. A multi-pellet
  // shot draws one offset per pellet; the first is reported here (the single-pellet
  // case, the common one, is exact).
  Vec2 offset{};
  bool hit = false;       ///< did any pellet of this sample hit?
  float damage = 0.0F;    ///< the damage the sample did: the **sum** over the pellets
                          ///< that hit (the engine's per-pellet damage), 0 on a miss
  int target = -1;        ///< the target of the nearest hit, -1 on a miss
  int hitgroup = 0;       ///< the hitgroup of the nearest hit
  float distance = 0.0F;  ///< the distance to the nearest hit, 0 on a miss

  // The **per-pellet attribution**: what each pellet that hit did, and to whom.
  //
  // **A sample's damage does not belong to one target.** A multi-pellet shot can
  // hit two targets, and the whole sample's damage used to be credited to the
  // target of the *nearest* hit — so the second target's damage was given to the
  // first (the review's M2 bug, one level down). The can-kill key then compared
  // one target's remaining health with the sum of **two** targets' damage, and a
  // candidate could claim a kill that cannot happen.
  //
  // The attribution is therefore **per pellet**: `pellet_damage[k]` is what the
  // `k`-th pellet that hit did and `pellet_target[k]` is the target it did it to.
  // `pellets_hit` is how many of the entries are meaningful (the rest are zero).
  std::array<float, MAX_PELLETS> pellet_damage{};
  std::array<int, MAX_PELLETS> pellet_target{};
  int pellets_hit = 0;
};

// The estimator's verdict for one candidate aim.
struct HitChance {
  float probability = 0.0F;      ///< the fraction of the samples that hit
  float expected_damage = 0.0F;  ///< the mean damage over ALL the samples
  int samples = 0;               ///< how many samples were drawn
  int hits = 0;                  ///< how many of them hit
  // **A diagnostic, not a probability.** The pair comes from the single
  // **nearest** hit across all the samples: in a multi-target scene it can come
  // from one sample in N and need not represent where the shot usually lands.
  int target = -1;               ///< the target of the nearest hit, -1 when nothing hit
  int hitgroup = 0;              ///< the hitgroup of the nearest hit
  // The mean damage the samples did **to `target`** — the per-target attribution.
  //
  // `expected_damage` is the **scene's** mean: in a multi-target scene it sums the
  // damage of every target a sample hit, so it says nothing about whether the
  // named target dies. The can-kill key is judged against **this** number and
  // `target`'s own health (the review's M2: the aggregate was attributed to one
  // target, and the rule's first key beat every honest candidate with it).
  float target_damage = 0.0F;
  // Which seed policy produced the number. The two policies answer the ledger's
  // question differently (§ the header's `SeedSource`), so a number that does not
  // say which one it came from cannot be read.
  SeedSource source = SeedSource::sequence;
};

// --- The hit test ------------------------------------------------------------------

// Whether the hitbox is a **well-formed solid**: every component finite, and the
// shape not degenerate (a capsule's radius positive, an oriented box's half-extents
// positive).
//
// **The damage multiplier is part of the check.** The multiplier is the hitbox's
// damage factor (`m_flDamageMultiplier`), and it is used as a **multiplier**: a NaN
// one makes every damage product a NaN, and *every comparison against a NaN is
// false* — so the selection rule kept the **first** candidate regardless of its
// quality and returned a NaN verdict **with no error**. The geometry was checked and
// the multiplier was not, so a malformed hitbox slipped past the "the solver refuses
// a malformed hitbox" guarantee (the review's G1).
//
// **Why the hit test validates and the span helpers do not.** The span arithmetic
// (`cs2/aim/ray_span.hpp`) is shared with the occluders, whose behaviour must not
// change, and it answers a different question — *"where does the ray lie inside
// this solid?"* — which is ill-posed for a solid that does not exist: the slab
// clamp compares against a NaN, and a comparison with a NaN is **false**, so a NaN
// half-extent was swallowed and the box degenerated into an unbounded solid (a hit
// at distance 0, and a silent 100 % hit chance on a target that cannot be hit — the
// review's M5). The hitbox is where the geometry is *interpreted*, so that is where
// it is validated.
//
// A caller that reads hitboxes from the game (`payload/aim/src/reader.cpp`, which
// does not validate the shape it reads) can filter with this before building the
// input; the solver refuses a scene that still holds one.
[[nodiscard]] auto hitbox_valid(const Hitbox& hitbox) noexcept -> bool;

// The distance from the eye to the **nearest** point of `hitbox` on the ray
// `eye + t·direction` (unit `direction`), or nullopt when the ray misses the hitbox
// or the hitbox lies entirely behind the eye.
//
// Exact: the hitbox is a capsule (the real studio hitbox, `docs/re/aim.md`) or an
// oriented box, and the span arithmetic is the same exact convex arithmetic the
// occluders use (`cs2/aim/ray_span.hpp`).
//
// **A degenerate hitbox is rejected** (nullopt), never turned into a hit: a NaN or
// an inf is not a solid, and a zero or negative radius is not a capsule.
[[nodiscard]] auto ray_hitbox(const Vec3& eye, const Vec3& direction, const Hitbox& hitbox) noexcept
    -> std::optional<float>;

// --- The solver ---------------------------------------------------------------------

// A candidate target's remaining health.
struct TargetHealth {
  int index = -1;  ///< the `Hitbox::target` index
  float health = 0.0F;
};

// The solver's input. The solver is **pure**: everything it needs is here.
struct MonteCarloInput {
  Vec3 eye{};
  Basis basis{};
  // Every candidate target's hitboxes, in world space. The **nearest** hit among them
  // wins — that is the point of the nearest-hit semantics: the field's cells overlap,
  // so the Gaussian's per-cell sum double-counts (`docs/known-issues.md` §11.2).
  std::span<const Hitbox> hitboxes{};
  // The remaining health of each candidate target, for the can-kill test.
  std::span<const TargetHealth> targets{};
  WeaponProfile weapon{};
  SpreadKernel kernel{};
  SeedContext seed{};
  int samples = 64;  ///< the sample count (the community's 64 / 128 / 256)
};

// The candidate budget: the **bounded** number of aims the search evaluates.
//
// **Bounded on purpose.** The field can hold hundreds of cells (the saved live set has
// 717) and every candidate costs `samples · pellets · hitboxes` ray tests, so the
// candidate count must not be a function of the field's size. The budget is
// `max_cells` cells (the highest-valued ones) times two candidates each — the cell's
// Chebyshev centre and its centroid, the same two the existing solver's
// `field_candidates` uses (`payload/aim/src/field.cpp`) — capped at `max_candidates`.
struct CandidateBudget {
  int max_cells = 16;
  int max_candidates = 64;
};

// Draw and test **one** sample: the engine's offsets for `seed` and the outcome.
//
// **The refusal is here too.** A sample for a weapon whose pellet pattern is not
// modelled is an **error**, not a `Sample`: the standard pattern must never be
// silently substituted for a shotgun's (`docs/orchestration/spec-t34-montecarlo.md`
// §2.2). The error channel is what makes that guarantee checkable — a caller that
// ignores the error still has no number.
//
// A scene whose hitboxes are **not well-formed solids** is refused the same way
// (`MonteCarloError::invalid_geometry`): the geometry is checked once, before the
// samples are drawn.
[[nodiscard]] auto evaluate_sample(const MonteCarloInput& input, const Vec2& aim,
                                   std::int32_t seed) noexcept
    -> std::expected<Sample, MonteCarloError>;

// Evaluate one candidate aim: `N` samples, each scored against the hitboxes.
//
// `probability = hits / N`. `expected_damage` is the mean damage over **all** the
// samples (a miss contributes 0). A multi-pellet sample hits when **any** of its
// pellets hits — the union, not the sum (`docs/research/aim-objective-math.md` §6).
//
// **An unmodelled weapon's pattern is refused** (`MonteCarloError::unsupported_pattern`),
// exactly as `evaluate_sample` refuses it: this entry point is the cheap, natural
// one for the next task to call, so it must not be the one that leaks a number.
//
// **A scene with a degenerate hitbox is refused** (`MonteCarloError::invalid_geometry`)
// for the same reason: the geometry is checked once, before the samples are drawn.
[[nodiscard]] auto evaluate_hit_chance(const MonteCarloInput& input, const Vec2& aim) noexcept
    -> std::expected<HitChance, MonteCarloError>;

// The solver's choice.
struct MonteCarloChoice {
  bool valid = false;
  Vec2 aim{};             ///< the chosen chart point
  HitChance chance{};     ///< its verdict
  int target = -1;        ///< the target of the nearest hit
  int hitgroup = 0;       ///< the hitgroup of the nearest hit
  bool can_kill = false;  ///< does the damage attributed to the target reach the
                          ///< target's remaining health?
};

// The search: the bounded evaluation over the candidate set and the **lexicographic**
// selection rule (the community's, `docs/research/aim-objective-math.md` §2):
//
//   1. a candidate that **can kill** (the damage **attributed to its target**
//      reaches that target's remaining health) beats one that cannot;
//   2. then the **highest probability of any hit**;
//   3. then the **hitgroup preference** (head before body);
//   4. then the highest expected damage (the final tie-break).
//
// The candidate set is the budgeted set of the field's best cells' Chebyshev centres
// and centroids (`CandidateBudget`).
[[nodiscard]] auto solve_montecarlo(const MonteCarloInput& input, std::span<const Cell> cells,
                                    const CandidateBudget& budget = {}) noexcept
    -> std::expected<MonteCarloChoice, MonteCarloError>;

// The hitgroup preference: the **lower the better**.
//
// The repo has no ordered hitgroup list of its own — the "head beats body" test in
// `payload/aim/tests/test_aim_solver.cpp` works through the *damage multiplier*, not
// through an order — so this module states the order it uses: the head first, then the
// chest, the stomach, the generic group (the neck and the pelvis), the arms and the
// legs.
[[nodiscard]] auto hitgroup_preference(int group) noexcept -> int;

}  // namespace cs2::aim
