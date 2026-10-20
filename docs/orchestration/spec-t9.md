# T9 — wire the ESP's out-of-module data providers (sound + visibility)

**Goal:** the ESP expansion shipped with two features that are **inert** — the
sound ESP and the "glow / visible-only" toggles — because the data they need
lives in the **control** module, which the features module cannot depend on
(meson subdir order: `features` before `control`; and `control` already depends
on `features` — a cycle).

The ESP side is complete: `cs2::features::EspProviders` carries a `sounds` and a
`visibility` callback, and `cs2::features::set_esp_providers(...)` is the
registry the owner uses. **What is missing is the two implementations and the
one registration call.**

**File ownership (you may ONLY touch these):**

- `payload/control/include/cs2/control/service.hpp`
- `payload/control/src/service.cpp`
- `payload/control/include/cs2/control/events.hpp` (only if the event
  subscription set must change — see §2)
- `payload/control/src/events.cpp` (same)
- `payload/control/include/cs2/control/event_ring.hpp`
- `payload/control/src/event_ring.cpp`
- `payload/features/include/cs2/features/esp.hpp` (only the provider **types**,
  if they need to change — the *wiring* is not yours)
- `payload/src/payload_main.cpp` (the registration call)
- `payload/features/tests/test_esp_geometry.cpp` or a new host test
- root `meson.build` (the test wiring)

**Do NOT touch** `payload/features/src/esp_*.cpp` (the ESP's own drawing) or
`overlay/**`.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`payload/features/include/cs2/features/esp.hpp` (the `EspProviders` type and
the `EspProvider` signatures) and `payload/control/src/service.cpp`.

---

## 1. What the ESP expects

Read `EspProviders` in `payload/features/include/cs2/features/esp.hpp` for the
exact callback signatures. In outline:

- **`sounds(context, out, max)`** — fill `out` with the recent sound events
  (the sounder's position, the radius, the age) and return how many.
- **`visibility(context, player_index)`** — return `visible` / `hidden` /
  `unknown` for a player index.

`set_esp_providers(...)` is the registry: one call installs both.

## 2. The sound provider

The payload already captures the game's events: `EventLog` hooks the client's
`CGameEventManager` dispatch vtable and pushes into an `EventRing` (64 entries,
6 args each, **all argument values are strings**).

The sound ESP wants **`player_footstep`** and **`player_sound`** events. Two
things are needed:

1. **They are not in the default subscription set.** The default 20 events are
   the round/player/bomb ones (`events.cpp`). `player_footstep` / `player_sound`
   are **known** event names (`event_names.hpp` has them) but are only captured
   if subscribed. **Subscribe them** — either in the default set, or lazily when
   the sound ESP is enabled. Say which, and why.
   - `player_sound` carries `radius` and `duration`; `player_footstep` carries
     only `userid`. `player_footstep`'s position must therefore come from the
     **sounding player's position at the time the event is seen** — say so in
     the comment, because it is an approximation (the player has moved since).
2. **A way to read the recent events from the control service.** The
   `EventRing` holds them; the service must expose a **typed** accessor for
   the sound provider, e.g.
   `Service::recent_sounds(SoundEvent* out, int max) -> int`, where `SoundEvent`
   carries `{player_index, x, y, z, radius, age_ms}`.

   The `userid` argument is a **player slot** (the `GetPlayerIndex` accessor
   returns the slot). The service must map the slot to the player's **world
   position** — the snapshot has the players and their origins.

   **Bound it**: the ring holds 64 events; the sound markers must be a bounded
   list (the ESP's spec says ≤ 32 markers). Do not allocate in the hot path.

**The provider's cost is one call per ESP refresh** (~30 Hz), not per frame, and
it must not block: it reads the ring (spinlock) and the snapshot (the control
thread's own reader). Keep it allocation-free — the payload is manually mapped.

## 3. The visibility provider

The authoritative test is `cs2::control::query_world_occluders`
(`payload/control/include/cs2/control/occlusion_query.hpp`) — it enumerates the
world solids between the eye and the target. **It is expensive**: the ragebot
uses it at ~1 Hz, and it is not a per-player-per-frame operation.

**Requirement:** the provider must return a **cached, low-rate** answer. The
design:

- a per-player visibility state in the `Service`, refreshed at a **low rate**
  (2–4 Hz is the spec's recommendation) and **on demand** for the players the
  ESP is actually drawing (the ESP asks per drawn player; the service answers
  from its cache, and refreshes the cache lazily);
- a **budget** per refresh: do not query every player every refresh. The ESP
  asks for the players it draws (usually ≤ 10), so a per-refresh query of the
  requested set is acceptable **if** each query is bounded — check what
  `query_world_occluders` costs and how it is used by the ragebot
  (`Service::do_heatmap` drives it) and **reuse that machinery**, do not write
  a second one;
- if a player's state is not known, return **`unknown`** — the ESP's contract
  is that `unknown` never hides a player and never fires the glow on a guess.
  That is already implemented in the ESP (`visibility_for`).

**The failure mode to avoid:** the ragebot's occlusion query is already heavy
(the handoff measures a ~1 ms spot-dependent cost and much worse at dense
spots). A per-player-per-refresh query from the **render** thread would
regress the frame time. **Measure** (or reason precisely) and report the cost.
If a cheap per-player visibility is not achievable, the honest answer is: keep
the `unknown` fallback, return `unknown` for everything, and **report that the
glow/visible-only toggles need a cheap visibility source** (e.g. the engine's
own "spotted" state) rather than shipping something that costs milliseconds
per frame. **Do not silently ship an expensive per-draw query.**

## 4. The registration

One call, on the install thread, in `payload/src/payload_main.cpp`:

```cpp
// The ESP's sound and visibility providers: the data lives in the control
// module, which cannot be a dependency of the features module, so the
// features module asks for it through these callbacks.
runtime().set_esp_providers({ ... });
```

Build the `EspProviders` value with the **control service** as the context and
its two methods as the callbacks.

## 5. Quality bar

- `docs/style-guide.md` is normative: trailing return types, `snake_case`,
  `SCREAMING_SNAKE_CASE`, `enum class`, `[[nodiscard]]`, `noexcept`,
  fully-qualified `std::`, **no exceptions**, no new global mutable state
  (ADR-0010).
- The payload is **manually mapped**: no CRT static-init, no `<print>`, and
  every engine read through the fault-safe helpers.
- The sound provider must be **allocation-free** in the hot path.
- The visibility cache must be **thread-safe**: the ESP asks from the **render**
  thread; the control service's state is touched from the control thread and
  the game thread. Follow the locking the existing shared state uses
  (`occlusion_mutex_`, `records_mutex_` — see `service.hpp`).

## 6. Verification

- `meson compile -C build && meson test -C build` → green (21/21 at baseline).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → clean.
- A **host test** for the parts that are testable without the game: the
  sound-event mapping (an event ring entry → a `SoundEvent`) and the
  visibility cache's state machine (unknown → visible/hidden, and the
  staleness rule). The game-dependent parts cannot be host-tested — say so.
- **Prove the test can fail.**

## 7. Report

- The sound provider's design, the subscription decision, and the position
  approximation's accuracy.
- The visibility provider's design and its **measured or reasoned cost per
  refresh**; what it returns when it cannot answer; and whether the glow and
  the visible-only toggle are actually functional or still inert.
- The registration call site.
- What you could not verify without a live game (you cannot launch CS2 — it
  needs `schtasks /run /tn CS2Launch` and a GPU in the interactive session).
