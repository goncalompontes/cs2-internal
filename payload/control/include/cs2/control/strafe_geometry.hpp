#pragma once

// The auto-strafe's **decision maths**: the travel heading the player's WASD
// asks for, the side of the velocity the wish is pushed on, and the
// `AirAccelerate` optimum the wish sits at.
//
// Why a module of its own: this used to live inline in `Input::strafe_tick`, a
// 150-line member function that could only be exercised in a live game. The
// decision is a pure function of the command's button mask, the camera yaw, the
// velocity and the previous side, so it belongs in a module that depends on
// nothing but the standard library — the host test then covers the goal
// formula, the dead zone and the optimum without a game.
//
// The engine conventions encoded here (`docs/re/strafing.md`):
//   * `forward(yaw) = (cos yaw, sin yaw)`; the engine's **second** view-frame
//     basis vector is `(-sin yaw, +cos yaw)` — the forward vector rotated
//     **+90 degrees**, i.e. the player's **left**;
//   * the engine's side axis (the command's `leftmove` field) is positive to
//     the player's **left** — see the convention constant below;
//   * the wish is pushed **perpendicular** to the velocity (at the
//     `AirAccelerate` optimum `theta*`), so the velocity curves toward the goal
//     and re-perpendicularises every tick instead of decaying.
//
// The module also owns the engine's **button bits and their names** (the
// `IN_*` masks and the name -> bit map the payload's button mask uses), so the
// host test can pin them without the engine.
//
// This header must stay dependency-free: no engine headers, no `<windows.h>`.
// It is compiled into the host test and into the payload.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cs2::control {

// --- The engine's button bits -----------------------------------------------
//
// They live here rather than in `input.hpp` so this header stays
// self-contained: the pure maths decodes a command's mask without dragging the
// engine in. The names are the engine's own `IN_*` bits (`docs/re/subtick.md`
// §"The command build"); the source of the mask is the `CUserCmd` button word
// at `cmd + 0x60`.
//
// **The engine's own button-name table is the authority for the bits**
// (`client.dll` build 25947352, sha256 `659940c1…`, image base `0x180000000`:
// the table at `0x181b28cf0`, the name strings at `0x181b28f70` — 20 records
// of `{const char* name; uint64_t bit;}`). The engine uses that very table to
// force a button from the console (`in_forceinput <IN_NAME> 0/1`,
// `client.dll` `0x180b85b80`), so the bits below are the ones the engine
// itself puts in a command's button mask — and the host test pins the whole
// table as data (`test_control_strafe.cpp`).
//
// The table spells them the way CS2 does: the bits 7/8 are `IN_TURNLEFT` /
// `IN_TURNRIGHT` (the **look**) and 9/10 are `IN_MOVELEFT` / `IN_MOVERIGHT`
// (the **strafe**). There is no `IN_LEFT`/`IN_RIGHT` in CS2.
//
// The bits this build does **not** have are gone from CS2: the Source-1
// `IN_CANCEL` (6), `IN_RUN` (12), `IN_ALT1` (14), `IN_ALT2` (15) and
// `IN_WALK` (18) have no name anywhere in `client.dll`, so a constant for one
// of them would be a bit the engine never reads. The community's CS2 SDK
// header still lists them — and still has the old `IN_SCORE` 16 / `IN_ZOOM`
// 19, which is where the wrong bits in this file used to come from: **the
// engine's table is the truth, not the SDK header.**
namespace button {
inline constexpr std::uint64_t k_attack = 1ull << 0;      // IN_ATTACK
inline constexpr std::uint64_t k_jump = 1ull << 1;        // IN_JUMP
inline constexpr std::uint64_t k_duck = 1ull << 2;        // IN_DUCK
inline constexpr std::uint64_t k_forward = 1ull << 3;     // IN_FORWARD
inline constexpr std::uint64_t k_back = 1ull << 4;        // IN_BACK
inline constexpr std::uint64_t k_use = 1ull << 5;         // IN_USE
inline constexpr std::uint64_t k_turnleft = 1ull << 7;    // IN_TURNLEFT
inline constexpr std::uint64_t k_turnright = 1ull << 8;   // IN_TURNRIGHT
inline constexpr std::uint64_t k_moveleft = 1ull << 9;    // IN_MOVELEFT
inline constexpr std::uint64_t k_moveright = 1ull << 10;  // IN_MOVERIGHT
inline constexpr std::uint64_t k_attack2 = 1ull << 11;    // IN_ATTACK2
inline constexpr std::uint64_t k_reload = 1ull << 13;     // IN_RELOAD
inline constexpr std::uint64_t k_speed = 1ull << 16;      // IN_SPEED
inline constexpr std::uint64_t k_score = 1ull << 33;      // IN_SCORE
inline constexpr std::uint64_t k_zoom = 1ull << 34;       // IN_ZOOM
}  // namespace button

// --- The engine's button names -----------------------------------------------
//
// The names the payload's button mask accepts, and the bit each one drives.
// The **engine's own** names come first, and they are the ones CS2 uses:
//
//   * the **strafe** actions are named `left` / `right` — `client.dll` registers
//     them (`FUN_1800d0980` and the table at `0x181c60548`ff) and the engine's
//     own input forms the lateral axis as `value("left") - value("right")`
//     (RVA `0xb8f6e0`); they drive `IN_MOVELEFT` / `IN_MOVERIGHT`;
//   * the **look** actions are named `turnleft` / `turnright` (the strings at
//     `0x1c60458` / `0x1c60468`); they drive `IN_TURNLEFT` / `IN_TURNRIGHT`.
//
// The console commands are the action names with a `+` in front — CS2's own
// shipped binds (`game/csgo/cfg/user_keys_default.vcfg`) are `"a" -> "+left"`,
// `"d" -> "+right"`, `"LEFTARROW" -> "+turnleft"`. There is **no**
// `+moveleft` / `+moveright` in CS2 (the strings do not exist in `client.dll`),
// so a name the engine does not know is a name the console path cannot use.
//
// The remaining entries are the runner's friendly aliases (`a`, `d`, `mouse1`,
// ...) — they resolve to the same bits.
struct NamedButton {
  std::string_view name;
  std::uint64_t bit;
};

//: The name -> bit map of the payload's button mask.
inline constexpr NamedButton k_named_buttons[] = {
    {"attack", button::k_attack},
    {"mouse1", button::k_attack},
    {"jump", button::k_jump},
    {"space", button::k_jump},
    {"duck", button::k_duck},
    {"crouch", button::k_duck},
    {"ctrl", button::k_duck},
    {"forward", button::k_forward},
    {"w", button::k_forward},
    {"back", button::k_back},
    {"s", button::k_back},
    {"use", button::k_use},
    {"e", button::k_use},
    {"left", button::k_moveleft},
    {"right", button::k_moveright},
    {"moveleft", button::k_moveleft},
    {"a", button::k_moveleft},
    {"moveright", button::k_moveright},
    {"d", button::k_moveright},
    {"turnleft", button::k_turnleft},
    {"turnright", button::k_turnright},
    {"attack2", button::k_attack2},
    {"mouse2", button::k_attack2},
    {"reload", button::k_reload},
    {"r", button::k_reload},
    // `IN_SPEED` is the **sprint** key: the engine's own `sprint` action
    // carries that mask (the console command object at `client.dll`
    // `0x182233d80`, the mask at `0x182233d80`), and CS2's shipped binds put
    // `"SHIFT" -> "+sprint"`. The Source-1 `IN_WALK` / `IN_RUN` are not in
    // this build at all, so there is no "walk" (or "run") button to name.
    {"speed", button::k_speed},
    {"sprint", button::k_speed},
    {"shift", button::k_speed},
    {"zoom", button::k_zoom},
};

//: Resolve a friendly or engine button name to its `IN_*` bit; 0 when the name
//: is unknown.
[[nodiscard]] inline auto button_bit(std::string_view name) noexcept -> std::uint64_t {
  for (const NamedButton& entry : k_named_buttons) {
    if (entry.name == name) {
      return entry.bit;
    }
  }
  return 0;
}

// --- The backend's button gate ------------------------------------------------
//
// The control channel's `press`/`release` commands are a **backend choice**:
// the `usercmd` backend writes the command's button mask itself, while the
// `console` backend drives the engine's own `+button` commands — so in that
// backend the command's mask must be left alone.
//
// The **payload features** are not a backend choice: the triggerbot, the
// ragebot's fire and the movement assists are owned by the overlay menu
// (ADR-0021), they have **no console equivalent**, and they must act whatever
// the backend is.
//
// The triggerbot is the case that proves it. Its press used to travel through
// the control channel's mask, which the `console` backend (the **default**)
// never applies to the command: the trigger decided to fire, counted the shot
// and answered `{"firing": true}` — and the gun stayed silent. The failure is
// invisible to the operator, so the decision is pinned here, in one pure
// function, where the host test can reach it (the live behaviour needs a
// game).
enum class ButtonPath {
  control_command,  // a `press`/`release` control command
  feature,          // a payload feature's own button (the triggerbot)
};

//: Whether a button held through `path` reaches the command's button mask.
//
// A **control command's** button reaches the command only in the `usercmd`
// backend. A **feature's** button reaches it in **every** backend: the
// features have no console equivalent, and the ragebot's own fire has always
// been written that way.
[[nodiscard]] inline constexpr auto button_reaches_command(ButtonPath path,
                                                           bool usercmd_backend) noexcept -> bool {
  switch (path) {
    case ButtonPath::control_command:
      return usercmd_backend;
    case ButtonPath::feature:
      break;
  }
  return true;
}

//: The command's button mask: the engine's own state, the control command's
//: buttons and the features' buttons.
//
// The features' bits are only ever **set**, never cleared: a feature that
// releases the attack button must not take the player's own press of the same
// button away.
[[nodiscard]] inline constexpr auto compose_button_mask(
    std::uint64_t engine_mask, std::uint64_t control_set, std::uint64_t control_clear,
    std::uint64_t feature_mask, bool usercmd_backend) noexcept -> std::uint64_t {
  std::uint64_t value = engine_mask;
  if (button_reaches_command(ButtonPath::control_command, usercmd_backend)) {
    value = (value | control_set) & ~control_clear;
  }
  if (button_reaches_command(ButtonPath::feature, usercmd_backend)) {
    value |= feature_mask;
  }
  return value;
}

// --- Constants ---------------------------------------------------------------

//: Pi, for the degrees <-> radians conversions.
inline constexpr float k_pi = 3.14159265358979323846F;
//: Radians per degree.
inline constexpr float k_deg_to_rad = k_pi / 180.0F;

//: The dead zone of the side decision, in degrees. Inside it the strafe keeps
//: the side of the last decisive tick instead of re-deciding it. Holding `W`
//: alone puts the goal exactly on the velocity heading, and the tie-break
//: there would flip the curve every tick — `W` would curve one way and `W`
//: plus a hair of `D` the other. 20 degrees sits below the 45 degrees a
//: diagonal key press moves the goal, so a deliberate `A`/`D` still flips the
//: side, and well above the per-tick drift of the velocity heading, so the
//: curve does not chatter.
inline constexpr float k_strafe_side_deadzone_deg = 20.0F;

//: The smallest wish angle the strafe writes, in degrees. Below it the wish is
//: parallel to the velocity and gains nothing, so it is clamped away.
inline constexpr float k_strafe_min_theta_deg = 1.0F;

//: The largest wish angle: a purely perpendicular push.
inline constexpr float k_strafe_max_theta_deg = 90.0F;

// --- The engine's side-axis convention ----------------------------------------
//
// **This is the one place the convention is recorded.** Every use of the
// side-axis sign in this module goes through it, so a change is a deliberate
// edit to this constant — and the host test fails when it moves.
//
// What the engine actually does (established from the engine's own code on the
// live build 25947352, `client.dll` sha256 `659940c1…`, image base
// `0x180000000`; the full derivation with the addresses is in
// `docs/re/strafe-sign.md`):
//
//   * the view-frame basis function (`AngleVectors`, RVA `0x173cb90`, an
//     identical copy at `0x1742af0`) returns the forward vector
//     `(cos yaw, sin yaw)` and a **second** vector
//     `(-sin yaw, +cos yaw)` — the forward vector rotated **+90 degrees**;
//   * `AirMove` (RVA `0x8e57b0`) builds the wish as
//     `wish = forward * movedata[0x2c] + basis * movedata[0x30]`, i.e. the
//     side axis is multiplied by that **second** vector;
//   * the side field is the command's `leftmove` (the movement services' schema
//     calls it `m_flCmdLeftMove`), and the engine's own input forms the lateral
//     axis as `value("left") - value("right")` (RVA `0xb8f6e0`), so a
//     **positive** side axis is the player's **left**.
//
// The two halves are coupled: a side axis that is positive to the left can
// only be multiplied by the +90-degree (left) vector, or the engine's own `A`
// key would strafe right.
//
// `true` is the established convention. Flip it **only** if a live check
// contradicts the derivation (the check is in `docs/re/strafe-sign.md`), and
// flip it here *and* in the payload's analog write together — the two are the
// same fact seen from the two ends.
inline constexpr bool k_engine_side_axis_positive_is_left = true;

//: A 2-D world vector.
struct Vec2 {
  float x{0.0F};
  float y{0.0F};
};

//: The engine's view-frame basis for a camera yaw: the forward vector and the
//: engine's **second** basis output (the forward vector rotated +90 degrees,
//: i.e. the player's left).
struct ViewBasis {
  Vec2 forward{};
  Vec2 side{};
};

//: The engine's view-frame basis for a camera yaw.
[[nodiscard]] inline auto engine_view_basis(float view_yaw) noexcept -> ViewBasis {
  const float yaw = view_yaw * k_deg_to_rad;
  ViewBasis basis{};
  basis.forward = Vec2{std::cos(yaw), std::sin(yaw)};
  // The engine's second basis output is the forward vector rotated +90 degrees
  // (the player's left) — or -90 degrees (the player's right) if the side axis
  // were positive to the right. The two are the same fact.
  const float sign = k_engine_side_axis_positive_is_left ? 1.0F : -1.0F;
  basis.side = Vec2{-sign * std::sin(yaw), sign * std::cos(yaw)};
  return basis;
}

//: The engine's **side axis** value for a lateral quantity that is positive to
//: the player's right. The engine's field is `leftmove` — positive to the left
//: — so the sign is inverted.
[[nodiscard]] inline auto engine_side_axis(float right) noexcept -> float {
  return k_engine_side_axis_positive_is_left ? -right : right;
}

//: The engine's side-axis value for a wish that sits `rel_deg` degrees
//: **counter-clockwise** of the view forward. The engine multiplies the side
//: axis by the +90-degree (left) basis vector, so a counter-clockwise wish
//: needs a **positive** side axis.
[[nodiscard]] inline auto engine_side_axis_for_wish(float rel_deg) noexcept -> float {
  const float rel = rel_deg * k_deg_to_rad;
  // The wish's lateral component, expressed the way `StrafeIntent::right` is
  // (positive to the player's right): the engine's basis is the left vector, so
  // a counter-clockwise wish is a *negative* right.
  return engine_side_axis(-std::sin(rel));
}

//: The engine's **button bits** for a camera-frame move: `forward` (positive =
//: the view forward) and `side` (the engine's side axis — positive to the
//: player's **left**).
//
// The bits follow the same convention as the value they accompany: a positive
// side axis is the player's LEFT, so it goes with `IN_MOVELEFT`, and a negative
// one with `IN_MOVERIGHT`. The command carries both, and they have to agree —
// the value is what multiplies into the wish, the bit is what the engine's own
// input reads (`docs/re/strafe-sign.md` §4.2).
[[nodiscard]] inline auto engine_move_buttons(float forward, float side, float epsilon) noexcept
    -> std::uint64_t {
  std::uint64_t bits = 0;
  if (forward > epsilon) {
    bits |= button::k_forward;
  } else if (forward < -epsilon) {
    bits |= button::k_back;
  }
  if (side > epsilon) {
    bits |= button::k_moveleft;
  } else if (side < -epsilon) {
    bits |= button::k_moveright;
  }
  return bits;
}

// --- The ragebot's auto-stop --------------------------------------------------
//
// The auto-stop brakes the player: it writes the wish that **opposes** the
// player's velocity (the "counter-wish") as the command's move axes.
//
// `rel_deg` is the counter-wish's bearing **from the aim**, counter-clockwise
// positive. Two things the naive computation got wrong:
//
//   * the **sign** of the side axis. The counter-wish's component along the
//     aim's **left** vector is `+sin(rel)`, and the engine's `leftmove` is
//     positive to the **left**, so a counter-wish at +90 degrees needs
//     `leftmove = +1` (the player strafes *left*, against their own rightward
//     drift). The old code wrote `sign(-sin(rel))` — the exact negative — so
//     the brake pushed the player the way they were **already** moving: the
//     assist's whole purpose inverted, and the button bits (`IN_MOVERIGHT`
//     instead of `IN_MOVELEFT`) with it;
//   * the **forward** component at exactly 90 degrees. `cos(pi/2)` in float is
//     `-4.37e-8`, and the sign of that is `-1`: the brake pushed the player
//     **backwards** instead of sideways. A counter-strafe wants **lateral
//     only**, so a component below the dead zone is dropped rather than
//     signed.
//
// The side goes through the convention helper (`engine_side_axis_for_wish`),
// so the value and the button bits (`engine_move_buttons`) cannot disagree.

//: The dead zone of the counter-wish's components. A component whose magnitude
//: is below it is dropped (0) instead of taking the sign of the rounding noise
//: (`cos(90 degrees)` is `-4.37e-8` in float).
inline constexpr float k_counter_wish_deadzone = 0.05F;

//: The sign of a component, with a dead zone: 0 when the component is inside
//: the dead zone.
[[nodiscard]] inline auto signed_axis(float value, float epsilon) noexcept -> float {
  if (value > epsilon) {
    return 1.0F;
  }
  if (value < -epsilon) {
    return -1.0F;
  }
  return 0.0F;
}

//: The ragebot's auto-stop: the move axes that brake the player.
struct CounterWish {
  float forward = 0.0F;  // the engine's forward axis
  float side = 0.0F;     // the engine's side axis (positive to the player's left)
};

//: The counter-wish for a bearing `rel_deg` counter-clockwise from the aim.
[[nodiscard]] inline auto counter_wish_for(float rel_deg) noexcept -> CounterWish {
  CounterWish wish{};
  const float rel = rel_deg * k_deg_to_rad;
  wish.forward = signed_axis(std::cos(rel), k_counter_wish_deadzone);
  wish.side = signed_axis(engine_side_axis_for_wish(rel_deg), k_counter_wish_deadzone);
  return wish;
}

// --- The decision ------------------------------------------------------------

// How the auto-strafe derives its goal heading. The numbering is the wire's
// `movement_strafe_mode` byte and matches `cs2::features::StrafeMode`.
enum class StrafeMode : std::uint8_t {
  off = 0,              // the feature is disabled
  camera_relative = 1,  // the goal is the camera's forward/back (the legacy goal)
  directional = 2,      // the goal is the player's W/A/S/D intent
};

// The player's travel intent, read in the **view frame**: `forward` is the
// forward/back axis (+1 = `W`, -1 = `S`) and `right` the left/right axis
// (+1 = `D`, -1 = `A`). Both are 0 when the key is not held, so an empty
// intent means "no movement key" — the strafe then follows the camera.
//
// `right` is **our** convention (positive to the player's right); the engine's
// side axis has the opposite sign. `engine_side_axis()` converts between the
// two — never negate the lateral component by hand.
struct StrafeIntent {
  float forward{0.0F};
  float right{0.0F};
};

// The auto-strafe's decision for one tick.
struct StrafePlan {
  float goal_yaw{0.0F};  // the travel heading the wish rotates toward (degrees)
  int side{1};           // +1 = counter-clockwise of the velocity, -1 = clockwise
  float theta{0.0F};     // the `AirAccelerate` optimum (degrees)
  float wish_yaw{0.0F};  // the heading of the wish (degrees)
  bool decisive{false};  // false when the dead zone kept the previous side
};

//: Shortest signed difference `a - b` in degrees, in `[-180, 180)`.
[[nodiscard]] inline auto normalize_angle(float value) noexcept -> float {
  auto wrapped = std::fmod(value + 180.0F, 360.0F);
  if (wrapped < 0.0F) {
    wrapped += 360.0F;
  }
  return wrapped - 180.0F;
}

//: The heading of a world direction, in degrees.
[[nodiscard]] inline auto heading_of(float x, float y) noexcept -> float {
  return static_cast<float>(std::atan2(y, x) * 180.0 / k_pi);
}

// --- The steering verdict ------------------------------------------------------
//
// **Did the engine apply the strafe's basis rotation?**
//
// The silent encoding writes the wish as a **sub-tick yaw delta**: the engine
// accumulates it onto the command's view angles and `AirMove` builds the wish
// from that rotated basis. When the server honours the sub-tick view angles
// (`sv_subtick_movement_view_angles 1`) the velocity heading turns by a
// fraction of a degree to a few degrees **per tick**, toward the wish. When the
// server ignores them the wish is never applied at all: the velocity heading
// does not move, the feature writes every tick and gains nothing.
//
// Measured live on 2026-10-20 (de_dust2 listen server, `docs/re/strafing.md`
// §"The gate"): with the policy on, the auto-strafe carries the player from
// 30 u/s to ~195 u/s in three hops; with it off, the speed is flat at the
// control's ~105 u/s. **The turn of the velocity heading is the observable
// that separates the two**, so it is what the payload reports — a silent
// auto-strafe that writes every tick and never turns the velocity is a feature
// that cannot work on this server, and the operator has to know.

//: The smallest requested rotation that counts as "the feature asked for a
// turn" (degrees). Below it the wish is nearly parallel to the velocity and the
// turn it could cause is under the noise floor anyway.
inline constexpr float k_strafe_min_requested_turn_deg = 5.0F;

//: The smallest turn of the velocity heading that counts as "the engine applied
// it" (degrees). The engine's own turn is ~1-11 degrees per tick with the
// 8-slice rotation; the noise (the float read, a rounding server, a wall) is
// far below a tenth of a degree.
inline constexpr float k_strafe_min_applied_turn_deg = 0.25F;

/// What the engine did with the requested rotation.
enum class SteerVerdict {
  unknown,   // nothing was asked for, or there is no earlier sample to compare with
  followed,  // the velocity turned toward the wish: the rotation was applied
  ignored,   // the velocity did not turn: the engine ignored the rotation
};

//: Classify the turn of the velocity heading against the requested rotation.
//
// `previous_velocity_yaw` is the heading the payload read on the **previous**
// strafe tick and `velocity_yaw` the one it reads now, so the turn between them
// is the effect of the rotation written in between. `side` is the side of the
// velocity the wish was pushed on (+1 = counter-clockwise): the velocity must
// turn that way, and a turn the other way is as good as no turn at all.
[[nodiscard]] inline constexpr auto classify_steer(float previous_velocity_yaw, float velocity_yaw,
                                                   int side, float requested_rotation_deg) noexcept
    -> SteerVerdict {
  if (std::fabs(requested_rotation_deg) < k_strafe_min_requested_turn_deg) {
    return SteerVerdict::unknown;
  }
  const float turned = normalize_angle(velocity_yaw - previous_velocity_yaw);
  if (std::fabs(turned) < k_strafe_min_applied_turn_deg) {
    return SteerVerdict::ignored;
  }
  if (side != 0 && ((turned > 0.0F) != (side > 0))) {
    return SteerVerdict::ignored;  // turned the wrong way
  }
  return SteerVerdict::followed;
}

//: The turn of the velocity heading between two samples, in degrees
//: (counter-clockwise positive). The measurement the verdict is made on — and
//: the evidence behind it: a **gated** rotation leaves it at zero, an applied
//: one turns the velocity by a few degrees per tick.
[[nodiscard]] inline constexpr auto steer_turn(float previous_velocity_yaw,
                                               float velocity_yaw) noexcept -> float {
  return normalize_angle(velocity_yaw - previous_velocity_yaw);
}

// --- The airtime gain ---------------------------------------------------------
//
// The auto-strafe's **outcome**: the horizontal speed gained over one airtime.
// The take-off speed is the speed the player left the ground with; the landing
// speed is the speed they touched down with — read *before* the ground slice of
// the landing tick, so it is still the air speed. The difference is what the
// feature gained (the working encoding: ~+85 u/s per hop).
//
// The bookkeeping is a small state machine of its own so it can be **host
// tested**: the payload's tick path needs the engine, and this is the part
// that is easy to get wrong — a stale air phase reports a gain for an airtime
// that never happened.
class AirtimeGain {
public:
  /// The player left the ground at this speed.
  void begin(float takeoff_speed) noexcept {
    takeoff_ = takeoff_speed;
    airborne_ = true;
  }
  /// The player touched down at this speed: the gain of the airtime.
  [[nodiscard]] float end(float landing_speed) noexcept {
    airborne_ = false;
    return landing_speed - takeoff_;
  }
  /// The airtime was abandoned (the feature stopped acting): there is no gain.
  void abandon() noexcept { airborne_ = false; }
  /// Whether an airtime is running.
  [[nodiscard]] bool airborne() const noexcept { return airborne_; }
  /// The speed the running airtime started with.
  [[nodiscard]] float takeoff_speed() const noexcept { return takeoff_; }

private:
  float takeoff_{0.0F};
  bool airborne_{false};
};

// Decode a command's button mask into the view-frame intent: `W` is +1
// forward, `S` is -1, `D` is +1 to the right and `A` is -1. A key held on both
// sides cancels (`W`+`S` is no forward intent at all).
[[nodiscard]] inline auto intent_from_buttons(std::uint64_t buttons) noexcept -> StrafeIntent {
  StrafeIntent intent{};
  intent.forward = ((buttons & button::k_forward) != 0 ? 1.0F : 0.0F) -
                   ((buttons & button::k_back) != 0 ? 1.0F : 0.0F);
  intent.right = ((buttons & button::k_moveright) != 0 ? 1.0F : 0.0F) -
                 ((buttons & button::k_moveleft) != 0 ? 1.0F : 0.0F);
  return intent;
}

// The **legacy** intent: only `W` and `S` reach the goal, and `S` wins over
// `W` (the old code tested the `S` bit directly, so holding both pointed
// backwards). The lateral keys are not part of it — in the legacy behaviour a
// held `A`/`D` stands the whole feature down.
[[nodiscard]] inline auto legacy_intent_from_buttons(std::uint64_t buttons) noexcept
    -> StrafeIntent {
  StrafeIntent intent{};
  if ((buttons & button::k_back) != 0) {
    intent.forward = -1.0F;
  } else if ((buttons & button::k_forward) != 0) {
    intent.forward = 1.0F;
  }
  return intent;
}

// The travel heading the intent asks for, in the world frame (degrees).
//
//   `intent_world = forward(view_yaw) * f + basis(view_yaw) * leftmove`
//
// where `basis` is the engine's **second** view-frame basis vector and
// `leftmove` is the engine's side axis for our intent — so the heading is
// built the way the engine builds the wish, not by assuming a sign.
//
// That turns `W` into the camera's forward, `S` into 180 degrees behind it,
// `D` into 90 degrees to the right, `A` into 90 degrees to the left and
// `W`+`D` into 45 degrees to the right. An empty intent (no movement key)
// falls back to the camera's forward — the strafe still acts and follows the
// camera, as it always did.
[[nodiscard]] inline auto goal_for(const StrafeIntent& intent, float view_yaw) noexcept -> float {
  const ViewBasis basis = engine_view_basis(view_yaw);
  // The engine's side axis for an intent that is positive to the right.
  const float side = engine_side_axis(intent.right);
  const float x = intent.forward * basis.forward.x + side * basis.side.x;
  const float y = intent.forward * basis.forward.y + side * basis.side.y;
  if (x == 0.0F && y == 0.0F) {
    return normalize_angle(view_yaw);
  }
  return normalize_angle(heading_of(x, y));
}

// The `AirAccelerate` optimum: the angle off the velocity at which the wish
// gains the most.
//
//   `theta* = acos((W - A) / speed)`
//
// with `W = sv_air_max_wishspeed` (the air cap, 30) and `A` the per-call
// acceleration budget. `A >= W` collapses the optimum to exactly 90 degrees (a
// pure perpendicular push), which is the usual case for a full analog. The
// result is always in `(0, 90]`.
[[nodiscard]] inline auto optimal_theta(float speed, float accel_per_tick, float cap) noexcept
    -> float {
  if (cap - accel_per_tick <= 0.0F || speed <= 0.0F) {
    return k_strafe_max_theta_deg;  // A >= W: exactly perpendicular
  }
  const auto radians = std::acos(std::clamp((cap - accel_per_tick) / speed, -1.0F, 1.0F));
  return std::clamp(static_cast<float>(radians * 180.0 / k_pi), k_strafe_min_theta_deg,
                    k_strafe_max_theta_deg);
}

// The wish heading for a side: the velocity's heading plus the side's
// `theta*`.
[[nodiscard]] inline auto wish_for(float vel_yaw, int side, float theta) noexcept -> float {
  return normalize_angle(vel_yaw + static_cast<float>(side) * theta);
}

// The whole decision for one tick.
//
// The wish is pushed **perpendicular** to the velocity, on the side that turns
// the velocity toward the goal:
//
//   `delta = normalize_angle(goal - vel_yaw)`
//   `side  = (delta >= 0) ? +1 : -1`
//   `wish_yaw = vel_yaw + side * theta`
//
// `delta` is the goal's offset from the velocity heading. When it is decisive
// (at or beyond `deadzone_deg`) the side follows it; when it is not, the
// previous side is kept — that is the hysteresis, and it is what makes holding
// `W` alone continue the current curve instead of flipping every tick. With no
// previous side the legacy tie-break (+1) applies.
[[nodiscard]] inline auto plan(const StrafeIntent& intent, float view_yaw, float vel_yaw,
                               float speed, float accel_per_tick, float cap, int previous_side,
                               float deadzone_deg = k_strafe_side_deadzone_deg) noexcept
    -> StrafePlan {
  StrafePlan result{};
  result.goal_yaw = goal_for(intent, view_yaw);
  const float delta = normalize_angle(result.goal_yaw - vel_yaw);
  if (std::fabs(delta) >= std::fabs(deadzone_deg)) {
    result.side = delta >= 0.0F ? 1 : -1;
    result.decisive = true;
  } else {
    result.side = previous_side != 0 ? previous_side : 1;
    result.decisive = false;
  }
  result.theta = optimal_theta(speed, accel_per_tick, cap);
  result.wish_yaw = wish_for(vel_yaw, result.side, result.theta);
  return result;
}

// The **legacy** decision: the side follows the sign of the goal's offset from
// the velocity heading, with **no** dead zone and **no** hysteresis.
//
// The pre-T3 code was unconditionally `side = (delta >= 0) ? +1 : -1`. The
// dead zone and the hysteresis exist only for the new WASD goal (where `W`
// alone puts the goal exactly on the velocity heading and the tie-break there
// would flip the curve every tick); applying them in the legacy modes would
// change the old behaviour, so the legacy path must not apply them.
//
// The legacy modes are the camera-relative goal and `strafe_ignore_keys` (the
// spec: "`movement_strafe_ignore_keys == 1` must restore exactly the old
// behaviour").
[[nodiscard]] inline auto legacy_plan(const StrafeIntent& intent, float view_yaw, float vel_yaw,
                                      float speed, float accel_per_tick, float cap) noexcept
    -> StrafePlan {
  StrafePlan result{};
  result.goal_yaw = goal_for(intent, view_yaw);
  const float delta = normalize_angle(result.goal_yaw - vel_yaw);
  result.side = delta >= 0.0F ? 1 : -1;
  // The legacy rule always decides: there is no dead zone to hold the side.
  result.decisive = true;
  result.theta = optimal_theta(speed, accel_per_tick, cap);
  result.wish_yaw = wish_for(vel_yaw, result.side, result.theta);
  return result;
}

}  // namespace cs2::control
