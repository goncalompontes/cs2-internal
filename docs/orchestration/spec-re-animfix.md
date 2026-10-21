# RE — the server's animation pipeline and the local animfix

## Why this task exists

The repository wants to **replicate the server's animation locally**:

> reverse how the server applies animations and other calculations and then
> apply them to our own player (and eventually other players, to get accurate
> local player hitboxes).

The community calls this **animfix** (and, for the local player only, **local
animfix**). The **research** sibling task (`docs/research/animfix.md`, being
written concurrently) establishes what the community means by the term and
what it has already established as fact.

**This task is the engine half**: find the functions, the fields and the
order, so the implementer can reproduce the server's animation on the client.

The scope, concretely:

1. **The animation pipeline** — the ordered chain of functions the engine
   runs to turn a player's state (velocity, angles, flags, the animation
   layers) into a **pose**, and where the **hitbox bones** come out of it.
2. **What the client and the server disagree about** — the specific
   calculations the server runs that the client does not (or runs
   differently), because those are the ones that move the hitboxes.
3. **The hook points** — where a client-side re-computation would have to
   intercept, with signatures, so the payload can force the local player's
   animation into the server's state.

## What you are

An **RE** agent. You do not write the feature and you do not write the spec.
You reverse the engine and you write the report the implementer will work
from.

## Your worktree

**`C:\Users\gpmpo\cs2-wt\re-animfix`**, branch `feat/re-animfix`.
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

Every address is an **RVA** (the module base is `0x180000000`; `client.dll`
→ `0x180000000`, `engine2.dll` → `0x180000000`). Say which module each
address is in.

A **server** build is on disk at `C:\Users\gpmpo\ghidra-analysis\server.dll`
(34 628 248 B). It is a **different build** from the live client, so its
addresses are indicative only — but it is the **only** way to see the
server's own animation code. Use it, label every finding from it as
`server-build (indicative)`.

**Use the integrated REA MCP tools** (`tools.rea.*` from the Code Mode
`execute` runtime). **Do not** shell out to the `rea` CLI and do not drive the
MCP server by hand. Read `docs/re/README.md` (§1–§9) for the loop, the
vtable/RTTI recipe, the gotchas, and the **build-delta warning**: most
absolute VAs in `docs/re/client-dll.md`, `aim.md`, `engine2.md` and
`vphysics2.md` are **stale** (build `25815307`).

## What to reverse

### 1. The animation pipeline (client side)

Find the chain that produces the **pose**:

* the **animation layers** — the array the engine blends (`m_animLayers` /
  `CAnimationLayer`); the layer count, the stride, the per-layer blend
  weights, the **sequence** and the **cycle** fields;
* the **animation state** — `CAnimState` (the velocity-derived state:
  the speed, the flags, the `m_flSpeed`, the "in-air"/"ducking"/"running"
  bits, the **aim yaw** / **body yaw** accumulators);
* the **animation time** — how the animation's clock advances
  (`m_flAnimTime`, the global `anim_time`/`curtime` the layers are keyed on);
* the **graph evaluation** — the function that evaluates the animation graph
  into the bone transforms (`C_BaseAnimating::SetupBones` /
  `ComputeAnimState` / the `AnimGraph` evaluation), and the **bones** it
  writes;
* the **bone array** — already known: `CModelState::m_pBoneArray` at
  `+0x80` (`CSkeletonInstance + 0x1C0`), stride `0x20`, the `CTransform`
  layout `{Vector m_vPosition; float m_flScale; Quaternion m_xyzw}` with the
  rotation at **`+0x10`** (`docs/re/aim.md`). **Confirm on this build** and
  find the *writer* of those bones.

For each step: the **RVA**, the signature, the caller, and the field it
reads and writes.

### 2. The hitbox link

`docs/re/aim.md` has the hitbox model (the studio hitbox set, the capsule
formula, the bone↔hitbox remap). Establish:

* which function takes the **animated bones** and produces the **hitbox
  transforms** the trace uses (the server has the same chain; the client's
  copy is what the ESP draws);
* whether the client's hitbox transforms come from the **same** bone array
  the animation just wrote, or from a separate cache;
* where the **local player's** hitboxes are cached on the client, so an
  animfix would have to invalidate them.

### 3. The client's/server's divergence

The payload's own state reader (`payload/game/src/state_reader.cpp`) already
reads a set of the animation-related fields — find them, and establish whether
each is a **server-sent** value (networked) or a **client-computed** one:

* the networked ones the server sends: the player's velocity, the eye angles,
  the flags, the animation state fields the server replicates;
* the client-computed ones: the layers, the blend weights, the graph
  evaluation, the bone cache.

The **divergence** is the set of fields in the second group that the server
computes **differently** (or not at all). That is the animfix's target list.

### 4. The hook points

For the implementation to work, the payload needs:

* a **post-animation** hook — a function that runs after the local player's
  bones are written, where the payload can overwrite the affected bones (or
  the eye/body yaw the animation used);
* an **invalidation** hook — where the hitbox cache is refreshed, so an
  overwritten pose is picked up;
* the **server-side** counterpart of each — from `server.dll`, so we know
  what to reproduce.

For each: the RVA, the signature, the **relocatability** of the prologue (the
repo's inline relocator needs a 12-byte, branch-free prologue; the T18b
report records that a short branch or an 18-byte prologue is refused — say
which of these functions are hookable), and the hook shape.

## Rules

- The repo's evidence standard: **every address is re-derived on this build**,
  with the string/xref/decompile path recorded, so the next agent can
  reproduce it. A row without a derivation is a row without evidence.
- **Say plainly what you could not establish.** A negative result is a
  valuable result — write it down rather than guessing.
- Do not invent an address. If you cannot confirm it, mark it `unverified`.
- Never edit the payload's source. This task produces **a document**, not code.

## The deliverable

**One file: `docs/re/animfix.md`**, committed on your branch with the message
`docs(re): the animation pipeline and the local animfix`.

It must contain, in this order:

1. **TL;DR** — the pipeline in five lines, and the one sentence that says
   whether a local animfix is feasible on this build.
2. **The pipeline** — the ordered table of §1: the stage, the function's RVA,
   the signature, the fields read, the fields written.
3. **The fields** — a table of every animation-related field you found:
   the name, the class, the offset, whether it is networked or computed, and
   the evidence.
4. **The hitbox link** — the chain of §2, and the answer to "are the client's
   hitbox transforms the animated bones?".
5. **The divergence** — the target list of §3, ordered by how much it moves
   the hitboxes.
6. **The hook points** — the table of §4 with the hook signatures and the
   relocatability verdict.
7. **What the implementer must do** — a numbered list of concrete steps, each
   with the field/RVA it uses and the risk it carries.
8. **What is still unknown** — the open questions, each with the concrete
   experiment that would settle it.

Also update `docs/re/README.md`'s index with one row for your file (read the
file first; match its format exactly).

## The report you hand back

The pipeline in five lines, the answer to "can a local animfix work on this
build", the hook-point table, the two things you could not establish, and the
file list you touched.
