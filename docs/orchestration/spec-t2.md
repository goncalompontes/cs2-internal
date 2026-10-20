# T2 — Menu overhaul (shared builder, keybind capture, persistence UI)

**Goal:** replace the 400×300 hard-coded tab strip with a real, maintainable menu
that is **defined once** and used by both renderers, with proper widgets, real key
binding, and a config section.

**File ownership (you may ONLY touch these):**

- `overlay/include/cs2/overlay/menu_pages.hpp` *(new)*
- `payload/features/src/menu.cpp`
- `payload/features/include/cs2/features/menu.hpp`
- `overlay/src/menu.cpp`

Do NOT touch `overlay/include/cs2/overlay/wire.hpp` (task T1 owns it) or
`payload/features/src/esp.cpp`. Assume T1 has landed: `wire::Settings` has the new
fields listed in `spec-t1.md` (`menu_key`, `menu_accent`, `menu_transparency`,
`menu_config_op`, `menu_config_token`, `menu_config_path`, the ESP colour fields,
the movement fields, the visuals fields) and `cs2::features::Config` exists with
matching sections.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

### 0.1 Already in place — do not re-implement

T1 (the settings/config foundation) and the orchestrator have already landed:

- `cs2::features::Config` — the sectioned, typed config (87 fields).
- `cs2::features::config_store.hpp` — `default_config_path()`,
  `load(path) -> std::optional<Config>`, `save(path, config) -> bool`,
  `apply_pending_op(Config&, const wire::Settings&) -> bool`,
  `set_config_path(Config&)`, and `enum class ConfigOp { none, save, load, reset }`.
- The **payload-side op handler**: `payload/src/payload_main.cpp`'s
  `on_movement_settings` already calls `apply_pending_op`, and when an op runs it
  refreshes **both** the in-process menu's block (`runtime->menu.settings()`) and
  the **shared segment** (`ModelPublisher::publish_settings_block`) so the
  Vulkan layer's menu sees the loaded configuration too.
- `ModelPublisher::publish_settings_block(const wire::Settings&)` — writes a
  block into the segment under the seqlock.

**Your job on the config side is therefore only the menu side:** the three
buttons, the op codes, the token bump, and the read-only path display. The
in-process menu sets the op on **its own** block; the layer menu sets it on the
**shared** block it writes back. Do **not** touch `payload/src/payload_main.cpp`
or `payload/features/src/model_publisher.cpp` — they are already wired.

---

## 1. The duplication problem (the main thing to fix)

`payload/features/src/menu.cpp` and `overlay/src/menu.cpp` are **near-duplicates**:
the same tabs, the same widget calls, the same `toggle()` helper, ~600 and ~500
lines. They drift (the payload copy has a "Backtrack" toggle the layer copy lacks).

**Fix:** put the menu *content* in one shared, header-only implementation:

- `overlay/include/cs2/overlay/menu_pages.hpp` — namespace `cs2::overlay::menu`:
  - `auto draw_pages(wire::Settings& settings) -> void` — draws the whole menu
    content (navigation + pages) and writes the edited values back into
    `settings`.
  - The header is **ImGui-only** and must not include anything Windows-specific
    (both consumers are Windows, but the header itself should stay portable).
  - Both `payload/features/src/menu.cpp` and `overlay/src/menu.cpp` then call
    `cs2::overlay::menu::draw_pages(settings)` and keep only their own
    open/close/toggle plumbing.

  The header lives next to `wire.hpp` because it is shared by the same two
  consumers. `payload/features/meson.build` already depends on
  `../../overlay/include`, so the include path already works — **verify this**.

**Anti-drift rule:** the page layout, the widget labels, the ordering and the
enable/disable logic must exist **exactly once**. The layer menu must not
re-implement a page. If a page needs platform-specific behaviour, expose a small
hook (e.g. a function pointer or a `bool` parameter) rather than duplicating the
page.

## 2. Layout

- **Left sidebar** with the section list (custom, not `ImGui::BeginTabBar`): a
  fixed-width column of selectable entries, the current one highlighted, with the
  section name (and, for the current one, a short subtitle).
- **Right content area**: the selected section, scrollable.
- **Header** above the sidebar: the product name and the build/branch.
- **Footer**: the current page's key hints.
- The window is **resizable**; its size and position are **persisted** in the
  settings (add the fields to `wire::Settings` **only if T1 did not already add
  them** — if you need new fields, add them to `wire.hpp` and say so clearly in
  your report, because T1 owns that file and a conflict must be visible).
  If you cannot add fields, keep the window size in a `static` local in the shared
  header and persist it yourself in a side file — **prefer adding the fields** and
  flag it in the report.

## 3. Sections

Group the existing features and add the new ones. Proposed:

1. **Players** — the existing ESP toggles (box, health bar, ammo bar, name, weapon,
   flags) plus the new ones T1 added: head box, skeleton, tracer, off-screen arrows,
   glow (through-wall), visible-only, distance, scale-by-distance, max distance,
   local player, spectators, teammates; the box thickness / style; the colour
   pickers.
2. **World** — radar, sound ESP, grenade ESP, C4 ESP, dropped-weapon ESP, and the
   max-distance slider.
3. **Ragebot** — everything the Ragebot tab has today (objective, hitbox, FOV, min
   damage, min hitchance, autostop, autoscope, autoreload, only-visible, mode,
   per-weapon profiles, recoil, stop speed, backtrack) **plus a real keybind widget**
   for the rage trigger key.
4. **Movement** — auto-strafe (with the mode combo: off / camera-relative /
   directional, the wishspeed slider, the sub-tick count, and the "A/D stands down"
   toggle), bunnyhop, third person (enable, distance, angle, pivot height, FOV,
   shoulder side).
5. **Visuals** — chams (enable, mode, colour, visible-only, local player), the menu
   accent colour, the menu transparency.
6. **Config** — the config file path (read-only text), **Save / Load / Reset** buttons
   that drive the `menu_config_op` / `menu_config_token` op protocol from T1.
7. **About** — what the build is, the host/branch, and the key list.

Every section must be collapsible or clearly separated; use
`ImGui::Separator`/`ImGui::TextDisabled` consistently, matching the existing
visual language (the current menu uses plain `ImGui::Checkbox` rows).

## 4. Widgets

Add a small widget toolkit **inside the shared header** (namespace `cs2::overlay::menu`):

- `toggle(const char* label, bool& value)`
- `slider_float(const char* label, float& value, float min, float max, const char* fmt)`
- `slider_int(const char* label, int& value, int min, int max)`
- `combo(const char* label, int& index, const char* const* items, int count)`
- `color(const char* label, std::uint8_t rgba[4])` — an RGBA picker; the wire stores
  4 bytes.
- `keybind(const char* label, std::uint8_t& vk)` — **a real key capture**.
- `text_input(const char* label, char* buffer, std::size_t size)`
- `button(const char* label)` → `bool`

Style rules for the widgets:
- Consistent label/control alignment: use `ImGui::SetNextItemWidth` and a fixed
  control column so the controls line up down the page.
- The **keybind** widget is the important one: the current rage key is a fixed list
  of 11 virtual-key codes. Replace it with a real capture:
  - the button shows the current binding's **name** ("X", "Left Alt", "Mouse 4", "None"),
  - clicking it arms the capture,
  - the next key press (or the next mouse button) is captured and shown,
  - `Escape`/`Delete` clears the binding to "None",
  - **the capture must not leak the key into the game** — the menu already subclasses
    the game's `WndProc` (`Menu::window_proc` in `payload/features/src/menu.cpp`),
    so the capture must consume the key there, and the game must not also see it.
  - The layer menu has its own `hooked_wndproc` in `overlay/src/menu.cpp` — the
    same rule applies there.
  - Both menus must therefore share the capture logic too; put it in the shared
    header as a small state machine (a file-scope `static` in the header is
    acceptable **only** if it is a single definition shared by both TUs — prefer
    an `inline` function with a function-local `static`).

## 5. Behaviour

- The menu must open/close on the configured key (`menu_key`, default Insert
  `0x2E`). The current code polls `GetAsyncKeyState` — keep polling (it is the
  only thing that works reliably under the session-0/SDL3 raw-input quirks) but
  make the key configurable and show it in the UI.
- The menu must **not** be interactive until the game is in a map
  (`model.in_game`) — the current behaviour. Keep it.
- The payload menu must keep its **SDL3 raw-input block** (`install_raw_input_block`)
  and its `WndProc` subclass. Do not break them.
- The layer menu must keep its own `hooked_wndproc` and its `Insert`/`Delete`
  polling.

## 6. Style

- Match the repo style guide: trailing return types, `snake_case`, 2-space indent,
  100 columns, `}  // namespace cs2::overlay::menu` closers.
- The shared header must have a `#pragma once` and a header comment explaining
  **why** it is shared (two renderers, one definition).
- No new global mutable state (ADR-0010) except the single keybind-capture state
  you justify in a comment.

## 7. Verification

- `meson compile -C build && meson test -C build` → 17/17.
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → clean.
- The two menus must now be **short**: `payload/features/src/menu.cpp` and
  `overlay/src/menu.cpp` should each be roughly a third of their current size
  (the page content moved to the shared header). Report the before/after line
  counts.
- **No duplicated page content** may remain. Prove it: `grep` the two menu files
  for `ImGui::Checkbox` / `ImGui::SliderFloat` / `ImGui::Combo` and show that the
  hits are gone (or are only in the shared header).

## 8. Report

The new section list, the widget list, the before/after line counts of both menu
files, the keybind capture design, any field you had to add to `wire.hpp`, and
what you could not verify without launching the game.
