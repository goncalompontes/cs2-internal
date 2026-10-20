#include "cs2/control/events.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "cs2/control/event_names.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/module.hpp"
#include "cs2/hooks/memory.hpp"
#include "cs2/tls/bootstrap.hpp"
#include "offsets.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::control {
namespace {

using namespace cs2::sdk;

constexpr std::string_view k_client_module = "client.dll";

// Window within which a repeated event name is treated as the same event
// delivered by both hooked dispatches.
constexpr unsigned long long k_dedupe_ms = 50;

// The manager's dispatch methods keep the caller's third register (FireEvent
// takes `bDontBroadcast`; FireEventClientSide does not), so the detour carries
// it through unchanged.
using fire_fn = void (*)(void* self, void* event, std::uintptr_t extra);

// `IGameEventManager2` vtable slots for `FireEvent` / `FireEventClientSide`.
constexpr std::size_t k_manager_fire_slots[] = {7, 8};  // +0x38, +0x40

// `IGameEvent` vtable slot for `GetName()`. The manager's own dispatch reads it
// at `+0x8` (`FireEvent` calls `(**(code**)(*event + 8))(event)`), so that slot
// is definitive: do not probe other slots, which would call a non-`GetName`
// function on a live event.
constexpr std::size_t k_event_name_slot = 1;  // +0x08

// `IGameEvent` getters (from the event object's own vtable, `0x181c2b5a0`;
// cross-checked against the manager's serializer and UnknownCheats' reverse of
// `CGameEvent`). `GetUint64` (slot 8) is unused here; bools read as bools.
constexpr std::size_t k_event_get_bool_slot = 6;     // +0x30
constexpr std::size_t k_event_get_int_slot = 7;      // +0x38
constexpr std::size_t k_event_get_float_slot = 9;    // +0x48
constexpr std::size_t k_event_get_string_slot = 10;  // +0x50
// `GetPlayerIndex(int* out, const EventKey& key)` — for `userid`/`attacker`.
constexpr std::size_t k_event_get_player_slot = 15;  // +0x78

// Field-name keys are `{uint32 hash; uint32 pad; const char* str}`, hashed with
// the engine's case-insensitive `MurmurHash2` (`event_key_hash`); its callers
// pass `seed = len ^ 0x31415926`.
struct EventKey {
  std::uint32_t hash;
  std::uint32_t pad;
  const char* str;
};

constexpr std::uint32_t k_string_token_seed = 0x31415926u;

using key_hash_fn = std::uint32_t (*)(const char* str, std::size_t length, std::uint32_t seed);
using get_int_fn = std::int32_t (*)(void* self, const void* key, std::int32_t def);
using get_bool_fn = bool (*)(void* self, const void* key, bool def);
using get_float_fn = float (*)(void* self, const void* key, float def);
using get_string_fn = const char* (*)(void* self, const void* key, const char* def);
using get_player_fn = void (*)(void* self, std::int32_t* out, const void* key);

using get_name_fn = const char* (*)(void* event);

// The one EventLog the payload owns, for the hook detours.
EventLog* g_active = nullptr;
// Resolved from the `event_key_hash` signature; null leaves keys unhashed.
key_hash_fn g_key_hash = nullptr;

// A game-event name is lower-case `[a-z0-9_]` and short.
auto plausible_name(std::string_view name) noexcept -> bool {
  if (name.empty() || name.size() > EventEntry::k_max_name_length) {
    return false;
  }
  for (const char character : name) {
    const bool ok = character == '_' || (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9');
    if (!ok) {
      return false;
    }
  }
  return true;
}

// Captured without an explicit `subscribe`. Every name is in the build's own
// definition list (`event_names.hpp`), so a stale entry shows up as a rejection
// rather than silently never matching. The filter also keeps high-rate noise out
// of the ring.
constexpr std::string_view k_default_events[] = {
    "round_start",   "round_end",           "round_freeze_end",   "round_officially_ended",
    "round_mvp",     "begin_new_match",     "announce_phase_end", "player_death",
    "player_hurt",   "player_spawned",      "other_death",        "bomb_planted",
    "bomb_defused",  "bomb_exploded",       "bomb_dropped",       "bomb_pickup",
    "item_purchase", "gameinstructor_draw", "cs_win_panel_round", "client_disconnect",
};

}  // namespace

void __fastcall EventLog::fire_detour_a(void* self, void* event, std::uintptr_t extra) noexcept {
  // Runs on the game thread, which has no TLS block for our mapped image yet.
  cs2::tls::ensure_thread_tls();
  if (g_active != nullptr) {
    g_active->capture(event);
    if (g_active->originals_[0] != nullptr) {
      reinterpret_cast<fire_fn>(g_active->originals_[0])(self, event, extra);
    }
  }
}

void __fastcall EventLog::fire_detour_b(void* self, void* event, std::uintptr_t extra) noexcept {
  cs2::tls::ensure_thread_tls();
  if (g_active != nullptr) {
    g_active->capture(event);
    if (g_active->originals_[1] != nullptr) {
      reinterpret_cast<fire_fn>(g_active->originals_[1])(self, event, extra);
    }
  }
}

auto EventLog::is_subscribed(std::string_view name) const noexcept -> bool {
  // The count is published with a release store (after the name is written),
  // so a reader that sees it also sees the name.
  const std::size_t count = subscription_count_.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < count; ++i) {
    if (name == std::string_view(subscribed_[i])) {
      return true;
    }
  }
  return false;
}

void EventLog::remember(std::string_view name) noexcept {
  const std::size_t count = subscription_count_.load(std::memory_order_relaxed);
  if (count >= k_max_subscriptions) {
    return;
  }
  const std::size_t length =
      name.size() < EventEntry::k_max_name_length ? name.size() : EventEntry::k_max_name_length;
  std::memcpy(subscribed_[count], name.data(), length);
  subscribed_[count][length] = '\0';
  // Publish the count only after the name is written: the game thread reads
  // it without the lock (it must not block on the control or render thread).
  subscription_count_.store(count + 1, std::memory_order_release);
}

void EventLog::refresh() noexcept {
  const std::lock_guard<std::mutex> lock(mutex_);
  refresh_locked();
}

void EventLog::refresh_locked() noexcept {
  if (installed_) {
    return;
  }
  const cs2::game::ModuleInfo module = cs2::game::find_module(k_client_module);
  if (!module.valid()) {
    return;
  }
  const OffsetInfo* offset = find_offset("game_event_manager");
  if (offset == nullptr || offset->module != k_client_module) {
    return;
  }
  void* manager = *reinterpret_cast<void**>(module.base + offset->rva);
  if (manager == nullptr) {
    return;
  }
  if (const OffsetInfo* hash_offset = find_offset("event_key_hash");
      hash_offset != nullptr && hash_offset->module == k_client_module) {
    g_key_hash = reinterpret_cast<key_hash_fn>(module.base + hash_offset->rva);
  }
  void** vtable = *reinterpret_cast<void***>(manager);
  if (vtable == nullptr || !module.contains(reinterpret_cast<std::uintptr_t>(vtable))) {
    return;
  }
  g_active = this;
  std::size_t patched = 0;
  void* const detours[k_hooks] = {reinterpret_cast<void*>(&EventLog::fire_detour_a),
                                  reinterpret_cast<void*>(&EventLog::fire_detour_b)};
  for (std::size_t i = 0; i < k_hooks; ++i) {
    // Patch the vtable slot rather than the prologue: the dispatch prologues are
    // too long for the inline hook's trampoline, and this matches the DXGI
    // method-table interposition (ADR-0015).
    void** slot = &vtable[k_manager_fire_slots[i]];
    if (*slot == nullptr || !cs2::hooks::make_writable(slot, sizeof(void*))) {
      continue;
    }
    originals_[i] = *slot;
    *slot = detours[i];
    ++patched;
  }
  if (patched == 0) {
    g_active = nullptr;
    return;
  }
  installed_ = true;
  cs2::game::append_log("[cs2-payload] events: dispatch hooked (" + std::to_string(patched) +
                        "/2)");
  for (const std::string_view name : k_default_events) {
    if (subscribe_locked(name) == k_subscribe_rejected) {
      cs2::game::append_log(std::string("[cs2-payload] events: stale default '") +
                            std::string(name) + "'");
    }
  }
}

auto EventLog::subscribe(std::string_view name) noexcept -> std::int32_t {
  // One lock for the install and the subscription: the control thread (the
  // harness's `events` command) and the render thread (the sound provider's
  // lazy subscription) can both ask for the events, and two concurrent
  // installs would patch the dispatch vtable twice and chain the detours.
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!installed_) {
    refresh_locked();
  }
  if (!installed_ || name.empty()) {
    return k_subscribe_not_ready;
  }
  return subscribe_locked(name);
}

auto EventLog::subscribe_locked(std::string_view name) noexcept -> std::int32_t {
  if (is_subscribed(name)) {
    return k_subscribe_already;
  }
  if (!is_known_event(name)) {
    return k_subscribe_rejected;  // not an event this build defines
  }
  remember(name);
  return static_cast<std::int32_t>(subscription_count_.load(std::memory_order_relaxed) - 1);
}

void EventLog::record_event(void* event) noexcept {
  void** vtable = *reinterpret_cast<void***>(event);
  if (vtable == nullptr) {
    return;
  }
  const char* name = reinterpret_cast<get_name_fn>(vtable[k_event_name_slot])(event);
  if (name == nullptr || !plausible_name(name) || !g_active->is_subscribed(name)) {
    return;
  }
  const unsigned long long now = GetTickCount64();
  if (g_active->has_last_ && std::strcmp(g_active->last_name_, name) == 0 &&
      now - g_active->last_tick_ms_ < k_dedupe_ms) {
    return;  // the two hooked dispatches both deliver each event
  }
  const std::size_t length = std::strlen(name);
  const std::size_t copy =
      length < EventEntry::k_max_name_length ? length : EventEntry::k_max_name_length;
  std::memcpy(g_active->last_name_, name, copy);
  g_active->last_name_[copy] = '\0';
  g_active->last_tick_ms_ = now;
  g_active->has_last_ = true;

  EventArg args[EventEntry::k_max_args];
  const std::size_t arg_count = read_args(event, vtable, name, args);
  g_active->ring_.push(name, args, arg_count, now);
}

auto EventLog::read_args(void* event, void** vtable, std::string_view name, EventArg* out) noexcept
    -> std::size_t {
  const EventFieldRange range = event_fields(name);
  std::size_t count = 0;
  for (std::size_t i = 0; i < range.size && count < EventEntry::k_max_args; ++i) {
    const EventFieldDef& field = range.data[i];
    char key_buf[EventArg::k_max_key_length + 1] = {};
    const std::size_t key_length = field.name.size() < EventArg::k_max_key_length
                                       ? field.name.size()
                                       : EventArg::k_max_key_length;
    std::memcpy(key_buf, field.name.data(), key_length);
    // The key carries the engine's own hash of the field name.
    const std::uint32_t hash =
        g_key_hash != nullptr
            ? g_key_hash(key_buf, key_length,
                         static_cast<std::uint32_t>(key_length) ^ k_string_token_seed)
            : 0u;
    const EventKey key{hash, 0, key_buf};
    char value[EventArg::k_max_value_length + 1] = {};
    if (field.kind == 'i') {
      std::snprintf(value, sizeof(value), "%d",
                    static_cast<int>(reinterpret_cast<get_int_fn>(vtable[k_event_get_int_slot])(
                        event, &key, -1)));
    } else if (field.kind == 'b') {
      const bool truth =
          reinterpret_cast<get_bool_fn>(vtable[k_event_get_bool_slot])(event, &key, false);
      std::snprintf(value, sizeof(value), "%s", truth ? "true" : "false");
    } else if (field.kind == 'p') {
      std::int32_t index = -1;
      reinterpret_cast<get_player_fn>(vtable[k_event_get_player_slot])(event, &index, &key);
      std::snprintf(value, sizeof(value), "%d", static_cast<int>(index));
    } else if (field.kind == 'f') {
      std::snprintf(value, sizeof(value), "%g",
                    static_cast<double>(reinterpret_cast<get_float_fn>(
                        vtable[k_event_get_float_slot])(event, &key, 0.0f)));
    } else {
      const char* text =
          reinterpret_cast<get_string_fn>(vtable[k_event_get_string_slot])(event, &key, "");
      std::snprintf(value, sizeof(value), "%s", text != nullptr ? text : "");
    }
    std::memcpy(out[count].key, key_buf, key_length + 1);
    std::snprintf(out[count].value, sizeof(out[count].value), "%s", value);
    ++count;
  }
  return count;
}

void EventLog::capture(void* event) noexcept {
  ++fire_calls_;
  if (event == nullptr) {
    return;
  }
  record_event(event);
}

auto EventLog::recent(std::size_t limit) const noexcept -> Json {
  EventEntry entries[EventRing::k_capacity];
  const std::size_t capacity = limit < EventRing::k_capacity ? limit : EventRing::k_capacity;
  const std::size_t captured = ring_.recent(entries, capacity);
  Json out = Json::array();
  for (std::size_t i = 0; i < captured; ++i) {
    const EventEntry& entry = entries[i];
    Json args = Json::object();
    for (std::size_t a = 0; a < entry.arg_count; ++a) {
      args.set(entry.args[a].key, Json::string(entry.args[a].value));
    }
    Json item = Json::object();
    item.set("name", Json::string(entry.name));
    item.set("seq", Json::integer(static_cast<std::int64_t>(entry.seq)));
    item.set("args", std::move(args));
    out.push_back(std::move(item));
  }
  return out;
}

}  // namespace cs2::control
