// The ESP's two out-of-module providers, host-side:
//
//   - the sound-event mapping (a captured event -> the sound fields and the
//     player the marker belongs to), and
//   - the visibility cache's state machine (unknown -> visible / hidden and
//     the staleness rule).
//
// Both are the parts that do not need the game: the parts that do (the event
// dispatch hooks, the snapshot and the world-occlusion query) live in the
// payload.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

#include "cs2/control/event_ring.hpp"
#include "cs2/control/service.hpp"

namespace {

using cs2::control::EventArg;
using cs2::control::EventEntry;
using cs2::control::EventRing;

// One captured event, as the event log pushes it.
auto event(std::string_view name, const EventArg* args, std::size_t count) -> EventEntry {
  EventEntry entry;
  std::memcpy(entry.name, name.data(), name.size());
  entry.arg_count = static_cast<std::uint8_t>(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::memcpy(entry.args[i].key, args[i].key, sizeof(args[i].key));
    std::memcpy(entry.args[i].value, args[i].value, sizeof(args[i].value));
  }
  return entry;
}

// One captured argument, as the event log formats it.
auto arg(std::string_view key, std::string_view value) -> EventArg {
  EventArg out;
  std::memcpy(out.key, key.data(), key.size());
  std::memcpy(out.value, value.data(), value.size());
  return out;
}

// The sound fields of the captured events.
void test_sound_fields() {
  const EventArg footstep[] = {arg("userid", "7")};
  const auto stepped = cs2::control::read_sound_fields(event("player_footstep", footstep, 1));
  assert(stepped.valid);
  assert(stepped.slot == 7);
  assert(stepped.radius == 0.0F);  // a footstep carries no radius

  const EventArg sound[] = {arg("userid", "3"), arg("radius", "500")};
  const auto heard = cs2::control::read_sound_fields(event("player_sound", sound, 2));
  assert(heard.valid);
  assert(heard.slot == 3);
  assert(heard.radius == 500.0F);

  // Another event is not a sound event, even with a `userid`.
  const auto death = cs2::control::read_sound_fields(event("player_death", footstep, 1));
  assert(!death.valid);

  // An unusable `userid` is rejected: the event cannot be placed.
  const EventArg broken[] = {arg("userid", "-")};
  assert(!cs2::control::read_sound_fields(event("player_footstep", broken, 1)).valid);

  // A missing `userid` is rejected too.
  assert(!cs2::control::read_sound_fields(event("player_footstep", nullptr, 0)).valid);
}

// The ring walk the sound provider does: the captured events, oldest first,
// reduced to their sound fields.
void test_ring_walk() {
  EventRing ring;
  const EventArg footstep[] = {arg("userid", "2")};
  const EventArg sound[] = {arg("userid", "5"), arg("radius", "250")};
  ring.push("player_death", nullptr, 0, 1000);
  ring.push("player_footstep", footstep, 1, 1100);
  ring.push("player_sound", sound, 2, 1200);

  struct Found {
    int slot = -1;
    float radius = 0.0F;
    std::uint64_t sequence = 0;
    std::uint64_t time_ms = 0;
  };
  Found found[8];
  std::size_t count = 0;
  const std::size_t visited = ring.visit([&found, &count](const EventEntry& entry) -> bool {
    const auto fields = cs2::control::read_sound_fields(entry);
    if (!fields.valid) {
      return true;
    }
    found[count] = Found{.slot = fields.slot,
                         .radius = fields.radius,
                         .sequence = entry.seq,
                         .time_ms = entry.time_ms};
    ++count;
    return true;
  });

  assert(visited == 3);  // every retained event is visited
  assert(count == 2);    // but only the two are sound events
  assert(found[0].slot == 2);
  assert(found[0].radius == 0.0F);
  assert(found[0].sequence == 2);
  assert(found[0].time_ms == 1100);
  assert(found[1].slot == 5);
  assert(found[1].radius == 250.0F);
  assert(found[1].sequence == 3);
  assert(found[1].time_ms == 1200);
}

// The slot -> player resolution (the event's `userid` is the game's player
// slot; the snapshot keys its players by the entity index).
void test_player_resolution() {
  const cs2::control::SoundPlayer players[] = {
      {.index = 100, .origin = {.x = 1.0F, .y = 2.0F, .z = 3.0F}},
      {.index = 200, .origin = {.x = 4.0F, .y = 5.0F, .z = 6.0F}},
  };
  // The rank: the slot is the player's rank in the snapshot's list.
  const auto* const ranked = cs2::control::resolve_sound_player(players, 2, 1);
  assert(ranked != nullptr);
  assert(ranked->index == 200);
  assert(ranked->origin.x == 4.0F);

  // The exact match (the fallback): the slot is the entity index.
  const auto* const exact = cs2::control::resolve_sound_player(players, 2, 200);
  assert(exact != nullptr);
  assert(exact->index == 200);

  // Out of range and malformed slots name no player.
  assert(cs2::control::resolve_sound_player(players, 2, 7) == nullptr);
  assert(cs2::control::resolve_sound_player(players, 2, -1) == nullptr);
  assert(cs2::control::resolve_sound_player(nullptr, 0, 0) == nullptr);
}

// The visibility cache: unknown until a state is published, then the state
// until it goes stale.
void test_visibility_cache() {
  cs2::control::VisibilityCache cache;
  assert(cache.get(3, 1000) == cs2::control::Visibility::unknown);

  cache.publish(3, cs2::control::Visibility::visible, 1000);
  assert(cache.get(3, 1000) == cs2::control::Visibility::visible);
  assert(cache.get(3, 1000 + cs2::control::VisibilityCache::k_ttl_ms) ==
         cs2::control::Visibility::visible);
  // Stale: the provider fails closed rather than answer with an old state.
  assert(cache.get(3, 1000 + cs2::control::VisibilityCache::k_ttl_ms + 1) ==
         cs2::control::Visibility::unknown);

  // A new state replaces the old one.
  cache.publish(3, cs2::control::Visibility::hidden, 2000);
  assert(cache.get(3, 2000) == cs2::control::Visibility::hidden);
  // Another player is unaffected.
  assert(cache.get(4, 2000) == cs2::control::Visibility::unknown);

  // The cache is bounded: the players that do not fit evict the oldest
  // states.
  for (int i = 0; i < 65; ++i) {
    cache.publish(100 + i, cs2::control::Visibility::visible, 3000 + static_cast<std::uint64_t>(i));
  }
  assert(cache.get(3, 3000) == cs2::control::Visibility::unknown);
  assert(cache.get(100, 3000) == cs2::control::Visibility::unknown);
  assert(cache.get(101, 3001) == cs2::control::Visibility::visible);
  assert(cache.get(164, 3064) == cs2::control::Visibility::visible);

  // Clearing forgets every state.
  cache.clear();
  assert(cache.get(164, 3064) == cs2::control::Visibility::unknown);
}

}  // namespace

auto main() -> int {
  test_sound_fields();
  test_ring_walk();
  test_player_resolution();
  test_visibility_cache();
  std::puts("control-sound: ok");
  return 0;
}
