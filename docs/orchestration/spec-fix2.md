# FIX-2 — the adversarial review's findings on T4 (the ESP) and T5 (the camera)

**Your worktree: `C:\Users\gpmpo\cs2-wt\fix2`**, branch `fix/t4-t5-review`, based
on the integration tip. Do all your work there; never touch
`C:\Users\gpmpo\cs2-internal` or another agent's worktree.

**Read first:** `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` and
`C:\Users\gpmpo\cs2-internal\docs\style-guide.md`.

The review's verdict was **SHIP WITH FIXES** — no blockers. Below is the finding
list with the fix guidance. **The review's reasoning is authoritative**; if you
think a finding is wrong, say so with evidence rather than skipping it.

---

## Majors

### M1 — the new ESP geometry test is a **no-op in the release build**

`payload/features/tests/test_esp_geometry.cpp` (and every other assertion in the
file) is built on `assert()`. `scripts/release.sh:64,66` does
`meson setup build-release . --buildtype=release` and then
`meson test -C build-release`; Meson's `b_ndebug` default is `if-release`, so
`NDEBUG` is defined and **every `assert()` is compiled out**. The review proved
it: with `distance_scale`'s clamp deliberately broken, the `-DNDEBUG` build
printed `features-esp-geometry: ok` and exited **0**.

**Fix:** make the test survive `NDEBUG`. The repo already has the pattern — the
new camera test (`payload/game/tests/test_camera.cpp`) uses an explicit
`expect()` + a failure counter and **still fails** under `NDEBUG` (the review
verified it). Use that pattern for the ESP geometry test.

**Also:** `payload/features/tests/test_features_config.cpp` has the same
problem (it is T1's, but it is in scope for this fix — the review called it
"pre-existing and out of that diff's scope", which no longer applies).

**Consider also** pinning `b_ndebug=false` for the **host** build in the root
`meson.build`, so a release-flavoured host configuration still runs the
assertions. Decide which of the two you do and say why — doing **both** is
fine.

### M2 — T5: the build-specific **reads** are not behind the build guard

`payload/game/src/camera.cpp`:
- `read_published_view` (hard-coded RVA `0x256af48`),
- `read_view_setup_origin` (`0x256b9e8`),
- `read_third_person_state` (`+0x229/+0x230/+0x234/+0x238/+0x6a8`)

are read on **any** client build, while `write_third_person_state` and
`clear_third_person_switch` correctly refuse unless `client_build_verified()`.

Two consequences the review spelled out:

1. `camera_origin()` returns the *published* state whenever the 24-byte read
   succeeds — and on a different build that address is still inside
   `client.dll`'s `.data`, so the read **succeeds with garbage floats**.
   `payload/features/src/overlay.cpp` feeds that into the capsule silhouette's
   tangent maths, so after a Steam update the hitbox capsules are drawn from a
   garbage camera origin — **wrong silhouettes, no crash, silently wrong**.
2. `payload/features/src/thirdperson.cpp:158` re-arms the camera only
   `if (!hook_.installed() && !game::read_third_person_switch())` — and that
   read is a build-specific offset on an unverified build, so the byte is
   arbitrary. If it happens to be non-zero the payload never re-issues
   `thirdperson 1` and the camera silently drops out of third person on the next
   death/respawn — exactly the failure the feature exists to prevent.

**Fix:** gate the reads the same way as the writes (return `valid=false` /
`unknown` when `!client_build_verified()`), and make the re-arm **unconditional
when the state is unknown** rather than trusting a byte that cannot be trusted.

### M3 — T5: three third-person controls in the menu do nothing, with no hint

`overlay/include/cs2/overlay/menu_pages.hpp:761-768` — "Pivot height", "FOV" and
"Shoulder" are ordinary sliders next to "Distance" and "Angle". The review
verified by grep that `pivot_z`, `right_shoulder` and `fov` reach only the
payload's own camera model and the log line — **they are never written to the
game**. The implementer discloses this honestly in `thirdperson.hpp` and in the
enable log line, but **the operator only sees the menu**: a slider that does
nothing looks like a broken feature.

**Fix:** grey the three controls out with a one-line explanation. The page
already has a `TextDisabled` idiom for exactly this (`:786`, "Insert also toggles
the menu."). Say *why* each is inert: the client has no shoulder camera, the
camera orbits the player's eye (no pivot), and the view setup's FOV field is
unconfirmed. Do **not** remove the settings — the wire format is versioned and
other code reads them.

---

## Minors

- **N1** `payload/features/src/esp_players.cpp:331` — the small-box test
  (`box_h < 3.0F || box_w < 1.0F`) runs **before** the on-screen test, so a
  target that is off screen *and* small gets **neither a box nor an arrow** — it
  is silently dropped. The spec says a target outside the viewport must get an
  arrow. **Apply the small-box test only to the on-screen path.**
- **N2** `payload/features/include/cs2/features/esp_geometry.hpp:152-156` — the
  edge clamp inverts on a viewport smaller than twice the margin (proven on a
  40×40 viewport: a target to the **left** places the arrow at `x = 24` with the
  centre at `20` — the wrong side of the screen). **Clamp to `max(cx - margin, 0)`.**
- **N3** `payload/features/src/esp.cpp:30-46` — `Esp::set_providers()` is
  **never called by anyone**, so the head box and the skeleton always take the
  fallback (`aim::read_model_hitboxes`) and re-read the model the overlay's own
  hitbox provider has just read for the same pawn — up to 4 extra model reads
  per frame (each a few hundred scattered `ReadProcessMemory` calls,
  ≈0.2–0.8 ms/frame). **One line in `Overlay` hands the hitbox provider to the
  `Esp`** and removes the duplicate.
- **N4** `payload/features/src/esp_players.cpp:151` — `else if (teammate &&
  !config.esp.show_teammates && config.esp.only_enemies)`: with `only_enemies`
  off, `esp_show_teammates` and `esp_local_player` are **no-ops** (teammates and
  the local player are always drawn). The menu shows four independent-looking
  toggles with a hidden dependency. **Make the new toggles authoritative** (they
  are the specific ones) and keep `only_enemies` as the master switch, or make
  the relationship explicit in the menu.
- **N5** `payload/features/src/esp_radar.cpp:105` — the radius test is a
  **square**, not a circle, so a player at 45° and 50 m is drawn outside the
  disc. **Use the squared distance.**
- **N6** `payload/features/src/esp_world.cpp:254-266` — an empty page costs a
  whole iteration of the 1024-slot budget, not "one read" as the comment at
  `:39-40` claims. **Fix the comment** (the budget *is* enforced).
- **N7** `payload/features/src/esp_world.cpp:275-277` — the publish fires when
  the cursor wraps, but the slot being examined at that moment is classified and
  pushed **after** the publish, so it appears one sweep (~1 s) late. **Reorder.**
- **N8** `payload/features/src/thirdperson.cpp:25-36` — if `g_active == nullptr`
  the detour returns 0 **without calling the original**, i.e. the game's camera
  update would be skipped entirely. Not reachable today, but the same
  copy-paste pattern is in `game_present_hook.cpp:32-34`. **Capture the
  trampoline in a file-scope static at install time and always call it.**
- **N9** `payload/game/src/camera.cpp:71-77` — `writable()` proves
  **readability**, not writability (it probes with the *read* helper), yet it
  guards the raw stores at `:259-263` and `:275`. A mapped-but-read-only page
  would fault inside the game (the payload is manually mapped without exception
  tables). **Use a checked write, or extend/rename the helper honestly.**
- **N10** `payload/features/src/thirdperson.cpp:162` — the console fallback
  hinges on an unconfirmed `;` split: `push_command("thirdperson 0; thirdperson 1")`.
  `Console::exec`'s header says a `;`-separated line is allowed, but
  `docs/re/engine2.md` only says the text is *appended* to the command buffer,
  and no other payload code passes a `;`-separated line. **Two `exec` calls
  instead of one** — it costs nothing and removes the assumption.
- **N11** `payload/features/src/esp_skeleton.cpp:100-115` — with the chest
  capsule missing, the "above the chest" test degenerates (`chest.empty()` ⇒
  everything is "above"), the pelvis is never identified, and **the legs vanish
  entirely** (17 → 8 segments — proven with the real `build_chain`). A third
  "generic" (gear) capsule below the pelvis also steals the pelvis role.
- **N12** `payload/features/src/esp_world.cpp:73-102` — the prefix test misses
  the grenades that are not in the list (`sensorgrenade_projectile`,
  `bumpmine_projectile` → no marker at all) and classifies the *deployed*
  incendiary `weapon_taggrenade` as a **dropped weapon**. The 7-char prefixes
  themselves do not mis-fire on the classes in the spec.
- **N13** `payload/features/src/esp_world.cpp:175-176,316-326` — the C4's timer
  and its "red when ticking" colour depend on the generated schema
  (`schemas/client_dll.hpp`), which **does not exist in this tree** (the
  calibration dumps are empty), so `schema_field()` always returns the `0`
  fallback and the bomb is a static orange square labelled "C4". The code
  documents the fallback; the feature is nevertheless inert in this build.
- **N14** `payload/features/src/thirdperson.cpp:162` — the console fallback also
  resets the camera distance: the `thirdperson` change callback writes
  `+0x238 = 30.0f` every time it fires, so each re-arm resets the distance and
  the engine then lerps it back to `cam_idealdist`.

## Nits

- **N15** The review found **no style regressions** in T4/T5: no line over 100
  columns, no `using namespace std`, no `<windows.h>` in the
  platform-independent headers, `enum class` for the modes, trailing return
  types, `[[nodiscard]]`/`noexcept` on the accessors, and no new global mutable
  state beyond the documented ADR-0010 registry. **Keep it that way** — run
  `clang-format --style=file` on everything you touch.

---

## One thing the review did NOT flag, that you should look at

The review noted (in the wire-limits row) that the **pre-existing** hitbox
visualiser can still overrun the shared segment's vertex pool on a full server:
19 capsules × 32 hull points × 64 players ≈ 39 k, against `k_max_vertices =
16384`. The publisher clamps and the layer bounds-checks, so an overflow
**drops** rather than corrupts — but the visualiser is off by default. If you
can bound it cheaply (e.g. cap the number of players the visualiser draws, or
the points per capsule), do; if not, say so in your report.

## Verification

- `meson compile -C build && meson test -C build` → green (22/22 at baseline).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → clean.
- **For M1 specifically, prove it**: build the host tests in a **release**
  configuration (`meson setup build-release . --buildtype=release`), break the
  maths on purpose, and show that the test **fails** there. A test that only
  bites in the debug build is not a test.
- For M2, prove the read is gated: the review's argument is that the address is
  still inside `.data` on a different build, so the read succeeds with garbage.
  Show the gate (or the changed behaviour) with a test or a clear demonstration.
- Commit in logical pieces (`fix(esp): …`, `fix(camera): …`), not one giant
  commit.

## Report

For each finding: **fixed / not fixed (with why)**. Plus: what the M1 fix is
(explicit checks, or `b_ndebug=false`, or both), how M2's read gate is
implemented, and anything the review missed.
