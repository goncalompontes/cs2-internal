#pragma once

// The anti-aim's **decision maths**: the fake (desync) yaw, the fake pitch, the
// per-command jitter, and the **movement re-base** that keeps the player's
// W/A/S/D camera-relative while the command carries the fake angles.
//
// Why a module of its own: the maths is what makes the feature *correct*, and it
// has to be checkable **without a game**. This header is pure — no engine
// headers, no `<windows.h>`, no globals, no state — so the host test
// (`control-anti-aim`) drives it directly.
//
// The engine facts it encodes (`docs/re/angle-split.md` §3 and §5.3,
// `docs/re/strafe-sign.md` §2-§3):
//
//   * **Two independent angle channels.** The **camera** reads the *live* view
//     angles (`CCSGOInput + 0x688`); the anti-aim writes the **command's own**
//     `viewangles` (`CBaseUserCmdPB::viewangles`, `base + 0x40`). They are
//     different fields with different owners, so the camera cannot follow the
//     fake angles — the split needs **no camera pin**.
//
//   * **The movement's wish basis is the command's angles.**
//     `CPlayer_MovementServices::SetupMove` (`0xa74890`) copies
//     `base->viewangles` into the `CMoveData`'s angles (`+0x14/+0x18`) and
//     `AirMove` (`0x8e57b0`) builds
//     `wish = forward * forwardmove + left * leftmove` from them. A fake yaw
//     therefore **rotates the walk**: without the re-base the operator's `W`
//     walks along the fake yaw, not along the camera.
//
//   * **`AngleVectors` (roll 0)** — the engine's own basis:
//     `forward = (cos p cos y, cos p sin y, -sin p)` and the second output
//     `left = (-sin y, cos y, 0)`: the player's **left**, and horizontal
//     whatever the pitch is.
//
//   * **The side axis** (`leftmove`) is positive to the **left**, and the
//     engine's own analog is in unit values (`±1`), so the re-based axes are
//     clamped to `±1` and the server's `sv_quantize_movement_input` rounds
//     them to `{-1, 0, +1}`.
//
// The re-base's contract, in one line: **the wish the engine computes from the
// re-based axes, in the anti-aim frame, is the wish it would have computed
// from the original axes, in the camera frame.** The host test pins exactly
// that.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "cs2/control/strafe_geometry.hpp"

namespace cs2::control::anti_aim {

// --- The engine's angle range -------------------------------------------------

//: The view pitch's limit (`cl_pitch`): the engine clamps the view angles to
//: ±89 degrees, so a fake pitch outside that range would be corrected by the
//: engine and the anti-aim's model of the command would be wrong.
inline constexpr float k_max_pitch = 89.0F;

//: The **down-pitch threshold**: past it the camera's forward axis points into
//: the ground, its horizontal component collapses and its sign flips at 90
//: degrees (`docs/research/angle-split-in-the-wild.md` §3.2 — the community's
//: `revers = wish_angle.x > 89.f ? -1 : 1`).
inline constexpr float k_down_pitch = 89.0F;

//: The smallest `|cos(pitch)|` the re-base divides by. The anti-aim's own
//: pitch is clamped to ±89 (`k_max_pitch`), so `|cos| >= 0.017` — the guard
//! only exists so a degenerate angle cannot produce a NaN.
inline constexpr float k_min_cos_pitch = 1.0e-3F;

// --- The value types -----------------------------------------------------------

//: The engine's view angles (degrees).
struct Angles {
  float pitch{0.0F};
  float yaw{0.0F};
  float roll{0.0F};
};

//: A 2-D world vector — the walk lives in the XY plane.
struct Vec2 {
  float x{0.0F};
  float y{0.0F};
};

//: A 3-D world vector.
struct Vec3 {
  float x{0.0F};
  float y{0.0F};
  float z{0.0F};
};

//: The engine's view-frame basis: the forward vector and the second basis
//: output — the player's **left**.
struct Basis {
  Vec3 forward{};
  Vec3 left{};
};

//: The player's held movement intent, expressed in the **camera** frame.
//!
//! `forward` is +1 for `W` and -1 for `S`; `left` is the engine's side axis
//! (+1 is the player's **left**, i.e. `A`). The axes are in the engine's own
//! unit values (`±1`).
struct MoveIntent {
  float forward{0.0F};
  float left{0.0F};
};

//: The analog axes the payload writes: `forward` (the command's `forwardmove`)
//! and `left` (the command's `leftmove`, positive to the player's **left**).
struct Axes {
  float forward{0.0F};
  float left{0.0F};
};

//: The side the anti-aim puts the fake yaw on.
enum class Side : std::int8_t {
  right = -1,
  automatic = 0,
  left = 1,
};

// --- The engine's model --------------------------------------------------------

//: The engine's view-frame basis (`AngleVectors`, roll 0). The roll is ignored:
//: CS2 has no roll angle (`docs/research/angle-split-in-the-wild.md` §3.3).
[[nodiscard]] inline auto angle_vectors(Angles angles) noexcept -> Basis {
  const float pitch = angles.pitch * cs2::control::k_deg_to_rad;
  const float yaw = angles.yaw * cs2::control::k_deg_to_rad;
  const float sin_pitch = std::sin(pitch);
  const float cos_pitch = std::cos(pitch);
  const float sin_yaw = std::sin(yaw);
  const float cos_yaw = std::cos(yaw);
  Basis basis{};
  basis.forward = Vec3{cos_pitch * cos_yaw, cos_pitch * sin_yaw, -sin_pitch};
  basis.left = Vec3{-sin_yaw, cos_yaw, 0.0F};
  return basis;
}

//: The engine's **planar wish** (`AirMove`): the direction the player walks
//! when the command carries `angles` and the axes `forward`/`left`.
//!
//! This is the reference model the whole feature is measured against: it is
//! what `AirMove` (`0x8e57b0`) computes from the `CMoveData`'s angles and its
//! two analog axes.
[[nodiscard]] inline auto engine_wish(Angles angles, float forward, float left) noexcept -> Vec2 {
  const Basis basis = angle_vectors(angles);
  return Vec2{basis.forward.x * forward + basis.left.x * left,
              basis.forward.y * forward + basis.left.y * left};
}

// --- The fake angles -----------------------------------------------------------

//: Clamp a configured pitch to the engine's view-angle range.
[[nodiscard]] inline auto clamp_pitch(float pitch) noexcept -> float {
  return std::clamp(pitch, -k_max_pitch, k_max_pitch);
}

//: A 32-bit avalanche mix (the "lowbias32" finaliser). It is pure integer
//! arithmetic — no state, no library — so the jitter is reproducible on every
//! platform and in the host test.
[[nodiscard]] inline auto mix32(std::uint32_t value) noexcept -> std::uint32_t {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

//: The per-command **jitter**: a pseudo-random offset in `[-amplitude,
//! amplitude]`, determined by the seed and the **tick**.
//!
//! The tick is the command's own sequence number, so the offset is **the same
//! for every write of that command** — the jitter is re-rolled once per
//! command (the engine writes the command's `viewangles` once per tick), never
//! per frame. A higher-frequency writer would fight the engine's own
//! write-back (`docs/re/angle-split.md` §7 step 6).
[[nodiscard]] inline auto jitter_offset(std::uint32_t seed, std::uint64_t tick,
                                        float amplitude) noexcept -> float {
  if (!(amplitude > 0.0F)) {
    return 0.0F;
  }
  const auto hash = mix32(seed ^ mix32(static_cast<std::uint32_t>(tick)));
  // The low 24 bits map onto [0, 1]; the offset is symmetric around zero.
  const float unit = static_cast<float>(hash & 0x00ffffffu) / 16777215.0F;
  return (unit * 2.0F - 1.0F) * amplitude;
}

//: The sign the fake yaw's offset takes: the configured side, or — for the
//! **automatic** side — the command's parity, so the desync wobbles around the
//! camera's yaw instead of sitting on one fixed side.
[[nodiscard]] inline auto side_sign(Side side, std::uint64_t tick) noexcept -> float {
  switch (side) {
    case Side::left:
      return 1.0F;
    case Side::right:
      return -1.0F;
    case Side::automatic:
      break;
  }
  return (tick % 2 == 0) ? 1.0F : -1.0F;
}

//: The **fake yaw** the anti-aim writes: the camera's own yaw (the *base
//! angle*) plus the configured offset, on the chosen side, plus the jitter.
[[nodiscard]] inline auto fake_yaw(float camera_yaw, float yaw_offset, Side side,
                                   std::uint32_t seed, std::uint64_t tick, float jitter) noexcept
    -> float {
  const float offset =
      side_sign(side, tick) * std::abs(yaw_offset) + jitter_offset(seed, tick, jitter);
  return cs2::control::normalize_angle(camera_yaw + offset);
}

// --- The movement re-base ------------------------------------------------------

//: The **re-based analog axes**: the player's intent, taken in the camera frame
//! and expressed in the anti-aim frame.
//!
//! The derivation, step by step:
//!
//!  1. **The camera-frame wish.** `engine_wish(camera, intent)` is exactly what
//!     the engine would compute with no anti-aim — the direction the player
//!     walks today. It is the thing the re-base has to preserve.
//!
//!  2. **The anti-aim frame.** The engine will read the axes in the anti-aim
//!     frame, so the wish is decomposed into that frame's horizontal basis
//!     (`along` = along the anti-aim's forward, `across` = along its left).
//!
//!  3. **The fake pitch.** `AirMove` multiplies the forward axis by
//!     `cos(anti-aim pitch)` — a down-pitched fake yaw tilts the walk's
//!     forward axis into the ground. Dividing that factor out is what keeps
//!     the *direction* of the wish; without it the operator's `W` would walk at
//!     an angle (the community's "flatten", `docs/research/angle-split-in-the-wild.md`
//!     §3.2).
//!
//!  4. **The down-pitch inversion.** With the camera past 89 degrees the
//!     operator is looking straight down: the camera's forward axis points into
//!     the ground, its horizontal component collapses and its sign flips at 90
//!     degrees. The community's rule (`revers = wish_angle.x > 89.f ? -1 : 1`)
//!     inverts the forward axis there, so the player does not walk backwards.
//!
//!  5. **The ±1 clamp.** The engine's analog is in unit values (`±1`) and the
//!     server rounds it to `{-1, 0, +1}` (`sv_quantize_movement_input`). The
//!     axes are scaled **together** so that neither exceeds ±1: clipping one
//!     axis alone would tilt the walk (the operator would walk at an angle),
//!     which is the very failure this re-base exists to prevent. A uniform
//!     scale keeps the direction and costs magnitude — and the magnitude is
//!     quantised away by the server anyway.
[[nodiscard]] inline auto rebase_axes(Angles camera, Angles anti_aim, MoveIntent intent) noexcept
    -> Axes {
  // 1. The camera-frame wish: what the engine computes with no anti-aim.
  const Vec2 wish = engine_wish(camera, intent.forward, intent.left);

  // 2. The anti-aim frame's horizontal basis.
  const float yaw = anti_aim.yaw * cs2::control::k_deg_to_rad;
  const float forward_x = std::cos(yaw);
  const float forward_y = std::sin(yaw);
  const float left_x = -std::sin(yaw);
  const float left_y = std::cos(yaw);
  const float along = wish.x * forward_x + wish.y * forward_y;
  const float across = wish.x * left_x + wish.y * left_y;

  // 3. The fake pitch's factor on the forward axis.
  const float cos_pitch = std::cos(anti_aim.pitch * cs2::control::k_deg_to_rad);
  const float denominator = (std::abs(cos_pitch) < k_min_cos_pitch)
                                ? (cos_pitch < 0.0F ? -k_min_cos_pitch : k_min_cos_pitch)
                                : cos_pitch;

  Axes axes{};
  axes.forward = along / denominator;
  axes.left = across;

  // 4. The down-pitch inversion (the community's rule).
  if (camera.pitch > k_down_pitch) {
    axes.forward = -axes.forward;
  }

  // 5. The ±1 clamp — a uniform scale, so the direction survives.
  if (!std::isfinite(axes.forward)) {
    axes.forward = 0.0F;
  }
  if (!std::isfinite(axes.left)) {
    axes.left = 0.0F;
  }
  const float peak = std::max(std::abs(axes.forward), std::abs(axes.left));
  if (peak > 1.0F) {
    const float scale = 1.0F / peak;
    axes.forward *= scale;
    axes.left *= scale;
  }
  return axes;
}

//: Snap an analog axis to the engine's discrete set (`{-1, 0, +1}`). The
//! server's `sv_quantize_movement_input` rounds the value it receives to an
//! integer, so a fractional axis is quantised away — the payload writes the
//! snapped value so the client and the server agree.
[[nodiscard]] inline auto quantize_axis(float value) noexcept -> float {
  if (!std::isfinite(value)) {
    return 0.0F;
  }
  return std::clamp(std::round(value), -1.0F, 1.0F);
}

//: Snap both axes to the engine's discrete set.
[[nodiscard]] inline auto quantize_axes(Axes axes) noexcept -> Axes {
  axes.forward = quantize_axis(axes.forward);
  axes.left = quantize_axis(axes.left);
  return axes;
}

}  // namespace cs2::control::anti_aim
