# T5 — Third person (force the game's camera)

**Goal:** make the game render from a third-person camera that the operator
controls (distance, angle, pivot height, FOV, shoulder side), and keep the whole
payload correct while it is active.

**This task depends on a reverse-engineering result.** The RE report is at
`C:\Users\gpmpo\AppData\Local\Temp\opencode\re-thirdperson.md` — **read it first**
and treat its `verified` findings as the ground truth. Where the report says
`unknown`, you must **not** guess: write the code so the unknown part is a
clearly-marked constant you can fill in, and say so in your report.

### 0.1 Already established (do not re-derive) — from the RE report

The live `client.dll` is build **25947352**, sha `659940c1…`, base `0x180000000`.
**The repo's `docs/re/client-dll.md` addresses are from a different build and are
stale** — the report re-derived everything on the live build.

| What | Where (RVA from the client.dll base) |
|---|---|
| `CCSGOInput*` global | `client + 0x223bd00` → the object at `client + 0x257b640` |
| **third-person flag** | **`CCSGOInput + 0x229`** (uint8) — **the switch the camera code gates on** |
| camera pitch | `CCSGOInput + 0x230` (float) |
| camera yaw | `CCSGOInput + 0x234` (float) |
| camera distance | `CCSGOInput + 0x238` (float; the engine's default is `30.0f`) |
| camera mode | `CCSGOInput + 0x6a8` (int32) — **0 = third person**, 1 = another path, 2 = first person |
| per-slot camera stride | `0x928` (slot 0 is at the object base) |
| the `thirdperson` convar | name string `client + 0x1c5ed70`; change callback RVA `0xb64190`; handle global `client + 0x2560280` |
| the callback's effect | `+0x229 = 1`, `+0x230/+0x234` = the current view pitch/yaw, `+0x238 = 30.0f`, `+0x6a8 = 0` |
| the per-frame camera update | RVA `0xb62a20`; it **clears `+0x229` in three places** (dead player, state check, mode 2) ⇒ the flag must be **re-asserted** |
| the `ICvar` interface pointer global | `client + 0x279cd58` (`FindVar` = vtable `+0x58`, `RegisterConCommand` = `+0x150`) |
| `CViewRender` object / global | `client + 0x2579600` / `client + 0x256b9e8`; vtable `0x1c7d1f8` |
| the per-slot view setup | `CViewRender + 0x10 + slot*0x990`; **origin `+0x4a0`**, **angles `+0x4b8`**, viewport `+0x420…`, **aspect `+0x4d4`** |
| **the published per-slot view state** | **`client + 0x256af48 + slot*0xc`** — origin (12 B) then angles (12 B). **This is the camera's real position.** |
| the view-setup builder / finaliser | RVA `0xb9c970` / `0xc1a710` — the view struct is **rewritten every frame** |
| `CViewRender::Render` | RVA `0xc17b60` |
| the camera's wall trace | `CGamePhysicsQueryInterface::TraceShape` RVA `0xa1a000` (the `cam_collision` path) |
| the `cam_*` convars | `cam_idealdist`, `cam_idealyaw`, `cam_idealpitch`, `cam_snapto`, `cam_collision`, `cam_showangles` (each has a 16-byte `ConVarRef` global; the `ConVar*` is at `+8`; the float value is at `ConVar* + 0x58`) |

### 0.2 What the engine does **not** support (read this before you design)

- **The view struct is rebuilt every frame** — writing it is pointless. The durable
  state is the `CCSGOInput` camera state.
- **There is no shoulder/side offset and no pivot in CS2's client.** The
  `c_thirdpersonshoulder*` convars exist and are registered, but the report
  verified that **nothing ever reads their values**. The camera orbits the
  player's **eye origin** at `distance` behind it — the pivot *is* the eye.
  ⇒ `movement_thirdperson_side` and `movement_thirdperson_pivot_z` have **no
  engine counterpart**. Implement them only if you can do it honestly; otherwise
  leave them inert and say so plainly in your report.
- **There is no fixed camera angle.** The engine smooths the camera yaw toward
  the *view* yaw (`cam_idealyaw`), so a fixed offset is damped away. The
  `movement_thirdperson_angle` setting can only be honoured as a **yaw offset
  applied by the payload** (e.g. by driving `cam_idealyaw` to `view_yaw + offset`
  each frame, if you can set a convar value) — and even then the engine's
  smoothing fights it. Report exactly what you achieved.
- **The FOV is not a client convar** (`cl_fov` does not exist in `client.dll`;
  it is engine-side). The view setup has a `1.0f` at `+0x4cc` and a value from
  `CViewRender` vfunc `+0x78` at `+0x4d0` — which one is the FOV is **unconfirmed**.
  `movement_thirdperson_fov` is therefore **uncertain**: do not write a FOV field
  you have not confirmed. Say so.
- The `thirdperson` convar's flags (`0x20004000`) do **not** include
  `FCVAR_CHEAT`, but a community report says `sv_cheats 1` was required — treat
  it as an **open discrepancy** and prefer the direct write (Route B) over the
  console route if it is more reliable.

**What the user asked for** is *"force the game's camera into 3rd person"*. The
engine's own third-person camera **is** that. The distance setting is fully
supported; the angle/pivot/side/FOV settings are the extras, and the engine
does not implement three of the four.

**File ownership (you may ONLY touch these):**

- `payload/features/src/thirdperson.cpp` *(new)*
- `payload/features/include/cs2/features/thirdperson.hpp` *(new)*
- `payload/features/meson.build` (to add the new source)
- `payload/game/include/cs2/game/camera.hpp` *(new)* — the raw camera access
- `payload/game/src/camera.cpp` *(new)*
- `payload/game/meson.build` (to add the new source)
- `payload/src/payload_main.cpp` (only the settings→third-person plumbing)
- `payload/features/src/overlay.cpp` (only the camera-origin fix, see §5)

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first.

**T1 owns the settings.** It has already added these `wire::Settings` fields and
the matching `features::Config` members — **use them, do not add new ones**:

```
uint8_t movement_thirdperson;          // force the camera into third person
float  movement_thirdperson_distance;  // 64..512 units
float  movement_thirdperson_angle;     // 0..360 degrees
float  movement_thirdperson_pivot_z;   // 0..64 units
float  movement_thirdperson_fov;       // 0 = keep the game's FOV
uint8_t movement_thirdperson_side;     // 0 = left shoulder, 1 = right shoulder
```

---

## 1. What "third person" means here

The engine already has a `thirdperson` convar and a third-person camera. The
operator wants the **payload** to hold the camera in a third-person view it
controls.

**Two write routes, in this order of preference:**

**Route B (preferred) — a direct per-frame write into the input object.**
From the `CCSGOInput::CreateMove` hook (the input phase, **before** the frame's
camera update), write the camera state for slot 0:

| # | address | value |
|---|---|---|
| 1 | `CCSGOInput + 0x238` (float) | the distance |
| 2 | `CCSGOInput + 0x230` (float) | the camera pitch |
| 3 | `CCSGOInput + 0x234` (float) | the camera yaw |
| 4 | `CCSGOInput + 0x6a8` (int32) | `0` = third person |
| 5 | **`CCSGOInput + 0x229` (uint8)** | **`1` — the switch** |

**Route A — the console.** `Console::exec("thirdperson 1")` (the payload already
has `Console::exec` → `Cbuf_AddText`, the engine's own path) runs the client's
change callback, which initialises the state correctly (including copying the
current view pitch/yaw and the `30.0f` distance). The catch: the callback fires
only on a **value change**, and the game **clears `+0x229`** on death / respawn /
round-state changes, so the command has to be **re-issued**.

**In practice you probably want both**: the console route to initialise the
state correctly (it also puts the camera in the right mode and copies the view
angles), and the direct write to **re-assert the flag every frame** so a respawn
does not drop the camera.

**Implement the route(s) the report supports and say which you used.** If the
console route is enough on its own, use it; if the direct write is needed to
survive a respawn, use both.

**Never** write the `CViewRender` view struct: it is fully rewritten every frame
by the view builder.

## 2. The camera access module

Create `payload/game/{include/cs2/game,src}/camera.{hpp,cpp}` — a small,
**read-only game accessor** in the same style as `payload/game/src/state_reader.cpp`
and `payload/game/src/module.cpp`:

- it resolves the `CViewRender` singleton (the report gives the RVA and the
  pointer global),
- it reads the **local slot's** view struct,
- it exposes the camera **origin**, **angles** and **FOV** as read-only accessors,
- and (for route 1) it exposes a **write** of the third-person fields.

Rules:
- `payload/game` is compiled into the **host tests** as well, so it must stay
  **free of `<windows.h>`** and of any engine dependency. The existing
  `payload/game` code uses `cs2::game::read` (a fault-safe read through
  `ReadProcessMemory`) and `cs2::game::find_module`.
- Reads of engine memory must go through the existing fault-safe helpers — a
  direct `*reinterpret_cast<...>` on an engine address will crash the game the
  first time the offset is wrong.
- Every accessor is `[[nodiscard]]` and `noexcept`; a failed read returns a
  sensible default (an invalid camera), never a garbage value.
- The offsets are **build-specific**. Prefer resolving them from the **generated
  SDK** (`payload/sdk/`) if the dumper exposes them; if not, hard-code the RVA
  with a comment naming the build it was verified on (the repo's convention —
  see `payload/sdk/offsets.hpp` and `docs/re/client-dll.md`).

## 3. The feature

`payload/features/{include/cs2/features,src}/thirdperson.{hpp,cpp}`:

- A small class that the payload owns (`Runtime` in `payload/src/payload_main.cpp`).
- It has a `configure(...)` (or `apply_settings`) that takes the operator's
  values, and an `update()` that is called **once per frame, before the view
  matrix is read**.
- When disabled, it must **write nothing** and must restore whatever it changed
  (if the convar route was used, restore the previous value; if the struct route
  was used, the engine rewrites the struct every frame anyway, so "do nothing" is
  enough — say which you rely on).

### 3.1 Where the update must run

The camera is built during the **render frame**, and the payload's view matrix is
read in `Overlay::update` (`payload/features/src/overlay.cpp`, at ~30 Hz).
The third-person write must land **before** the engine builds the camera for
that frame, and it must be re-applied **every frame** (the engine rewrites the
view struct every frame).

The payload's hooks are:
- `CCSGOInput::CreateMove` / `CreateMoveInput` (the game's **tick** thread), and
- the DXGI `Present` interposer (the **render** thread).

**Decide, from the RE report, which thread can write the camera early enough**,
and put the write there. The render-thread `Present` hook is too late for the
*current* frame (the camera has already been built), so the write must either go
through the tick thread (`CreateMove`) or be a **deferred** write that takes
effect on the next frame. Say which you chose and why.

**Never** write the camera state from a thread that is not the game's — a
racy write into the view struct while the render thread reads it is a crash.

## 4. The values

- **distance** (64..512 units): the camera's distance behind the player.
- **angle** (0..360°): the camera's yaw offset around the player.
- **pivot_z** (0..64): the height of the point the camera orbits (the player's
  origin is at the feet; the head is ~64-72 units up).
- **fov** (0 = keep the game's): the camera FOV.
- **side** (0/1): the left/right shoulder offset.

The engine's own `thirdperson` semantics define how these combine. **Follow the
engine's model**, do not invent a camera: read how the engine computes the
third-person origin from the distance/angle/pivot/side and reproduce that
(formula + the report's evidence), so the payload's camera is the engine's camera
with the operator's parameters.

If the engine's model is not fully recovered, the payload must implement the
missing part itself and say so.

## 5. Keeping the payload correct in third person

**This is not optional.** In third person the camera is no longer at the player's
eye, and several things in the payload assume it is:

- `payload/features/src/esp.cpp` projects the world with the view matrix (fine),
  but the **hitbox capsule silhouette** (`payload/features/src/overlay.cpp`,
  `draw_capsule_*`) uses `snapshot.local->eye_origin` as the camera eye for its
  tangent-circle maths. With the camera behind the player, that eye is wrong and
  the silhouettes are wrong.
- The **ragebot's** aim maths uses the **eye origin** as the origin of the aim
  rays (`payload/aim`). In third person the *bullet* still leaves the player's
  eye, so the **ragebot must keep using the player's eye**, not the camera's.
  Do not "fix" the ragebot to use the camera origin — that would break it.

**The camera origin is available**: the engine publishes the per-slot view state
at **`client + 0x256af48 + slot*0xc`** — the origin (12 bytes) then the angles
(12 bytes). The RE report verified that the view-setup finaliser (`0xc1a710`)
writes exactly that. Alternatively the origin is at `CViewRender + 0x10 +
slot*0x990 + 0x4a0`.

**Requirement:** expose the **camera origin** (from the new camera module, read
from the published per-slot state) and use it for the *rendering* maths (the
capsule silhouettes, and any screen-space effect that needs the camera
position). Keep the *gameplay* maths on the player's eye. Say clearly in the
report which is which.

The world-to-screen projection itself is unchanged — it uses the view matrix,
which the engine recomputes for the third-person camera.

## 6. The settings plumbing

`payload/src/payload_main.cpp`:
- `Runtime` gains a third-person object.
- The settings sink (`on_movement_settings` or a new sink) forwards the
  `movement_thirdperson*` values.

**Only touch those parts of `payload_main.cpp`.**

## 7. Verification

- `meson compile -C build && meson test -C build` → 17/17 or more.
- `meson compile -C build-payload` → clean.
- Add a **host test** for the pure camera maths (the origin formula, the
  pivot/distance/angle/side composition) — it is pure arithmetic and must be
  testable without the engine. Put it in `payload/game/tests/` (or
  `payload/features/tests/`) and wire it into the root `meson.build` the way the
  other tests are wired.
  The test must cover: the camera ends up behind the player for the expected
  angle; the shoulder side mirrors correctly; the pivot height lifts the camera;
  the FOV override is applied only when non-zero.
- **You cannot launch CS2** (it needs `schtasks /run /tn CS2Launch` and a GPU in
  the interactive session). State clearly what is unverified.

## 8. Report

- The route you implemented (struct write / convar) and the evidence for it.
- The camera formula and its test.
- **Where** the write happens (which hook, which thread) and why that is early
  enough.
- What you changed for the ESP/capsule eye, and what you deliberately left on
  the player's eye.
- Every offset you used, with its provenance (SDK-generated or RE-verified) and
  the build it was verified on.
- What is still `unknown` from the RE report and how the code behaves then.
