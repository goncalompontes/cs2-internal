#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cs2/features/esp.hpp"
#include "cs2/features/esp_geometry.hpp"
#include "cs2/game/entity_system.hpp"
#include "cs2/game/math.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "cs2/render/draw_list.hpp"

#include "offsets.hpp"

#include "esp_detail.hpp"

#if __has_include("schemas/client_dll.hpp")
#include "schemas/client_dll.hpp"
#define CS2_FEATURES_HAVE_CLIENT_SCHEMA 1
#endif

namespace cs2::features::esp_detail {
namespace {

// How many identity slots the walk examines per frame.
//
// The entity list is paged (64 pages of 512 slots) and every populated slot
// costs two scattered reads (the designer-name pointer and the name's first
// word), so a full walk in one frame would cost milliseconds. The walk is
// therefore spread over the frames: a fixed budget per frame, and the
// entities are published when a sweep finishes. An empty page costs a whole
// iteration of the budget (its slots are skipped in one step, but the budget
// is spent on it), so the budget — not the number of reads — is what bounds
// the walk.
constexpr int k_world_slots_per_frame = 1024;

// `CEntityIdentity`'s layout (`cs2/game/entity_system.hpp`): the instance
// pointer and the designer name.
constexpr std::uintptr_t k_identity_instance = 0x00;
constexpr std::uintptr_t k_identity_designer = 0x20;

// `C_BaseEntity::m_pGameSceneNode` and `CGameSceneNode::m_vecAbsOrigin` — the
// same offsets the state reader uses for the players.
constexpr std::uintptr_t k_scene_node = 0x330;
constexpr std::uintptr_t k_abs_origin = 0xc8;

// The world entity classes, identified by their designer name's prefix. The
// name is a class name ("smokegrenade_projectile", "planted_c4",
// "weapon_ak47"), so the prefix identifies the class without a class
// pointer — the game updates the classes, the prefixes stay.
enum class WorldClass : std::uint8_t {
  none,
  smoke,
  flash,
  molotov,
  incendiary,
  he,
  decoy,
  sensor,    // the motion grenade
  bumpmine,  // the proximity mine
  c4,
  weapon,
};

auto matches(const char* text, const char* prefix, std::size_t length) noexcept -> bool {
  return std::memcmp(text, prefix, length) == 0;
}

auto classify(const char* name) noexcept -> WorldClass {
  if (matches(name, "smokegr", 7)) {
    return WorldClass::smoke;
  }
  if (matches(name, "flashba", 7)) {
    return WorldClass::flash;
  }
  if (matches(name, "molotov", 7)) {
    return WorldClass::molotov;
  }
  if (matches(name, "incendi", 7)) {
    return WorldClass::incendiary;
  }
  if (matches(name, "hegrena", 7)) {
    return WorldClass::he;
  }
  if (matches(name, "decoy_p", 7)) {
    return WorldClass::decoy;
  }
  // The two grenades the list did not name: without them they got no marker
  // at all.
  if (matches(name, "sensorgr", 8)) {
    return WorldClass::sensor;
  }
  if (matches(name, "bumpmine", 8)) {
    return WorldClass::bumpmine;
  }
  if (matches(name, "planted", 7)) {
    return WorldClass::c4;
  }
  if (matches(name, "c4", 2)) {
    return WorldClass::c4;
  }
  if (matches(name, "weapon_", 7)) {
    return WorldClass::weapon;
  }
  return WorldClass::none;
}

// The deployed incendiary (`weapon_taggrenade`) is a grenade, not a dropped
// weapon. The seven-character prefix cannot tell the two apart, so the name
// is read once more when the prefix says "weapon_".
auto is_deployed_incendiary(std::uintptr_t name_ptr) noexcept -> bool {
  char rest[10] = {};
  if (!game::read_bytes(name_ptr + 7, rest, sizeof(rest))) {
    return false;
  }
  return matches(rest, "taggrenad", 9);
}

auto grenade_kind(WorldClass type) noexcept -> GrenadeKind {
  switch (type) {
    case WorldClass::smoke:
      return GrenadeKind::smoke;
    case WorldClass::flash:
      return GrenadeKind::flash;
    case WorldClass::molotov:
      return GrenadeKind::molotov;
    case WorldClass::incendiary:
      return GrenadeKind::incendiary;
    case WorldClass::decoy:
      return GrenadeKind::decoy;
    case WorldClass::he:
      return GrenadeKind::he;
    default:
      // The sensor grenade and the bump mine have no tint of their own.
      return GrenadeKind::other;
  }
}

// The per-type tints, applied to the grenade colour.
struct Tint {
  float r = 1.0F;
  float g = 1.0F;
  float b = 1.0F;
};

auto tint_for(GrenadeKind kind) noexcept -> Tint {
  switch (kind) {
    case GrenadeKind::smoke:
      return Tint{.r = 0.55F, .g = 0.85F, .b = 1.0F};
    case GrenadeKind::flash:
      return Tint{.r = 1.0F, .g = 1.0F, .b = 0.65F};
    case GrenadeKind::molotov:
      return Tint{.r = 1.0F, .g = 0.6F, .b = 0.3F};
    case GrenadeKind::incendiary:
      return Tint{.r = 1.0F, .g = 0.45F, .b = 0.2F};
    case GrenadeKind::decoy:
      return Tint{.r = 0.8F, .g = 0.65F, .b = 1.0F};
    case GrenadeKind::he:
      return Tint{.r = 0.7F, .g = 0.8F, .b = 0.9F};
    case GrenadeKind::other:
    default:
      // A neutral grey: the sensor grenade and the bump mine have no tint of
      // their own.
      return Tint{.r = 0.75F, .g = 0.75F, .b = 0.75F};
  }
}

auto apply_tint(const render::Color& base, const Tint& tint) noexcept -> render::Color {
  const auto scale = [](std::uint8_t value, float factor) noexcept -> std::uint8_t {
    const float scaled = static_cast<float>(value) * factor;
    return static_cast<std::uint8_t>(std::clamp(scaled, 0.0F, 255.0F));
  };
  return render::Color{.r = scale(base.r, tint.r),
                       .g = scale(base.g, tint.g),
                       .b = scale(base.b, tint.b),
                       .a = base.a};
}

// A schema field offset, with a fallback for a build whose schema dump is
// older than the field.
auto schema_field(std::string_view class_target, std::string_view field_target,
                  std::uint32_t fallback) noexcept -> std::uint32_t {
#if defined(CS2_FEATURES_HAVE_CLIENT_SCHEMA)
  const std::uint32_t offset =
      cs2::sdk::schemas::client_dll::field_offset_by_target(class_target, field_target);
  return offset != 0 ? offset : fallback;
#else
  (void)class_target;
  (void)field_target;
  return fallback;
#endif
}

// The bomb's fields. The offsets come from the generated client schema
// (ADR-0001); the fallbacks are zero, so a build without a schema dump
// shows the bomb without its timer.
constexpr std::uint32_t k_c4_blow_time_field = 0x000;  // C_PlantedC4::m_flC4Blow
constexpr std::uint32_t k_c4_ticking_field = 0x000;    // C_PlantedC4::m_bBombTicking

auto read_origin(std::uintptr_t instance, game::Vec3& out) noexcept -> bool {
  std::uintptr_t scene = 0;
  if (!game::read(instance + k_scene_node, scene) || scene == 0) {
    return false;
  }
  return game::read(scene + k_abs_origin, out);
}

// The weapons the players are holding: they are not "dropped".
auto collect_held_weapons(const game::Snapshot& snapshot) -> std::array<std::uintptr_t, 64> {
  std::array<std::uintptr_t, 64> held{};
  std::size_t count = 0;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.weapon_entity == 0 || count >= held.size()) {
      continue;
    }
    held[count++] = player.weapon_entity;
  }
  return held;
}

auto is_held(const std::array<std::uintptr_t, 64>& held, std::uintptr_t instance) noexcept -> bool {
  for (const std::uintptr_t weapon : held) {
    if (weapon != 0 && weapon == instance) {
      return true;
    }
  }
  return false;
}

// The entity list's base, resolved once.
auto resolve_entity_list(std::uintptr_t& blocks) noexcept -> bool {
  const game::ModuleInfo client = game::find_module("client.dll");
  if (!client.valid()) {
    return false;
  }
  const cs2::sdk::OffsetInfo* info = cs2::sdk::find_offset("entity_list");
  if (info == nullptr || info->module != "client.dll") {
    return false;
  }
  return game::read(client.base + info->rva, blocks) && blocks != 0;
}

// Examine a slice of the entity list and classify what it finds.
void walk(Frame& frame) {
  WorldCache& cache = frame.world;
  std::uintptr_t blocks = 0;
  if (!resolve_entity_list(blocks)) {
    return;
  }
  cache.entity_system = game::EntitySystem(blocks);
  const std::array<std::uintptr_t, 64> held = collect_held_weapons(frame.snapshot);
  const float now = frame.snapshot.local.has_value() ? frame.snapshot.local->simulation_time : 0.0F;
  const std::uint32_t blow_field = schema_field("C_PlantedC4", "m_flC4Blow", k_c4_blow_time_field);
  const std::uint32_t ticking_field =
      schema_field("C_PlantedC4", "m_bBombTicking", k_c4_ticking_field);

  constexpr int k_page_count = game::EntitySystem::k_page_count;
  constexpr int k_slots = game::EntitySystem::k_slots_per_page;

  // A sweep is complete when the cursor wraps: the entities it found are
  // published then.
  const auto publish = [&cache]() -> void {
    cache.items = std::move(cache.scratch);
    cache.scratch.clear();
    ++cache.sweeps;
  };

  int page = cache.cursor / k_slots;
  int slot = cache.cursor % k_slots;
  if (page >= k_page_count) {
    page = 0;
    slot = 0;
  }
  std::uintptr_t page_base = 0;

  // Examine one identity slot and push what it holds. The function returns
  // when the slot holds nothing of interest — the publish must happen after
  // the slot has been classified, so it is not done here.
  const auto examine = [&](const std::uintptr_t identity) -> void {
    // The designer name's pointer, then the name's first word: the class
    // prefix is what the walk matches on, and a word is one read instead of
    // a string.
    std::uintptr_t name_ptr = 0;
    if (!game::read(identity + k_identity_designer, name_ptr) || name_ptr == 0) {
      return;
    }
    char prefix[8] = {};
    if (!game::read_bytes(name_ptr, prefix, sizeof(prefix))) {
      return;
    }
    WorldClass type = classify(prefix);
    if (type == WorldClass::none) {
      return;
    }
    // The deployed incendiary is a grenade, not a dropped weapon.
    if (type == WorldClass::weapon && is_deployed_incendiary(name_ptr)) {
      type = WorldClass::incendiary;
    }

    std::uintptr_t instance = 0;
    if (!game::read(identity + k_identity_instance, instance) || instance == 0) {
      return;
    }
    if (type == WorldClass::weapon && is_held(held, instance)) {
      return;  // the weapon a player is carrying, not a dropped one
    }

    WorldEntity entity;
    if (type == WorldClass::weapon) {
      entity.kind = WorldEntityKind::dropped_weapon;
      entity.name = game::read_cstring(name_ptr, 48);
    } else if (type == WorldClass::c4) {
      entity.kind = WorldEntityKind::c4;
    } else {
      entity.kind = WorldEntityKind::grenade;
      entity.grenade = grenade_kind(type);
    }
    if (!read_origin(instance, entity.origin)) {
      return;
    }
    if (entity.kind == WorldEntityKind::c4 && blow_field != 0) {
      float blow = 0.0F;
      if (game::read(instance + blow_field, blow) && blow > now) {
        entity.time_left = blow - now;
      }
      std::uint8_t ticking = 0;
      if (ticking_field != 0) {
        (void)game::read(instance + ticking_field, ticking);
      }
      entity.ticking = ticking != 0;
    }
    cache.scratch.push_back(std::move(entity));
  };

  for (int i = 0; i < k_world_slots_per_frame; ++i) {
    if (slot == 0) {
      page_base = cache.entity_system.page(page);
      if (page_base == 0) {
        // An empty page: skip it in one step.
        page = (page + 1) % k_page_count;
        cache.cursor = page * k_slots;
        if (cache.cursor == 0) {
          publish();
        }
        continue;
      }
    }
    const std::uintptr_t identity =
        page_base + game::EntitySystem::k_identity_stride * static_cast<std::uintptr_t>(slot);
    ++slot;
    if (slot >= k_slots) {
      slot = 0;
      page = (page + 1) % k_page_count;
    }
    cache.cursor = page * k_slots + slot;
    // The publish happens **after** the slot has been classified: the slot
    // that wraps the cursor must not appear one sweep late.
    const bool wrapped = cache.cursor == 0;

    examine(identity);

    if (wrapped) {
      publish();
    }
  }
  cache.last_walk_ms = frame.now_ms;
  if (frame.stats != nullptr) {
    ++frame.stats->entity_walks;
  }
}

}  // namespace

void build_world(Frame& frame) {
  const Config& config = frame.config;
  if (!config.esp.grenades && !config.esp.c4 && !config.esp.weapons) {
    return;
  }
  // The walk is spread over the frames; the entities it found are published
  // when a sweep finishes.
  walk(frame);

  for (const WorldEntity& entity : frame.world.items) {
    if (entity.kind == WorldEntityKind::grenade && !config.esp.grenades) {
      continue;
    }
    if (entity.kind == WorldEntityKind::c4 && !config.esp.c4) {
      continue;
    }
    if (entity.kind == WorldEntityKind::dropped_weapon && !config.esp.weapons) {
      continue;
    }
    const auto screen = frame.view.project(entity.origin, frame.width, frame.height);
    if (!screen.has_value()) {
      continue;
    }
    if (entity.kind == WorldEntityKind::grenade) {
      const render::Color color =
          apply_tint(to_render(config.esp.color_grenade), tint_for(entity.grenade));
      frame.out.poly(esp_geometry::diamond_points(screen->x, screen->y, 5.0F), color);
    } else if (entity.kind == WorldEntityKind::c4) {
      render::Color color = to_render(config.esp.color_c4);
      if (entity.ticking) {
        // The bomb is ticking: it turns red.
        color = render::Color{.r = 255, .g = 40, .b = 40, .a = color.a};
      }
      frame.out.fill(screen->x - 5.0F, screen->y - 5.0F, screen->x + 5.0F, screen->y + 5.0F, color);
      char label[32] = {};
      if (entity.time_left > 0.0F) {
        std::snprintf(label, sizeof(label), "C4 %.1f", static_cast<double>(entity.time_left));
      } else {
        std::snprintf(label, sizeof(label), "C4");
      }
      frame.out.text(screen->x, screen->y - 22.0F, color, label, true, true);
    } else {
      const render::Color color = to_render(config.esp.color_grenade);
      frame.out.fill(screen->x - 3.0F, screen->y - 3.0F, screen->x + 3.0F, screen->y + 3.0F, color);
      std::string label = entity.name;
      constexpr std::string_view prefix = "weapon_";
      if (label.starts_with(prefix)) {
        label = label.substr(prefix.size());
      }
      frame.out.text(screen->x, screen->y - 20.0F, color, label, true, true);
    }
    if (frame.stats != nullptr) {
      ++frame.stats->world_markers;
    }
  }
}

}  // namespace cs2::features::esp_detail
