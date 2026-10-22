#pragma once

// The ragebot's settings as the **ownership rule** sees them: the wire's rage
// group as a snapshot, the per-field comparison, and the field names.
//
// **Why a header of its own.** The overlay's settings sink
// (`payload/src/payload_main.cpp`) re-applied the **whole** rage group on every
// publication — the menu publishes its block every frame — so whatever the
// control channel had set was overwritten within a second. The control channel
// is the **only** way to reach the Monte-Carlo objective (the menu has no such
// option), so the feature T35 shipped was inert in practice.
//
// The rule — "did the operator change this field?" — is a **pure** comparison
// over two snapshots, so it is host-testable
// (`payload/control/tests/test_control_rage_settings.cpp`). That is the repo's
// pattern for exactly this kind of rule (`cs2/control/rage_objective.hpp`).
//
// **The wire block and the menu are not touched.** The rule reads the block;
// it does not change it, and it does not change the menu. The wire's version
// is not bumped either: the block is shared between two separately compiled
// binaries, and a version bump is a silent-corruption risk.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cs2/overlay/wire.hpp"

namespace cs2::control {

// Who wrote a rage field.
//
// The menu and the control channel both drive the ragebot, and the status has
// to say which one owns the objective in force: the control channel is the
// only way to reach the Monte-Carlo objective, so the operator has to be able
// to tell "the control channel set it and it stuck" from "the menu set it and
// it stuck" — and the menu cannot show the objective at all.
enum class RageSource : std::uint8_t {
  // The operator's menu: the in-process menu or the Vulkan layer's.
  menu,
  // The control channel — and the payload's own startup defaults, which are
  // not the operator's menu either.
  control,
};

// --- The rage group ---------------------------------------------------------

// The wire's rage group, as a snapshot.
//
// **A snapshot, not the block.** The block is the whole configuration in one
// POD; the rage group is a handful of fields inside it. The rule works on the
// group alone, so the group is copied out first — and the copy is what the
// "last applied" state holds.
//
// **Every setting the wire's rage group owns is here.** A field left out of
// the snapshot is a field that keeps clobbering the control channel. The host
// test reads `wire.hpp` and proves the coverage.
//
// **The one-shot op is not here.** `rage_config_op` and `rage_config_token`
// are a *request* ("save this profile"), not a setting: the service's own
// token makes each request fire exactly once, so it is delivered on every
// write rather than compared.
struct RageSettings {
  bool enabled = false;
  std::uint8_t key = 0;        // the virtual-key code held to trigger
  bool fire = false;           // auto-fire while aiming
  std::uint8_t objective = 1;  // 0 = damage, 1 = hitchance, 2 = expected
  std::uint8_t hitbox = 0;     // 0 = auto, 1 = head, 2 = body
  float fov = 0.0F;            // the target cone in degrees (0 = unlimited)
  float min_damage = 0.0F;     // don't aim/fire below this damage
  float min_hitchance = 0.0F;  // don't fire below this hitchance
  std::uint8_t autostop = 0;   // 0 = off, 1 = always, 2 = when firing
  std::uint8_t autoscope = 0;  // 0 = off, 1 = always, 2 = when firing
  bool autoreload = false;
  bool only_visible = false;
  std::uint8_t mode = 0;  // the trigger's mode (0 hold, 1 always, 2 toggle)
  bool per_weapon = false;
  std::array<char, 24> weapon{};  // the designer name the profile is keyed on
  bool recoil = false;
  float stop_speed = 0.0F;  // the auto-stop threshold in units/s
  bool backtrack = false;
};

// The wire block's rage group, as a snapshot.
//
// **The bytes, not the clamped view.** The comparison answers "did the
// operator change the field?", and the operator changes the **block**: the
// clamped view (`features::from_wire_settings`) is what the features may take,
// and it is a different question. The block's values are what the menu
// publishes, so they are what the comparison runs on.
[[nodiscard]] inline auto rage_settings_from_wire(
    const cs2::overlay::wire::Settings& settings) noexcept -> RageSettings {
  RageSettings out{};
  out.enabled = settings.rage != 0;
  out.key = settings.rage_key;
  out.fire = settings.rage_fire != 0;
  out.objective = settings.rage_objective;
  out.hitbox = settings.rage_hitbox;
  out.fov = settings.rage_fov;
  out.min_damage = settings.rage_min_damage;
  out.min_hitchance = settings.rage_min_hitchance;
  out.autostop = settings.rage_autostop;
  out.autoscope = settings.rage_autoscope;
  out.autoreload = settings.rage_autoreload != 0;
  out.only_visible = settings.rage_only_visible != 0;
  out.mode = settings.rage_mode;
  out.per_weapon = settings.rage_per_weapon != 0;
  for (std::size_t index = 0; index < out.weapon.size(); ++index) {
    out.weapon[index] = settings.rage_weapon[index];
  }
  out.recoil = settings.rage_recoil != 0;
  out.stop_speed = settings.rage_stop_speed;
  out.backtrack = settings.rage_backtrack != 0;
  return out;
}

// --- The fields -------------------------------------------------------------

// The rage group's fields, as bits.
//
// The mask is what a write carries: the fields the operator changed (the
// menu's write) or the fields the control command named (its write). **The
// fields a write does not carry keep their value *and* their owner** — that is
// the whole rule.
inline constexpr std::uint32_t RAGE_FIELD_ENABLED = 1U << 0;
inline constexpr std::uint32_t RAGE_FIELD_KEY = 1U << 1;
inline constexpr std::uint32_t RAGE_FIELD_FIRE = 1U << 2;
inline constexpr std::uint32_t RAGE_FIELD_OBJECTIVE = 1U << 3;
inline constexpr std::uint32_t RAGE_FIELD_HITBOX = 1U << 4;
inline constexpr std::uint32_t RAGE_FIELD_FOV = 1U << 5;
inline constexpr std::uint32_t RAGE_FIELD_MIN_DAMAGE = 1U << 6;
inline constexpr std::uint32_t RAGE_FIELD_MIN_HITCHANCE = 1U << 7;
inline constexpr std::uint32_t RAGE_FIELD_AUTOSTOP = 1U << 8;
inline constexpr std::uint32_t RAGE_FIELD_AUTOSCOPE = 1U << 9;
inline constexpr std::uint32_t RAGE_FIELD_AUTORELOAD = 1U << 10;
inline constexpr std::uint32_t RAGE_FIELD_ONLY_VISIBLE = 1U << 11;
inline constexpr std::uint32_t RAGE_FIELD_MODE = 1U << 12;
inline constexpr std::uint32_t RAGE_FIELD_PER_WEAPON = 1U << 13;
inline constexpr std::uint32_t RAGE_FIELD_WEAPON = 1U << 14;
inline constexpr std::uint32_t RAGE_FIELD_RECOIL = 1U << 15;
inline constexpr std::uint32_t RAGE_FIELD_STOP_SPEED = 1U << 16;
inline constexpr std::uint32_t RAGE_FIELD_BACKTRACK = 1U << 17;
// Every field: the **first** publication applies the whole group (there is no
// earlier view to compare against).
inline constexpr std::uint32_t RAGE_FIELD_ALL = (1U << 18) - 1U;

// The rage fields, by name.
//
// The status lists them, so the operator can see **which channel owns each
// field** and not only the objective's owner.
struct RageFieldName {
  std::uint32_t bit;
  std::string_view name;
};

inline constexpr RageFieldName k_rage_field_names[] = {
    {RAGE_FIELD_ENABLED, "enabled"},
    {RAGE_FIELD_KEY, "key"},
    {RAGE_FIELD_FIRE, "fire"},
    {RAGE_FIELD_OBJECTIVE, "objective"},
    {RAGE_FIELD_HITBOX, "hitbox"},
    {RAGE_FIELD_FOV, "fov"},
    {RAGE_FIELD_MIN_DAMAGE, "min_damage"},
    {RAGE_FIELD_MIN_HITCHANCE, "min_hitchance"},
    {RAGE_FIELD_AUTOSTOP, "autostop"},
    {RAGE_FIELD_AUTOSCOPE, "autoscope"},
    {RAGE_FIELD_AUTORELOAD, "autoreload"},
    {RAGE_FIELD_ONLY_VISIBLE, "only_visible"},
    {RAGE_FIELD_MODE, "mode"},
    {RAGE_FIELD_PER_WEAPON, "per_weapon"},
    {RAGE_FIELD_WEAPON, "weapon"},
    {RAGE_FIELD_RECOIL, "recoil"},
    {RAGE_FIELD_STOP_SPEED, "stop_speed"},
    {RAGE_FIELD_BACKTRACK, "backtrack"},
};

// The name of the field with that bit ("" for a bit that is not a field).
[[nodiscard]] constexpr auto rage_field_name(std::uint32_t bit) noexcept -> std::string_view {
  for (const RageFieldName& field : k_rage_field_names) {
    if (field.bit == bit) {
      return field.name;
    }
  }
  return "";
}

// Whether the **menu** owns the field.
//
// `menu_fields` is the mask of the fields the menu owns: a set bit is the
// menu's, a clear bit is the control channel's.
[[nodiscard]] constexpr auto rage_menu_owns(std::uint32_t menu_fields, std::uint32_t bit) noexcept
    -> bool {
  return (menu_fields & bit) != 0;
}

// The owner of a field, by name (the status).
//
// The operator has to be able to tell "the control channel set it and it
// stuck" from "the menu set it and it stuck": the control channel is the only
// way to reach the Monte-Carlo objective, which the menu cannot select.
[[nodiscard]] constexpr auto rage_owner_name(std::uint32_t menu_fields, std::uint32_t bit) noexcept
    -> std::string_view {
  return rage_menu_owns(menu_fields, bit) ? "menu" : "control";
}

// --- The rule ---------------------------------------------------------------

// The weapon key: the name up to the NUL.
//
// The wire's field is a fixed 24-byte buffer, and the menu's text field writes
// the string and its NUL but leaves the bytes **after** the NUL as they were —
// so the bytes are not the value. The name is.
[[nodiscard]] constexpr auto weapon_name_equal(const std::array<char, 24>& left,
                                               const std::array<char, 24>& right) noexcept -> bool {
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) {
      return false;
    }
    if (left[index] == '\0') {
      return true;  // the two names agree up to the NUL
    }
  }
  return true;
}

// The fields whose values differ between `now` and `before`.
//
// **The comparison is exact.** Every field is compared with `==`, the floats
// included: a float that differs in the last bit is a value the operator
// moved, and a slider that was not touched writes the bits it already held.
//
// **The menu does not republish a recomputed value.** Dear ImGui's widgets
// write the block only when the operator acts on them (a slider writes back
// while it is being dragged, a combo only when an item is selected, a check
// box only on a click), and the one widget that writes unconditionally — the
// combo's byte wrapper, which pulls an out-of-range byte back into the
// widget's range — writes the **same** value for an in-range byte. So the
// comparison converges: the value the menu publishes is the value the callback
// last saw, and the rule does not degenerate into "always changed".
//
// **The weapon's bytes are compared as a name**, not byte by byte: the menu's
// text field leaves the bytes after the NUL stale, and a stale byte is not a
// change of the value.
[[nodiscard]] constexpr auto rage_changed_fields(const RageSettings& now,
                                                 const RageSettings& before) noexcept
    -> std::uint32_t {
  std::uint32_t changed = 0;
  if (now.enabled != before.enabled) {
    changed |= RAGE_FIELD_ENABLED;
  }
  if (now.key != before.key) {
    changed |= RAGE_FIELD_KEY;
  }
  if (now.fire != before.fire) {
    changed |= RAGE_FIELD_FIRE;
  }
  if (now.objective != before.objective) {
    changed |= RAGE_FIELD_OBJECTIVE;
  }
  if (now.hitbox != before.hitbox) {
    changed |= RAGE_FIELD_HITBOX;
  }
  if (now.fov != before.fov) {
    changed |= RAGE_FIELD_FOV;
  }
  if (now.min_damage != before.min_damage) {
    changed |= RAGE_FIELD_MIN_DAMAGE;
  }
  if (now.min_hitchance != before.min_hitchance) {
    changed |= RAGE_FIELD_MIN_HITCHANCE;
  }
  if (now.autostop != before.autostop) {
    changed |= RAGE_FIELD_AUTOSTOP;
  }
  if (now.autoscope != before.autoscope) {
    changed |= RAGE_FIELD_AUTOSCOPE;
  }
  if (now.autoreload != before.autoreload) {
    changed |= RAGE_FIELD_AUTORELOAD;
  }
  if (now.only_visible != before.only_visible) {
    changed |= RAGE_FIELD_ONLY_VISIBLE;
  }
  if (now.mode != before.mode) {
    changed |= RAGE_FIELD_MODE;
  }
  if (now.per_weapon != before.per_weapon) {
    changed |= RAGE_FIELD_PER_WEAPON;
  }
  if (!weapon_name_equal(now.weapon, before.weapon)) {
    changed |= RAGE_FIELD_WEAPON;
  }
  if (now.recoil != before.recoil) {
    changed |= RAGE_FIELD_RECOIL;
  }
  if (now.stop_speed != before.stop_speed) {
    changed |= RAGE_FIELD_STOP_SPEED;
  }
  if (now.backtrack != before.backtrack) {
    changed |= RAGE_FIELD_BACKTRACK;
  }
  return changed;
}

}  // namespace cs2::control
