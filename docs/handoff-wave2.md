# Handoff — session 2 of the feature wave (2026-10-21)

> **Start here.** This is the entry point for the next session. It says where
> everything is, what is done, what is open, and how to run the same
> sub-agent + adversarial-review orchestration this wave used.
>
> The one-line version: the wave landed (121 commits, 45 Meson tests, 409 Python
> tests, mypy strict clean — the test counts are of the tree as it stands on
> 2026-10-21); the **reviews** caught four blockers and eleven
> majors that would otherwise have shipped; the **chams** are refused because
> their hook target is wrong; and the **auto-strafe's gate is now measured** — the
> silent encoding works, but only on a server that honours the sub-tick view
> angles, and the payload now says so instead of doing nothing quietly (§5.1).

---

## 1. Where everything is

| What | Where |
|---|---|
| The repo | `C:\Users\gpmpo\cs2-internal`, branch `feat/occlusion-penetration` |
| The task worktrees | `C:\Users\gpmpo\cs2-wt\<name>` — one per agent, each on its own branch; the build directories live **outside** the worktree, in `C:\Users\gpmpo\cs2-build\<task>\<build-name>\`, so a worktree holds about **8 MB** of sources — **with the project's environment applied**, i.e. `. <worktree>\.orchestration\env.ps1` **before the first build**; a build that skips it puts the cargo target directory (117 MB) and the mypy cache (43 MB) back into the worktree, and it is **170 MB** again (this worktree, `t25-handoff`, is **127 MB** for exactly that reason) |
| The neverlose artifacts | `C:\Users\gpmpo\nea\rea-work\` — the decompilation, the Ghidra project (`gproj3`), and the reconstructed PE (`neverlose_seeded3.exe`, sha `21efa969…`) |
| The Linux box (the neverlose reversal) | `ssh pontes@100.104.244.127` (Tailscale SSH — it asks for a **one-time browser check** the first time) |
| The game | Steam, `F:\SteamLibrary\…\Counter-Strike Global Offensive` |
| The OpenCode config | `C:\Users\gpmpo\.config\opencode\opencode.jsonc` — the `rea` and `cs2` MCP servers |
| The orchestration plugin | its own repository `C:\Users\gpmpo\orchestration-plugin` (this session's addition) — the **installed copy** is `C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts` plus `C:\Users\gpmpo\.config\opencode\orchestration\`, and `node scripts/install.mjs` is what makes an edit live |
| The task specs (all of them) | `docs/orchestration/` in the repo — **not** the temp dir, which is wiped between sessions |

## 2. The orchestration playbook (this is the part to copy)

The wave ran **task → implementer sub-agent → adversarial reviewer sub-agent →
fix round → merge**, in **git worktrees** (never in a shared tree), with a
**written spec** both agents read. The plugin now does the mechanical parts,
and it runs one more step than the wave did by hand: the **verify loop in the
task's own worktree**, before the review.

### 2.1 The loop

```
WRITE THE SPEC               docs/orchestration/spec-<task>.md

1. orchestrate_plan         -> the worktree + the implementer's prompt
   IMPLEMENT                 a `general` sub-agent, in that worktree
2. orchestrate_verify       -> the verify loop, in that worktree
3. orchestrate_review       -> the review worktree + the reviewer's prompt
   REVIEW                   a `general` sub-agent, hostile, reads the SAME spec
4. orchestrate_fix          -> the findings, back to the implementer's worktree
   FIX                      a `general` sub-agent, in that worktree
5. orchestrate_merge        -> merge + the whole verify loop, before the commit
```

The plugin's eight tools (namespace `orchestrate`, callable from `execute`).
The signatures are the ones the tool's schema accepts — a call with a
parameter the schema does not know is **rejected**:

- `orchestrate.plan({name, spec | spec_file, base?, project?, sparse?, no_seed?})`
  — creates `cs2-wt\<name>` on `feat/<name>` off the integration tip, seeds it
  (the shared trees linked, the build directories **outside** the worktree),
  writes the spec into it, and returns the **implementer's prompt**.
- `orchestrate.verify({task, groups?, skip?, project?})` — runs the project's
  verify loop **in the task's own worktree** (`task: "integration"` verifies the
  integration tree). The task is named on purpose, and the schema **requires**
  it: a call without one is rejected before the tool runs, so a verify can
  never land on the integration tree by accident.
- `orchestrate.review({commit, task?, base?, spec_file?, focus?, shared?, project?})`
  — points a **per-task** review worktree (`cs2-wt\rev-<task>`, so two reviews
  can run at once) at the commit and returns the **reviewer's prompt**. The
  report goes to a fixed path, so the verdict can be read back before the
  merge.
- `orchestrate.fix({task? | branch?, findings?, report?, project?})` — takes the
  findings of the review back to the **implementer's own worktree** and returns
  the **fixer's prompt**. Without `findings`, the findings are read from the
  task's latest review report. One of `task` / `branch` is needed.
- `orchestrate.merge({branch, message?, task?, skip?, allow_unreviewed?, project?})`
  — merges into `feat/occlusion-penetration`. The parameter is **`skip`**, not
  `skip_tests`. The merge is staged with `--no-ff --no-commit`: the **whole
  verify loop runs against the merged tree first**, and only a tree that passes
  is committed. A conflict or a red verify loop aborts the merge and leaves the
  integration tree exactly as it was. A branch that was **never reviewed**, or
  one whose review said **DO NOT SHIP**, is refused unless
  `allow_unreviewed: true` is passed. A branch that moved **after** its
  review is not refused: the **older** review still counts, and the merge goes
  ahead with a warning that the review is of an older commit.
- `orchestrate.status({project?})` — the task board: every worktree, its
  branch, how far it is ahead and behind, whether it is dirty, merged or
  reviewed, and its disk usage.
- `orchestrate.size({project?, task?})` — what makes the worktrees big and what
  to do about it; one task, or all of them.
- `orchestrate.cleanup({task?, merged_only?, scratch?, apply?, prune_branches?, remove_builds?, force?, project?})`
  — removes the worktrees that are done with: the shared links are unlinked
  first, so nothing can follow a junction into a shared tree. Without
  `apply: true` it only reports, and a worktree with uncommitted changes to a
  tracked file is skipped unless `force: true` is passed. `merged_only: true`
  selects the worktrees whose branch is already merged into the integration
  branch; `scratch: true` adds the detached `rev-*` review worktrees, which
  hold no work.

The index of the tools, and the whole story of the worktree size, is
[`docs/orchestration/README.md`](orchestration/README.md).

### 2.2 Why a **written spec** matters

The reviewer can only check conformance if it reads the same text the implementer
did. Every finding in this wave was either "the spec says X and the code does Y"
or "the code does X and **that breaks**", and the second kind is only findable
against the spec. Keep the spec in the repo, not in a temp file.

### 2.3 The reviewer's contract

`docs/orchestration/reviewer-role.md` is the reviewer's brief, and it is strict
on purpose: find what is wrong; write a **hostile test** and try to break the
change; check the spec, the house style, the second-order effects, and the
**wire/ABI boundary**; report `file:line` + a concrete failure scenario per
finding; **never invent a finding**. The template that fills it in is
`docs/orchestration/review-prompt-template.md`.

**A review that finds nothing is a failed review.** That sentence is in the
brief for a reason: it is what pushes the reviewer past "looks fine to me".

## 3. What the reviews caught (keep this list — it is the argument for the loop)

**Four blockers**, all of which would have shipped:

| | What | How it was found |
|---|---|---|
| 1 | The menu rendered into a **32×32 window** — `SetNextWindowSizeConstraints(min, (0,0))` passes a *real* maximum in ImGui 1.91.5, so the whole menu was invisible and unclickable | a reviewer harness driving the real `draw_menu()` |
| 2 | "Save config" persisted the **defaults** and wiped the live settings | a reviewer test starting from a non-default config |
| 3 | A config "Load" never reached the ESP | the change-detection sequence was swallowed by the write-back |
| 4 | The **triggerbot never fired** in the default input backend — it counted the shot, reported `{"firing": true}`, and the gun stayed silent | reading `Input::apply`'s backend gate |

**Eleven majors**, including: the strafe's side axis **mirrored** (the default
analog path steered the wrong way); the repo's `IN_*` button bits wrong (three
wrong, five naming bits that do not exist in this build, which made the ragebot's
auto-scope half-inert); the `settings` command's clamping never reaching the
movement/ragebot/camera features; the settings **write path** an unsynchronised
441-byte copy against the render thread; a **hung pipe wedging the whole MCP
server** forever; and the advertised button-name enum being a **no-op** (a typo
reached the game as `+spcae`).

**Two more that only a live run found**: both new inline hooks installed
themselves **while the feature was off** (a feature that is off must not patch
the game's code), and the chams hook's **target address was wrong** (see §5).

## 4. The traps this wave hit (each one cost time)

1. **A junction inside a removable worktree.** `git worktree remove --force`
   *follows* a directory junction and **deleted the contents of the target**.
   The vendored `external/Vulkan-Headers` was destroyed that way. The rule is
   unchanged: a junction inside a worktree is only safe if something unlinks it
   before the worktree goes. The plugin now **shares** the headers through a
   junction — zero bytes per worktree — and unlinks it before it removes the
   worktree (`orchestrate.cleanup`); a `git worktree remove --force` by hand is
   still the thing that destroys the shared tree.
2. **Two clang-tidy checks emit code that does not compile** — and the sweep
   *verified* each group, so they were caught before being committed:
   `readability-convert-member-functions-to-static` emits `static [[nodiscard]]`
   (an attribute list may not appear there), and `modernize-use-ranges` emits
   `erase(ranges::remove_if(...))` (a subrange, not an iterator). Both are now
   excluded with the reason in `docs/style-guide.md` §7.
3. **A test built on `assert()` is a no-op in a release build.** Meson's
   `b_ndebug` default is `if-release`, and `scripts/release.sh` configures
   `--buildtype=release` — so every `assert` was compiled out and the suite was
   green while the maths was wrong. The host build now pins `b_ndebug=false`, and
   the new tests use an explicit `expect()` + a failure counter.
4. **A test that runs *after* the thing it checks is vacuous.** The
   `settings-table` test ran after the build had already regenerated the file, so
   it compared the file with itself. It now compares the **checked-in** file, and
   a Python unit test drives the generator against a copy.
5. **The menu tests shared one directory** and raced on the geometry side file;
   Meson runs the suites concurrently, so the whole suite aborted with
   `0xc0000409`. Per-process directories now.
6. **A runtime `isinstance` against a `Protocol` checks attribute presence, not
   signatures.** The `Game` façade did not satisfy `ControlTransport` and the
   mypy gate could not see it. The check is now **static**: a test assigns the
   object to a protocol-typed variable.

## 5. Open items, in the order they should be picked up

### 5.1 The auto-strafe no longer accelerates — **RESOLVED (2026-10-20)**

The operator reports that the movement (the auto-strafer or the bhop) no longer
accelerates. The suspect was this:

> `wire::Settings::movement_strafe_subticks` was changed from **0 to 8**
> (`d6fc5bc`), on the reasoning that 8 is the live-validated slice count
> (+85.1 u/s per hop, `docs/handoff.md`).

**Measured, the suspect is wrong and the proposed fix would have made it worse.**
The live A/B (de_dust2 listen server, the `autostrafe` scenario, everything else
identical) is in [`docs/re/strafing.md`](re/strafing.md) §"The gate":

| arm | `subticks` | `sv_subtick_movement_view_angles` | max speed |
|---|---|---|---|
| the control (the strafe off) | – | 1 | 105.7 u/s |
| the rotation encoding | 8 | **0** | 100.2 u/s |
| the rotation encoding | 8 | **1** | **195.8 u/s** |
| the camera-frame analog | 0 | 0 | 106.4 u/s |
| the camera-frame analog | 0 | 1 | 109.0 u/s |

Three things follow:

1. **The rotation encoding (`subticks = 8`) is the only one that accelerates** —
   and only when the server honours the sub-tick view angles. With the policy off
   the payload still writes every tick (744 writes, 54 of 55 airborne samples
   acting) and the player does not accelerate: the wish is never applied.
2. **The camera-frame analog (`subticks = 0`) accelerates nothing in either
   configuration** — it is the control's own number. Defaulting back to 0 would
   have swapped one inert path for another. **The default stays 8**, and the
   golden settings checksum does not move.
3. **The policy is on by default.** A freshly launched server (the convar never
   touched) gives the working numbers: 107.4 u/s for the control, **184.5 u/s**
   with the strafe on. The earlier "the policy is off on a listen server" note
   describes a server that *something else* had set it to 0 on — the
   `movement-probe` scenario sets it to 0 and only restores it in its `finally`,
   so an interrupted run leaves it off for the rest of the session.

**What landed (T16):**

* the strafe **measures** the gate and reports it: `strafe_steered`,
  `strafe_steered_ticks`, `strafe_ignored` (the counters) and `strafe_gain` (the
  speed gained over the last airtime — the outcome) are in the `input` status and
  on the overlay's **movement** HUD line;
* the `autostrafe` scenario now says it outright: *"the server ignored the
  rotation … sv_subtick_movement_view_angles is off on this server, so the silent
  encoding cannot steer"*;
* the movement scenarios' settings read was **fixed**: `scripts/scenario/settings.py`
  parsed the shared segment's **version 4** layout (a 16-byte block at offset 24)
  while the wire is now **version 13** (441 bytes at offset 28, with a checksum), so
  every call raised *"segment version 13 != 4"* and the scenarios reported the
  features as **off while the payload was acting** (748 strafe writes in a window
  the scenario called "skipped"). The scenarios now read the **live** block
  through the control channel, which cannot go stale.

**What is left for the operator:** if their server ignores the sub-tick view
angles, the silent auto-strafe cannot work there at all — no silent channel can
express a wish relative to the velocity when the rotation is switched off. The
payload now says so instead of doing nothing quietly. The non-silent alternative
(the command's own view angles) would move the eye angles, so it is not a drop-in.

### 5.1b The third-person camera **crashes the game** (reported 2026-10-20)

The operator turned the third-person camera on and the game died. The payload's
log has the evidence, one line before the crash:

```
[cs2-features] third person: the camera update is hooked
[crash] first-chance code=0xc0000005 at 0x1832100dd9c read addr=0xffffffffffffffff
```

So the **inline hook on the camera update is installed and then the first
detour call faults**. The faulting address (`0x1832100dd9c`) is **inside the
payload**, not inside the game, and the access is a **read** of
`0xFFFFFFFFFFFFFFFF`.

**Corrected 2026-10-21: the address is *not* stale.** REA/Ghidra on the live
`client.dll` (sha256 `659940c1…`, PE timestamp `0x6AD01314` — the very build
the payload's gate accepts) shows that `client.dll + 0xB62A20` **is** a valid
function (`FUN_180b62a20`, 3789 bytes) whose prologue and `(rcx, edx)`
calling shape match the payload's `CameraUpdateFn`. So the previous
explanation here ("the address was taken from a build that no longer matches")
was wrong.

The prime suspect is the **`thread_local` re-entrancy guard in the detour**
(`payload/features/src/thirdperson.cpp:39`): the payload is **manually
mapped**, so only the injector's entry thread has a TLS block
(`payload/tls/include/cs2/tls/bootstrap.hpp` says exactly that), and the
game's camera thread is not one of the threads the payload initialized — the
detour's first instruction, the `thread_local` read, faults on an
uninitialized per-thread block. The repository already states the rule
(`docs/orchestration/spec-t20-animfix.md`): *"no `thread_local` in a detour on
the manually mapped image"*.

Until that is fixed, the third-person camera should **stay off** (the console
route still works on any build — the code says so at
`payload/features/src/thirdperson.cpp`). **T27** is the task that fixes it.

### 5.2 The chams: the hook target is wrong — **RE-DERIVED (2026-10-21)**

**The target has been re-derived on the live build; T26 implements it.**

The RTTI chain, read out of the live `scenesystem.dll` (sha256 `040eb851…`,
PE timestamp `0x6ACFCD4F`):

| what | VA | RVA |
|---|---|---|
| the type descriptor (`.?AVCAnimatableSceneObjectDesc@@` at +0x10) | `0x18067C448` | `0x67C448` |
| the complete object locator (this build's RTTI uses **32-bit** RVA fields) | `0x180606DC0` | `0x606DC0` |
| the COL pointer (the vftable's `[-1]` slot — the only one in the module) | `0x1805E5C88` | `0x5E5C88` |
| ⇒ the **vftable** starts at | `0x1805E5C90` | `0x5E5C90` |
| slot 0 | `0x1800541D0` | a `GetTypeName` virtual (returns the class-name string) |
| slot 1 | `0x180059FC0` | the **batch** wrapper (7 arguments, the material is the *fifth*) — **not** the draw |
| slot 2 | `0x18001C240` | a three-byte `ret 0` stub |
| slot 3 | `0x18005B030` | — |

**The real draw is the function the batch wrapper calls** at `0x18005A465`:

> **`scenesystem.dll` RVA `0x580A0`** (VA `0x1800580A0`)

It is a **nine-argument** member function, and its arguments are settled by
its own body: `RCX` = `this`, `RDX` = `IRenderContext*`, `R8` =
`CMeshDrawPrimitive_t*` (it reads the primitive's index at `+0x372A`), **`R9`
= `const IMaterial2*`** (the body calls `[R9 + 0x130]` and `[R9 + 0x4B8]` —
the `IMaterial2` vtable), and the stack: the mode, the draw values, a **byte**
(the deferred flag), the stats and the scene layer.

**That is exactly the signature the payload already has** — the material is
the fourth argument, index 3 counting `this` — so **the payload's detour was
never wrong; only the address was.** The prologue
`4C 89 4C 24 20 48 89 54 24 10 55 53 56 57 41 54 41 55 41 57` is **unique**
in the module and its first 14 bytes hold no RIP-relative operand and no
relative branch, so the repo's inline hook can displace them.

The old note follows for the record.

<details>
<summary>The previous text (the hypothesis that was wrong)</summary>

`payload/features/src/chams.cpp` now **refuses** to install the draw hook, behind
`k_draw_hook_target_verified = false`. The address came from an MSVC RTTI
**lambda** type; a lambda's `operator()` is **static** (eight arguments, no
`this`), while the member function it mirrors takes the `this` plus the same
eight — so the nine-argument detour shifted the whole argument list and the
callee dereferenced the material pointer as a mesh pointer. That is the access
violation (`0xc0000005`) that killed the game.

</details>

**To finish it:** the address is now known; what it needs is the ADR-0012
route (a dumper signature, so it is re-resolved per build), a **runtime
verification** (the prologue bytes are read through the fault-safe reader and
compared before anything is patched) and the removal of the `thread_local`
guard — the chams' detour has the same defect as the third-person's
(§5.1b). **T26** is the task that does it.

Everything else is ready: the material system (`CMaterialSystem2` vtable
`0x12d920`, `FindMaterial` = slot 14), the flat material
`materials/physics/flat_basic.vmat`, the colour setter
(`SetMaterialVectorParam`, vtable slot 23) and the parameter-name hash
(MurmurHash2, case-folded, seed `0x31415926`).

**Also:** the two inline hooks now install only when the feature is **on** and
carry a thread-local re-entrancy guard. Keep that property for every new hook.

### 5.3 The neverlose deep reversal - **DONE** (it ran to completion)

**It is already landed** - four reports, 1373 lines, in `docs/research/`:
`neverlose-renderer.md`, `neverlose-hooks.md`, `neverlose-protection.md` and the
index `neverlose-deep.md`. The headline results:

- **Route A (the renderer)** - the 28 DXBC containers carry a `RDEF` chunk, so
  the constant-buffer layouts are readable **straight out of the bytes**. The
  parser was validated against a reference shader compiled with the OS's own
  `d3dcompiler_47!D3DCompile` and reproduces it exactly. `PolygonConstants` is
  20 fields: `hull_color` @80, `global_alpha` @104, the per-pass opacities
  @160-168, and `points` @176 = 256 x a **48-byte** `HullPoint`
  `{position_radius, parent_growth, appearance}`. **The colour is per-draw; the
  per-hull colour is `HullPoint::appearance`.** The numeric depth state is
  **not** recoverable statically (it lives in the PSO) - the mechanism is
  identified, the value is not.
- **Route B (the hooks)** - the prior session's "zero references" finding was a
  **byte-scan artefact** (a scan cannot tell an opcode from a displacement). A
  forward capstone sweep (13.8M instructions) shows **seven of the eight are
  referenced**, five through one-instruction **tail-jump stubs** whose addresses
  have **no** static reference of any form - so they are runtime-only entry
  points. **Their per-tick handler that drives the auto-strafe is
  `FUN_212c38c66b8`** (stub `0x212c38c65e4`), and there is a **keyed callback
  registry** at `0x1bf01450`.
- **Route C (the protection layer)** - the loader end to end, **all ten**
  version-gated patterns decoded out of the image, and the finding that the
  loader reads its own `SizeOfImage`/`SizeOfHeaders` from **inside the
  encrypted region** - so it **cannot run from the static image** and the key is
  runtime-derived. `.enc1` measures **9.159 bits/byte** with exactly one data
  reference.
- **Route D (the maths) - a useful negative:** their ragebot has **no hit-chance
  model at all** (the weapon spread only picks a smoothing rank), so our
  damage-field solver is strictly more advanced and there is **nothing to steal**
  there. The stealable idea is their 16-step smoothing profile with a
  **deterministic per-target seed**.

**What this changes for us** (the report's own list): the chams become *one*
constant buffer with a per-draw colour and a per-hull `appearance`; the strafe
should assert **one write per user command**; hook targets must be hunted by
**pattern** (a hook entry is a jump **into** the handler); the loader should
fail loudly on an unknown configuration and consider a cheap self-integrity
check; and **do not** build a hardware fingerprint. The caveat that matters:
their feature list is **server-side**, so their binary is a **lower bound** on
what the product does.

### 5.4 The other public artifacts

`docs/research/cheat-dump-inventory.md` is the inventory you asked for: three
public CS2 **source** trees (Weave - the only feature-complete one, Fatality - a
real base with no combat features, Plaguecheat - a 2026 recompiled tree whose
feature set is not yet read), a complete **CS:GO** implementation whose
material-override chams match our plan, three **MIT-licensed** CS2 sources (the
only code we may legally copy), and a queue of binary dumps whose authenticity
ranges from plausible to disputed.

Nothing was downloaded or executed and no artifact is in the repository; every
claim is a description, and the forum rows are marked `snippet-only` because the
sites block automated fetches. The useful negative: **no public artifact
contains a damage-field ragebot**, so our solver is genuinely ahead of
everything public.

### 5.5 The neverlose mining (the first, shallower pass)

The neverlose module is the **most valuable source we have**: a shipped product
with every feature we want and more. The prior session already reversed its
**first-party surface** (12 subsystems, **890 functions**, with a *corrected*
deobfuscation pass — `gproj11`; the earlier `gproj3`/`gproj10` carry 9222 bytes
of possibly-corrupted patches, so prefer the `_v2` corpora). What it did **not**
do is the three hard parts, and the spec for them is
**`docs/orchestration/spec-t17-neverlose-deep.md`**:

| Route | What it buys us |
|---|---|
| **A — the renderer's per-pass constant buffers** | the four DXBC families (Tracer / Impact / Trajectory / **Polygon** — the player model, i.e. their chams). The CB layouts, the hull record (`{position_radius, parent_growth, appearance}`), `hull_color` / `global_alpha` / the per-pass opacities, and **whether the draw is depth-tested** — the one question our chams need. Our chams are *refused* today (`payload/features/src/chams.cpp`, `k_draw_hook_target_verified = false`) because the hook target is wrong. |
| **B — the hook bindings** | seven functions have **zero** static references and are reached only through runtime-registered hooks — including **their movement handler** (`212c37beb50`, the auto-strafe). Knowing *what drives it* is worth more to us than its body. |
| **C — the protection layer** | the reflective loader (the header rebuild, the version-gated ntdll unwind-registration routine, the build table), the **encrypted-at-rest `.enc1`** (16.5 MB, entropy ≥ 8.00, no static decryption loop — so a **runtime dump** is the only way), the integrity and anti-tamper machinery, the HWID / hardware-monitoring collection, and the **server-side** feature delivery (a CJSON feature list — which explains why some of their features are not in the client binary at all). |
| **D — the maths** | their rage cluster (the damage field, the hit chance, the spread model) and their movement command builder, against ours. |

**The scope note in the spec is deliberate:** the *evasion* side (how the module
avoids VAC, and what anti-cheat-evasion measures it performs) is **not** a
deliverable, and nothing from it may become an evasion feature here. What the
task delivers is the **architecture** — how a shipped product loads itself,
protects its own code and delivers features — because that informs our own
loader (`payload/loader/`) and our own integrity story. The plan is to document
what the binary does, not to design a bypass and not to port detection-evading
mechanisms.

**On the box:** `ssh pontes@100.104.244.127`, work in `/var/tmp/rea-work/`.
**Tailscale SSH asks for a one-time browser check** the first time — the prompt
prints a `https://login.tailscale.com/a/…` URL. That is expected, not a fault.

**On other sources.** The neverlose dump is the richest one, and the *legitimate*
adjacent material is: the game's **own** anti-cheat binaries (`steamclient64.dll`
and its VAC modules) and the **public academic RE/malware corpora** — cheat
detection datasets and the anti-cheat research literature. I will not go
acquiring other commercial products' binaries to reverse; where a public,
legitimately-published analysis of a comparable product exists, cite it.

### 5.6 The mining report

`docs/research/neverlose-mining.md` answers the five questions with addresses.
The two open threads:
- the **slot → player** mapping for the sound ESP needs the engine's real player
  slot per controller (the current code uses the *rank* in the snapshot's table,
  which is only right while every lower slot is still connected);
- their **strafe** implementation is worth re-reading against ours now that the
  sign is settled: their wish is `forwardmove·F + leftmove·R` — the held keys are
  the heading and the camera only supplies the frame, like ours — but they have
  **no dead zone and no hysteresis** (their side is the sign of three velocity
  components against zero, with a 0.2 activity gate).

### 5.7 Item 5 - the remaining feature candidates

Ranked in `docs/feature-wave.md` §6. The **triggerbot** is done; the rest:
anti-aim (fake-lag/desync), hit logs + damage indicators, movement QoL
(auto-crouch, ledge auto-jump), world-ESP volumes (smoke radius, molotov fire).

### 5.8 The style sweep

All seven remaining clang-tidy groups are applied (one commit each, with the
verify loop between them). Two checks are excluded with a documented reason
(§4.2). The manual refactors the reviews drove are in.

## 6. The commands

```powershell
# the build (the host build needs no vcvars; the Windows targets do)
meson compile -C build ; meson test -C build
# in a TASK worktree take the project's environment FIRST - it is what keeps
# the cargo target directory and the tool caches out of the worktree:
. C:\Users\gpmpo\cs2-wt\<task>\.orchestration\env.ps1
# ... and the build directories are OUTSIDE the worktree, in
# C:\Users\gpmpo\cs2-build\<task>\<build-name>\ - ONE directory per build
# (build, build-payload, build-overlay, build-injector, cargo), so the task
# directory on its own is NOT a build directory:
meson compile -C C:\Users\gpmpo\cs2-build\t25-handoff\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
# ... and the same for build-overlay / build-injector
# (the bare names above are the INTEGRATION tree's, which builds in-tree; in a
# task worktree they are C:\Users\gpmpo\cs2-build\<task>\<build-name>\)

# the Python side
python -m unittest discover -s scripts\scenario\tests -t scripts   # 409 tests
python scripts\typecheck.py                                        # mypy strict, 0 errors

# the game (the agent runs in session 0: no GPU, so the game must start in
# the interactive session, through the scheduled task)
schtasks /run /tn CS2Launch
build-injector\injector.exe cs2.exe build-payload\payload.dll --load-missing

# the MCP tools (tools.cs2.* in the execute runtime) - the game must be up:
#   cs2_launch({confirm:true}) / cs2_inject({confirm:true}) / cs2_ping()
#   cs2_run_scenario({name:"bot-match", args:{map_name:"de_dust2", per_team:5}})
#   cs2_screen() / cs2_state() / cs2_settings_get() / cs2_settings_set()
```

A **bot match** is one call: `cs2_run_scenario({name: "bot-match", args: {map_name:
"de_dust2", per_team: 5}})` — it loads the map, applies the playground convars
(no warmup, no freeze, infinite ammo, $60 000, respawn on death, `bot_stop 1` so
the bots hold still), joins a team and adds the bots.

## 7. The state of the tree

- The integration branch is **`feat/occlusion-penetration`**, and it is **clean**.
- The worktrees are still on disk: **11 task worktrees and one `rev-*` review
  worktree under `cs2-wt\`** (the board counts 12 task worktrees, 3.1 GB — the
  twelfth, `cs2-modern`, sits outside `cs2-wt\`; measured 2026-10-21, and
  `orchestrate.status({project: "cs2"})` is the live number). The repository
  has **16 local branches**, of which **5 task branches are merged** into the
  integration branch: `feat/re-angle-split`, `feat/re-animfix`,
  `feat/research-angle-split`, `feat/research-animfix` and
  `feat/t19-protect-call-encryption` (merged on 2026-10-21). The rest —
  `feat/t18-neverlose-strafe`, `feat/t18b-neverlose-camera-split`,
  `feat/t20-animfix`, `feat/t21-quality-pass`, `feat/t25-handoff` and
  `research/server-client-discrepancies` — hold **unmerged work** and are not
  candidates for removal.
- To get the space back, run the cleanup **in report mode first** and read what
  it selects, then apply it:

  ```text
  orchestrate.cleanup({project: "cs2", merged_only: true, scratch: true})
  # -> 6 worktrees, 949 MB would be reclaimed   (2026-10-21)
  orchestrate.cleanup({project: "cs2", merged_only: true, scratch: true,
                        apply: true, prune_branches: true,
                        remove_builds: true, force: true})
  ```

  What that does, and what it does **not** do:

  - `merged_only: true` selects the worktrees whose branch is **already an
    ancestor of the integration branch**. A detached `rev-*` review worktree
    has no branch, so it is never "merged" — `scratch: true` is what selects
    the review worktrees, and they hold no work (a review produces a report,
    not a commit).
  - `force: true` removes a worktree **even when a tracked file has
    uncommitted changes** — it **throws that work away**. Without it the
    cleanup *skips* every dirty worktree and removes nothing at all. Of the
    six the cleanup selects, only the four research worktrees are dirty, and
    only in their spec file (a line-ending difference — the diff is empty),
    so nothing is lost there; read the report before you apply it.
  - `prune_branches: true` deletes a branch only when it is merged
    (`git branch -d`), so an unmerged branch is never deleted.
  - The cleanup unlinks the shared links before it removes a worktree.
    **`git worktree remove --force` by hand still destroys the shared tree**
    if the worktree still has a junction inside it — see §4.1. Do not fall
    back to it when the cleanup skips something.
- The host suite is **45/45**, the Python suite **409**, mypy **0 errors**
  (measured 2026-10-21 on `feat/occlusion-penetration`).
- The payload, the overlay and the injector all build.

## 8. The one thing to do first

**§5.1 is done** — the strafe's gate is measured, the default is right, and the
payload reports the outcome and the gate (§5.1).

**The order for the next session (2026-10-21):**

1. **T26 — the chams' draw hook** (§5.2): the target is **re-derived**; the
   task is the ADR-0012 route (a dumper signature), a runtime prologue
   verification, and the removal of the `thread_local` guard.
2. **T27 — the third-person camera's crash** (§5.1b): the address is **valid**;
   the `thread_local` in the detour is the fault. The chams' detour has the
   same defect.
3. **T28 — the curated offsets, resolved offline**: the dumper's `--file` mode
   already resolves the same signatures **without the game** (it reproduces
   the shipped dump exactly), so a new signature becomes usable without the
   operator launching CS2.
4. **The live run** — the one thing no static review closes. The operator has
   to launch the game (`schtasks /run /tn CS2Launch`) and inject **once**.

**The protection workstream is out of scope** (the operator's decision,
2026-10-21): `docs/handoff-protect.md`'s T20 (the virtualization) is **not**
a task for this repository. The T18/T19 work that already landed stays.

**`cpp-modernization` is not a merge candidate** (2026-10-21): it is C++23
(the same dialect as the integration tree, so it is not "cpp2"), but it is
**239 commits behind**, its **ADR numbering collides** with the integration
tree's (its ADR-0023 "Enumerated domain types" against the integration
tree's ADR-0023 "Default renderer"), and it rewrites the same ~90 files the
integration tree has since rewritten. Keep the branch as a **source of ideas**
(enumerated domain types, `std::expected` hook errors, the concept-constrained
`Vec`/geometry, `magic_enum`, the typed SDK accessors); merging it is its
own project.
