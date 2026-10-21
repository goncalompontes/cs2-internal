# RE — the angle channels, re-derived for the anti-aim

## Why this task exists

The split between the **camera** angles and the **player / eye** angles is the
capability the **anti-aim** will be built on. The anti-aim has to set the
player's angles (the fake/desync yaw, the pitch, the jitter) **without**
moving the camera and **without** disturbing the movement's controls.

The T18b pass re-reversed the split on the live build and left a report:
`docs/re/camera-split.md` (it lives only on the branch
`feat/t18b-neverlose-camera-split` — read it with
`git show feat/t18b-neverlose-camera-split:docs/re/camera-split.md`).

**That report was derived for the auto-strafe.** This task re-derives the same
pipeline **for the anti-aim**, which needs more of it:

* the anti-aim must set the player's angles to something **arbitrary**, and
  often *differently from the camera by a large angle* (the desync is up to
  180°). The T18b report proved the camera reads the **local view angles**
  (`CCSGOInput + 0x688`), not the pawn's `m_angEyeAngles` — so writing the
  pawn's eye angles is the *animation/hitbox* channel and writing the view
  angles is the *camera* channel. **Which one does the server see?** That is
  the central open question of this task.
* the anti-aim must survive the engine's own per-frame writes — the engine
  copies the command's angles into the view angles, and the view angles into
  the pawn's eye angles, several times a frame.
* the anti-aim must not disturb the **movement**: the movement reads the
  **command's own** view angles (`CBaseUserCmdPB::viewangles`, base `+0x40`).
  If the anti-aim writes the command's angles, the movement's basis moves with
  it — that is the whole point of the desync, but the *player-held* controls
  must still work.

## What you are

An **RE** agent. You do not write the feature and you do not write the spec.
You reverse the engine and you write the report the implementer will work
from.

## Your worktree

**`C:\Users\gpmpo\cs2-wt\re-angle-split`**, branch `feat/re-angle-split`.
Do all your work there.

**Never** edit `C:\Users\gpmpo\cs2-internal` (the integration tree) or another
agent's worktree. **Never** `git reset --hard`, `git checkout -- .`,
`git clean` or `git stash`. Never amend or rebase.

## The target

The **live** `client.dll`:

```
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll
sha256 659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0
```

Every address you report must be an **RVA** (the module's base is
`0x180000000`; `client.dll` → `0x180000000`, `engine2.dll` → `0x180000000`).
Report the RVA and say which module it is in.

**Use the integrated REA MCP tools** (`tools.rea.*`, called from the Code Mode
`execute` runtime — `execute` with
`await tools.rea.open_binary({...})`, `tools.rea.procedure_pseudo_code({...})`,
`tools.rea.search_strings({...})`, `tools.rea.xrefs({...})`, …). **Do not**
shell out to the `rea` CLI and do not drive the MCP server by hand — the
integrated tools reuse the bound session and the warm Ghidra project cache.
Read `docs/re/README.md` (§1–§9) for the loop: string → xref → decompile,
the vtable/RTTI recipe, the gotchas, the build-delta warning, and the
"warm analysis is automatic" note.

The Ghidra project for this exact build is **warm** — opening it takes
seconds. The build-delta warning in `docs/re/README.md` matters: most of the
absolute VAs in `client-dll.md`, `aim.md`, `engine2.md` and `vphysics2.md` are
**stale** (they are from build `25815307`). `occlusion.md` and
`strafing.md`/`subtick.md` are the ones that use RVAs and are roughly current.

## What to reverse

### 1. The angle channels, end to end

Confirm or correct each row of the T18b report on **this** build, and give
the **RVA** for each:

| what | T18b's finding | what to establish |
|---|---|---|
| the local view angles | `CCSGOInput + 0x688` (per-slot block, stride `0x928`) | the offset, the stride, and the **slot 0** base |
| the per-slot angle **history** | `input + 0xb50 / +0xb58` | the offset, the layout, and **when** the engine reads the history instead of the field |
| the view-angle **setter** | `FUN_180b80e70(input, slot, angles)` | the RVA, the signature, and **who calls it** (the callers are the interesting part — the anti-aim needs to know every path that can write the camera) |
| the per-tick **copy** | `FUN_180b07590(pawn, flags)` | the RVA, what it writes, and where in the frame it runs |
| the pawn's `EyeAngles()` | `FUN_180c74ec0` (vtable `+0x570`) | the RVA, the vtable slot, and the branch between the local and the remote path |
| the local eye-angle **source** | `FUN_18093c4f0` | the RVA and what it reads |
| the eye-angle **copy** | `FUN_1808cb9b0` | the RVA and what it stores |
| the camera **CalcView** | `FUN_180a64570(services, origin, angles, …)` | the RVA, the signature, and the callers |

### 2. The question that decides the whole feature

**Which angle source does the server see for the local player, and which does
the camera see?**

Three candidates, and the answer is probably "two of them":

* the **command's own** view angles (`CBaseUserCmdPB::viewangles`, base
  `+0x40`) — the server reads these from the command it receives;
* the **silent** channel — the movement services' **input history** of view
  angles (`subtick.md`), which the server honours only when
  `sv_subtick_movement_view_angles` is on;
* the **pawn's** `m_angEyeAngles` (`+0x3600`) — the *client's* copy of the
  animation aim; the server has its own.

Establish, with evidence:

1. What the server's **own** pawn does with the angles it receives — find the
   **server-side** code path if you can (the `server.dll` is on disk at
   `C:\Users\gpmpo\ghidra-analysis\server.dll`; it is a **different build**,
   so treat its addresses as indicative only and say so).
2. Whether the **silent** channel and the **command's** channel can carry
   *different* angles in the same command (the desync needs exactly that),
   and what the admission rules are (the `viewangles` history's own
   constraints — the per-command array length, the per-entry delta limit, the
   `when` ordering).
3. Whether there is a **third** channel — an angle field that the client
   sends that the server uses for the **player's body yaw** rather than for
   the **shot direction**. This is the question that decides whether an
   anti-aim can desync the body without desyncing the shot.

### 3. The anti-aim's write points

For each candidate write point, the report must answer:

* **when** in the frame does it run (before/after the command is built,
  before/after the movement services consume it, before/after the camera's
  `CalcView`, before/after the animation);
* **who else writes it in the same frame** (so the payload can win the race
  deterministically);
* **is it re-entrant-safe** to hook (the relocator's limits: a 12-byte
  prologue is the practical ceiling; `FUN_180b80e70`'s short branch and
  `FUN_180b07590`'s 18-byte prologue are refused by the repo's relocator —
  confirm and look for an alternative call site);
* what the **hook signature** is (the `__fastcall` shape, the argument count,
  the register mapping) — the payload's inline hook needs it.

### 4. The animation/hitbox link

The split's *purpose* is that the pawn's eye angles are what the **animation**
(and therefore the hitboxes) uses. Establish:

* the exact chain from the eye angles to the **bone transforms**
  (`docs/re/aim.md` has the bone array and the `CTransform` layout): which
  function consumes the eye angles to produce the body yaw, and where the
  **aim yaw** / **body yaw** layers are set;
* whether the local player's animation is computed **locally** (client-side
  prediction of the animation) or copied from the server — this decides
  whether the "animfix" (the sibling task) is even needed for the local
  player.

## Rules

- The repo's evidence standard: **every address is re-derived on this build**,
  with the string/xref/decompile path that found it recorded in the report, so
  the next agent can reproduce it. A row without a derivation is a row without
  evidence.
- **Say plainly what you could not establish.** A negative result
  ("the server's own body-yaw path is not reachable from the client build")
  is a valuable result — write it down rather than guessing.
- Do not invent an address. If you cannot confirm it, mark it
  `unverified` and give what you have.
- Never edit the payload's source. This task produces **a document**, not code.

## The deliverable

**One file: `docs/re/angle-split.md`**, committed on your branch with the
message `docs(re): the angle channels, re-derived for the anti-aim`.

It must contain, in this order:

1. **TL;DR** — which channel the server sees, which the camera sees, which the
   animation sees, and the one sentence that decides the feature.
2. **The frame timeline** — an ordered table: the tick's stages, the function
   that runs in each, its RVA, and what it writes.
3. **The channels** — the table of §1 above, with RVAs and the derivation
   path.
4. **The server's view** — the answer to §2, with the evidence.
5. **The write points for the anti-aim** — the table of §3 above, with the
   hook signatures.
6. **The animation link** — the chain of §4.
7. **What the implementer must do** — a numbered list of concrete steps, each
   with the field/RVA it uses, and the risk it carries.
8. **What is still unknown** — the open questions, each with the concrete
   experiment that would settle it (a live measurement, a further reversal).

Also update `docs/re/README.md`'s index with one row for your file (read the
file first; match its format exactly).

## The report you hand back

The answer to "which channel does the server see" in one sentence, the
write-point table, the two things you could not establish, and the file list
you touched.
