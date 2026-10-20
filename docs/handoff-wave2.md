# Handoff — session 2 of the feature wave (2026-10-21)

> **Start here.** This is the entry point for the next session. It says where
> everything is, what is done, what is open, and how to run the same
> sub-agent + adversarial-review orchestration this wave used.
>
> The one-line version: the wave landed (121 commits, 42 Meson tests, 389 Python
> tests, mypy strict clean); the **reviews** caught four blockers and eleven
> majors that would otherwise have shipped; the **chams** are refused because
> their hook target is wrong; and **the auto-strafe no longer accelerates** —
> there is a specific, testable suspect for that on this page.

---

## 1. Where everything is

| What | Where |
|---|---|
| The repo | `C:\Users\gpmpo\cs2-internal`, branch `feat/occlusion-penetration` |
| The task worktrees | `C:\Users\gpmpo\cs2-wt\<name>` — one per agent, each on its own branch, each with its own `build/` |
| The neverlose artifacts | `C:\Users\gpmpo\nea\rea-work\` — the decompilation, the Ghidra project (`gproj3`), and the reconstructed PE (`neverlose_seeded3.exe`, sha `21efa969…`) |
| The Linux box (the neverlose reversal) | `ssh pontes@100.104.244.127` (Tailscale SSH — it asks for a **one-time browser check** the first time) |
| The game | Steam, `F:\SteamLibrary\…\Counter-Strike Global Offensive` |
| The OpenCode config | `C:\Users\gpmpo\.config\opencode\opencode.jsonc` — the `rea` and `cs2` MCP servers |
| The orchestration plugin | `C:\Users\gpmpo\.config\opencode\plugins\orchestration.ts` (this session's addition) |
| The task specs (all of them) | `docs/orchestration/` in the repo — **not** the temp dir, which is wiped between sessions |

## 2. The orchestration playbook (this is the part to copy)

The wave ran **task → implementer sub-agent → adversarial reviewer sub-agent →
fix round → merge**, in **git worktrees** (never in a shared tree), with a
**written spec** both agents read. The plugin now does the mechanical parts.

### 2.1 The loop

```
1. WRITE THE SPEC            docs/orchestration/spec-<task>.md
2. orchestrate_plan         -> worktree + the implementer's prompt
3. IMPLEMENT                 a `general` sub-agent, in its own worktree
4. orchestrate_review       -> the review worktree + the reviewer's prompt
5. REVIEW                   a `general` sub-agent, hostile, reads the SAME spec
6. FIX                      a `general` sub-agent, in the implementer's worktree
7. orchestrate_merge        -> merge + the whole verify loop
```

The plugin's tools (namespace `orchestrate`, callable from `execute`):

- `orchestrate.plan({name, spec, base?})` — creates `cs2-wt\<name>` on
  `feat/<name>` off the integration tip, copies the vendored Vulkan headers in,
  writes the spec into the worktree, and returns the **implementer's prompt**.
- `orchestrate.review({commit, spec_file?, focus?})` — points the shared `cs2-wt\rev`
  worktree at the commit and returns the **reviewer's prompt**.
- `orchestrate.merge({branch, message?, skip_tests?})` — merges into
  `feat/occlusion-penetration` and runs the full verify loop; on a conflict it
  reports and stops rather than committing something broken.

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
   The vendored `external/Vulkan-Headers` was destroyed that way. **Copy the
   headers into each worktree; never junction or symlink them.** The plugin does
   the copy.
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

### 5.1 The auto-strafe no longer accelerates — **the suspect is known**

The operator reports that the movement (the auto-strafer or the bhop) no longer
accelerates. **The most likely cause is a default this wave changed**, not a
regression in the strafe's maths:

> `wire::Settings::movement_strafe_subticks` was changed from **0 to 8**
> (`d6fc5bc`), on the reasoning that 8 is the live-validated slice count
> (+85.1 u/s per hop, `docs/handoff.md`).

But **0 is the camera-frame analog path** and **8 is the sub-tick `yaw_delta`
rotation path**, and the RE says the rotation path is **gated** by
`sv_subtick_movement_view_angles`: *"the rotation ran but **did not move the
velocity**, which is what landmine 11 predicts when the policy is off"*
(`docs/re/strafing.md`, §"The two biggest suspects"). On a listen server that
policy was **off**. So the default flip moved the strafe from the path that
works to the path that is gated off — and it would silently stop accelerating.

**How to settle it (do this first, it is cheap):**

```powershell
# with the game in a bot match, from the repo root:
python scripts/run-scenario.py autostrafe --set watch=6
```

The `autostrafe` scenario observes the menu-owned strafe and reports the gain and
the eye drift. Run it with `movement_strafe_subticks` at **0** and at **8** and
compare. Also try `sv_subtick_movement_view_angles 1` on the server.

**If the analog path is the one that gains**, the fix is to default
`movement_strafe_subticks` back to **0** (and keep 8 as an opt-in), or to make
the strafe **fall back** to the analog path when the rotation has demonstrably not
steered the velocity. Both are one-line changes in
`overlay/include/cs2/overlay/wire.hpp` **plus** the matching default in
`payload/features/include/cs2/features/esp.hpp` — and the golden settings
checksum must be **recomputed** (the `static_assert` fires in all three builds
and prints the new value).

> The sign fix (T13) also changed the analog write — from `-sin(rel)` to
> `+sin(rel)`, and the button bit from `IN_MOVERIGHT` to `IN_MOVELEFT`. That is
> **correct** (confirmed live: holding A moves the player +20.1 units along the
> engine's LEFT vector, `docs/re/strafe-sign.md` §5.0), and an air-strafe gains
> speed in either direction — so it should not be the cause. Check it anyway by
> running the same scenario with the pre-T13 binary, but the `subticks` default
> is the stronger suspect.

### 5.2 The chams: the hook target is wrong

`payload/features/src/chams.cpp` now **refuses** to install the draw hook, behind
`k_draw_hook_target_verified = false`. The address came from an MSVC RTTI
**lambda** type; a lambda's `operator()` is **static** (eight arguments, no
`this`), while the member function it mirrors takes the `this` plus the same
eight — so the nine-argument detour shifted the whole argument list and the
callee dereferenced the material pointer as a mesh pointer. That is the access
violation (`0xc0000005`) that killed the game.

**To finish it:** re-derive the target from the class's **own vtable** —
`CAnimatableSceneObjectDesc` is at `scenesystem.dll + 0x5E5C90` (RTTI TypeDescriptor
`0x18067C448` → COL `0x180606DC0` → the vtable at `+0x5E5C88`), and its **slot 1**
holds the virtual that calls the draw. Hook the **vtable slot** (a pointer you
can *verify* — it must equal the address you expect) rather than a raw function
address, and derive the signature from the class, not from the lambda.

Everything else is ready: the material system (`CMaterialSystem2` vtable
`0x12d920`, `FindMaterial` = slot 14), the flat material
`materials/physics/flat_basic.vmat`, the colour setter
(`SetMaterialVectorParam`, vtable slot 23) and the parameter-name hash
(MurmurHash2, case-folded, seed `0x31415926`).

**Also:** the two inline hooks now install only when the feature is **on** and
carry a thread-local re-entrancy guard. Keep that property for every new hook.

### 5.3 The neverlose mining — the report is written, the follow-up is not

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

### 5.4 Item 5 — the remaining feature candidates

Ranked in `docs/feature-wave.md` §6. The **triggerbot** is done; the rest:
anti-aim (fake-lag/desync), hit logs + damage indicators, movement QoL
(auto-crouch, ledge auto-jump), world-ESP volumes (smoke radius, molotov fire).

### 5.5 The style sweep

All seven remaining clang-tidy groups are applied (one commit each, with the
verify loop between them). Two checks are excluded with a documented reason
(§4.2). The manual refactors the reviews drove are in.

## 6. The commands

```powershell
# the build (the host build needs no vcvars; the Windows targets do)
meson compile -C build ; meson test -C build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload"
# ... and the same for build-overlay / build-injector

# the Python side
python -m unittest discover -s scripts\scenario\tests -t scripts   # 389 tests
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
- **27 merged task branches** and **15 worktrees** are still on disk under
  `cs2-wt\`. They are all merged; prune them with
  `git worktree remove <path>` (and `git branch -d <branch>`) when you want the
  space back. **Do not `worktree remove --force` one that still has a junction
  inside it** — see §4.1.
- The host suite is **42/42**, the Python suite **389**, mypy **0 errors**.
- The payload, the overlay and the injector all build.

## 8. The one thing to do first

**Run the `autostrafe` scenario and settle whether the strafe still accelerates**
(§5.1). It is a two-minute measurement, it has a strong suspect, and the fix is
one line in each of two files plus a checksum recomputation. Everything else in
this handoff is either done or a bigger project.
