# FIX-4 — the C++ wave's review findings

**Your worktree: `C:\Users\gpmpo\cs2-wt\fix4`**, branch `fix/cpp-review`, based
on the integration tip. Do all your work there; never touch
`C:\Users\gpmpo\cs2-internal` or another agent's worktree.

**Read first:** `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` and
`C:\Users\gpmpo\cs2-internal\docs\style-guide.md`.

The review's verdict was **DO NOT SHIP**. The findings are below. **The review's
reasoning is authoritative** — it ran hostile experiments and its numeric probes
are reproducible; if you think a finding is wrong, say so with evidence.

> **Coordination:** the integration branch is receiving a mechanical clang-tidy
> sweep right now. Before your final commit, `git merge feat/occlusion-penetration`
> into your branch and resolve any conflict **in favour of both** (the sweep's
> mechanical change *and* your fix).

---

## Blocker

### B1 — the triggerbot **never fires** in the default input backend

`payload/control/src/trigger.cpp:317` — `input.press(button::k_attack);`
`payload/control/src/input.cpp:195-198` — `Input::press` → `set_mask_.fetch_or(bits)`
`payload/control/src/input.cpp:1682` — `const bool control = backend_ == Backend::usercmd;`
`payload/control/src/input.cpp:1691` — `value = control ? (current | set_mask_) & ~clear_mask_ : current;`
`payload/control/include/cs2/control/input.hpp:636` — `Backend backend_ = Backend::console;`

`set_mask_`/`clear_mask_` are only applied to the command **when the backend is
`usercmd`**, and the default is `console`. `set_mask_` is read in exactly one
place (line 1691), so in the default configuration the trigger's press never
reaches the command's button mask.

**Failure scenario:** the operator enables the trigger and aims at an enemy.
`Trigger::tick` presses, `shots_` increments, `decision.hold` is true, and the
`trigger` command answers `{"firing": true, "shots": 3, "backend": "console"}` —
**but the gun never fires.** The failure is invisible.

**The asymmetry is the point:** the ragebot's own fire is **not** backend-gated
(`input.cpp:1701-1703`, `value |= button::k_attack`), and the comment at
`input.cpp:1673-1681` says the payload features "must act regardless of the
backend". The triggerbot is such a feature (it has no console equivalent) and
does not.

**Fix:** the trigger's press/release must reach the command the way the ragebot's
fire does — an **unconditional** `value |= k_attack` under the trigger's own hold
state — or `Input::press` must be split into a control-command path
(backend-gated) and a feature path (not). **Pick the second** if it can be done
without touching every existing `press` caller; the first is simpler. Say which
and why.

**A host test cannot cover this** (it is the backend gate), so add a test that
pins the *decision* the backend makes: a pure function that says whether a
feature-held button reaches the command, given the backend. Prove it can fail.

## Majors

### M1 — the ragebot's **auto-stop** writes the inverted side axis

`payload/control/src/input.cpp:1807` — `side = -std::sin(rel) >= 0.0F ? 1.0F : -1.0F;`
`payload/control/src/input.cpp:1813` — `move[1] = side;` (the command's `leftmove`)
`payload/control/src/input.cpp:1819` — `engine_move_buttons(forward, side, k_move_epsilon)`

T13 introduced the single convention constant
`k_engine_side_axis_positive_is_left` and claims "**Every** use of the side-axis
sign in this module goes through it" — the auto-stop path does not: it computes
the side axis by hand.

`rel` is the counter-wish's bearing from the aim, counter-clockwise positive.
The counter-wish's component along the aim's **left** vector is therefore
`+sin(rel)`, and the engine's `leftmove` is positive-to-the-**LEFT**
(`docs/re/strafe-sign.md`). The code writes `sign(-sin(rel))` — the exact
negative.

The review's probe, using the repo's own helpers:

```
strafe:  rel=+90 -> leftmove=+1  world wish heading=+90.0 deg (want +90 = LEFT)
autostop: rel=+1.571 -> forward=-1 leftmove=-1  world wish heading=-135.0 deg
          the counter-wish should point at +90.0 deg (the player's LEFT)
          with engine_side_axis_for_wish(): leftmove=+1  world wish heading=+135.0 deg
buttons: the code's side -> IN_MOVERIGHT ; the corrected side -> IN_MOVELEFT
```

**Failure scenario:** the player strafes right while the ragebot's auto-stop is
active. The counter-strafe is supposed to press *A* (left) to kill the lateral
speed; the code presses *D* — the same way the player is already moving — and
adds a backwards component, so the "brake" accelerates the player sideways. The
auto-stop is exactly the assist that must not move the player; this is the
feature's whole purpose inverted.

**Fix:** route the site through the convention helper (so the value and the
button bits both flip).

**Secondary, same block:** `forward = sign(cos(rel))` is decided by
floating-point noise at exactly 90° — `cos(π/2)` in float is `-4.37e-8`, so the
forward component is `-1` (backwards) instead of 0. A counter-strafe wants
**lateral only**. Fix that too.

### M2 — the `settings` command's clamping never reaches the movement / ragebot / third-person features

`payload/src/payload_main.cpp:335-342` hands the **wire fields directly** to
`runtime->thirdperson.configure({... .distance = settings.movement_thirdperson_distance ...})`,
and `payload/features/src/settings.cpp:212-222` (`from_wire_settings`) is where the
clamping lives.

`docs/control-harness.md` promises: *"A number that is out of the field's range
is accepted and clamped, not an error: the payload's features read the block
through `features::from_wire_settings`, which clamps every numeric field
again."*

That is true only for the **ESP** (`Menu::apply()` → `set_esp_config(
from_wire_settings(settings_))`). The settings sink reads the wire fields
directly and hands them to the control service and to the camera. The review's
probe:

```
the command accepted it: changed=1
the live block holds movement_thirdperson_distance = 1000000000.0
from_wire_settings() would clamp it to  = 512.0
the third-person module would write 1000000000.0 into the engine's camera distance field
```

**Failure scenario:** `{"cmd":"settings","args":{"set":{"movement_thirdperson_distance":1e9}}}`
→ the third-person module writes `1e9` into `CCSGOInput + 0x238` → the camera is
a billion units away. `movement_thirdperson_angle` writes a garbage yaw into
`+0x234`; `rage_fov` puts `1e30` into the ragebot's FOV cone;
`movement_strafe_wishspeed` is unbounded.

**Fix:** route the sink's values through the clamp before handing them to the
features — one place, so every consumer gets the clamped value. The cleanest
shape: the sink converts the block **once** (`from_wire_settings`) and hands the
**typed, clamped** config to the features, instead of reading the wire fields
one by one. (That also removes the duplication between the ESP path and the
others.)

### M3 — the `settings` write path is an unsynchronised whole-block copy against the render thread

`payload/src/payload_main.cpp:416` — `runtime->menu.settings() = in;` ← **control thread**
`payload/src/payload_main.cpp:380` — `runtime->menu.settings() = outcome.block;` ← render thread
`payload/features/include/cs2/features/menu.hpp:57` — `wire::Settings& settings()` — a plain member, **no lock**
`payload/features/src/menu.cpp:480` — `draw_menu(settings_, ...)` writes individual fields on the render thread

The control channel's *own* copy (`LiveSettings`) is mutex-protected, but the
**owner** it writes through is not. Two 441-byte non-atomic assignments of the
same object from two threads, plus per-field writes from the menu's widgets, is a
data race that can tear the block.

There is also a plain **lost update**: `run_settings` does read-modify-write
(`settings_access.cpp:235` read, `:253` write) — the read comes from the last
published copy and the write replaces the **whole** block, so any change the
render thread made in between is clobbered.

**Fix:** give the owner's block a lock (or route the control channel's write
through a queue the render thread drains), and make the write a **field-wise
merge** rather than a whole-block replacement.

### M4 — the generated settings table is not regenerated by the **payload** build

`meson.build:202-216` — the `custom_target('settings-table', …)` exists **only in
the root (host) project**; `payload/meson.build` has the `sdk-headers` target but
**no** settings-table one.
`payload/control/meson.build:29` — the payload compiles against the
**checked-in** `overlay/include/cs2/overlay/settings_table.hpp`
`payload/control/src/settings_access.cpp:37-43` — the field access is a raw
`reinterpret_cast` at the table's offset
`meson.build:224` — `test('settings-table', gen_settings_table, args : ['--check'])`
runs **after** the custom target has already rewritten the file, so it can never
fail in a `meson test` run.

The checksum `static_assert` catches a `wire.hpp` change *until* you update
`k_default_settings_checksum` — and the header's own comment tells you to do
exactly that. After that, a payload-only build compiles cleanly against a stale
table.

**Proven:** with one byte added to `wire.hpp` and the checksum updated, a
**payload-only** build succeeded with the stale 87-entry table, and
`settings set esp_max_box_scale = 2.0` wrote **0.75** into the field (the
neighbouring bytes were shifted). No error, no log line: silent corruption.

**Fix:** add the same `custom_target` to `payload/meson.build`, **and** make the
staleness test compare against a **pristine** copy rather than the regenerated
file (or move the generation out of the build into a `meson setup`/CI step).

## Minors

- **m1** `payload/features/src/chams.cpp:273,309` — the draw hook does **four
  `ReadProcessMemory` syscalls per drawn primitive** (measured at 284 ns each →
  1.1 µs/primitive → 0.57 ms/frame at 500 primitives, 2.3 ms at 2000), and it
  pays them for **every** primitive in the scene, not only the players' — the
  entity lookup runs before the "is this one of ours" test. The header's claim
  ("The render path is cheap") is wrong. **Either** reorder so the cheap test
  comes first, **or** use a guarded direct read (the addresses are engine-owned
  and stable) instead of the syscall. Correct the header either way.
- **m2** `payload/control/src/service.cpp:2203-2214` — the sound provider takes
  the **oldest** 32 events (the ring is walked oldest-first) and the ring is
  shared with *all* game events. Under load the freshest footsteps are the ones
  dropped, and the 1 s age filter then discards the survivors. **Keep the
  newest 32.**
- **m3** `payload/control/include/cs2/control/service.hpp:137-138` — the slot →
  player resolution is a **rank** over the entity-index-ordered table. The rank
  equals the slot only while every lower slot is still connected; after slot 2
  leaves, a sound from slot 3 is placed on the player at rank 3 (slot 4). The
  exact-match fallback only runs when `slot >= count`. **Fix the mapping** (the
  table should carry the slot explicitly) and say how.
- **m4** `scripts/scenario/protocol.py:153-154` — the `ButtonName` literal still
  advertises `moveleft`, `moveright`, `speed`, `zoom`, which are **not**
  console-inexpressible names in CS2 (the engine's actions are `attack, attack2,
  back, duck, forward, jump, klook, left, movedown, moveup, reload, right,
  sprint, strafe, turndown, turnleft, turnright, turnup, use`). T14 fixed the
  `BUTTONS` table but left these in the *typed* set, so `press("zoom")`
  type-checks and then issues an unknown console command. **Make the typed set
  and the console table agree**, and say which names are console-expressible
  and which are `usercmd`-only.
- **m5** `payload/control/include/cs2/control/settings_access.hpp:140` — the
  `BatchRunner::k_max_depth` constant is dead code (the nested-`batch` rejection
  is a string comparison, so the depth check can never fire). Remove it, and fix
  the doc's "a nesting depth of 1" (it is really "no nesting at all").
- **m6** `payload/hooks/src/inline.cpp:410-416` + `payload/features/src/chams.cpp:117-123`
  — the patch is written to the target **before** the trampoline is published, and
  a detour that sees `original() == nullptr` returns **without drawing**. A
  primitive drawn in that window is silently dropped. **Publish the trampoline
  before the patch** (in the hook engine, so every user benefits).
- **m7** `payload/src/payload_main.cpp:322` + `payload/features/src/config_store.cpp:889-900`
  — `publish_config_path` overwrites `menu_config_path` with the payload's own
  path, so a `settings` set of that field reports `changed: ["menu_config_path"]`
  and the value is reverted on the next frame. The same applies to the one-shot
  op fields (`menu_config_op`/`menu_config_token`, `rage_config_op`/
  `rage_config_token`), which a `settings` set will *trigger*. **Exclude the
  protocol fields from the command's write set** (or make them read-only).
- **m8** `payload/control/src/service.cpp:2901` — the `heatmap` `target` window
  is centred on the target's **eye** while the cells are its **body**, so the
  window is offset by ~1.6 m from what it should frame.

## Nits

- `payload/control/src/service.cpp:436-438` — the `events` `limit` argument has
  no upper bound (only a lower one). `EventRing::recent` clamps it, so it is
  safe, but the asymmetry is odd next to `log`'s explicit cap.
- `payload/control/src/service.cpp:471` — `last_seq` and `count` are the same
  value (`EventRing::count()` is the highest sequence, not the number retained).
  Two names for one number in one object.
- `docs/control-harness.md:178` — "so it cannot drift from the struct" is
  stronger than the mechanism delivers (see M4).

## Verification

- `meson compile -C build && meson test -C build` → green (39/39 at baseline).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → clean.
- **Prove each fix's test can fail.**
- You cannot launch CS2 (it needs `schtasks /run /tn CS2Launch` and a GPU in the
  interactive session) — say clearly what is unverified.

## Report

For each finding: **fixed / not fixed (with why)**. Plus: which write path the
trigger now uses, the clamp's single place, the lock/merge design for the
settings block, how the table is kept in sync, and what you could not verify
without a live game.
