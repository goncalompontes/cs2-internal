# `client.dll` — lag compensation & backtracking (build 25815307)

Image base `0x180000000`. Addresses are **absolute VAs**; RVA = `VA - 0x180000000`.

> Companion to `docs/re/aim.md`. Records the client-side machinery the ragebot's
> **backtracking** uses: the lag-compensation rewind, the per-entity record
> list, the interpolation history, and the error feedback.

## Why this file exists

Backtracking = firing at a target's **past** pose within the server's
lag-compensation window. The heat map (`docs/ragebot.md`) treats the target pose
as uncertain; the lag records are the *discrete realizations* of that
uncertainty. This file records what the client exposes.

## TL;DR / decision

- The client **computes the lag-comp rewind tick** and **trims per-entity lag
  records** to the window (`FUN_1808a0390` → `FUN_180c90b90`).
- Window ≈ **`sv_maxunlag`** (~0.2 s ≈ 13 ticks @ 64), plus interp/latency.
- The client keeps a **per-entity record list** (`+0x34D0`/`+0x34D8`) and an
  **interpolation history** (`+0xE8`, 32 entries).
- The server's rewind is server-side, but the client **predicts** it and the
  server reports the disagreement (`CUserMessageLagCompensationError`), which
  the ragebot can use to calibrate.

## The rewind tick — `FUN_1808a0390` (VA `0x8a0390`)

```c
tick   = controller->m_nTickBase                 // FUN_1809433e0 (controller+0x6B8)
unlag  = min(0.07, sv_maxunlag * 0.95)           // FUN_180159ca0 = min
interp = FUN_18093e6d0() * (1/64)                // from cl_interp_ratio / cl_interp
rewind_tick = tick - (int)((unlag + interp + 0.015625 + 0.1) * 64.0);
if (rewind_tick < floor_tick) rewind_tick = floor_tick;
for each player entity:  FUN_180c90b90(entity, rewind_tick);   // trim records
```

So the window is on the order of `sv_maxunlag` (~0.2 s). `0.015625` = 1/64 (one
tick) and `0.1` is a fixed 100 ms term.

Convars: `sv_maxunlag` (`&DAT_182533818`), `cl_interp` (`&DAT_1825336d0`),
`cl_interp_ratio` (`&DAT_1825336a0`), `sv_client_min/max_interp_ratio`.

## The per-entity record list — `FUN_180c90b90` (VA `0xc90b90`)

- `entity + 0x34D0` = **count**, `entity + 0x34D8` = **array pointer**.
- Entries are **8 bytes**, keyed by an `int` tick at entry offset `+0`.
- The function swap-removes every entry whose `tick <= rewind_tick` (i.e. trims
  the list to the lag-comp window).

> The *pose payload* the entries index is the open item (the 8-byte entry only
> carries the key). The ragebot builds its own pose snapshots regardless.

## Interpolation history — `FUN_180903020` (VA `0x903020`)

`FindInterpolationEntries(entity, {tick, time}, &before, &after, &frac)`:

- Per entity: a **ring of 32 entries × `0x14` (20 B)** at `entity + 0xE8`;
  head at `+0x368`, count at `+0x36C`.
- Entry: `{ int tick @+0; float time @+4; … }` — binary-searched by tick, then
  the bracketing pair is lerped.

Related types: `SequenceHistory_t`, `TransformHistory_t` (the interp history
containers), `CInterpolatedVar*`.

## Time / tick fields

| Field | Offset |
|---|---|
| `C_BaseEntity::m_flAnimTime` | `+0x3B4` |
| `C_BaseEntity::m_flSimulationTime` | `+0x3B8` |
| `C_BaseEntity::m_bHasSuccessfullyInterpolated` | `+0x3BD` |
| `CBasePlayerController::m_nTickBase` | `+0x6B8` |
| `m_vecPredictionError` / `m_flPredictionErrorTime` | `+0x1460` / `+0x146C` (pawn) |

`m_nLastCommandNumberProcessed` (`0x181c33928`) and
`RestoreEntityToPredictedSlot` (`0x181c6e568`) are the prediction-side plumbing.

## The error feedback

- `sv_csgo_shoot_lagcompensation_max_error` (`&DAT_1824c0cf8`, default 1.0):
  *"Warn if lag compensated head hitbox position doesn't match that on client."*
- `sv_csgo_shoot_assert_lagcompensation_error`.
- `CUserMessageLagCompensationError` (server → client), `UM_LagCompensationError`.
- `GetInterpolatedShootPosition` (`FUN_180902b70`) predicts the shoot position
  from interp.

So the client and server both compute the rewind; the server reports the delta.
The ragebot can calibrate its own rewind model against this.

## The server side (`server.dll`)

The authoritative lag comp lives in `server.dll`
(sha256 `09e8c04c5f44040155d04fa79a3f43491d542a386f45807ed77785729e5925a9`,
`game/server/player_lagcompensation.cpp`) — `CLagCompensationManager`:

| Function | VA | Role |
|---|---|---|
| `StartLagCompensation` | `0x180ed6f10` | the rewind |
| `RecordDataIntoTrack` | `0x180ed1910` | writes/evicts lag records |
| `FinishLagCompensation` | `0x180ebf350` | restore |
| `FrameUpdatePostEntityThink` | — | per-frame driver |

**The record** (`CLagCompensationManager::EntityLagData`) is **`0x480` (1152) B**,
allocated in blocks from a scratch pool:

| Offset | Meaning |
|---|---|
| `+0x04` | origin (Vector) — a >64-unit move drops the records |
| `+0x34` | **tick** (the record key) |
| `+0x50` | a state int (a change drops the records) |
| `+0x60`/`+0x70` | embedded `CUtlVector` (inline cap `0x20`) |
| `+0x470`/`+0x478` | list links |

The pose is filled by `FUN_180ed1660`.

**The window.** `RecordDataIntoTrack` evicts records with
`tick < (now − sv_maxunlag_player) · 64 + 0.5`; `sv_maxunlag_player`
(*"maximumum lag compensation used for other human pawns. Applied after
sv_maxunlag!"*) is the per-player cap.

**The rewind.** `StartLagCompensation` takes the command's **attack-history
target time**, clamps it to `sv_max_unlag`, and applies the player's interp
(`pawn+0x5A8 · 64 + 1`) — the log strings name the fallbacks
(*"clamping to sv_max_unlag (%.3f), using target time"*, *"full interp info
doesn't match target time"*). So the server rewinds to the **command's target
time, clamped to `sv_maxunlag`/`sv_maxunlag_player`** — the same form the client
predicts (`FUN_1808a0390`), which is why the client can predict it and the
server reports the disagreement.

## Resolved

1. **Record pose payload.** Server: `EntityLagData`, `0x480` B, tick `+0x34`,
   origin `+0x04`, pose filled by `FUN_180ed1660`. Client: only a **lean,
   tick-keyed list** (`pawn+0x34D0`/`+0x34D8`, 8-B entries) trimmed to the
   window — the client re-derives poses from the interp/animation history rather
   than storing them. The ragebot builds its own snapshots, so nothing is
   missing.
2. **Server rewind formula.** `StartLagCompensation` — command target time,
   clamped to `sv_maxunlag`/`sv_maxunlag_player`, with player interp. Matches the
   client prediction in §"The rewind tick".
3. **`RestoreEntityToPredictedSlot`** (`0x181c6e568` → `FUN_180bec880` →
   `FUN_180b098c0`): the client's **prediction** restore (CPredictionCopy), *not*
   a lag-comp pose reader. Not used for backtracking.
