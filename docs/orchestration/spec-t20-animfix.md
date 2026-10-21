# T20 — the local animfix: the server's animation on our own player

## What this task is

The repository wants to **replicate the server's animation locally**:

> reverse how the server applies animations and other calculations and then
> apply them to our own player (and eventually other players, to get accurate
> local player hitboxes)

The community calls this **animfix** (and, for the local player only,
**local animfix**). The **web research** (`docs/research/animfix.md`) and the
**RE** (`docs/re/animfix.md`) are both merged into the integration branch, and
both are **mandatory reading**.

**This task is the first implementation of the local animfix.** The other
players are explicitly **out of scope** — they are a later task, and the RE
gives the same seams for them.

## The ground truth you must build on

| document | what it settles |
|---|---|
| [`docs/re/animfix.md`](../re/animfix.md) | the **engine**: the pipeline, the fields (net vs client), the hitbox link, the divergence, the hook points and their relocatability, and the numbered implementer blueprint |
| [`docs/research/animfix.md`](../research/animfix.md) | the **community**: the five distinct meanings of "animfix", the server's calculation order, the divergence table (D1–D11), the public implementations, and the RE handover |

### The one-sentence answer you are building on

> The pose is computed **entirely on the client** and the hitbox transforms
> are re-derived from the client's own bone array — so overriding the anim
> state, the graph inputs, or the bones themselves **moves the local player's
> hitboxes**.

So a local animfix is feasible on this build. The semantically "ideal" seam
(`CCS2PawnGraphController::ReflectPawnState`, `0x8e3540`) is **not**
inline-hookable (a 20-byte prologue); the workable seams are:

| function | RVA | prologue | what it is |
|---|---|---|---|
| `CCSPlayerAnimationState::Update` | **`0x8a5d70`** | 14 B | the velocity → state derivation (the move state, the aim yaw, the turn angle) |
| the **abs-angle writer** (CS2's `SetAbsAngles`) | **`0x22b470`** | 16 B | writes the scene node's angles — **this is the CS2 equivalent of CS:GO's `SetAbsAngles(QAngle(0, m_flFootYaw, 0))`**, the single line that rotates every hitbox |
| the graph-input **codec** (the server's values) | **`0x8d9740`** | 14 B | writes the server's 28 graph inputs |
| `CAnimGraphGameSystem::AnimTickUpdate` | **`0x35aeb0`** | 15 B | the per-frame anim driver |
| `CSkeletonInstance::SetupBones` | **`0xaa0f80`** | 15 B | writes the bone array |

## The scope, precisely

The divergence the local animfix must close is **one** thing:

> the client predicts the local player's animation, and the **server** sends
> its own values for the **28 graph inputs** of `CCS2PawnGraphController` —
> but the client's prediction (`ReflectPawnState`, `0x8e3540`) writes the
> **same slots**, and whichever runs last wins.

The community's "other-player animfix" recipe is the model:
*"Updating every other player's animations only once on
`NET_POSTDATAUPDATE_END` if they have simulated since the last update gets
you really close-to-perfect hitboxes"* (`docs/research/animfix.md` §5).

**Your task: make the server's values win for the local player.**

### The concrete deliverable

1. **Capture** the server's graph inputs at the codec (`0x8d9740`): the
   server's `aim_angle_yaw` (`controller+0x4e8`), `aim_angle_pitch`
   (`+0x4d0`), `move_speed_horizontal` (`+0x350`), `move_crouch_amount`
   (`+0x380`) and the rest of the 28 slots (`+0x2d8 … +0x560`, stride `0x18`).
2. **Re-apply** them after the client's prediction has overwritten them —
   the seam is the `AnimTickUpdate` detour (`0x35aeb0`) or the AG2 update
   (`0x12e4f90`), whichever the ordering measurement says is correct.
3. **Verify** the result: the local player's hitboxes (the ones the ESP
   draws, from the same bone array) must now match what the server computes.

### The measurement you must make first

The RE could **not** establish the ordering of the snapshot decode and
`ReflectPawnState` inside one frame (§8 of the RE, item 4). **That ordering
is the whole design.** So:

> **Measure it before you choose the seam.** Hook the codec (`0x8d9740`,
> hookable) and log when it writes the graph inputs; hook or observe
> `ReflectPawnState` (`0x8e3540` — 20 bytes, **not** inline-hookable, so you
> may need the vtable or a different observation point) and log when it runs.
> If the codec's write is visible in the graph inputs when the frame's
> animation is evaluated, the server already wins and the task is to stop the
> client from overwriting; if the prediction wins, the fix is to re-apply
> after it.

Record the measurement in `docs/re/animfix.md` (append a dated section — do
**not** rewrite the RE's existing sections) and report it.

### What "the server's animation" means here — and the limit

The client has the server's **sequence layer**, the server's **timing list**
and the server's **28 graph inputs** — but **not** the server's
`CCSPlayerAnimationState` (it is never replicated: the RE shows no codec
encodes it, and no netvar RTTI names it). Reproducing the server's *anim
state* would mean re-running the velocity → state derivation locally; the
derivation function **was not isolated** in the RE.

**Do not attempt to re-derive the anim state.** This task closes the
**graph-input** divergence, which is the one that is actually reachable with
the seams that exist. Say plainly in your report that the anim-state
divergence remains open, and what it would take (the RE §8 item 1's
experiment: a hardware watchpoint on `movementServices+0x328`).

## The rules

- `docs/style-guide.md` is normative. C++23, 2-space indent, 100 columns, K&R
  braces, trailing return types on declarations **and** definitions, fully
  qualified `std::`, errors as values, `[[nodiscard]]` / `noexcept`,
  value-init with `{}`, `static_cast` / `reinterpret_cast` only.
- **No exceptions**, no CRT static init, **no `thread_local` in a detour** on
  the manually mapped image (the TLS block is not installed on the game's
  threads — see `docs/re/camera-split.md`).
- **ADR-0012: no hard-coded RVAs.** Every address goes through the repo's
  dumper signatures in `external/dumper/src/analysis.rs`
  (`known_signatures()`), with the **signature material** the RE gives you in
  its §7 step 8. The signatures must stay **unique** in the module — check
  that.
- Do **not** hand-edit the generated SDK headers
  (`payload/sdk/**`) — they are generated by the build.
- The payload is **manually mapped**: no new global mutable state without a
  documented reason (ADR-0010); the counters the control thread reads are
  atomic; the animfix's own state is touched on the animation thread only
  (note: `SetupBones` runs on a **worker thread** — the AG2 job — so the
  bone write must not race the render thread).
- A bone write must be a **full `CTransform`** (position **and**
  quaternion). A naive `{Vector, Quat}` write produces non-unit quaternions
  and **stretches** the hitboxes (`docs/re/aim.md`).
- The maths goes in a **pure header** with a host test that **can fail**.
- Commit as you go, `type(scope): summary`.
- **Never** `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase. Never edit the integration tree or another agent's
  worktree.

## What to build

### 1. The capture-and-re-apply mechanism

In the payload's control module (`payload/control/`), a module that:

* hooks the codec (`0x8d9740`) and captures the server's graph-input slots
  for the **local player only** (the other players are out of scope);
* re-applies them at the seam the measurement says is correct;
* is **off by default** and switchable from the control channel.

The capture must be a **copy** of the server's values, not a live pointer:
the graph inputs are rewritten every frame, so a captured copy is what makes
the re-apply well-defined.

### 2. The pure part

Put the **decision** logic in a pure header
(`payload/control/include/cs2/control/animfix.hpp`): which slots to
capture, which to re-apply, and the validity rule (a captured value is only
re-applied if it was captured **this** frame — otherwise a stale value would
be written over a fresh prediction). A host test under
`payload/control/tests/` must pin it and **can fail** (break it, watch it
fail, revert).

### 3. The diagnostics

The `input` status must report, so the operator can see what the animfix did:

* whether the animfix is **enabled** and whether it **acted** this frame;
* how many frames were **captured** and how many were **re-applied**;
* the **counters**: captures, re-applies, skips (no fresh capture);
* the local player's **aim yaw** — the server's value and the client's
  predicted value — so the **divergence is visible** (this is the operator's
  evidence that the fix does anything).

### 4. The control channel

```
animfix {"on": true}
```

in the same shape as `strafe` / `bhop` / `antiaim`, returning the same status
block.

### 5. The scenario

A scenario under `scripts/scenario/scenarios/` that observes the animfix in
a live round, in the shape of `autostrafe.py` (observation-only):

* the animfix is on and it acted;
* the **server's** aim yaw and the **client's predicted** aim yaw are both
  reported, and the re-apply count is non-zero.

Register it in the scenario list and cover it in
`scripts/scenario/tests/` if the harness's own tests apply.

## File ownership

You own:

* `payload/control/include/cs2/control/animfix.hpp` (new — the pure logic)
* `payload/control/include/cs2/control/input.hpp` and
  `payload/control/src/input.cpp` (the animfix's engine access)
* `payload/control/src/service.cpp` and
  `payload/control/include/cs2/control/service.hpp` (the control command)
* `payload/control/tests/test_control_animfix.cpp` (new — the host test)
* `payload/control/meson.build` and the root `meson.build` (registering the
  test)
* `external/dumper/src/analysis.rs` (the new signatures)
* `scripts/scenario/scenarios/animfix.py` (new) and the scenario's
  registration/tests
* `docs/re/animfix.md` — **append only** (a dated section with your
  measurement); do not rewrite the RE's existing sections

**You do not own** `wire::Settings` (the menu wiring is **not** part of this
task — the control channel drives the feature), the payload's ESP, ragebot,
chams or third-person code, or the anti-aim work (a **separate agent** is
implementing it in `payload/control/` **concurrently** — see below).

### The concurrency hazard — read this

**Another agent is implementing the anti-aim (T19) in `payload/control/` at
the same time.** You will both want to touch `payload/control/src/input.cpp`,
`payload/control/include/cs2/control/input.hpp` and
`payload/control/src/service.cpp`.

* **Stay in your own worktree.** Never edit the integration tree or the other
  agent's worktree.
* **Keep your changes small and additive.** Put your engine access in your own
  **new** members and functions, named so they cannot collide (prefix them
  with `animfix_`). Do not refactor shared code.
* If you need a change in a shared area that the other agent is also
  changing, **make the smallest possible edit** and say so in your report —
  the orchestrator merges the branches, and a small additive hunk merges
  cleanly where a refactor does not.

## The verify loop

```powershell
meson compile -C build
meson test -C build            # the host suite must stay green
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

**You cannot launch CS2 from here** — the agent runs in session 0 with no GPU;
the game needs `schtasks /run /tn CS2Launch` (the interactive session) and
then `build-injector\injector.exe cs2.exe <your build-payload\payload.dll>
--load-missing`. **Never inject twice into the same game.** If you can do a
live run, do it and report the numbers (the divergence before and after the
fix is the evidence); if you cannot, say plainly what is unverified.

## What the report must state

1. **The ordering measurement** — when the codec writes the graph inputs
   relative to the client's prediction, and how you measured it.
2. **The seam** — which hook you used and why.
3. **The capture-and-re-apply** — the slots, the validity rule, and the
   proof (the host test) that a stale value is never re-applied.
4. **The evidence** — the server's aim yaw vs the client's predicted one,
   before and after (the live numbers if you ran it).
5. **What is still open** — the anim-state divergence, and the bone-write
   seam (which you did **not** use, and why not).
6. **What you could not verify.**
7. The file list and the commit hashes.

## The review

The reviewer reads this **same spec** and is hostile. A review that finds
nothing is a failed review. The things it will look for hardest:

* **the validity rule** — can a stale capture be re-applied over a fresh
  prediction? (That is the bug that makes an animfix make things *worse*.)
* **the thread safety** — `SetupBones` runs on a worker thread; is the
  shared state written from two threads without a race?
* **the concurrency with the anti-aim agent** — did you make a change that
  will conflict?
