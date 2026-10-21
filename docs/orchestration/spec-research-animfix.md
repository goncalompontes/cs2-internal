# Research — animfix and local animfix

## Why this task exists

The repository wants to **replicate the server's animation** locally:

> reverse how the server applies animations and other calculations and then
> apply them to our own player (and eventually other players, to get accurate
> local player hitboxes).

In the cheat community this is called **animfix** (and, when it is applied to
the local player only, **local animfix**). The idea: the **client** and the
**server** each compute an animation for a player, and they can disagree. The
server's animation is the authoritative one — it is what the hitboxes are
built from and what the other players see. If the client shows a *different*
animation, then the local player's own hitboxes (the ones the client draws
for ESP, and the ones a local triggerbot or a local aim solution reasons
about) are **wrong**.

So an animfix is a **client-side re-computation of the server's animation**:
the client runs the server's animation model itself and forces the local
model into the state the server would have produced.

Before we reverse the engine's animation pipeline, we need to know what the
community has already established:

* what "animfix" means in each of its uses (there are several, and they are
  **not** the same thing);
* which parts of the animation the server computes and the client does not
  (the "server-only" calculations), and which of those matter for hitboxes;
* what the client already does right, so we do not redo it;
* how the "local animfix" variant differs (the local player only) and why one
  would want it.

## What you are

A **research** agent. You do not write C++ and you do not reverse anything.
You read the web, you read this repository's own research notes, and you
write one document.

## Your worktree

**`C:\Users\gpmpo\cs2-wt\research-animfix`**, branch `feat/research-animfix`.
Do all your work there.

**Never** edit `C:\Users\gpmpo\cs2-internal` (the integration tree) or another
agent's worktree. **Never** `git reset --hard`, `git checkout -- .`,
`git clean` or `git stash`. Never amend or rebase.

## What to read first (in this repository)

| File | What it gives you |
|---|---|
| `docs/re/aim.md` | the **hitbox and bone** model: the studio hitbox set, the bone array, the `CTransform` layout, the bone↔hitbox remap, the world capsule formula. This is the *output* the animation feeds, so it is what "accurate hitboxes" has to mean. |
| `docs/re/lagcomp.md` | the server's **lag compensation** and the interpolation history — the other half of "what the server sees" |
| `docs/re/prediction.md` | the client's prediction of the movement; the "client predicts, server decides" boundary |
| `docs/re/subtick.md` | the sub-tick model: the command's angles, the input history, the per-tick timeline |
| `docs/re/client-dll.md` | the entity system, the local player, the view matrix |
| `docs/research/gamesense-csgo-re.md` | the CS:GO Gamesense's surface: what it exposes about animation and the resolver |
| `docs/research/fatality-csgo-algorithms.md` | the CS:GO Fatality's setup-bones calls and the animation handling |
| `docs/research/weave-cs2-ragebot.md` | the public CS2 tree's lag-comp / animation handling |
| `docs/research/README.md` | the index of the other research documents |

## What to research on the web

Use the `websearch` and `webfetch` tools.

1. **UnknownCheats** — the threads on:
   * "**animfix**" (the CS:GO term of art) — the original meaning, the
     server-side vs client-side versions, the "**local animfix**" variant;
   * **CS2's** animation model — `C_BaseAnimating`, the animation layers
     (`m_animLayers`), the animation state (`m_animState`, `CAnimState`),
     the `m_flAnimTime` / the `AnimTime` accumulation, the velocity-driven
     lean and the "aim yaw" layers;
   * what the **server** simulates that the **client** does not, and the
     resulting client/server **animation desync** (the classic symptom: the
     local model lags, rotates the wrong way, or the lean is wrong);
   * the **server's** hitbox construction from the animated bones, and the
     "the client draws the hitboxes from a different bone state" problem;
   * the "**animfix** for the local player**" idea — reproducing the server's
     animation locally so the local hitboxes are right;
   * the CS2-specific rework (the 2023 animation rework, the new
     `CS2AnimGraph` / animation layer behaviour, the "animstate" changes).
2. **Public source trees** — Weave (CS2), Fatality (CS:GO/CS2 base),
   Gamesense (CS:GO) and any other tree that implements an animfix or a
   server-animation replication. Look specifically for:
   * `setup_bones` / `SetupBones` calls and their gating;
   * the **animation state** structures the tree reads;
   * whether the tree forces the local player's animation anywhere;
   * the "**bypass**"/"**no-bypass**" and "**anim**" configuration knobs.
3. **Valve's public material** — the CS2 schema/netvar dumps for
   `C_BaseAnimating`, `CAnimState`, `C_CSPlayerPawn`'s animation fields, and
   the `netvars` for the animation layers; the UC threads that document them.

### What to extract, for each source

* the **exact definition** of animfix the source uses (there are several —
  do not merge them into one word; keep them apart);
* the **list of the server's animation calculations** that the client must
  replicate, in the order the server applies them;
* the **fields** involved (the names at least — the offsets we will reverse
  ourselves);
* the **observable symptom** each one fixes — this is what we will use to
  validate our implementation;
* the **boundary**: which of them are *not* needed for hitboxes (the visual
  layers) and which are (the pose that moves the hitbox bones).

## Rules of the report

* **Every claim carries its source** — a URL, the thread's title, and (when
  it is a public source tree) the file and line. A claim with no source is
  not a claim; mark it `[inference]` and say why.
* **Distinguish** what you *read* from what you *inferred*.
* **Do not invent a quote.** If a forum site blocks automated fetching, say
  so and mark the row `snippet-only` (this repository already uses that
  convention).
* **Do not download or execute** a third-party binary.
* The scope is **technique**, not evasion. Nothing about anti-cheat evasion,
  detection avoidance or VAC belongs in the report.

## The deliverable

**One file: `docs/research/animfix.md`**, committed on your branch with the
message `docs(research): animfix and local animfix`.

It must contain, in this order:

1. **TL;DR** — what animfix is, in the several senses, and which one this
   repository will build.
2. **The vocabulary** — a table of every term the community uses for this
   (`animfix`, `local animfix`, `server animfix`, `bypass`, `animstate`,
   `anim layers`, …), each with the definition the source gives it.
3. **The server's animation pipeline** — the ordered list of the
   calculations the server applies, and for each: what it computes, which
   client-side field it corresponds to, and whether it moves the hitboxes.
4. **The divergence** — what the client does differently, the symptom, and
   the community's evidence for it.
5. **The public implementations** — one subsection each, with the sources.
6. **What we must reverse** — the concrete list of the engine's functions and
   fields the reverser must find, written so that an RE agent can go straight
   at them. This is the handover to the sibling task.
7. **Open questions** — what the research could **not** settle.

Also update `docs/research/README.md`'s index table with one row for your
file (read the file first; match its format exactly).

## Rules

- Keep it dense: tables, field names, file:line references — not narrative.
- Commit as you go; leave the tree clean.

## The report you hand back

The TL;DR in three sentences, the definitions you had to separate, the list of
the engine's functions the reverser must find, the sources that were
unreadable, and the file list you touched.
