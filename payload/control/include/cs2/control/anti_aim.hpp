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
// `docs/re/strafing.md` §"the silent movement-yaw channel",
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
//     engine's own analog is in unit values (`±1`).
//
//   * **`sv_quantize_movement_input` is on by default** (Valve servers,
//     `docs/re/strafing.md` §"the gate"): the server **rounds** the analog it
//     receives to integers, so only the nine `{-1, 0, +1}²` move states survive.
//     A fractional re-based axis is therefore **not** what the server acts on —
//     the re-base delivers a **digital** pair, and `deliver_axes` reports what
//     that costs (the direction error and the walk-speed factor).
//
// The re-base's contract, in one line: **the wish the engine computes from the
// delivered axes, in the anti-aim frame, points the same way as the wish it
// would have computed from the original axes, in the camera frame** — up to the
// discrete analog set's granularity (22.5 degrees), and exactly for an offset
// that is a multiple of 45 degrees. The host test pins that on the value that
// is actually **delivered**, not on an intermediate one.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "cs2/control/strafe_geometry.hpp"

namespace cs2::control::anti_aim {

// --- The engine's angle range -------------------------------------------------

//: The view pitch's limit (`cl_pitch`): the engine clamps the view angles to
//: ±89 degrees, so a fake pitch outside that range would be corrected by the
//: engine and the anti-aim's model of the command would be wrong.
inline constexpr float k_view_pitch_limit = 89.0F;

//: The **down-pitch threshold**: the camera's forward axis points into the
//: ground past it, its horizontal component collapses and its sign flips at 90
//! degrees (`docs/research/angle-split-in-the-wild.md` §3.2 — the community's
//: `revers = wish_angle.x > 89.f ? -1 : 1`).
//!
//! **The branch is defensive.** The engine clamps the *camera's* pitch to
//! ±89 degrees (`k_view_pitch_limit`), so a live game never reaches it; the
//! host test exercises it as the contract it is (the community's rule).
inline constexpr float k_down_pitch_threshold = 89.0F;

//: The smallest `|cos(pitch)|` the re-base divides by. The anti-aim's own
//: pitch is clamped to ±89 (`k_view_pitch_limit`), so `|cos| >= 0.017` — the
//! guard only exists so a degenerate angle cannot produce a NaN.
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

//: The analog axes the payload writes: `forward` (the command's `forwardmove`)
//! and `left` (the command's `leftmove`, positive to the player's **left**).
//!
//! The **camera frame** is the frame the engine's own input is in: the axes
//! the command carries before the anti-aim touches them. The re-base takes
//! them and returns the same intent in the **anti-aim frame**.
struct Axes {
  float forward{0.0F};
  float left{0.0F};
};

//: The player's held movement intent, expressed in the **camera** frame
//! (`forward` is +1 for `W`, `left` is the engine's side axis, +1 for `A`).
//!
//! **Deprecated**: the re-base works on the *command's current axes* — what
//! wrote them (the engine's own input, the auto-strafe's wish, the ragebot's
//! auto-stop) — so that the anti-aim composes with them. This is the thin
//! adapter the payload's old call site uses until it moves over.
struct MoveIntent {
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

//: The length of a 2-D vector.
[[nodiscard]] inline auto length(Vec2 value) noexcept -> float {
  return std::sqrt(value.x * value.x + value.y * value.y);
}

//: The angle between two 2-D vectors, in degrees (`0..180`).
[[nodiscard]] inline auto angle_between(Vec2 first, Vec2 second) noexcept -> float {
  const float cross = first.x * second.y - first.y * second.x;
  const float dot = first.x * second.x + first.y * second.y;
  return std::atan2(std::abs(cross), dot) * 180.0F / cs2::control::k_pi;
}

// --- The fake angles -----------------------------------------------------------

//: Clamp a configured pitch to the engine's view-angle range.
[[nodiscard]] inline auto clamp_pitch(float pitch) noexcept -> float {
  return std::clamp(pitch, -k_view_pitch_limit, k_view_pitch_limit);
}

//: A 32-bit avalanche mix (the "lowbias32" finaliser). It is pure integer
//: arithmetic — no state, no library — so the jitter is reproducible on every
//: platform and in the host test.
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

//: The **ideal** re-based axes: the player's intent, taken in the camera frame
//! and expressed in the anti-aim frame. **Unclamped** — the delivered axes are
//! `deliver_axes`'s.
//!
//! The derivation, step by step:
//!
//!  1. **The camera-frame wish.** `engine_wish(camera, axes)` is exactly what
//!     the engine computes with no anti-aim — the direction the player walks
//!     today. It is the thing the re-base has to preserve.
//!
//!  2. **The anti-aim frame.** The engine reads the axes in the anti-aim
//!     frame, so the wish is decomposed into that frame's horizontal basis
//!     (`along` = along the anti-aim's forward, `across` = along its left).
//!
//!  3. **The fake pitch.** `AirMove` multiplies the forward axis by
//!     `cos(anti-aim pitch)` — a down-pitched fake yaw tilts the walk's
//!     forward axis into the ground. Dividing that factor out is what keeps
//!     the *direction* of the wish; without it the operator's `W` would walk at
//!     an angle (the community's "flatten",
//!     `docs/research/angle-split-in-the-wild.md` §3.2).
//!
//!  4. **The down-pitch inversion.** With the camera past 89 degrees the
//!     operator is looking straight down: the camera's forward axis points into
//!     the ground, its horizontal component collapses and its sign flips at 90
//!     degrees. The community's rule (`revers = wish_angle.x > 89.f ? -1 : 1`)
//!     inverts the forward axis there, so the player does not walk backwards.
//!     **Defensive**: the engine clamps the camera's pitch to ±89 degrees, so
//!     the branch is unreachable in a live game.
//!
//!  5. **The ±1 clamp** (`deliver_axes`): the engine's analog is in unit
//!     values (`±1`) and the server rounds it to `{-1, 0, +1}`. The axes are
//!     scaled **together** so that neither exceeds ±1: clipping one axis alone
//!     would tilt the walk (the operator would walk at an angle), which is the
//!     very failure this re-base exists to prevent.
[[nodiscard]] inline auto rebase_axes(Angles camera, Angles anti_aim, Axes camera_axes) noexcept
    -> Axes {
  // 1. The camera-frame wish: what the engine computes with no anti-aim.
  const Vec2 wish = engine_wish(camera, camera_axes.forward, camera_axes.left);

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

  // 4. The down-pitch inversion (the community's rule; defensive — see above).
  if (camera.pitch > k_down_pitch_threshold) {
    axes.forward = -axes.forward;
  }
  return axes;
}

//: The re-base of a **held intent** — the thin adapter for the payload's old
//! call site. The re-base itself works on the command's *current axes*, so the
//! anti-aim composes with the other movement features.
[[nodiscard]] inline auto rebase_axes(Angles camera, Angles anti_aim, MoveIntent intent) noexcept
    -> Axes {
  return rebase_axes(camera, anti_aim, Axes{intent.forward, intent.left});
}

//: The engine's **discrete analog set**: the nine `{-1, 0, +1}²` move states
//! `sv_quantize_movement_input` leaves behind. The server rounds the analog it
//! receives to integers, so a fractional axis is quantised away — the payload
//! has to deliver a member of this set, or the client predicts one thing and
//! the server acts on another (`docs/re/strafing.md` §"the gate").
inline constexpr Axes k_digital_axes[] = {
    {0.0F, 0.0F}, {1.0F, 0.0F},  {-1.0F, 0.0F}, {0.0F, 1.0F},   {0.0F, -1.0F},
    {1.0F, 1.0F}, {1.0F, -1.0F}, {-1.0F, 1.0F}, {-1.0F, -1.0F},
};

//: The **delivery**: the axes the payload writes, and what they cost.
struct Delivery {
  //! The axes to write: a member of the engine's discrete set.
  Axes axes{};
  //! The angle between the delivered wish and the camera's own wish (degrees).
  //! Zero for a multiple of 45 degrees of offset, at most 22.5 degrees for a
  //! well-conditioned re-base, and **not** bounded when the re-base saturates
  //! (see `saturated`).
  float direction_error_deg{0.0F};
  //! The delivered wish's length against the camera's: the factor the
  //! operator's walk speed is multiplied by. Below 1 the walk is slower than it
  //! would be without the anti-aim (a steep fake pitch costs the most).
  float wish_factor{1.0F};
  //! The ideal axes did not fit in ±1: the delivery is a compromise and the
  //! direction is **not** preserved.
  bool saturated{false};
};

//: The **delivered** axes: the re-based intent, clamped to ±1 and snapped to
//! the engine's discrete set.
//!
//! The snap is **direction-optimal**: of the nine digital pairs, the one whose
//! wish points closest to the camera's own wish wins (the magnitude breaks a
//! tie). A per-axis rounding would be wrong — the axes it produced can point
//! up to 30 degrees off — and the *server*'s rounding is not the payload's to
//! choose, so the payload snaps first and the server's rounding becomes a
//! no-op: the client predicts exactly what the server acts on.
//!
//! The cost is reported, not hidden: `direction_error_deg` is the angle the
//! delivered walk direction is off by, `wish_factor` is how much of the
//! walk speed survives, and `saturated` says that the ideal axes did not fit
//! (a steep fake pitch) — in which case the direction is **not** preserved and
//! only the factor says how bad it is.
[[nodiscard]] inline auto deliver_axes(Angles camera, Angles anti_aim, Axes camera_axes) noexcept
    -> Delivery {
  const Vec2 want = engine_wish(camera, camera_axes.forward, camera_axes.left);
  const float want_length = length(want);

  // The ideal axes, then the ±1 clamp: a **uniform** scale, so the direction
  // survives (clipping one axis alone would tilt the walk).
  const Axes ideal = rebase_axes(camera, anti_aim, camera_axes);
  Axes scaled = ideal;
  if (!std::isfinite(scaled.forward)) {
    scaled.forward = 0.0F;
  }
  if (!std::isfinite(scaled.left)) {
    scaled.left = 0.0F;
  }
  const float peak = std::max(std::abs(scaled.forward), std::abs(scaled.left));

  Delivery delivery{};
  delivery.saturated = peak > 1.0F;
  if (peak > 1.0F) {
    const float scale = 1.0F / peak;
    scaled.forward *= scale;
    scaled.left *= scale;
  }

  // No intent: the player holds no movement key, so the delivered axes are the
  // zero pair — the anti-aim must not make a standing player walk.
  if (want_length <= 0.0F) {
    delivery.axes = Axes{};
    delivery.direction_error_deg = 0.0F;
    delivery.wish_factor = 1.0F;
    return delivery;
  }

  // The direction-optimal digital pair.
  Axes best{};
  float best_error = 360.0F;
  float best_length = -1.0F;
  for (const Axes& candidate : k_digital_axes) {
    const Vec2 wish = engine_wish(anti_aim, candidate.forward, candidate.left);
    const float wish_length = length(wish);
    if (wish_length <= 0.0F) {
      continue;  // the zero pair carries no direction
    }
    const float error = want_length <= 0.0F ? 0.0F : angle_between(wish, want);
    const bool better = error < best_error - 1.0e-4F ||
                        (error <= best_error + 1.0e-4F && wish_length > best_length);
    if (better) {
      best = candidate;
      best_error = error;
      best_length = wish_length;
    }
  }
  delivery.axes = best;
  delivery.direction_error_deg = best_error > 360.0F ? 0.0F : best_error;
  delivery.wish_factor =
      want_length <= 0.0F ? 1.0F : (best_length < 0.0F ? 0.0F : best_length / want_length);
  return delivery;
}

//: Snap an analog axis to the engine's discrete set (`{-1, 0, +1}`). The
//: server's `sv_quantize_movement_input` rounds the value it receives to an
//: integer, so a fractional axis is quantised away.
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
