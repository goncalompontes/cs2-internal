# Research — the eye-angles / camera-angles split in the wild

## Why this task exists

The repository is building a **split** between the two angle sources in CS2:

* the **camera** angles — what the operator sees and what the local view
  angles (`CCSGOInput + 0x688`) hold;
* the **player / eye** angles — `C_CSPlayerPawn::m_angEyeAngles` (`pawn + 0x3600`),
  the animation and hitbox aim, and the angles the **server** sees.

The purpose of the split, as it is now framed, is **anti-aim**: the anti-aim
must be able to set the player's angles (the fake/desync yaw, the pitch, the
jitter) **without** dragging the camera with it and **without** disturbing the
movement's own controls. The T18b pass built the split for the *auto-strafe*;
this task's successor reframes it for the anti-aim.

Before we design it, we want to know how the **real thing** does it: what the
shipped cheats and the community threads actually do, what the failure modes
are, and what the community has already established as fact.

## What you are

A **research** agent. You do not write C++ and you do not reverse anything.
You read the web, you read this repository's own research notes, and you
write one document.

## Your worktree

**`C:\Users\gpmpo\cs2-wt\research-angle-split`**, branch
`feat/research-angle-split`. Do all your work there.

**Never** edit `C:\Users\gpmpo\cs2-internal` (the integration tree) or another
agent's worktree. **Never** `git reset --hard`, `git checkout -- .`,
`git clean` or `git stash`. Never amend or rebase.

## What to read first (in this repository)

| File | What it gives you |
|---|---|
| `docs/re/camera-split.md` | **the ground truth of the split today** (this file exists only on the branch `feat/t18b-neverlose-camera-split` — read it with `git show feat/t18b-neverlose-camera-split:docs/re/camera-split.md` from the integration tree, or copy it into your worktree; do not check it out) |
| `docs/re/subtick.md` | the two angle channels: the command's own `viewangles` and the silent `input_history` angles; `sv_subtick_movement_view_angles` |
| `docs/re/strafing.md` | what the movement reads, and the measured gate |
| `docs/re/aim.md` | the shot model, the hitboxes, the bones |
| `docs/research/weave-cs2-ragebot.md` | the public CS2 source tree's anti-aim (CS1-shaped, no desync) and its movement-correction model |
| `docs/research/gamesense-csgo-re.md` | the CS:GO Gamesense's anti-aim surface (`Antiaim.desync`, `desync_range`, `fake_lag`, `lby_type`, `at_targets`) |
| `docs/research/fatality-csgo-algorithms.md` | the CS:GO Fatality anti-aim: the yaw model, the choke, the send-time rewrite of the choked commands' angles |
| `docs/research/neverlose-mining.md` | what the shipped Neverlose module's movement and strafe actually do |
| `docs/feature-wave.md` §6 | how the repo planned the anti-aim |

## What to research on the web

Use the `websearch` and `webfetch` tools. Sources that pay off, in order of
usefulness:

1. **UnknownCheats** (the CS:GO/CS2 subforums) — threads on:
   * the **desync** / **fake angles** model in CS2, and how the *server* is
     made to see a different yaw than the client renders;
   * `sv_subtick_movement_view_angles` and the silent view-angle channel;
   * the **hitbox / eye angles** the server uses for the local player;
   * "**local animfix**" and "**animfix**" (the sibling task researches this
     in depth — you only need the *angle* side of it: how the server's angle
     and the client's angle diverge and how a cheat re-synchronises them);
   * the **CS2 desync rework** (the 2023+ change that made the silent channel
     conditional).
2. **Public source trees** — Weave (CS2), Fatality (CS:GO / CS2 base),
   Gamesense (CS:GO), and any other tree that implements an anti-aim. We
   already have notes on the first three; a **fresh** tree (or a newer
   revision of one of them) is worth reading for the angle handling.
3. **Neverlose / product documentation and public teardowns** — what the
   shipped product's anti-aim surface is (desync, fake-lag, LBY, at-targets,
   freestand) and any public statement about how it keeps the camera and the
   movement independent of the fake angles.
4. **Valve's own sources** — the public CS2 `netvars`/schema dumps and the
   `CCSGOInput` / `CBaseUserCmdPB::viewangles` definitions; the UC ticket
   threads that document the subtick movement-view-angle policy.

### What to extract, for each source

* **The exact mechanism** by which the fake angles reach the server while the
  camera keeps the real angles — the field, the channel, the timing.
* **The CS2-specific gates** — which convar, which tick, which channel.
* **The known failure modes** — what makes the camera follow the fake angles,
  what makes the movement change, what makes the server ignore the fake
  angles, and how the community detects each.
* **The terminology** — the community's words for each channel (silent,
  real, fake, desync, eye angles, view angles). Use their words in the report
  so the implementer can map them onto our fields.

## Rules of the report

* **Every claim carries its source** — a URL, the thread's title, and (when
  it is a public source tree) the file and line. A claim with no source is
  not a claim; mark it `[inference]` and say why.
* **Distinguish** what you *read* from what you *inferred*. Never present an
  inference as a source's statement.
* **Do not invent a quote.** If a forum site blocks automated fetching, say so
  and mark the row `snippet-only` — this repository already uses that
  convention (`docs/research/cheat-dump-inventory.md`).
* **Do not download or execute** a third-party binary. Reading a public
  source tree's text is fine; running an unknown binary is not.
* The scope is **technique**, not evasion. Nothing about anti-cheat evasion,
  detection avoidance or VAC belongs in the report.

## The deliverable

**One file: `docs/research/angle-split-in-the-wild.md`**, committed on your
branch with the message `docs(research): the eye-angles / camera-angles split in the wild`.

It must contain, in this order:

1. **TL;DR** — the mechanism in ten lines, and the three things the community
   established that we did not know.
2. **The angle channels** — a table: name (the community's word), the field
   it is in, who reads it (client / server / both), and when it is written.
3. **How the shipped products split the camera from the fake angles** — one
   subsection each, with the sources.
4. **The CS2 gates and the failure modes** — the table a implementer can use
   as a checklist.
5. **What it means for our anti-aim** — the concrete requirements this
   research imposes on our split: what the anti-aim must be able to set, what
   it must not disturb, and what the local player must still see.
6. **Open questions** — what the research could **not** settle, and what has to
   be settled by reversing the game instead.

Also update `docs/research/README.md`'s index table with one row for your
file (read the file first; match its format exactly).

## Rules

- The house style for prose is the surrounding documents: `docs/style-guide.md`
  is normative for code, and the research documents set the tone for prose.
- Keep it dense: tables and addresses, not narrative.
- Commit as you go; leave the tree clean.

## The report you hand back

The TL;DR in three sentences, the three most useful findings, the sources that
were unreadable, and the file list you touched.
