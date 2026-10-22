# T19 — the anti-aim on the angle split

## What this task is

The repository has spent two passes on the **camera / eye-angles split**:

* the **T18** pass reimplemented the auto-strafe as Neverlose's, and
* the **T18b** pass built the **split** itself — the capability that lets a
  feature own an angle channel while the camera stays where the operator put
  it (`docs/re/camera-split.md`, branch `feat/t18b-neverlose-camera-split`).

T18b built the split **for the auto-strafe**. That is the wrong consumer. The
split's real purpose is the **anti-aim**:

> the anti-aim must be able to set the player's angles — the fake/desync yaw,
> the pitch, the jitter — **without** moving the camera and **without**
> disturbing the movement's controls.

This task builds that feature on top of the split.

## The ground truth you must build on

Two documents, both merged into the integration branch and both **mandatory
reading**:

| document | what it settles |
|---|---|
| [`docs/re/angle-split.md`](../re/angle-split.md) | the **engine**: the angle channels, the frame timeline, the write points, the hook signatures, the movement re-base, and what is still unknown |
| [`docs/research/angle-split-in-the-wild.md`](../research/angle-split-in-the-wild.md) | the **community**: the shipped products' shapes, the camera-pin shapes and their failure modes, the movement re-base every implementation does |

### The one-sentence answer you are building on

> The command carries **two** independent angle channels in the same tick — the
> command's own `viewangles` (the **body**: the player's angles and the
> movement's basis) and the silent `input_history[].view_angles` (the **shot**
> direction) — while the camera reads a **third** field entirely
> (`CCSGOInput + 0x688`, the live view angles).

So the split the anti-aim needs is **not** a camera pin: the camera already
reads a different field from the one the anti-aim writes. The anti-aim writes
the **command**, and the camera does not follow.

### The consequence that decides the shape

The command's own `viewangles` is also the **movement's wish basis**
(`SetupMove` `0xa74890` → `CMoveData+0x14/+0x18` → `AirMove` `0x8e57b0`
builds `wishdir` from it). So writing a fake yaw into it **rotates the
player's walk basis** — the operator's WASD would steer relative to the fake
angle instead of the camera.

**The anti-aim therefore has two halves, and the second one is not optional:**

1. write the fake angles into the command's `viewangles` (the body), and
2. **re-base the movement** into the anti-aim frame, so the player's controls
   stay camera-relative.

Half 1 without half 2 is a broken feature: the operator's W key would walk
sideways. Half 2 without half 1 does nothing.

### The open blocker you must handle honestly

`move_crc` (`CBaseUserCmdPB` field 19, at `base + 0x30`) is **not written by
the client** — the RE found no CRC routine in the input path, and the payload
computes none today (`docs/re/angle-split.md` §5.4). The community's Weave
note says a modified `forward`/`side` move is rejected by the server unless
the command is re-serialised and the CRC recomputed.

> **CORRECTED 2026-10-21, after the adversarial review.** This section's
> original premise was **wrong**, and the review proved it from this
> repository's own measurements:
>
> * `docs/re/strafing.md:240` (the measured table): the **analog move**
>   (`forwardmove`/`leftmove` + the raw axes) — "the **direction** is honoured
>   — it *does* take over the air movement". The payload's auto-strafe already
>   writes exactly the fields the re-base would write, and the server honours
>   it (measured at +85 u/s per hop).
> * `docs/re/angle-split.md` §5.4: the client **never writes** `move_crc` at
>   all. If the server verified the move values against it, *every* command —
>   modified or not — would be rejected.
> * Weave's note describes a **different** code path: it **re-serialises**
>   the command, which is what makes its CRC stale. The payload writes **in
>   place**, after the engine built the command.
>
> **So the CRC is not load-bearing on this build**, and a gate that makes the
> feature inert protects against a risk that does not exist. The gate must
> therefore default **open**, with "we do not recompute the CRC" kept as a
> **caveat in the status** — not as a gate.

**You must not guess the CRC.** And you must not gate the feature on a risk
this build's evidence excludes. If you keep any gate, it must be reported
with an honest name and an honest meaning — the status must say what is
actually gated.

## The base branch

Your branch is **`feat/t19-anti-aim`**, based on the **integration tip**
(`feat/occlusion-penetration`).

**The T18b split is NOT merged into the integration branch.** It lives on
`feat/t18b-neverlose-camera-split` (tip `eb75f08`) and it also contains the
T18 strafe rework, which is a large, separately-reviewed body of work that is
**not** part of this task.

**Your task does not need T18b's strafe rework**, and it must not pull it in.
You need only the **split capability**, and the split capability is small and
self-contained:

| what | where it is on T18b |
|---|---|
| the pure maths | `payload/control/include/cs2/control/camera_split.hpp` (168 lines, no engine, no `<windows.h>`) |
| its host test | `payload/control/tests/test_control_camera_split.cpp` (150 lines) |
| the dumper signatures | `external/dumper/src/analysis.rs` — the `eye_angles_copy`, `view_angles_setter`, `post_think`, `calc_view`, `local_eye_angles` entries |
| the plumbing | `payload/control/src/input.cpp` — the `Input` members, the detours, the install code — **interleaved with the strafe rework; do not copy it wholesale** |

**Port the capability, do not merge the branch.** Read
`git show feat/t18b-neverlose-camera-split:payload/control/include/cs2/control/camera_split.hpp`
and port it (or write your own, better-shaped equivalent) into your branch.
`git cherry-pick` from T18b is **not** acceptable: it would drag the strafe
rework in with it.

The **anti-aim does not need the camera split at all**, and that is the
point of the RE: the camera reads `CCSGOInput + 0x688` and the anti-aim writes
the command's `base->viewangles` — different fields, so the camera cannot
follow the fake angles. **Do not port the camera-split plumbing unless your
design actually needs it.** If you find yourself adding a camera pin, stop and
re-read `docs/re/angle-split.md` §5.2 and §7 step 7: the command-only shape
needs no pin, and the *view-setup* pin has a published failure mode (the
model keeps the fake pose's cached world transform — "the hands spazz").

## What to build

### 1. The anti-aim's angle model (pure, host-tested)

A **pure** module — no engine, no `<windows.h>`, no globals — with the
anti-aim's decision maths, so the host test can pin it:

* the **fake yaw** the anti-aim writes (the desync yaw: the base angle plus a
  configured offset, the side chosen by the anti-aim's own rule);
* the **pitch** (a configured down-pitch, clamped to the engine's range);
* the **jitter** (the per-tick variation, and the rule for when it flips);
* the **movement re-base** — the projection of the player's intent from the
  camera frame into the anti-aim frame, with the **down-pitch inversion** and
  the **±1 clamp**, exactly as `docs/research/angle-split-in-the-wild.md` §3.2
  describes (Axion's `movment_fix`, MIT, and the community's own correction).

The re-base is the mathematically interesting part and it must be **pure**:
given the camera angles, the anti-aim angles and the player's held intent
(W/A/S/D), it returns the analog axes to write. Put it in
`payload/control/include/cs2/control/anti_aim.hpp` (or an equivalent
pure header) with a host test under `payload/control/tests/` that **can
fail**.

The host test must cover at least:

* the re-base is the **identity** when the anti-aim angles equal the camera
  angles (no anti-aim → the movement must be untouched);
* the re-base **preserves the world-space direction**: the wish direction the
  engine computes from the re-based axes, in the anti-aim frame, must equal
  the wish direction the engine would have computed from the original axes in
  the camera frame (this is the invariant that makes the feature correct);
* the **down-pitch inversion**: a camera pitch past 89° flips the forward
  axis (the player would otherwise walk backwards);
* the **±1 clamp**: the returned axes are within ±1;
* the **jitter** is deterministic given its seed and the tick.

### 2. The command write

Write the fake angles into the command's own `viewangles`:

* the field: `CBaseUserCmdPB::viewangles`, at `cmd + 0x10 + 0x30 + 0x40`
  (the `CMsgQAngle*`); the has-bit `0x4` in `base + 0x10`; the x/y/z at
  `+0x18/+0x1c/+0x20`;
* the engine's only writer is `FUN_180b85d70` (RVA **`0xb85d70`**), called
  from `create_move_input` (slot 5) — an inline hook on it is the clean seam
  (15-byte branch-free prologue), **or** a direct write into the command
  after the engine built it. Either is acceptable; **say which you chose and
  why** in the report.

The payload already has a command-angles path (`Input::set_command_angles` /
`command_angles_enabled_`, `payload/control/src/input.cpp`) — **read it and
reuse it** if it fits; do not add a second mechanism for the same field.

**The write happens once per command**, not per frame: the command's own
`viewangles` is written exactly once per tick by the engine, and a
higher-frequency writer fights the engine's own write-back (the post-think,
`0xb07590`).

### 3. The movement re-base

Apply the re-based axes to **both** the command's floats
(`base->forwardmove/leftmove`, `+0x58/+0x5c`) **and** the raw axes
(`CCSGOInput + 0x270/0x274`) — the slot-5 builder re-derives the command's
floats from the raw axes, so a command-only write is overwritten.

**Gate this behind the CRC question** (see "the open blocker" above). The
gate must be a **real** gate, not a comment: the anti-aim's movement re-base
is off until the CRC is settled, and the payload must **report** which mode
it is in (`input` status), so the operator can see that the movement re-base
is disabled rather than silently broken.

### 4. The diagnostics

The `input` status (`payload/control/src/service.cpp`) must report, so the
operator can see what the anti-aim did:

* whether the anti-aim is **enabled** and whether it **acted** this tick;
* the **fake yaw** and **pitch** it wrote;
* the **camera yaw** (the operator's own — it must **not** move; this is the
  proof the split works);
* the **re-based axes** it wrote (and that they were **not** written, if the
  CRC gate is closed);
* the **counters**: how many times the anti-aim wrote, how many times the
  movement re-base was applied, how many times the re-base was skipped.

### 5. The control channel

A control-channel command to drive it (`payload/control/src/service.cpp`), in
the same shape as `strafe` / `bhop` / `look`:

```
antiaim {"on": true, "yaw_offset": 0.0, "pitch": 0.0, "jitter": 0.0, "side": 0}
```

`side`: 0 = automatic, +1 = left, -1 = right. The command returns the same
status block the other movement commands return.

### 6. The settings (optional, and only if you can do it cleanly)

The anti-aim should be switchable from the **menu**, like the other movement
features. That means a `wire::Settings` field, and `wire::Settings` is
**shared** (ADR-0021): a change must bump `wire::k_version` and update **all**
of `payload/features/include/cs2/features/esp.hpp` (`EspConfig`),
`payload/features/src/settings.cpp` (`to_wire_settings` / `from_wire_settings`)
and `overlay/src/renderer.cpp` (`default_ui_settings`).

**If you cannot do the settings work cleanly within this task, do not do it
partially** — leave the feature driven by the control channel only and say so
in the report. A half-wired settings field is a bug.

### 7. The scenario

A scenario under `scripts/scenario/scenarios/` that observes the anti-aim in
a live round, in the shape of `autostrafe.py` (observation-only, no driving):

* the anti-aim is on and it acted;
* the **camera yaw did not move** while the fake yaw did (this is the whole
  point — the split);
* the re-based axes were written (or the CRC gate is closed, which the report
  must say).

Register it in `scripts/run-scenario.py` (or wherever the scenarios are
listed) and cover it in `scripts/scenario/tests/` if the harness's own tests
apply.

## File ownership

You own:

* `payload/control/include/cs2/control/anti_aim.hpp` (new — the pure maths)
* `payload/control/include/cs2/control/input.hpp` and
  `payload/control/src/input.cpp` (the anti-aim's engine access)
* `payload/control/src/service.cpp` and
  `payload/control/include/cs2/control/service.hpp` (the control command)
* `payload/control/tests/test_control_anti_aim.cpp` (new — the host test)
* `payload/control/meson.build` and the root `meson.build` (registering the
  test)
* `external/dumper/src/analysis.rs` (only if you need a new signature — the
  RE gives you the RVAs; add the signatures the repo's dumper resolves, do
  **not** hand-edit the generated SDK headers)
* `scripts/scenario/scenarios/anti_aim.py` (new) and the scenario's
  registration/tests
* `scripts/scenario/protocol.py` — the scenario's typed view of the payload's
  status block. **Added to the ownership list by the 2026-10-21 review**: the
  scenario reads the anti-aim's status keys through the typed `InputStatus`,
  and a `total=False` TypedDict rejects unknown keys in mypy, so the edit is
  **necessary** and **purely additive**.
* `docs/re/` or `docs/research/` — **only** if you measure something new that
  belongs there (with the repo's evidence standard)

**You do not own** `wire::Settings` unless you take on the whole settings
change (item 6, all three files) — in which case say so in the report. You do
not own the payload's ESP, ragebot, chams or third-person code. If you need a
file outside this list, **stop and report it** instead of editing it.

## The rules

- `docs/style-guide.md` is normative. C++23, 2-space indent, 100 columns, K&R
  braces, trailing return types on declarations **and** definitions, fully
  qualified `std::`, errors as values, `[[nodiscard]]` / `noexcept`,
  value-init with `{}`, `static_cast` / `reinterpret_cast` only.
- **No exceptions**, no CRT static init, no `thread_local` in a detour on the
  manually mapped image (the T18b crash: the TLS block is not installed on the
  game's camera thread — see `docs/re/camera-split.md`).
- The maths goes in the **pure header**, with a host test that **can fail**:
  break it on purpose, watch it fail, revert.
- Commit as you go, `type(scope): summary`.
- **Never** `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase. Never edit the integration tree or another agent's
  worktree.
- The payload is **manually mapped**: no new global mutable state without a
  documented reason (ADR-0010); the counters the control thread reads are
  atomic; the anti-aim's own state is touched on the game thread only.

## The verify loop

Before you claim done:

```powershell
meson compile -C build
meson test -C build            # the host suite must stay green
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

**You cannot launch CS2 from here** — the agent runs in session 0 with no GPU;
the game needs `schtasks /run /tn CS2Launch` (the interactive session) and
then `build-injector\injector.exe cs2.exe <your build-payload\payload.dll>
--load-missing`. **Never inject twice into the same game.** If you can do a
live run, do it and report the numbers; if you cannot, say plainly what is
unverified.

## What the report must state

1. **The anti-aim's write point** — which seam you used (`0xb85d70` hook or
   a direct write), and why.
2. **The movement re-base** — the formula, the pitch inversion, the clamp, and
   the proof that the world-space wish direction is preserved (the host test
   that pins it).
3. **The CRC blocker** — what you did about `move_crc`, and what the operator
   sees when the gate is closed.
4. **The camera** — the evidence that the camera did not follow the fake
   angles (the diagnostic, and the live numbers if you ran it).
5. **The jitter** — the rule, and that it is written once per command.
6. **What you could not verify**, and **what still differs** from the
   shipped products.
7. The file list and the commit hashes.

## The review

The reviewer reads this **same spec** and is hostile. A review that finds
nothing is a failed review. The two things it will look for hardest:

* **the movement invariant** — does the re-based analog really preserve the
  player's intent? (A re-base that is merely "plausible" is a bug: the
  operator walks at an angle.)
* **the camera** — can anything in your change make the camera follow the
  fake angles? (A stray write to `CCSGOInput + 0x688` is the classic.)
