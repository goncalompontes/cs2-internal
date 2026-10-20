#pragma once

// The engine's real bullet penetration / damage model (docs/re/penetration.md).
//
// The Tier-2 occlusion layer (`occlusion.hpp`, `heatmap.cpp`) projects the world
// solids into the chart and knows — per direction — the thickness of every solid
// between the eye and a hitbox (`OccluderCell::ray_span`). Turning that stack of
// crossed walls into the damage the bullet actually deals is the *penetration
// model*, and it is not the naive product `prod(1 - t/pen)` the first pass used.
//
// This is the CS2 `HandleBulletPenetration` (`client.dll` `FUN_1808a1820`,
// `sv_penetration_type 2`, docs/re/penetration.md). Per wall it:
//   * takes the **minimum** `bulletPenetrationDistanceModifier` over the surfaces
//     the span crosses (with the material special cases), and
//   * removes `t^2·(1/m)/24 + (3/pen·1.25)·(1/m)·3 + entry_fraction·damage`,
//     where `entry_fraction` is 0.16 (0.05 through a grate/glass, 1e-5 through a
//     grate-contents span), and
//   * stops the shot when a surface's distance modifier is < 0.1, when the loss
//     would take the damage below 1, or when the 4-wall budget / ≈90-unit crossed
//     thickness cap (`FUN_18089ca80`) is reached.
//
// The per-surface properties come from the game's surface table
// (`CGameSurfaceProperties`: `bulletPenetrationDistanceModifier` +0x08,
// `bulletPenetrationDamageModifier` +0x0C, `gamematerial` +0x14 —
// `docs/re/penetration.md`). Note the engine's per-wall loss uses only the
// **distance** modifier; `damage_modifier` is carried for completeness.

#include <cstdint>
#include <optional>
#include <span>

namespace cs2::aim {

// How a wall stack reduces a hitbox's damage (`docs/re/penetration.md`).
//  - `simple`: the first-pass approximation, `prod(1 - t*mod/m_flPenetration)`.
//  - `engine`: the engine's real per-wall loss (`penetration_after_walls`), with the
//    per-surface penetration/damage modifiers, the 4-wall budget and the ≈90-unit
//    thickness cap.
enum class PenetrationModel : std::uint8_t { simple, engine };

// One surface's penetration properties — the engine's `CGameSurfaceProperties`:
// `bulletPenetrationDistanceModifier`, `bulletPenetrationDamageModifier` and
// `gamematerial` (a `CHAR_TEX_*` id).
struct SurfaceProperties {
  float penetration_modifier = 1.0F;  // bulletPenetrationDistanceModifier
  float damage_modifier = 1.0F;       // bulletPenetrationDamageModifier (unused by the loss)
  int material = -1;                  // gamematerial (CHAR_TEX_*)
  bool grate = false;                 // CONTENTS_GRATE span
};

// One wall the bullet crosses: its thickness in world units and the surfaces it
// enters and exits through. A brush exposes one surface at both interfaces in our
// enumeration, so `exit` defaults to `enter` (set `exit = enter` when unknown).
struct PenetrationLayer {
  float thickness = 0.0F;
  float entry = 0.0F;  // distance from the eye at which the wall starts
  SurfaceProperties enter{};
  SurfaceProperties exit{};
};

// The shot-level budget. Defaults are the engine's: 4 penetrable walls, the
// ~90-unit crossed-thickness cap from the reversed `FUN_18089ca80`, and the
// 3000-unit trace-length cap from `HandleBulletPenetration`.
struct PenetrationParams {
  float weapon_penetration = 1.0F;   // m_flPenetration (the weapon's penetration power)
  int max_penetrations = 4;          // the engine's `penetrate_count` budget
  float max_total_thickness = 90.0F; // FUN_18089ca80's accumulated-thickness cap
  float max_trace_length = 3000.0F;  // HandleBulletPenetration's trace-length cap
  // Temporary arrangement work cap: examine at most this many candidate solids per
  // hitbox, nearest-first (0 = unlimited). A farther solid can still be the first wall on
  // some ray, so this is not safe pruning. `occlude_polygon` reports candidate overflow;
  // the aim solver must fail closed instead of using its partial score.
  // Without it, a position whose cone crosses a dense map exploded into ~15k fragments and
  // ~0.6 s per tick (`docs/ragebot-perf.md` §3.9).
  int max_occluders = 0;
};

// The engine's per-wall damage loss. `damage` is the strength arriving at the wall
// (already range-faded and reduced by earlier walls). Returns the damage that
// survives the wall, or nullopt when the wall stops the bullet (the loss would take
// it below 1).
[[nodiscard]] auto penetration_layer_damage(float damage, float weapon_penetration,
                                            const PenetrationLayer& layer) noexcept
    -> std::optional<float>;

// Apply only wall losses to damage arriving at the first layer. Layers must be sorted
// nearest-first. This helper does not apply free-flight range falloff; use
// `penetration_damage_at_target` for a complete muzzle-to-target shot.
[[nodiscard]] auto penetration_after_walls(float damage, const PenetrationParams& params,
                                           std::span<const PenetrationLayer> layers) noexcept
    -> float;

// Evaluate a complete shot from the muzzle to the target: apply range falloff over each
// free-flight segment, wall loss at each layer, then the final free-flight segment. `damage`
// is unscaled weapon damage at the muzzle; the caller applies hitgroup scaling afterward.
// Armor scaling is not represented by this function.
[[nodiscard]] auto penetration_damage_at_target(float damage, float range_modifier,
                                                float target_distance,
                                                const PenetrationParams& params,
                                                std::span<const PenetrationLayer> layers) noexcept
    -> float;

}  // namespace cs2::aim
