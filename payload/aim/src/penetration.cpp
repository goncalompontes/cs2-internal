#include "cs2/aim/penetration.hpp"

#include <algorithm>
#include <cmath>

namespace cs2::aim {
namespace {

// The `CHAR_TEX_*` game-material ids the CS2 penetration rules special-case
// (`CGameSurfaceProperties.gamematerial`, the short at record `+0x14`):
//   'G' 71 = grate, 'Y' 89 = glass, 'U' 85 = cardboard, 'W' 87 = wood,
//   'L' 76 = plastic. Live-verified against the game's surface table
//   (`docs/re/penetration.md`).
constexpr int k_tex_grate = 71;      // 'G'
constexpr int k_tex_glass = 89;      // 'Y'
constexpr int k_tex_cardboard = 85;  // 'U'
constexpr int k_tex_wood = 87;       // 'W'
constexpr int k_tex_plastic = 76;    // 'L'

// The entering damage fraction the engine applies per wall (0.16, 0.05 through a
// grate/glass, 1e-5 through a grate-contents span) — `FUN_1808a1820`.
constexpr float k_entry_loss_default = 0.16F;
constexpr float k_entry_loss_grate = 0.05F;
constexpr float k_entry_loss_grate_contents = 1.0e-5F;

// A wall's resolved penetration parameters.
struct WallParams {
  float combined = 1.0F;        // the combined penetration (distance) modifier
  float entry_fraction = k_entry_loss_default;
};

// Resolve a wall's combined modifier and entering fraction exactly as
// `HandleBulletPenetration` (`FUN_1808a1820`) does (docs/re/penetration.md).
[[nodiscard]] auto wall_params(const PenetrationLayer& layer) noexcept -> WallParams {
  // CS2: the combined modifier is the **minimum** over the surfaces crossed by the
  // span (the engine takes the min of `bulletPenetrationDistanceModifier`, +0x08).
  WallParams params;
  params.combined =
      std::min(layer.enter.penetration_modifier, layer.exit.penetration_modifier);
  const bool grate_material =
      layer.enter.material == k_tex_grate || layer.enter.material == k_tex_glass;
  // The special cases only apply when the entry and exit materials match.
  if (layer.enter.material >= 0 && layer.enter.material == layer.exit.material) {
    if (layer.enter.material == k_tex_cardboard || layer.enter.material == k_tex_wood) {
      params.combined = 3.0F;
    } else if (layer.enter.material == k_tex_plastic) {
      params.combined = 2.0F;
    }
    if (layer.thickness >= 6.0F) {
      if (layer.enter.grate) {
        params.combined = 3.0F;
      }
    } else {
      if (grate_material) {
        params.entry_fraction = k_entry_loss_grate;
        params.combined = 3.0F;
      }
      if (layer.enter.grate) {
        params.combined = 32.0F;
        params.entry_fraction = k_entry_loss_grate_contents;
      }
    }
  }
  return params;
}

}  // namespace

auto penetration_layer_damage(float damage, float weapon_penetration,
                             const PenetrationLayer& layer) noexcept -> std::optional<float> {
  if (damage <= 0.0F) {
    return std::nullopt;
  }
  // The engine's entry gate: a surface whose distance modifier is < 0.1 cannot be
  // penetrated at all (`FUN_1808a1820` sets the penetration count to 0).
  if (layer.enter.penetration_modifier < 0.1F) {
    return std::nullopt;
  }
  const WallParams params = wall_params(layer);
  const float inverse = params.combined > 0.0F ? 1.0F / params.combined : 1.0e30F;
  const float power = std::max(0.0F, (3.0F / std::max(weapon_penetration, 1.0e-6F)) * 1.25F);

  // `lost = t^2 * inverse / 24 + (3/pen * 1.25) * inverse * 3 + entry_fraction * damage`.
  const float thickness = (layer.thickness * layer.thickness) * inverse / 24.0F;
  const float lost = std::max(
      0.0F, thickness + power * inverse * 3.0F + params.entry_fraction * damage);

  const float remaining = damage - lost;
  if (remaining < 1.0F) {
    return std::nullopt;  // the engine drops a shot below 1 damage
  }
  return remaining;
}

auto penetration_after_walls(float damage, const PenetrationParams& params,
                             std::span<const PenetrationLayer> layers) noexcept -> float {
  if (damage <= 0.0F) {
    return 0.0F;
  }
  float remaining = damage;
  float crossed = 0.0F;
  int penetrations = 0;
  for (const PenetrationLayer& layer : layers) {
    if (penetrations >= params.max_penetrations) {
      return 0.0F;
    }
    if (layer.entry > params.max_trace_length) {
      return 0.0F;  // the shot has travelled too far to keep penetrating
    }
    if (crossed + layer.thickness > params.max_total_thickness) {
      return 0.0F;  // the reversed loop's ≈90-unit accumulated-thickness cap
    }
    const std::optional<float> next =
        penetration_layer_damage(remaining, params.weapon_penetration, layer);
    if (!next.has_value()) {
      return 0.0F;
    }
    remaining = *next;
    crossed += layer.thickness;
    ++penetrations;
  }
  return remaining;
}

}  // namespace cs2::aim
