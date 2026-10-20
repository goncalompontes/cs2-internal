#pragma once

// The ESP's out-of-module data sources and its per-frame caches.
//
// The features module cannot reach the control module's event ring or the
// engine's entity table on its own, so the owner of the `Esp` supplies them
// through the provider callbacks below — the same provider/sink pattern the
// overlay already uses for the hitbox visualiser (`Overlay::set_hitbox_provider`).
//
// The caches are members of the `Esp` instance, never globals (ADR-0010).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cs2/game/entity_system.hpp"
#include "cs2/game/math.hpp"

namespace cs2::features {

// The world markers the entity walk classifies.
enum class WorldEntityKind : std::uint8_t {
  grenade,
  c4,
  dropped_weapon,
};

// The kind of grenade a marker stands for (the per-type tints).
enum class GrenadeKind : std::uint8_t {
  smoke,
  flash,
  molotov,
  incendiary,
  he,
  decoy,
};

// Where the radar widget sits. There is no setting for it, so it is a
// constant; the enum keeps the position a mode rather than a magic int.
enum class RadarPosition : std::uint8_t {
  bottom_left,
  bottom_right,
  top_left,
  top_right,
};

// A player's visibility state.
//
// The authoritative test is the engine's world-occlusion query
// (`cs2::control::query_world_occluders`), which is expensive: it enumerates
// the solids between the eye and the target. It lives in the control module,
// so the ESP asks for the answer through a provider instead of running the
// query itself. Without a provider the state stays `unknown`, and the
// visibility toggles fall back to "treat every player as visible".
enum class Visibility : std::uint8_t {
  unknown = 0,  // no source wired: the player is treated as visible
  visible = 1,
  hidden = 2,
};

// One captured sound event.
//
// `player_index` is the entity index of the player the sound came from
// (`-1` when the event does not name one) and `radius` the sound's radius in
// world units. `sequence` is the event's sequence number, which the ESP uses
// to spot the events it has not seen yet.
struct SoundEvent {
  int player_index = -1;
  float radius = 0.0F;
  std::uint64_t sequence = 0;
};

// The data sources the ESP asks for.
struct EspProviders {
  // A pawn's real studio hitbox capsules, in world space. Returns the number
  // written (<= `max`); 0 when the pawn has no readable model.
  //
  // The overlay's hitbox provider is the intended implementation; when it is
  // not set the ESP falls back to the same model reader the provider uses
  // (`aim::read_model_hitboxes`), so there is exactly one model reader in the
  // tree.
  using HitboxProvider = int (*)(void* context, std::uintptr_t pawn, game::HitboxCapsule* out,
                                 int max);
  HitboxProvider hitboxes = nullptr;
  void* hitboxes_context = nullptr;

  // The most recent sound events, oldest first. Returns the number written.
  using SoundProvider = int (*)(void* context, SoundEvent* out, int max);
  SoundProvider sounds = nullptr;
  void* sounds_context = nullptr;

  // A player's visibility state. The provider is expected to answer from a
  // **cached, low-rate** query: the authoritative world-occlusion query is
  // far too expensive to run per player per frame.
  using VisibilityProvider = Visibility (*)(void* context, int player_index);
  VisibilityProvider visibility = nullptr;
  void* visibility_context = nullptr;
};

// The per-pawn studio hitbox cache.
//
// Reading a pawn's studio hitboxes is the ESP's most expensive read (the
// model's hitbox set and the bone remap are several hundred scattered reads),
// so the set is cached per pawn and refreshed at a bounded rate.
struct HitboxCache {
  // A player has 19 studio hitboxes; leave headroom.
  static constexpr int k_max_capsules = 40;

  struct Entry {
    std::uintptr_t pawn = 0;
    int count = 0;
    std::array<game::HitboxCapsule, k_max_capsules> capsules{};
    std::uint64_t stamp_ms = 0;
  };

  std::vector<Entry> entries;

  // The reads performed in the current frame, against the per-frame cap.
  int reads = 0;

  [[nodiscard]] auto find(std::uintptr_t pawn) noexcept -> Entry* {
    for (Entry& entry : entries) {
      if (entry.pawn == pawn) {
        return &entry;
      }
    }
    return nullptr;
  }
};

// A classified world entity (a grenade, the bomb or a dropped weapon).
struct WorldEntity {
  WorldEntityKind kind = WorldEntityKind::grenade;
  GrenadeKind grenade = GrenadeKind::he;
  game::Vec3 origin{};
  std::string name;        // the designer name (the dropped weapon's name)
  float time_left = 0.0F;  // the bomb's remaining time, in seconds
  bool ticking = false;    // the bomb is counting down
};

// The classified world entities. The entity list is paged and large, so the
// walk runs at a low rate and the result is cached.
struct WorldCache {
  // The classified entities (published when a sweep finishes).
  std::vector<WorldEntity> items;

  // The sweep's scratch buffer: the entities found so far.
  std::vector<WorldEntity> scratch;

  // The entity list the walk reads.
  game::EntitySystem entity_system{};

  // The next identity slot the walk examines.
  int cursor = 0;

  // How many sweeps have finished (diagnostics).
  int sweeps = 0;

  // The last walk's time, in milliseconds.
  std::uint64_t last_walk_ms = 0;
};

// The ESP's own ring of recent sounds. The provider replays the game's recent
// events every frame; the ESP keeps the ones it has not seen yet and drops
// them once they have faded out.
struct SoundCache {
  static constexpr std::size_t k_capacity = 32;

  struct Entry {
    SoundEvent event{};
    std::uint64_t time_ms = 0;
  };

  std::array<Entry, k_capacity> entries{};
  std::size_t count = 0;
  std::size_t next = 0;

  // The newest sequence number the ESP has ingested.
  std::uint64_t last_sequence = 0;

  // Append an event, evicting the oldest one when the ring is full.
  void push(const SoundEvent& event, std::uint64_t time_ms) noexcept {
    entries[next].event = event;
    entries[next].time_ms = time_ms;
    next = (next + 1) % k_capacity;
    if (count < k_capacity) {
      ++count;
    }
    if (event.sequence > last_sequence) {
      last_sequence = event.sequence;
    }
  }
};

}  // namespace cs2::features
