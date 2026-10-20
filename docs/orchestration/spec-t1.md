# T1 — Settings & config foundation

**Goal:** give every feature in this wave a place to live in the settings block, and
give the settings a real home on disk.

**File ownership (you may ONLY touch these):**

- `overlay/include/cs2/overlay/wire.hpp`
- `payload/features/include/cs2/features/esp.hpp`
- `payload/features/src/settings.cpp`
- `payload/features/src/esp.cpp`
- `payload/features/src/overlay.cpp`
- `overlay/src/renderer.cpp`
- `payload/features/include/cs2/features/config_store.hpp` *(new)*
- `payload/features/src/config_store.cpp` *(new)*
- `payload/features/meson.build`

Do NOT touch the menus (`payload/features/src/menu.cpp`, `overlay/src/menu.cpp`) — a
separate task owns them. Do NOT touch `payload/src/payload_main.cpp`.

---

## 1. `wire::Settings` — extend, do not rename

`wire::Settings` is the **flat, packed transport struct** that the overlay menu edits
and the payload reads. It is `#pragma pack(push,1)` and every field is `uint8_t`,
`float` or `char[N]`.

**Rules:**

- **Keep every existing field name and its position.** Other code, the Vulkan
  fallback and the layer menu all read them. Do not reorder, rename or re-type
  anything that already exists.
- **Append** the new fields below, grouped with a `// --- <group> ---` comment.
- Bump `wire::k_version` (currently `11`) to `12`.
- `wire.hpp` must stay **free of ImGui** and free of anything that is not a plain
  POD — it is compiled by both the payload (MSVC ABI) and the Vulkan layer (gcc).
  Keep the `#pragma pack(push,1)` / `#pragma pack(pop)` bracket around the structs.
- Add a `static_assert(sizeof(Settings) == <N>)`? No — the struct is shared with a
  separately-compiled layer, so the size is only guaranteed by the version. Instead
  add a compile-time check that the struct is standard-layout and trivially copyable.

**New fields to append** (exact names and types — later tasks will read them):

```cpp
// --- ESP: players -------------------------------------------------------
uint8_t esp_box_thickness;     // 0..20 -> 0.5..5.0 px
uint8_t esp_box_cornered;       // 0 = straight box, 1 = corner box
uint8_t esp_head_box;           // draw a small box on the head
uint8_t esp_skeleton;           // draw bone lines
uint8_t esp_tracer;             // line from the screen bottom to the player
uint8_t esp_tracer_origin;      // 0 = screen bottom, 1 = local eye origin
uint8_t esp_offscreen;          // off-screen target arrows
uint8_t esp_glow;               // through-wall highlight (visible-state tint)
uint8_t esp_visible_only;       // only draw players that are currently visible
uint8_t esp_distance;           // show the distance in metres
uint8_t esp_scale_by_distance;  // shrink the box with distance
uint8_t esp_max_distance_m;     // 0 = unlimited; otherwise cap in metres
uint8_t esp_radar;              // radar widget
uint8_t esp_sound;              // sound (footstep) ESP
uint8_t esp_grenades;           // grenade ESP
uint8_t esp_c4;                 // planted C4 ESP
uint8_t esp_weapons;            // dropped-weapon ESP
uint8_t esp_local_player;       // draw the local player too
uint8_t esp_show_spectators;    // draw spectators
uint8_t esp_show_teammates;     // draw teammates
float  esp_max_box_scale;       // 0.25 .. 2.0
// --- ESP: colours (RGBA bytes) -----------------------------------------
uint8_t esp_color_enemy[4];
uint8_t esp_color_team[4];
uint8_t esp_color_box[4];
uint8_t esp_color_skeleton[4];
uint8_t esp_color_tracer[4];
uint8_t esp_color_offscreen[4];
uint8_t esp_color_sound[4];
uint8_t esp_color_grenade[4];
uint8_t esp_color_c4[4];
// --- Movement ------------------------------------------------------------
uint8_t movement_strafe_mode;   // 0 = off, 1 = camera-relative, 2 = directional (WASD)
uint8_t movement_strafe_ignore_keys;  // 0 = A/D biases the heading, 1 = A/D stands down
float  movement_strafe_wishspeed;
uint8_t movement_strafe_subticks;
uint8_t movement_thirdperson;    // force the camera into third person
float  movement_thirdperson_distance;  // 64..512 units
float  movement_thirdperson_angle;     // 0..360 degrees
float  movement_thirdperson_pivot_z;   // 0..64 units
float  movement_thirdperson_fov;       // 0 = keep the game's FOV
uint8_t movement_thirdperson_side;    // 0 = left shoulder, 1 = right shoulder
// --- Visuals (chams) -----------------------------------------------------
uint8_t visuals_chams;
uint8_t visuals_chams_mode;    // 0 = flat, 1 = glow, 2 = wireframe
uint8_t visuals_chams_visible; // 1 = only when the player is visible
uint8_t visuals_chams_local;   // 1 = also chams the local player
uint8_t visuals_chams_color[4];
// --- Menu / config -------------------------------------------------------
uint8_t menu_key;              // virtual-key code that opens the menu, 0 = Insert
uint8_t menu_accent[4];        // RGBA accent colour
uint8_t menu_transparency;     // 0..100 (window alpha)
uint8_t menu_config_op;        // 1 = save, 2 = load, 3 = reset
uint32_t menu_config_token;    // bumped by the menu to signal the op
char    menu_config_path[128]; // absolute path of the config file (read-only)
```

The `menu_config_op` / `menu_config_token` pair is the **same one-shot action
protocol** the ragebot per-weapon profiles already use (`rage_config_op` /
`rage_config_token`): the menu sets an op code and bumps the token; the payload
performs the action when it sees the token change. Reuse that pattern exactly.

## 2. `features::Config` — the typed, sectioned config the features read

Today `features::EspConfig` is a flat near-copy of the wire struct. Replace it with a
**sectioned, typed** struct and rename it to `Config`.

**File ownership:** `payload/features/include/cs2/features/esp.hpp` and
`payload/features/src/settings.cpp` are yours. Keep the *file names* (other build
files reference them) but change the contents:

- `payload/features/include/cs2/features/esp.hpp`:
  - define `namespace cs2::features { struct Config { ... }; }` with sections
    `Esp`, `Movement`, `Visuals`, `Menu` that mirror the wire groups above but with
    **real types**: `bool` for flags, `float` for numbers, `std::array<std::uint8_t,4>`
    or a small `struct Color { std::uint8_t r,g,b,a; }` for colours, and `enum class`
    for the enumerated modes (`StrafeMode`, `ChamsMode`, `ThirdPersonSide`,
    `BoxStyle`, `TracerOrigin`).
  - `Color` must have a conversion to/from the wire's 4 bytes.
  - Keep `EspConfig` as a deprecated alias? No — update the two users
    (`esp.cpp`, `overlay.cpp`) to `Config`. Both are in your ownership.
  - `HitboxMode` stays where it is (it is used by the hitbox renderer).
- `payload/features/src/settings.cpp`: keep the free functions
  `to_wire_settings(const Config&) -> wire::Settings`,
  `from_wire_settings(const wire::Settings&) -> Config` and
  `default_wire_settings() -> wire::Settings`, but implement them **section by
  section**. The `from_` direction must **clamp every numeric field** into its
  legal range (a corrupt/old shared segment must not be able to push a slider out
  of range). The `to_` direction must map every field.

**Rule:** every field in `wire::Settings` must be represented in `Config`, and
vice versa. The compiler will not catch a forgotten field, so **write the
conversion as an explicit, exhaustive list of assignments in both directions** and
then verify by inspection that the two lists have the same number of statements.

## 3. `config_store` — persistence

New module `payload/features/{include/cs2/features,src}/config_store.{hpp,cpp}`.

- `auto load(const std::filesystem::path& path) -> std::optional<Config>` /
  `auto save(const std::filesystem::path& path, const Config&) -> bool` — or take a
  `wire::Settings` instead; **your choice, but pick one and be consistent.** The
  menu and the payload both use it, and the payload is the only writer.
- **Format**: a plain-text `key = value` file, one setting per line, `#` starts a
  comment. It must round-trip **every** field of the settings block. Booleans as
  `0`/`1`, floats with enough precision to round-trip (`%.9g`), colours as
  `r,g,b,a`, strings quoted or escaped so a path with spaces works.
- **Path**: `%LOCALAPPDATA%\cs2-internal\cs2-config.cfg`, via the existing
  `cs2::game::data_file_path()` helper (`payload/game/include/cs2/game/log.hpp`).
  Allow an override with the `CS2_PAYLOAD_CONFIG` environment variable.
- The file must be **created on first save** with a header comment, and **loading a
  missing or corrupt file must not crash** — return `std::nullopt` and leave the
  defaults in place.
- Unknown keys in the file are ignored; missing keys keep the default.

## 4. Wire the new op protocol

The `menu_config_op` / `menu_config_token` op must be **acted upon** somewhere the
payload already reacts to settings changes. Look at how `rage_config_op` /
`rage_config_token` is handled (`payload/control/src/service.cpp`) and follow the
same shape — but the config store lives in `features`, so the natural place is the
payload's settings sink (`payload/src/payload_main.cpp` → `on_movement_settings`).
**You do not own `payload_main.cpp`** — so instead expose the store as a small
API from `config_store.hpp` and leave the *call site* to the menu task. Document
clearly in the header what the call site must do:

```cpp
// The payload applies a pending config op here (the menu sets `menu_config_op`
// and bumps `menu_config_token`).
auto apply_pending_op(Config& config, const wire::Settings& settings) -> bool;
```

and make it so the *only* thing the call site has to do is call this with the
current settings and the current config.

## 5. Defaults

`default_wire_settings()` and `default_wire_settings()` in
`overlay/src/renderer.cpp` (`default_ui_settings()`) must agree. Pick defaults that
are **conservative**: the new ESP elements **off**, third person **off**, chams
**off**, the menu key = Insert (`0x2E`), the strafe mode = camera-relative (1).

## 6. Verification

- `meson compile -C build && meson test -C build` → 17/17 (or more).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → all clean.
- Add a **host test** for the config store round-trip if the module can be built
  host-side. `payload/features` currently has no host test and depends on
  `render` → `imgui`; if a host test is impractical because of the ImGui
  dependency, put the round-trip logic in a **dependency-free** helper (e.g. a
  `config_store` that only deals with `wire::Settings`, which is a pure POD) and
  test that. **A round-trip test is expected** — wire it into
  `payload/features/meson.build` and the root `meson.build` the way the other
  tests are wired. If you genuinely cannot, say so in the report and explain.

## 7. Report

State: the new `Config` layout, the list of settings added, how the round-trip is
verified, the exact call site the menu task must add, and anything you had to
decide that was ambiguous.
