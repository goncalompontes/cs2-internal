#pragma once

// `usercmd` input backend: hook the client's per-tick command build and write
// the `CUserCmd` (ADR-0020; `docs/re/client-dll.md` §"Input path",
// `docs/control-harness.md` §"Input backends").
//
// Community cross-check (UC threads 687361 / 730511): the `CCSGOInput`
// `CreateMove` that receives the command is the `create_move` signature
// (`+0xd01610`, vtable slot 25). We scan the `CCSGOInput` vtable for that
// function, patch the slot, call the original first, then apply the harness
// overrides to the command's legacy button mask (`cmd + 0x60`, `CInButtonState`).
//
// Live-verified (build 25815307): the write drives **discrete** actions — `jump`
// leaves the ground and `bhop` hops repeatedly. The sub-tick **view angles** are
// written separately (`set_view_angles`, the newest `input_history` entry's
// `view_angles`; `docs/re/subtick.md`) and are *silent* — the camera does not
// follow. **Analog movement works** (`move()`): measured live 2026-10-15,
// `forward = 1.0` walks 204 units/s (1.01x the engine's own console `+forward`,
// 203) and `0.5` walks 115 (0.57x) — the axes are in the engine's own units
// (`k_move_scale = 1.0`), so `450` moved nothing.
//
// The write is inert unless the `usercmd` backend is selected, so the
// live-validated `console` path is unaffected.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cs2::control {

// `CUserCmd` layout (`docs/re/client-dll.md` §"Input path"): stride `0x98`,
// `nSequenceNumber` `+0x8`, `CSGOUserCmdPB` `+0x10`, `CInButtonState` `+0x58`,
// the three button masks `+0x60/+0x68/+0x70`. Verified live: console `+forward`
// sets `+0x60` bit 3 and `+jump` bit 1.
inline constexpr std::size_t k_cmd_stride = 0x98;
inline constexpr std::size_t k_cmd_buttons_offset = 0x60;

// Sub-tick offsets (`docs/re/subtick.md`). The embedded `CSGOUserCmdPB` holds
// the `input_history` view-angle list and the `base` (CBaseUserCmdPB) that
// carries the analog move + buttons.
inline constexpr std::size_t k_cmd_pb_offset = 0x10;   // CSGOUserCmdPB begins
inline constexpr std::size_t k_pb_base_offset = 0x30;  // CBaseUserCmdPB* (within the PB)
inline constexpr std::size_t k_base_has_bits_offset = 0x10;
inline constexpr std::uint32_t k_base_move_bits = 0x40u | 0x80u | 0x100u;  // fields 5/6/7
inline constexpr std::size_t k_base_move_offset = 0x58;  // forwardmove/sidemove/upmove
inline constexpr std::size_t k_base_buttons_pb_offset = 0x38;  // CInButtonStatePB* (field 3)
inline constexpr std::size_t k_base_viewangles_offset = 0x40;  // CMsgQAngle* (field 4)
inline constexpr std::uint32_t k_base_viewangles_bit = 0x4u;   // `viewangles` has-bit
inline constexpr std::size_t k_buttonstate_has_bits_offset = 0x10;
inline constexpr std::size_t k_buttonstate_values_offset = 0x18;  // 3x uint64
inline constexpr std::size_t k_base_subtick_moves_offset = 0x18;  // RepeatedPtrField
// `CSubtickMoveStep` (community-confirmed, UC 769450).
inline constexpr std::size_t k_step_has_bits_offset = 0x10;
inline constexpr std::size_t k_step_button_offset = 0x18;
inline constexpr std::size_t k_step_pressed_offset = 0x20;
inline constexpr std::size_t k_step_when_offset = 0x24;
inline constexpr std::size_t k_step_forward_offset = 0x28;  // analog_forward_delta
inline constexpr std::size_t k_step_left_offset = 0x2c;     // analog_left_delta
inline constexpr std::size_t k_step_pitch_offset = 0x30;  // pitch_delta
inline constexpr std::size_t k_step_yaw_offset = 0x34;    // yaw_delta
inline constexpr std::uint32_t k_step_bits = 0x4u | 0x8u | 0x10u;  // when/forward/left
inline constexpr std::uint32_t k_step_angle_bits = 0x4u | 0x20u | 0x40u;  // when/pitch/yaw
inline constexpr std::uint32_t k_step_button_bits = 0x1u | 0x2u | 0x4u;  // button/pressed/when

// The **raw** per-sub-tick angle history on `CCSGOInput`: the array the command's
// `input_history` is built from (`FUN_180cf9c30` copies entry → PB), stride `0x60`,
// angles (`x/y/z` floats) at `+0x10`, count at `+0xbc8`. Writing these (before the
// original runs) is the candidate for steering movement *silently* — unlike the
// command's own `viewangles`, which the pawn's eye angles follow.
inline constexpr std::size_t k_input_history_count_offset = 0xbc8;
inline constexpr std::size_t k_input_history_rep_offset = 0xbd0;
inline constexpr std::size_t k_input_history_stride = 0x60;
inline constexpr std::size_t k_input_history_angles_offset = 0x10;
// protobuf `RepeatedPtrField`: arena@0, current_size@8, allocated@0xc, rep@0x10;
// `rep` = { allocated@0, elements@8 }.
inline constexpr std::size_t k_field_arena_offset = 0x00;
inline constexpr std::size_t k_field_size_offset = 0x08;
inline constexpr std::size_t k_field_rep_offset = 0x10;
inline constexpr std::size_t k_rep_allocated_offset = 0x00;
inline constexpr std::size_t k_rep_elements_offset = 0x08;
inline constexpr std::size_t k_pb_history_size_offset = 0x20;  // input_history current_size
inline constexpr std::size_t k_pb_history_rep_offset = 0x28;   // input_history rep (void**)
inline constexpr std::size_t k_history_entry_angles_offset = 0x18;  // CSGOInputHistoryEntryPB::view_angles
inline constexpr std::size_t k_angles_has_bits_offset = 0x10;
inline constexpr std::size_t k_angles_values_offset = 0x18;  // x/y/z floats
inline constexpr std::uint32_t k_angles_bits = 0x7u;
// `CUserCmdArray::m_nSequenceNumber` (build 25815307).
inline constexpr std::size_t k_user_cmd_array_sequence_offset = 0x5910;

// The raw analog move axes on `CCSGOInput` that `create_move_input` copies into
// `CBaseUserCmdPB::forwardmove`/`leftmove` each tick (`docs/re/subtick.md`
// §"The command build"). Writing them (before and after the original) is the way
// to synthesise movement the way the console `+forward` does. `+0x270`/`+0x274`
// are forward/left; `+0x278` is the up axis.
inline constexpr std::size_t k_input_move_offset = 0x270;

// The **source** of `+0x270`: `create_move_input` calls `FUN_180b5f7b0(input, 0)`,
// which copies the local player's per-controller input entry (array
// `CCSGOInput + 0xb58`, stride `0x440`, move pair at `+0x18`) into `+0x270/0x274`.
// Writing the entry *before* the original therefore survives that copy — the
// documented next probe (`docs/re/subtick.md` §"Actionable seams"). Entry `0` is
// the local player (`FUN_180b5f7b0(input, 0)`).
inline constexpr std::size_t k_input_entry_array_offset = 0xb58;
inline constexpr std::size_t k_input_entry_stride = 0x440;
inline constexpr std::size_t k_input_entry_move_offset = 0x18;

// The move scale of `forwardmove`/`leftmove`. **Measured live (2026-10-15):** the
// engine's own input writes **`1.0`** — a console `+forward` puts
// `forwardmove = 1.0` on the command *and* `1.0` on the raw axes
// (`CCSGOInput + 0x270`), with has-bit `0x40` and nothing for the other axes;
// everything downstream scales by `cl_forwardspeed` (450). So a request is in
// these units, `1.0` is a full-speed run, and only a sub-unit magnitude is
// "analog" at all. (`sv_quantize_movement_input` rounds the value to an integer,
// i.e. the reachable set is `{-1, 0, +1}` — which is why the community
// `MOVEMENT_SCALE = 450` recipe bit us: snapping `1.0` to a 450-grid zeroes it.)
inline constexpr float k_move_scale = 1.0F;
inline constexpr float k_move_epsilon = 0.01F;

// Degrees <-> radians and angle wrapping for the auto-strafe maths.
inline constexpr float k_pi = 3.14159265358979323846F;
//: Below this horizontal speed the air-strafe wish angle is meaningless (the
//: community's own guard is `speed < 5`) — nothing is written.
inline constexpr float k_strafe_min_speed = 25.0F;
// The air-strafe gain model (`CGameMovement::AirAccelerate`, re-derived on build
// 25815307; `docs/re/strafing.md`): the optimal wish angle is where
// `dot(velocity, wishdir)` falls to `W - A`, with the air cap `W =
// sv_air_max_wishspeed` (30) and the per-call budget `A = sv_airaccelerate *
// wishspeed * frametime`. Once `A >= W` the optimum is exactly 90 degrees (a pure
// perpendicular push). `wishspeed` is the engine's own clamped speed for a full
// analog (a `1.0` axis reaches `m_flMaxSpeed`), so the offline default (a rifle at
// 250) puts `A = 46.875 > W` and the answer at 90 degrees. Computing it rather than
// hard-coding the 90 keeps the raid correct for a slower weapon or a short slice.
inline constexpr float k_strafe_air_wishspeed = 30.0F;
inline constexpr float k_strafe_max_speed = 250.0F;
inline constexpr float k_strafe_airaccelerate = 12.0F;
inline constexpr float k_tick_interval = 1.0F / 64.0F;
// The admission gate discards the *whole* sub-tick array when the accumulated
// view delta exceeds ~2.0 (radians) per command, so the basis rotation is clamped
// to this many degrees (1.75 rad) with the bhop pair in the same array.
inline constexpr float k_strafe_rot_limit_deg = 100.0F;
//: The most `yaw_delta` steps the strafe will emit for one tick (the shared array's
//: admission gate caps the whole field at 32 entries; the bhop pair shares it).
inline constexpr int k_strafe_max_steps = 16;

//: Shortest signed difference `a - b` in degrees, in `(-180, 180]`.
[[nodiscard]] inline auto normalize_angle(float value) noexcept -> float {
  auto wrapped = std::fmod(value + 180.0F, 360.0F);
  if (wrapped < 0.0F) {
    wrapped += 360.0F;
  }
  return wrapped - 180.0F;
}

// How many `CSubtickMoveStep`s the probe reads back from the last command.
inline constexpr std::size_t k_probe_steps = 4;

// Source `IN_*` button bits (the `CUserCmd` button mask).
namespace button {
inline constexpr std::uint64_t k_attack = 1ull << 0;
inline constexpr std::uint64_t k_jump = 1ull << 1;
inline constexpr std::uint64_t k_duck = 1ull << 2;
inline constexpr std::uint64_t k_forward = 1ull << 3;
inline constexpr std::uint64_t k_back = 1ull << 4;
inline constexpr std::uint64_t k_use = 1ull << 5;
inline constexpr std::uint64_t k_cancel = 1ull << 6;
inline constexpr std::uint64_t k_left = 1ull << 7;
inline constexpr std::uint64_t k_right = 1ull << 8;
inline constexpr std::uint64_t k_moveleft = 1ull << 9;
inline constexpr std::uint64_t k_moveright = 1ull << 10;
inline constexpr std::uint64_t k_attack2 = 1ull << 11;
inline constexpr std::uint64_t k_run = 1ull << 12;
inline constexpr std::uint64_t k_reload = 1ull << 13;
inline constexpr std::uint64_t k_alt1 = 1ull << 14;
inline constexpr std::uint64_t k_alt2 = 1ull << 15;
inline constexpr std::uint64_t k_score = 1ull << 16;
inline constexpr std::uint64_t k_speed = 1ull << 17;
inline constexpr std::uint64_t k_walk = 1ull << 18;
inline constexpr std::uint64_t k_zoom = 1ull << 19;
}  // namespace button

// Resolve a friendly or `in_*` button name to its bit; 0 when unknown.
[[nodiscard]] std::uint64_t button_bit(std::string_view name) noexcept;

// A read-only sample of a move vector (engine units, `k_move_scale` per axis).
struct MoveSample {
  float forward = 0.0F;
  float side = 0.0F;
  float up = 0.0F;
  bool valid = false;
};

// Read-only probe of one `CSubtickMoveStep` of the last command's
// `base->subtick_moves`. Used to learn the engine's own convention (a console
// `+forward`/`+jump` produces real steps) before synthesising movement.
struct StepProbe {
  float when = 0.0F;
  float forward = 0.0F;  // analog_forward_delta
  float left = 0.0F;     // analog_left_delta
  std::uint64_t button = 0;
  bool pressed = false;
  bool valid = false;
};

// The `usercmd` backend: persistent button overrides plus auto-jump.
class Input {
 public:
  Input() = default;
  ~Input();
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  enum class Backend { console, usercmd };

  // Resolve `CCSGOInput` and install the hook (idempotent; safe on every call).
  void refresh() noexcept;

  [[nodiscard]] bool ready() const noexcept { return installed_; }
  [[nodiscard]] bool hooked() const noexcept { return original_ != nullptr; }
  [[nodiscard]] std::size_t slot_index() const noexcept { return slot_index_; }
  [[nodiscard]] std::size_t slot5_index() const noexcept { return slot5_index_; }

  [[nodiscard]] Backend backend() const noexcept { return backend_; }
  void set_backend(Backend backend) noexcept { backend_ = backend; }
  [[nodiscard]] static const char* backend_name(Backend backend) noexcept;

  // Hold / release buttons. `set_mask` bits are forced on every tick, `clear_mask`
  // bits forced off; a press removes the bit from the clear mask and vice versa.
  void press(std::uint64_t bits) noexcept;
  void release(std::uint64_t bits) noexcept;
  void reset_buttons() noexcept;
  [[nodiscard]] std::uint64_t buttons_set() const noexcept { return set_mask_.load(); }
  [[nodiscard]] std::uint64_t buttons_clear() const noexcept { return clear_mask_.load(); }

  // Auto-jump (**assist**): while the player is actually holding jump it answers
  // each landing with a sub-tick press placed at the engine's own recorded landing
  // fraction, so the hop takes the *alpha* takeoff path (no ground-slice speed
  // loss) instead of being a whole tick late. It never jumps on its own — the
  // player's held input is the trigger — and it is owned by the overlay menu's
  // settings (`wire::Settings.bhop`), not by the control channel.
  //
  // The algorithm is the community's authoritative sub-tick bunnyhop
  // (UC 771389 / 774033, decomposed in `docs/re/research-bhop.md`): a
  // **release+press pair sharing one `when`**, because a lone press while the key
  // is held produces no press edge (`edge = w2 || (w0 && w1)`: with the key held at
  // command start, `w1` never flips). The pair is re-derived and re-appended on
  // every frame (the command's sub-tick table is rebuilt each frame), never
  // *cleared*, and the whole array dies on the admission gate if it stops being
  // monotonic or exceeds 32 entries.
  void set_bhop(bool on) noexcept { bhop_.store(on); }
  [[nodiscard]] bool bhop() const noexcept { return bhop_.load(); }

  // The manual engine-simulation path (`RunCommand`) for the bhop — **off by
  // default**: without the full `CPrediction` setup it faults inside client.dll
  // and moves the pawn. Kept behind `predict(on)` until the record-based design
  // (UC 771389) lands. See `docs/re/prediction.md`.
  void set_predict(bool on) noexcept { predict_.store(on); }
  [[nodiscard]] bool predict() const noexcept { return predict_.load(); }

  // Sub-tick view angles (usercmd-only): every `input_history` entry's
  // `view_angles` sub-message (3 floats). This is the silent-aim seam — the
  // camera does not follow.
  void set_view_angles(float pitch, float yaw, float roll) noexcept;
  void clear_view_angles() noexcept;
  [[nodiscard]] bool angles_enabled() const noexcept { return angles_enabled_.load(); }
  [[nodiscard]] float angle_pitch() const noexcept { return angle_pitch_.load(); }
  [[nodiscard]] float angle_yaw() const noexcept { return angle_yaw_.load(); }
  [[nodiscard]] float angle_roll() const noexcept { return angle_roll_.load(); }

  // The **command's own** `viewangles` (`base + 0x40`, field 4) — the angles the
  // movement/wishdir is computed from. Measured live 2026-10-15: writing the
  // `input_history` entries does **not** steer movement (the walk kept following
  // the camera), so this is the channel an auto-strafe needs. Written every tick
  // with the base has-bit `0x4`; the camera is *not* moved by it (the camera is
  // the engine's own view global), which is what makes a silent strafe possible.
  void set_command_angles(float pitch, float yaw, float roll) noexcept;
  void clear_command_angles() noexcept;
  [[nodiscard]] bool command_angles_enabled() const noexcept {
    return command_angles_enabled_.load();
  }
  [[nodiscard]] float command_angle_yaw() const noexcept { return command_angle_yaw_.load(); }

  // The **raw** per-sub-tick input angle history (`CCSGOInput + 0xbd0`) — the
  // candidate for a movement yaw that does *not* turn the pawn's eye angles.
  void set_raw_angles(float pitch, float yaw, float roll) noexcept;
  void clear_raw_angles() noexcept;
  [[nodiscard]] bool raw_angles_enabled() const noexcept { return raw_angles_enabled_.load(); }
  [[nodiscard]] std::uint64_t raw_angle_writes() const noexcept {
    return raw_angle_writes_.load();
  }

  // A `CSubtickMoveStep` carrying only a **yaw delta** (`when = 1`, no button, no
  // analog) — the other silent-movement candidate (community recipe, UC 753644:
  // `set_when(1.f)` with a yaw delta). Appended once per tick.
  void set_step_yaw(float yaw_delta_degrees) noexcept;
  void clear_step_yaw() noexcept;
  [[nodiscard]] bool step_yaw_enabled() const noexcept { return step_yaw_enabled_.load(); }
  [[nodiscard]] float step_yaw() const noexcept { return step_yaw_.load(); }

  // Analog move (usercmd-only): the raw axes on `CCSGOInput` (`+0x270`) that
  // `create_move_input` copies into `CBaseUserCmdPB` each tick, plus the command's
  // move floats and both button states (`cmd + 0x60` and `buttons_pb` at
  // `base + 0x38`). This is the movement seam (`docs/re/subtick.md`).
  //
  // `quantize` snaps the requested axes to the `sv_quantize_movement_input` valid
  // set (`{-k_move_scale, 0, +k_move_scale}`) so a server that rounds the analog
  // floats still moves the player; `step` additionally writes the residual as a
  // `CSubtickMoveStep` (the sub-tick analog channel). Both default off. A third
  // flag, `raw_entry`, also wrote the per-controller input entry
  // (`CCSGOInput + 0xb58`) — **it crashed the game (input-path AV) on 2026-10-15,
  // so it must not be enabled** until the layout is re-derived
  // (`docs/re/subtick.md` §"Actionable seams").
  void set_move(float forward, float side, float up, bool quantize = false,
                bool step = false, bool raw_entry = false) noexcept;
  void clear_move() noexcept;

  [[nodiscard]] bool move_enabled() const noexcept { return move_enabled_.load(); }
  [[nodiscard]] bool move_quantize() const noexcept { return move_quantize_.load(); }
  [[nodiscard]] bool move_step() const noexcept { return move_step_.load(); }
  [[nodiscard]] bool move_raw_entry() const noexcept { return move_raw_entry_.load(); }
  [[nodiscard]] float move_forward() const noexcept { return move_forward_.load(); }
  [[nodiscard]] float move_side() const noexcept { return move_side_.load(); }
  [[nodiscard]] float move_up() const noexcept { return move_up_.load(); }

  // --- auto-strafe -------------------------------------------------------
  // A **normal, silent** auto-strafe: while **airborne** (and above a minimum
  // speed) each tick it
  //   1. takes the goal heading from the held keys read in the *view* frame
  //      (W/nothing ⇒ the camera's forward, S ⇒ behind; holding A/D stands the
  //      feature down — the player's own air control wins), and
  //   2. pushes the air-acceleration wish **perpendicular to the velocity** on the
  //      side of that goal (the optimal `theta*` of `AirAccelerate`:
  //      `theta* = A >= W ? 90 : acos((W - A) / speed)`, `W = sv_air_max_wishspeed
  //      = 30`, `A = sv_airaccelerate * wishspeed * frametime`), re-derived from the
  //      *current* velocity every tick so the wish re-perpendicularises.
  //
  // The wish is written **silently** — no view angle is touched, so neither the
  // camera nor the pawn's eye angles move. Two encodings, chosen by `subticks`:
  //
  //   * `subticks >= 1` (**default 8**, the precise channel — validated live at
  //     **+85.1 median / +104.9 max u/s per hop**, scaling with the slice count): the analog stays on the
  //     valid set (`forwardmove = 1`, `leftmove = 0`) and the *movement basis* is
  //     rotated onto the wish with `subticks` `CSubtickMoveStep` `yaw_delta`s
  //     (`when = i/N`). The engine accumulates them onto the command's `viewangles`
  //     and `AirMove` builds `wishdir` from that rotated basis, so the wish is exact
  //     and `sv_quantize_movement_input` is a no-op (`docs/re/strafing.md` §"The
  //     silent movement-yaw channel"). Needs `sv_subtick_movement_view_angles` on.
  //   * `subticks == 0` (the fallback): the wish direction is encoded in the analog
  //     itself, in the camera frame, snapped to the `{-1,0,1}` valid set. Needs no
  //     sub-tick policy but the snap loses the exact perpendicular angle — measured
  //     nearly flat.
  //
  // On the ground, below the minimum speed, when no command/basis is readable, or
  // when the player holds A/D, it writes nothing. Pairs with `bhop` for the classic
  // auto-bhop-strafe, sharing the sub-tick array and its admission gate.
  void set_strafe(bool on) noexcept;
  [[nodiscard]] bool strafe() const noexcept { return strafe_enabled_.load(); }
  void set_strafe_config(float wishspeed, int subticks) noexcept;
  [[nodiscard]] float strafe_wishspeed() const noexcept { return strafe_wishspeed_.load(); }
  [[nodiscard]] int strafe_subticks() const noexcept { return strafe_subticks_.load(); }
  [[nodiscard]] std::uint64_t strafe_writes() const noexcept { return strafe_writes_.load(); }
  [[nodiscard]] std::uint64_t strafe_raw_writes() const noexcept {
    return strafe_raw_writes_.load();
  }
  [[nodiscard]] bool strafe_airborne() const noexcept { return strafe_airborne_.load(); }
  [[nodiscard]] float strafe_speed() const noexcept { return strafe_speed_.load(); }
  [[nodiscard]] float strafe_delta() const noexcept { return strafe_delta_.load(); }
  // The goal heading (from the held keys, world yaw), the chosen wish yaw, the
  // basis rotation written (`wish - view`, degrees, after the gate clamp), the side
  // (+1 CCW / −1 CW of the velocity) and how many `yaw_delta` steps were queued.
  [[nodiscard]] float strafe_goal() const noexcept { return strafe_goal_.load(); }
  [[nodiscard]] float strafe_wish() const noexcept { return strafe_wish_.load(); }
  [[nodiscard]] float strafe_rot() const noexcept { return strafe_rot_.load(); }
  [[nodiscard]] int strafe_side() const noexcept { return strafe_side_.load(); }
  [[nodiscard]] int strafe_steps() const noexcept { return strafe_steps_.load(); }
  // Why the last tick did or did not act: 0 off/no command, 1 on the ground,
  // 2 too slow, 3 no readable basis, 4 acted, 5 no room in the sub-tick array,
  // 6 nothing to write, 7 the player is holding A/D (their input wins).
  [[nodiscard]] int strafe_reason() const noexcept { return strafe_reason_.load(); }
  // True when the basis rotation had to be clamped so the admission gate's
  // view-delta limit could not discard the whole array (the bhop pair included).
  [[nodiscard]] bool strafe_clamped() const noexcept { return strafe_clamped_.load(); }
  // How many times the bhop assist queued a (sub-tick) jump press.
  [[nodiscard]] std::uint64_t bhop_presses() const noexcept { return bhop_presses_.load(); }
  // Bhop gate diagnostics: ticks the engine's button state showed the player
  // holding jump, and of those how many were airborne.
  [[nodiscard]] std::uint64_t bhop_holding_ticks() const noexcept {
    return bhop_holding_ticks_.load();
  }
  [[nodiscard]] std::uint64_t bhop_air_ticks() const noexcept { return bhop_air_ticks_.load(); }

  // --- ragebot (`docs/ragebot.md`) ----------------------------------------
  // The aim solve runs on the render thread (the overlay's per-frame state
  // refresh, which owns the snapshot) and stashes the result here; `apply()`
  // writes it into the command each tick as the **silent** sub-tick view angles
  // and, when `set_rage_fire(true)`, presses attack. Independent of the control
  // backend (like the movement features) and owned by the overlay menu / the
  // `rage` control command.
  void set_rage_aim(float pitch, float yaw, int target, int hitbox) noexcept;
  void clear_rage_aim() noexcept;
  void set_rage_fire(bool on) noexcept { rage_fire_.store(on); }
  [[nodiscard]] bool rage_fire() const noexcept { return rage_fire_.load(); }
  [[nodiscard]] bool rage_active() const noexcept { return rage_active_.load(); }
  [[nodiscard]] int rage_target() const noexcept { return rage_target_.load(); }
  [[nodiscard]] int rage_hitbox() const noexcept { return rage_hitbox_.load(); }
  // Ticks a rage aim was written, and attack presses issued.
  [[nodiscard]] std::uint64_t rage_writes() const noexcept { return rage_writes_.load(); }
  [[nodiscard]] std::uint64_t rage_shots() const noexcept { return rage_shots_.load(); }

  // --- record-based bunnyhop (UC 771389) ---------------------------------
  // Read-only post-hook state (`docs/re/research-bhop.md`). `bhop_landed_tick` /
  // `bhop_landed_frac` are the engine's own `m_ModernJump` landing record (its
  // `tick` is in `{build-1, f}` coordinates), `bhop_tick_base` the controller's
  // `m_nTickBase`, `bhop_client_tick` the command's own `client_tick`,
  // `bhop_build` the derived command tick, `bhop_age` = `build - landed_tick`
  // (the write wants 1), `bhop_mode` 1 = A (contact placed past), 2 = B (beta
  // catch-up), 0 = nothing, `bhop_when` the sub-tick fraction written.
  [[nodiscard]] std::uint64_t bhop_writes() const noexcept { return bhop_writes_.load(); }
  [[nodiscard]] int bhop_landed_tick() const noexcept { return bhop_landed_tick_.load(); }
  [[nodiscard]] float bhop_landed_frac() const noexcept { return bhop_landed_frac_.load(); }
  [[nodiscard]] int bhop_build() const noexcept { return bhop_build_.load(); }
  [[nodiscard]] int bhop_age() const noexcept { return bhop_age_.load(); }
  [[nodiscard]] int bhop_tick_base() const noexcept { return bhop_tick_base_.load(); }
  [[nodiscard]] int bhop_client_tick() const noexcept { return bhop_client_tick_.load(); }
  [[nodiscard]] int bhop_cmd_number() const noexcept { return bhop_cmd_number_.load(); }
  [[nodiscard]] int bhop_mode() const noexcept { return bhop_mode_.load(); }
  [[nodiscard]] float bhop_when() const noexcept { return bhop_when_.load(); }
  // How many landings were answered with placement A (the tick-perfect one) and
  // with placement B (the beta catch-up). Sampled counters, not a sampled read:
  // A firing at all proves the phase, and A >> B means speed is preserved.
  [[nodiscard]] std::uint64_t bhop_a() const noexcept { return bhop_a_.load(); }
  [[nodiscard]] std::uint64_t bhop_b() const noexcept { return bhop_b_.load(); }
  [[nodiscard]] std::uint64_t bhop_skipped() const noexcept { return bhop_skipped_.load(); }

  // --- engine prediction (sub-tick bhop) ---------------------------------
  // Runs the game's own per-command movement (`CPlayer_MovementServices::
  // RunCommand`, vtable slot 29) a few ticks ahead and reads the landing the
  // modern-jump system records (`CCSPlayerModernJump`), then restores every field
  // the simulation touched (`docs/re/prediction.md`). That is what lets the bhop
  // place a sub-tick `IN_JUMP` press at the exact landing fraction.
  [[nodiscard]] bool predict_ready() const noexcept { return run_command_ != nullptr; }
  [[nodiscard]] std::uint64_t predict_runs() const noexcept { return predict_runs_.load(); }
  [[nodiscard]] int predicted_landing_tick() const noexcept { return predicted_tick_.load(); }
  [[nodiscard]] float predicted_landing_frac() const noexcept { return predicted_frac_.load(); }
  [[nodiscard]] int predicted_landing_delta() const noexcept { return predicted_delta_.load(); }
  // The position the prediction simulation ended at (the box the overlay draws to
  // show the prediction is live). False until the first simulation.
  [[nodiscard]] bool predicted_origin_valid() const noexcept {
    return predicted_origin_valid_.load();
  }
  auto predicted_origin(float out[3]) const noexcept -> bool;
  // Simulate up to `ticks` commands ahead; returns true and fills the landing
  // tick/fraction (and how many ticks ahead it is) when the modern-jump system
  // records a landing inside the window.
  auto simulate_landing(void* cmd, int ticks, int& landed_tick, float& landed_frac,
                        int& landed_delta) noexcept -> bool;

  // The local pawn, noted from a snapshot — used for the auto-jump on-ground test.
  void note_local_pawn(std::uintptr_t pawn) noexcept { local_pawn_.store(pawn); }
  [[nodiscard]] std::uintptr_t local_pawn() const noexcept { return local_pawn_.load(); }

  // Diagnostics.
  [[nodiscard]] std::uint64_t calls() const noexcept { return calls_.load(); }
  [[nodiscard]] std::uint64_t calls5() const noexcept { return calls5_.load(); }
  [[nodiscard]] std::uint64_t writes() const noexcept { return writes_.load(); }
  [[nodiscard]] std::uint64_t angle_writes() const noexcept { return angle_writes_.load(); }
  [[nodiscard]] std::uint64_t move_writes() const noexcept { return move_writes_.load(); }
  [[nodiscard]] std::uint64_t raw_writes() const noexcept { return raw_writes_.load(); }
  [[nodiscard]] std::uint64_t entry_writes() const noexcept { return entry_writes_.load(); }
  [[nodiscard]] std::uint64_t step_writes() const noexcept { return step_writes_.load(); }
  [[nodiscard]] std::uint64_t command_angle_writes() const noexcept {
    return command_angle_writes_.load();
  }

  // Diagnostic: the last command's first `k_probe_words` qwords (offsets
  // `0x00..0x78`), captured on every call, plus its address. Also captures the
  // `CBaseUserCmdPB` (`base`) words, to locate the movement/angle fields.
  // Read-only.
  static constexpr std::size_t k_probe_words = 16;
  static constexpr std::size_t k_probe_base_words = 16;
  [[nodiscard]] std::uintptr_t last_cmd() const noexcept { return last_cmd_.load(); }
  [[nodiscard]] std::uint64_t last_word(std::size_t index) const noexcept;
  [[nodiscard]] std::uintptr_t last_base() const noexcept { return last_base_.load(); }
  [[nodiscard]] std::uint64_t last_base_word(std::size_t index) const noexcept;

  // Read-only probes of the last command's `base` (`CBaseUserCmdPB`) and of the
  // engine's own sub-tick steps — the "read what the engine writes" half of the
  // movement probe (`docs/re/subtick.md` §"Actionable seams").
  [[nodiscard]] std::uint32_t base_has_bits() const noexcept;
  [[nodiscard]] MoveSample base_move() const noexcept;
  [[nodiscard]] int step_count() const noexcept { return step_count_.load(); }
  [[nodiscard]] StepProbe step_probe(std::size_t index) const noexcept;
  // The raw axes the engine currently has on `CCSGOInput + 0x270` and, for
  // comparison, the per-controller input entry (`+0xb58`) it derives them from.
  [[nodiscard]] MoveSample raw_move() const noexcept;
  [[nodiscard]] MoveSample entry_move() const noexcept;

 private:
  using create_move_fn = void (*)(void* self, int slot, void* cmd);
  using create_move_input_fn = void (*)(void* self, int slot, char active);
  using alloc_step_fn = void* (*)(void* arena);
  using add_step_fn = void* (*)(void* field, void* step);
  using tick_fn = void (*)(void* controller, int* out_tick);
  using array_fn = void* (*)(void* array, int tick);
  using sequence_fn = void* (*)(void* controller, std::uint32_t sequence);

  static void __fastcall create_move_detour(void* self, int slot, void* cmd) noexcept;
  static void __fastcall create_move_input_detour(void* self, int slot, char active) noexcept;
  // The command the game will simulate, via the `GetUserCmd` chain.
  [[nodiscard]] void* current_cmd() noexcept;
  void capture(void* cmd) noexcept;
  void apply(void* cmd) noexcept;
  // Append a `CSubtickMoveStep` (analog forward/left) to `base->subtick_moves`.
  void* add_analog_step(void* base, float forward, float side, float when) noexcept;
  // A step carrying only `yaw_delta` (+ `pitch_delta = 0`) at `when`.
  void* add_yaw_step(void* base, float yaw_delta, float when) noexcept;
  // A step carrying only a **button press/release** at `when` — the sub-tick jump
  // timing (`CSubtickMoveStep.button`/`pressed`).
  void* add_button_step(void* base, std::uint64_t button, bool pressed, float when) noexcept;
  // Allocate (or reuse) a `CSubtickMoveStep` and return it ready to fill.
  void* alloc_step(void* base) noexcept;
  // --- record-based bunnyhop (`docs/re/research-bhop.md`) ----------------
  // The per-frame post-hook: read the engine's landing record + the command's
  // build tick, derive `when`, and append the release+press pair. `base` is the
  // command's `CBaseUserCmdPB`, `buttons` its button mask read *before* any write.
  void bhop_tick(void* cmd, std::uint8_t* base, std::uint64_t buttons) noexcept;
  // Append `release` then `press` at `when`, keeping `base->subtick_moves`
  // monotonic and inside the admission gate's 32-entry budget. False when it did
  // not fit (nothing is appended in that case's budget check).
  auto write_bhop_pair(std::uint8_t* base, float when) noexcept -> bool;
  // True when a jump release+press pair already sits at `when` in
  // `base->subtick_moves` — the per-frame idempotency check (the engine rebuilds
  // the table every frame, so this cannot silently drop the steps).
  [[nodiscard]] auto bhop_pair_present(std::uint8_t* base, float when) const noexcept -> bool;
  // The local player controller (`local_player_controllers` → `CBasePlayerController*`).
  [[nodiscard]] auto controller() const noexcept -> std::uintptr_t;
  // Write the residual (`requested - base`) movement as sub-tick analog deltas —
  // the `step` mode of `set_move` (`docs/re/subtick.md` §"The movement step").
  void write_move_steps(void* base, float forward, float side) noexcept;
  // Write the raw analog move axes on `CCSGOInput` (`+0x270`) — the source
  // `create_move_input` copies from. Gated by `move_enabled_`.
  void write_raw_move() noexcept;
  // Write the per-controller input entry (`CCSGOInput + 0xb58`, stride `0x440`,
  // move pair `+0x18`) that `FUN_180b5f7b0` copies into `+0x270` inside slot 5.
  void write_entry_move() noexcept;
  // Write every raw per-sub-tick angle-history entry's angles (silent candidate).
  void write_raw_angles() noexcept;
  // Overwrite every `input_history` entry's `view_angles` sub-message (the silent
  // aim seam; used by both the `look` command and the ragebot).
  void write_silent_angles(void* cmd, float pitch, float yaw, float roll) noexcept;
  // One tick of the ragebot: write the render-thread solve as the silent sub-tick
  // view angles and (optionally) press attack.
  void rage_tick(std::uint8_t* cmd, std::uint64_t* buttons) noexcept;
  // One tick of the auto-strafe (see `set_strafe`); called from `apply()` with the
  // `CBaseUserCmdPB` (`base`) and the owning `CUserCmd` (`cmd`).
  void strafe_tick(void* base, void* cmd) noexcept;
  // Write the strafe's wish: the analog move floats + both button states, and (in
  // the `subticks >= 1` mode) the basis-rotating `yaw_delta` steps (`deltas[0..steps)`,
  // already the per-slice optimum — see `strafe_tick`). Returns false when nothing
  // could be written.
  auto write_strafe_wish(std::uint8_t* base, std::uint8_t* cmd, float forward, float side,
                         const float* deltas, int steps) noexcept -> bool;
  // True when `base->subtick_moves` already holds a yaw-only step (button 0, no
  // analog, a non-zero `yaw_delta`) — the per-frame idempotency check for the
  // strafe's rotation (the hook can run more than once for the same command, and
  // the engine rebuilds the table every frame).
  [[nodiscard]] auto strafe_steps_present(std::uint8_t* base) const noexcept -> bool;
  // Move the just-appended last element of `base->subtick_moves` to its `when`
  // order, so the admission gate's monotonicity clause cannot discard the whole
  // array (the engine's own steps and the bhop pair share the field).
  void sort_last_step(std::uint8_t* base, float when) noexcept;
  [[nodiscard]] bool on_ground() const noexcept;
  // The local pawn's horizontal velocity (``false`` when unavailable).
  [[nodiscard]] bool velocity(float& x, float& y) const noexcept;

  void** slot_ = nullptr;         // patched vtable slot 25 (`create_move`)
  void* original_ = nullptr;      // its previous value
  std::size_t slot_index_ = 0;
  void** slot5_ = nullptr;        // patched vtable slot 5 (`create_move_input`)
  void* original5_ = nullptr;     // its previous value
  std::size_t slot5_index_ = 0;
  bool installed_ = false;
  // One-shot guard so `refresh()` (called from the control thread *and* the
  // per-frame state refresh) cannot patch the vtable twice.
  std::atomic<bool> refresh_claimed_{false};
  Backend backend_ = Backend::console;
  std::uint32_t flags_offset_ = 0x3f4;  // C_BaseEntity::m_fFlags
  std::uint32_t velocity_offset_ = 0x3f8;  // C_BaseEntity::m_vecAbsVelocity
  alloc_step_fn alloc_step_ = nullptr;
  add_step_fn add_step_ = nullptr;
  std::uintptr_t client_base_ = 0;
  std::uintptr_t ccsgo_input_ = 0;  // the `CCSGOInput` singleton object
  std::uint32_t user_cmd_array_rva_ = 0;
  std::uint32_t controllers_rva_ = 0;
  tick_fn tick_ = nullptr;
  array_fn array_ = nullptr;
  sequence_fn sequence_ = nullptr;
  std::atomic<std::uint64_t> calls5_{0};
  std::atomic<std::uint64_t> set_mask_{0};
  std::atomic<std::uint64_t> clear_mask_{0};
  std::atomic<bool> bhop_{false};
  std::atomic<bool> predict_{false};
  std::atomic<bool> angles_enabled_{false};
  std::atomic<float> angle_pitch_{0.0F};
  std::atomic<float> angle_yaw_{0.0F};
  std::atomic<float> angle_roll_{0.0F};
  std::atomic<bool> command_angles_enabled_{false};
  std::atomic<float> command_angle_pitch_{0.0F};
  std::atomic<float> command_angle_yaw_{0.0F};
  std::atomic<float> command_angle_roll_{0.0F};
  std::atomic<bool> raw_angles_enabled_{false};
  std::atomic<float> raw_angle_pitch_{0.0F};
  std::atomic<float> raw_angle_yaw_{0.0F};
  std::atomic<float> raw_angle_roll_{0.0F};
  std::atomic<std::uint64_t> raw_angle_writes_{0};
  std::atomic<bool> step_yaw_enabled_{false};
  std::atomic<float> step_yaw_{0.0F};
  std::atomic<bool> strafe_enabled_{false};
  std::atomic<std::uint64_t> bhop_presses_{0};
  std::atomic<std::uint64_t> bhop_holding_ticks_{0};
  std::atomic<std::uint64_t> bhop_air_ticks_{0};
  // Ragebot: the render-thread solve -> the per-tick silent write.
  std::atomic<bool> rage_active_{false};
  std::atomic<float> rage_pitch_{0.0F};
  std::atomic<float> rage_yaw_{0.0F};
  std::atomic<int> rage_target_{-1};
  std::atomic<int> rage_hitbox_{-1};
  std::atomic<bool> rage_fire_{false};
  std::atomic<std::uint64_t> rage_writes_{0};
  std::atomic<std::uint64_t> rage_shots_{0};
  // Record-based bunnyhop: diagnostics + the per-command decision cache (the hook
  // can run more than once per frame, and the `fresh` phase must only be sampled
  // once — see `bhop_tick`).
  std::atomic<std::uint64_t> bhop_writes_{0};
  std::atomic<int> bhop_landed_tick_{0};
  std::atomic<float> bhop_landed_frac_{0.0F};
  std::atomic<int> bhop_build_{0};
  std::atomic<int> bhop_age_{0};
  std::atomic<int> bhop_tick_base_{0};
  std::atomic<int> bhop_client_tick_{0};
  std::atomic<int> bhop_cmd_number_{0};
  std::atomic<int> bhop_mode_{0};
  std::atomic<float> bhop_when_{0.0F};
  std::uint32_t bhop_prev_number_ = 0;    // command number seen on the previous call
  std::uint32_t bhop_active_number_ = 0;  // the command the cached decision is for
  int bhop_active_landed_ = 0;            // the landing it answers
  float bhop_active_when_ = 0.0F;         // its sub-tick fraction
  int bhop_active_mode_ = 0;              // 1 = A, 2 = B
  std::atomic<std::uint64_t> bhop_a_{0};        // landings answered with A
  std::atomic<std::uint64_t> bhop_b_{0};        // landings answered with B
  std::atomic<std::uint64_t> bhop_skipped_{0};  // landings with no pair placed
  std::atomic<float> strafe_wishspeed_{250.0F};
  std::atomic<int> strafe_subticks_{8};
  std::atomic<std::uint64_t> strafe_writes_{0};
  std::atomic<std::uint64_t> strafe_raw_writes_{0};
  // Engine prediction (sub-tick bhop): the movement-services vtable entries the
  // simulation calls, and its last result (`docs/re/prediction.md`).
  void* run_command_ = nullptr;             // vtable slot 29
  void* set_prediction_command_ = nullptr;  // vtable slot 43
  void* reset_prediction_command_ = nullptr;  // vtable slot 44
  std::atomic<std::uint64_t> predict_runs_{0};
  std::atomic<int> predicted_tick_{0};
  std::atomic<float> predicted_frac_{0.0F};
  std::atomic<int> predicted_delta_{0};
  std::atomic<bool> predicted_origin_valid_{false};
  std::atomic<float> predicted_origin_x_{0.0F};
  std::atomic<float> predicted_origin_y_{0.0F};
  std::atomic<float> predicted_origin_z_{0.0F};
  std::atomic<bool> strafe_airborne_{false};
  std::atomic<float> strafe_speed_{0.0F};
  std::atomic<float> strafe_delta_{0.0F};
  std::atomic<float> strafe_goal_{0.0F};
  std::atomic<float> strafe_wish_{0.0F};
  std::atomic<float> strafe_rot_{0.0F};
  std::atomic<int> strafe_side_{0};
  std::atomic<int> strafe_steps_{0};
  std::atomic<int> strafe_reason_{0};
  std::atomic<bool> strafe_clamped_{false};
  std::atomic<bool> move_enabled_{false};
  std::atomic<bool> move_quantize_{false};
  std::atomic<bool> move_step_{false};
  std::atomic<bool> move_raw_entry_{false};
  std::atomic<float> move_forward_{0.0F};
  std::atomic<float> move_side_{0.0F};
  std::atomic<float> move_up_{0.0F};
  std::atomic<std::uintptr_t> local_pawn_{0};
  std::atomic<std::uint64_t> calls_{0};
  std::atomic<std::uint64_t> writes_{0};
  std::atomic<std::uint64_t> angle_writes_{0};
  std::atomic<std::uint64_t> move_writes_{0};
  std::atomic<std::uint64_t> raw_writes_{0};
  std::atomic<std::uint64_t> entry_writes_{0};
  std::atomic<std::uint64_t> step_writes_{0};
  std::atomic<std::uint64_t> command_angle_writes_{0};
  std::atomic<std::uintptr_t> last_cmd_{0};
  std::atomic<std::uint64_t> probe_[k_probe_words]{};
  std::atomic<std::uintptr_t> last_base_{0};
  std::atomic<std::uint64_t> base_probe_[k_probe_base_words]{};
  std::atomic<int> step_count_{0};
  std::atomic<float> step_when_[k_probe_steps]{};
  std::atomic<float> step_forward_[k_probe_steps]{};
  std::atomic<float> step_left_[k_probe_steps]{};
  std::atomic<std::uint64_t> step_button_[k_probe_steps]{};
  std::atomic<bool> step_pressed_[k_probe_steps]{};
};

}  // namespace cs2::control
