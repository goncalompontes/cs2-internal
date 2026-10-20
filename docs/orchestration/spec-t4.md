# T4 — ESP expansion (the "chams and other ESP features" part that needs no new engine hook)

**Goal:** turn the current ESP (box, health bar, ammo bar, name, weapon, flags) into a
full-featured visual suite: box styles, head box, skeleton, tracers, off-screen
arrows, distance, distance scaling, team/spectator filters, a radar, sound ESP,
grenade/C4/dropped-weapon ESP, and per-element colours.

**File ownership (you may ONLY touch these):**

- `payload/features/src/esp.cpp`
- `payload/features/include/cs2/features/esp.hpp`
- `payload/render/include/cs2/render/draw_list.hpp`
- `payload/render/src/imgui_backend.cpp`
- `overlay/src/renderer.cpp` (only the `filled_poly` rendering gap — see §6)
- new files under `payload/features/src/` and `payload/features/include/cs2/features/`
  that you create (name them descriptively)
- `payload/features/meson.build` (to add the new sources)

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

**T1 owns the settings.** It has already added these `wire::Settings` fields and the
matching `features::Config` members — **use them, do not add new ones**:

```
uint8_t esp_box_thickness;      // 0..20 -> 0.5..5.0 px
uint8_t esp_box_cornered;        // 0 = straight box, 1 = corner box
uint8_t esp_head_box;
uint8_t esp_skeleton;
uint8_t esp_tracer;
uint8_t esp_tracer_origin;       // 0 = screen bottom, 1 = local eye origin
uint8_t esp_offscreen;
uint8_t esp_glow;
uint8_t esp_visible_only;
uint8_t esp_distance;
uint8_t esp_scale_by_distance;
uint8_t esp_max_distance_m;      // 0 = unlimited
uint8_t esp_radar;
uint8_t esp_sound;
uint8_t esp_grenades;
uint8_t esp_c4;
uint8_t esp_weapons;
uint8_t esp_local_player;
uint8_t esp_show_spectators;
uint8_t esp_show_teammates;
float  esp_max_box_scale;        // 0.25 .. 2.0
uint8_t esp_color_enemy[4];
uint8_t esp_color_team[4];
uint8_t esp_color_box[4];
uint8_t esp_color_skeleton[4];
uint8_t esp_color_tracer[4];
uint8_t esp_color_offscreen[4];
uint8_t esp_color_sound[4];
uint8_t esp_color_grenade[4];
uint8_t esp_color_c4[4];
```

T1 also replaced the flat `features::EspConfig` with a **sectioned, typed**
`features::Config` (`Config::esp`, `Config::movement`, `Config::visuals`,
`Config::menu`). The ESP code must read `config_.esp.*` (a typed struct with
`bool` flags, `float`s, a `Color` type and `enum class`es), **not** raw
`uint8_t`s. `HitboxMode` stays where it is.

If a needed setting does **not** exist in the list above, do **not** add it —
implement the feature with the settings that exist and say so in the report.

---

## 1. The current state

`payload/features/src/esp.cpp` (`Esp::build`) draws, per enemy: a box, a health
bar, an ammo bar, the name, the weapon and a right-hand flag column
(`$money`, `A`, `H`, `K`, `Z`, `D`, `B`, `F`, ping, distance).

`payload/features/src/overlay.cpp` draws the studio hitbox capsules (outline /
solid / wireframe) and a bhop-prediction box.

`payload/render/include/cs2/render/draw_list.hpp` is the UI-agnostic draw list:
`outline`, `fill`, `line`, `text`, `poly` (a filled convex polygon), over a
`std::variant` of `Rect | FilledRect | Line | Text | FilledPoly`.

`payload/game/include/cs2/game/matrix.hpp` has `Matrix4x4::project(world, w, h)`
(world → screen pixels, `std::nullopt` when behind the camera).

`payload/game/include/cs2/game/snapshot.hpp` has `PlayerSnapshot` (health, armor,
team, life state, origin, eye origin, velocity, eye angles, weapon, clip, reserve,
money, ping, flags, distance).

## 2. What to build

Split the work into focused translation units under `payload/features/src/`
(e.g. `esp_players.cpp`, `esp_world.cpp`, `esp_radar.cpp`, `esp_sound.cpp`) so the
file does not become a 1000-line function. `Esp::build` should orchestrate them.

### 2.1 Box

- **Straight** (today) and **cornered** (the classic "corner accents" style:
  8 short segments at the box corners, each ~1/4 of the side length).
- **Thickness** from `esp_box_thickness` (0..20 → 0.5..5.0 px).
- The 1 px black outline behind the coloured box must stay (readability).

### 2.2 Head box

A small box around the head. The head's world position comes from the **studio
hitbox** with hitgroup `HITGROUP_HEAD` (group 1) — the overlay already reads the
studio capsules (`Service::read_hitboxes` → `Overlay`'s hitbox provider). Reuse
that path; do **not** re-read the model from the ESP.

If no head hitbox is available for a player, fall back to an estimate
(eye origin + a fixed offset along the look direction) — and say so in the report.

### 2.3 Skeleton

Draw the player's bones as lines. **Derive them from the studio hitbox capsules**:
each capsule has a hitgroup and a bone index, and the capsule endpoints are bone
positions. Build an anatomical chain from the hitgroups and connect the
corresponding capsule midpoints.

Define the chain (hitgroup → body part) explicitly, e.g.:

```
head → neck → chest → stomach → pelvis
chest → left shoulder  → left upper arm  → left forearm  → left hand
chest → right shoulder → right upper arm → right forearm → right hand
pelvis → left thigh → left calf → left foot
pelvis → right thigh → right calf → right foot
```

The hitgroup constants: `HITGROUP_HEAD = 1`, `HITGROUP_CHEST = 2`,
`HITGROUP_STOMACH = 3`, `HITGROUP_LEFTARM = 4`, `HITGROUP_RIGHTARM = 5`,
`HITGROUP_LEGLEFT = 6`, `HITGROUP_LEGRIGHT = 7`, `HITGROUP_HEAD = 1`,
`HITGROUP_GEAR = 10` (see `payload/aim/include/cs2/aim/hitbox.hpp` and
`payload/aim/src/reader.cpp` for the group values actually used).

A capsule that is missing must **not** break the chain — draw the segments whose
endpoints both exist.

**Report the limitation**: this is a hitbox-derived skeleton, not the model's real
bone hierarchy. A true skeleton needs the model's bone-name table (extra RE) —
say so explicitly.

### 2.4 Tracer

A line from the **screen bottom centre** (`esp_tracer_origin == 0`) or from the
**local player's eye origin projected to the screen** (`== 1`) to the target's box
centre. Colour from `esp_color_tracer`.

### 2.5 Off-screen arrows

When a target is **outside** the screen (the projection returns `nullopt`, or the
projected point is outside the viewport), draw a small **rotated arrow** at the
screen edge pointing towards the target, with the distance next to it.

`DrawList` has no rotation primitive. **Add one**: extend the draw list with a
`Triangle` command (a filled triangle with three points) — or reuse `poly` with
three points, which already works. Then the arrow is a triangle whose apex points
at the target. Add the missing ImGui branch in `payload/render/src/imgui_backend.cpp`
if you need a new command kind (see §6).

The arrow must be **clamped to the screen edge** with a margin (e.g. 24 px) and
must never be drawn for a target that is on screen.

### 2.6 Distance, scaling, range

- `esp_distance` → show the distance in metres next to the name (the distance is
  already in `PlayerSnapshot`).
- `esp_scale_by_distance` → shrink the box with distance: a linear scale from 1.0
  at 0 m down to `esp_max_box_scale` at some reference distance (pick **40 m** and
  document it). The scale must never go below `esp_max_box_scale`.
- `esp_max_distance_m` → skip players further away than that (0 = unlimited).

### 2.7 Filters

- `esp_local_player` → also draw the local player.
- `esp_show_teammates` → also draw teammates (the current ESP draws enemies only;
  the palette has a team colour, so use it).
- `esp_show_spectators` → also draw players who are spectating/dead-and-watching.
  A player is a spectator when their life state is not alive
  (`PlayerSnapshot::life_state`) — check the existing code and the schema for the
  right test; do not guess silently.

### 2.8 Colours

Every element's colour comes from the `esp_color_*` fields. The existing hard-coded
palette (`k_color_enemy`, `k_color_team`, `k_color_health_*`, …) must become
**settings-driven**, with the current values as the defaults. The health bar ramp
(green → yellow → red) and the ammo-bar colour may keep their ramps, but the
**base** colours must be configurable.

### 2.9 Radar

A radar widget in a screen corner: a circle, the local player at the centre
(rotated so the view direction is up), the other players as dots coloured by
team, and the world axes. Scale: a configurable radius in metres — **reuse
`esp_max_box_scale`? No — add a constant** (e.g. 50 m radius) and document it.

The radar needs its own draw pass. `DrawList` has no circle primitive — build the
circle from `poly` (a filled polygon of ~24 points) and the ring from a series of
`line` segments. Add a small helper.

The radar must be **toggled off** by default and must not appear in the shared
memory model as garbage.

### 2.10 Sound ESP

The payload already hooks the game's event dispatch
(`payload/control/src/events.cpp`, `payload/control/include/cs2/control/event_ring.hpp`)
and captures **event names and arguments** — `player_footstep` and `player_sound`
are both in the game's event list
(`payload/control/include/cs2/control/event_names.hpp`).

Implement: when a `player_footstep` / `player_sound` event arrives for a player,
draw a **short-lived marker** at that player's position (fade out over ~1 s),
scaled by the sound's radius. The marker must be **transient** — it lives in a
small ring buffer of recent sounds (bounded, e.g. 32 entries, oldest evicted).

The events live in the **control** module and the ESP lives in **features** — the
bridge already exists in spirit (`Overlay` has provider/sink callbacks, e.g.
`set_hitbox_provider`). Follow that pattern: the ESP asks for the recent sounds
through a provider callback, and the control service supplies them.

**If the event arguments do not carry a usable position**, fall back to the
player's current position at the time the event is processed and say so in the
report. `player_sound` carries `radius` and `duration`; `player_footstep` carries
only `userid`.

### 2.11 Grenade / C4 / dropped-weapon ESP

The entity system is already walked by `payload/game` (`EntitySystem`), and the
**generated schema** has the classes:

- `C_PlantedC4` (fields: `m_bBombTicking`, `m_nBombSite`, `m_flC4Blow`, …)
- `C_SmokeGrenadeProjectile`, `C_MolotovProjectile`, `C_FlashbangProjectile`,
  `C_BaseCSGrenadeProjectile`
- `C_BasePlayerWeapon`

Draw:
- **grenades**: a small marker (a diamond / a coloured box) at the projectile's
  origin, coloured per type (smoke / flash / molotov / HE) from
  `esp_color_grenade` with per-type tints;
- **C4**: a marker at the bomb's origin with the remaining time, coloured from
  `esp_color_c4` (red when ticking);
- **dropped weapons**: a small marker at the weapon's origin with the weapon name.

**The entity walk must be cheap.** The entity list is paged (64 pages × 512
slots). The current ESP walks the players; extending the walk to *all* entities
every frame may be too slow. **Measure**: the ESP runs on the render thread
(`Overlay::update`). Cache the entity classification and refresh it at a lower
rate (e.g. 4 Hz) if a full walk per frame is too slow. Say what you did.

The **class identification** must be robust: use the entity's **designer name**
(`EntitySystem::designer_name`) and/or its **vtable** compared against the local
pawn's vtable (the existing player matching already does the vtable trick). Do
not hard-code a class pointer; the game updates.

### 2.12 "Glow" / visible-only

`esp_visible_only` needs a visibility test. The repo has an **occlusion query**
(`payload/control/include/cs2/control/occlusion_query.hpp`,
`query_world_occluders`) used by the ragebot — it is **expensive** (it enumerates
`EntitiesInBox`).

**Requirement:** do **not** run the full occlusion query for every player every
frame. Options: run it at a low rate and cache per player; or use a cheaper
proxy. If you cannot make it cheap, implement the toggle as a **cached,
low-rate** visibility state and document the cost. `esp_glow` (a "through-wall"
highlight) is the visual inverse: tint the player differently when **not**
visible. Both need the same visibility state — implement it once.

**If the visibility state cannot be made cheap enough, implement the UI plumbing
and a clearly-marked `unknown`/always-visible fallback, and say so.** Do not
silently ship something that costs 5 ms per frame.

## 3. The `filled_poly` gap (a real bug — fix it)

`render::DrawCommand` has a `FilledPoly` variant, and the **Vulkan layer** renders
it (`overlay/src/renderer.cpp` → `AddConvexPolyFilled`), but the **payload's**
ImGui backend (`payload/render/src/imgui_backend.cpp`) has **no branch for it** —
so the solid hitbox capsules are silently dropped on the live (D3D11) path.

Fix it: add the `FilledPoly` branch to the payload's ImGui backend
(`ImDrawList::AddConvexPolyFilled`), matching the layer's implementation.

This is a **bug fix**, not a feature — call it out in the commit message.

## 4. Performance

The ESP runs on the **render thread**. The existing code is a plain per-frame walk.

- The draw list is a `std::vector` that is cleared and rebuilt every frame — fine.
- The new entity walk (§2.11) is the risk. Do not regress the frame time.
- The radar's circle polygon is rebuilt every frame — that is fine (24 points),
  but do not allocate in a loop; build it in a `static`/reused buffer.

### 4.1 The shared-memory wire limits (do not exceed them)

The draw list is serialized into the shared segment by
`payload/features/src/model_publisher.cpp` and rendered by the Vulkan layer.
The wire format's fixed capacities are:

- `wire::k_max_draws = 12288` draw commands,
- `wire::k_max_vertices = 16384` **xy pairs** — the pool the `filled_poly`
  commands index into.

A radar (one 24-point circle + one 24-segment ring) plus a skeleton (≈ 20 lines
per player) plus tracers and arrows must fit. **Do not extend the wire format**
(T1 owns `wire.hpp`) — express everything with the existing kinds: a triangle is
a `filled_poly` with three points, a circle is a `filled_poly` with ~24 points, a
line is a `line`.

**Check the pool does not overflow.** The publisher copies the vertices into a
fixed pool; if a frame produces more vertices than the pool holds, the excess is
dropped (or worse, corrupts the segment). Count the worst case — e.g. 10 players
× (1 box outline + 1 head box + 20 skeleton lines + 1 tracer) + the radar — and
make sure it is comfortably inside the limits. If it is not, say so in the report
and propose a bound (e.g. cap the skeleton to the nearest N players).

**Do not change `wire::k_version`** — T1 owns it.


## 5. Style

- `docs/style-guide.md` is normative. Trailing return types, `snake_case`,
  `SCREAMING_SNAKE_CASE` constants, `enum class`, `[[nodiscard]]`, `noexcept`,
  fully-qualified `std::`.
- `enum class` for the modes (`BoxStyle`, `TracerOrigin`, `RadarPosition`,
  `GrenadeKind`) — do not use `int` mode fields.
- The `Color` type: T1 introduces one in `features::Config`. Use it; do not
  invent a second colour type.
- Comments explain **why**.
- Split into focused files; no 1000-line function.

## 6. Verification

- `meson compile -C build && meson test -C build` → 17/17 or more.
- `meson compile -C build-payload` → clean. `meson compile -C build-overlay` → clean.
- If you add a **pure geometry** helper (the off-screen arrow's edge clamping, the
  radar's world→radar mapping, the distance scale), give it a **host test** —
  the ESP's maths is exactly the kind of thing that is impossible to check in a
  live game. A host test for `payload/features` may be impractical (it depends on
  ImGui); if so, put the pure helpers in a **dependency-free header** and test
  that.
- Report the ESP's per-frame cost estimate and how you bounded it.

## 7. Report

- Every element implemented, and the settings each one uses.
- The visibility/"glow" approach and its cost.
- The entity-walk strategy and its cost.
- The skeleton approach and its limitation.
- The `filled_poly` fix.
- What you could not verify without launching the game.
