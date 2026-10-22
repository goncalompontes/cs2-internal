# FIX-3 — the adversarial review's findings on T2 (the menu) and T3 (the strafe)

**Your worktree: `C:\Users\gpmpo\cs2-wt\fix3`**, branch `fix/t2-t3-review`, based
on the integration tip. Do all your work there; never touch
`C:\Users\gpmpo\cs2-internal` or another agent's worktree.

**Read first:** `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` and
`C:\Users\gpmpo\cs2-internal\docs\style-guide.md`.

The review's verdict was **DO NOT SHIP**. The findings are below. **The review's
reasoning is authoritative** — it wrote hostile harnesses that drive the real code;
if you think a finding is wrong, say so with evidence rather than skipping it.

> **T3 is in good shape** — the reviewer verified the goal formula by hand against
> the engine conventions, proved `write_strafe_wish` and the `theta*` formula are
> **byte-identical** to the old revision, and showed the new host test catches
> **9 of 9** injected mutations. Only M5, m4, m5, m7 and m8 apply to T3.

---

## Blocker

### B1 — the menu window collapses to **32×32**: the whole menu is unusable

`overlay/include/cs2/overlay/menu_pages.hpp:934`:

```cpp
ImGui::SetNextWindowSizeConstraints(ImVec2(k_min_width, k_min_height), ImVec2(0.0F, 0.0F));
```

In Dear ImGui 1.91.5 the second argument of `SetNextWindowSizeConstraints(min, max)`
is a **real maximum**, not "unbounded" (`imgui.cpp:6234`:
`new_size.x = (cr.Min.x >= 0 && cr.Max.x >= 0) ? ImClamp(new_size.x, cr.Min.x, cr.Max.x) : …`).
With `max = (0,0)` that is `ImClamp(size, 520, 0)` → `0` → ImGui's 32×32 floor.
"No maximum" is `ImVec2(FLT_MAX, FLT_MAX)`.

The reviewer's harness, driving the **real** `draw_menu()`:

```
frame 1: pos (300.0,200.0) size (32.0,32.0); the file says (300,200) 700x500
the window opens at the persisted size                         FAIL
after asking for 900x700: Size 32.0x32.0  SizeFull 32.0x32.0
the window can be made wider than the minimum (it is resizable)  FAIL
the side file now says: '120.0 340.0 32.0 32.0'
```

and the minimal probe: `max=(0,0)` → **32×32**; `max=FLT_MAX` → 700×500; no
constraint → 700×500. **Patching only that one line** makes all four geometry
checks pass.

**Failure scenario:** the menu opens as a 32×32 box. `row_height =
GetContentRegionAvail().y - footer_height` is ≈ −32, so the sidebar and content
children collapse too. Nothing — no sidebar, no page, no widget — is visible or
clickable, and the persisted geometry is poisoned with `32.0 32.0`. The pre-T2
menus had no size constraint and rendered fine, so this is a **regression**.

**Fix:** `ImGui::SetNextWindowSizeConstraints(ImVec2(k_min_width, k_min_height), ImVec2(FLT_MAX, FLT_MAX));`
(needs `<cfloat>`), or drop the call.

**Add a regression test** for the geometry (the reviewer has a harness that drives
the real `draw_menu()` with a real ImGui context — write an equivalent).

---

## Majors

### M1 — the mouse-button binding names are off by one

`menu_pages.hpp:202-203` names `0x04 "Mouse 4"`, `0x05 "Mouse 5"`, `0x06 "Mouse 6"`.
The capture produces `0x04 + GET_XBUTTON_WPARAM(wparam)` = **0x05** (the physical
4th button, `VK_XBUTTON1`) and **0x06** (the 5th). The repo's established
convention is the opposite: the pre-T2 menu's combo was `{"Mouse 4", "Mouse 5"}`
for `k_rage_key_vks = {…, 0x05, 0x06}`. `0x04` is `VK_MBUTTON` (the middle
button) and `0x03` is `VK_CANCEL`, not a mouse button at all.

**Fix:** `{0x05, "Mouse 4"}, {0x06, "Mouse 5"}`, `{0x04, "Middle Mouse"}`, and
drop `0x03` from the mouse names (the nit).

### M2 — the two renderers' config-op tokens collide

`payload/features/src/config_store.cpp` holds **one** global
`std::atomic<std::uint32_t> last_op_token` (note: FIX-1 has since replaced that
global with a `ConfigOpState` owned by the payload's `Runtime` — **check the
current code**, the fix shape may differ) and the op is skipped when the token
matches the last one acted on. But the payload's menu block and the Vulkan
layer's block each carry **their own** counter, both starting at 0, and
`menu_pages.hpp:806-818` bumps whichever block the renderer owns.

The reviewer's harness, driving the **real** `apply_pending_op`:

```
the payload menu's Save op runs                       ok
the layer menu's Save op runs too (its own token)    FAIL
the layer's Load op runs (first)                      FAIL
the payload's Load op runs (its own token)            FAIL
```

**Fix:** make the token unique across renderers — e.g. the layer's menu derives
its token from the **shared** block's current token rather than from its own local
counter, so the two never produce the same value. (FIX-1's `ConfigOpState` change
may already have altered the shape — verify against the current code.)

### M3 — `Delete` no longer toggles the menu, and the UI still promises it does

`payload/features/src/menu.cpp:402-405` and `overlay/src/menu.cpp:287-290` now
poll only `settings.menu_key` **and** `VK_INSERT`. Pre-T2 both polled `VK_INSERT`
**and** `VK_DELETE`. The spec (§5) explicitly requires the layer menu to keep its
`Insert`/`Delete` polling.

**Fix:** keep polling `VK_DELETE` alongside `menu_key`, and fix the **copy**:
the About page (`menu_pages.hpp:832`) and the About hint (`:611-612`) say
"Insert or Delete toggles the menu", while the Visuals page (`:786`) says
"Insert also toggles the menu" — the two pages contradict each other.

### M4 — the shipped default `menu_key` is `VK_DELETE`, mislabelled "Insert"

`overlay/include/cs2/overlay/wire.hpp:363`: `settings.menu_key = 0x2E;  // Insert`.
**0x2E is `VK_DELETE`; `VK_INSERT` is 0x2D.** The shared header gets it right
(`menu_pages.hpp:107-108`: `k_key_delete = 0x2E`, `k_key_insert = 0x2D`), which
makes the wire's comment — and the original spec's "default Insert `0x2E`" —
visibly wrong.

**Failure scenario:** on a fresh install the "Open key" button reads **"Delete"**,
and `Delete` is *also* the capture's "clear the binding" key — two different
actions on the shipped default.

**Fix:** the default becomes **Insert (`0x2D`)** — the original *intent*. Change
it in **both** `wire::Settings::default_settings()` and
`features::Config::Menu::menu_key`, and **recompute the golden settings
checksum** (`wire::k_default_settings_checksum`) — the `static_assert` will fire
in all three builds, and the compiler prints the new value in the assertion's
message. Say so in your commit.

### M5 — the legacy strafe mode does **not** restore "exactly the old behaviour"

`payload/control/src/input.cpp:569-580` computes `legacy = (mode ==
camera_relative) || strafe_ignore_keys`, and the legacy goal is then still pushed
through `plan()` (`:594`), which applies the 20° dead zone and the hysteresis.
The old code was unconditionally `side = (delta >= 0) ? +1 : -1`.

The spec (§3): *"`movement_strafe_ignore_keys == 1` must restore exactly the old
behaviour."* The reviewer compared the two rules over the whole
yaw/velocity/previous-side space: **509 of 2596 legacy-mode decisions differ** —
every one inside the dead zone, where the new code keeps the previous side instead
of following `sign(delta)`.

**Fix:** when `legacy` is true, take `side = delta >= 0 ? +1 : -1` — **no** dead
zone, **no** hysteresis. The dead zone exists only for the new WASD goal (where
`W` alone puts the goal exactly on the velocity heading). Add a host test that
compares the legacy rule with the old one over a sweep.

### M6 — the publisher re-pushes the stale shared-segment block every frame

`payload/features/src/model_publisher.cpp:260-262` calls the settings sink **every
frame** with the segment's copy; `payload/features/src/menu.cpp:465-471` calls the
same sink **every frame** with the payload menu's own block; both become
`set_strafe_features(...)` in `payload/src/payload_main.cpp:197-199`.

The segment's settings block is written only by `publish_settings()` (once, at
`map()`) and by `publish_settings_block()` (after a config op). It is **never
refreshed from the payload's menu**. So in the in-process mode the two blocks
disagree and the strafe's settings are written twice per frame with different
values from two threads — last writer wins.

**Failure scenario:** the user picks "Camera-relative" on the Movement page;
within 16 ms the publisher pushes the segment's stale value back, so the choice
flip-flops and is effectively ignored.

**Fix:** the publisher must not re-push the segment's block when the payload's
menu owns the block (the in-process mode) — or the payload's menu must write its
block into the segment. Pick one, and make the **ownership** explicit rather than
implied (the repo already has a `layer_owns_settings` flag — use it).

---

## Minors

- **m1** `payload/features/src/menu.cpp:248-250` / `overlay/src/menu.cpp:218-220`
  — every `WM_CHAR` is fed to ImGui, even when the matching `WM_KEYDOWN` was
  consumed by the capture. The captured key's character therefore lands in
  whatever `InputText` has focus. **Suppress the character that follows a
  captured keydown** (and the captured key's `WM_KEYUP`, which is currently fed
  with no matching keydown).
- **m2** `menu_pages.hpp:139-141` — `cancel_capture()` clears only `armed`, so a
  result captured just before the menu closed is applied on reopen. **Clear
  `has_result` and `target` too.**
- **m3** `menu_pages.hpp:504-512` — the header's `config_path()` honours
  `CS2_PAYLOAD_CONFIG` and `LOCALAPPDATA`, while
  `payload/features/src/config_store.cpp:744-761` honours `CS2_PAYLOAD_DIR` (the
  repo's own directory override) **first**. The reviewer's harness shows the two
  **differ**, while the comment at `:479-482` claims they are the same. **Make
  them agree** — the simplest is to have the header resolve the path the same way
  the store does (or to have the store's path resolution be the single
  implementation the header calls).
- **m4** `payload/control/src/input.cpp:572-576` — the legacy A/D stand-down
  returns without `strafe_side_last_.store(0)`, unlike every other "the strafe
  stops acting" path (`:514, 520, 525, 532, 539, 552`). The spec: *"Reset it when
  the strafe stops acting."*
- **m5** `menu_pages.hpp:755` — the Sub-ticks slider offers 0..255, but
  `payload/control/src/input.cpp:451` clamps to `std::clamp(subticks, 0, 16)`
  (`k_strafe_max_steps = 16`). Picking 17..255 shows a value that is silently not
  in effect. **Cap the slider at 16.**
- **m6** `menu_pages.hpp:57` — `k_build_info` is a hard-coded "0.1.0" and
  `draw_about` (`:823-835`) never prints a host/branch, which the spec (§3.7)
  asked for. **Print the build and the branch** (a compile-time constant is fine).
- **m7** `payload/control/include/cs2/control/strafe_geometry.hpp:113` — the
  documented range of `normalize_angle` is wrong: it says `(-180, 180]`, the
  implementation returns `[-180, 180)` (`normalize_angle(180) == -180`). The same
  wrong claim is in `payload/control/tests/test_control_strafe.cpp:94`. **Fix both.**
- **m8** `payload/features/src/settings.cpp:204-205` — the unknown-`movement_strafe_mode`
  fallback is still `StrafeMode::camera_relative`, but the documented default is
  now `directional`. **Fix the fallback** (and check the other fallbacks against
  the current defaults).
- **m9** The payload's menu went 603 → 470 lines (78 %), the layer's 471 → 335
  (71 %) — the spec's target was "roughly a third". The page content *is* gone
  (0 widget hits in both), but the payload's copy is dominated by the
  pre-existing raw-input block, the window search and the WndProc. **Say whether
  you can reach the target** — if not, say why, rather than leaving it.
- **m10** `menu_pages.hpp:108` — `k_key_insert` is declared and never used.
- **m11** `menu_pages.hpp:483, 521, 844, 922` — `config_path`, `geometry_path`,
  `draw_pages` and `draw_menu` are `noexcept` but manipulate `std::string`; an
  allocation failure calls `std::terminate`. Correct for the payload, but the
  header also compiles into the Vulkan layer, where it aborts the game. **Either
  drop the `noexcept` or make the paths allocation-free.**
- **m12** The side-file fallback (the window geometry in a side file rather than in
  `wire::Settings`) was not flagged in the commit message, and neither was the
  `wire.hpp` default-key mismatch (M4). The spec asked for both to be flagged.
  **Flag them in your commit message and in your report.** (Another agent may
  since have added the geometry fields to `wire::Settings` — check.)

## Nits

- `menu_pages.hpp:204` lists `0x03` as "Mouse 3"; `0x03` is `VK_CANCEL`.
- `menu_pages.hpp:478-482` — the comment claiming the fallback equals
  `default_config_path()` (see m3).

---

## Coordination

Another agent (**FIX-2**) is fixing the T4/T5 review's findings and may also touch
`overlay/include/cs2/overlay/menu_pages.hpp` — **the third-person section** (it
greys out three inert sliders). **Keep your hunks away from that section** so
the two merges do not collide. Everything else in `menu_pages.hpp` is yours.

Note that the `wire::Settings` defaults are also touched by other agents; if you
change a default, **recompute the golden settings checksum** and say so.

## Verification

- `meson compile -C build && meson test -C build` → green (22/22 at baseline).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → clean.
- **For B1, prove it**: write a test that drives the real `draw_menu()` with a
  real ImGui context and asserts the window opens at the persisted size and can
  be resized. The reviewer's harness is the model.
- For M5, prove it: a host test that compares the legacy rule with the old
  `side = sign(delta)` rule over a sweep of the yaw/velocity space.
- Commit in logical pieces (`fix(menu): …`, `fix(movement): …`).

## Report

For each finding: **fixed / not fixed (with why)**. Plus the new geometry test and
the legacy-mode test, the checksum recomputation, and anything the review missed.
