# T31 — the bot match must finish with the round **live**

> **The one-line version:** the `bot-match` scenario returns while the round is
> still in the freeze/buy time, so the operator has to wait by hand before
> anything can be exercised. The scenario must **join the team as early as it
> can** and **not return until the round is live**.

---

## 1. What happens now (measured live, 2026-10-21)

```
> cs2_run_scenario({name:"bot-match", args:{map_name:"de_dust2", per_team:5}})
{ "convars": [...15 convars...], "local_alive": true,
  "local_name": "nao falem comigo hoje.", "map": "de_dust2",
  "players": 11, "screen": "buy-time", "teams": {"2": 5, "3": 6} }
```

`screen: "buy-time"` — the scenario has **finished** while the round is still
in its freeze/buy window. The operator's request:

> "once i join a team and the round start/freeze time ends, you should run all
> the setup (ideally as soon as i have team would be better but that might not
> be possible)"

So: **the team join as early as possible**, and **the setup finished with the
round live**.

## 2. The flow today

`awaiting.ensure_bot_match` (`scripts/scenario/awaiting.py:331-384`):

| # | step | line |
|---|---|---|
| 1 | `sv_cheats 1` | `:353` |
| 2 | `ensure_map(...)` — the state machine drives to `TEAM_SELECT` (a casual server may auto-assign and overshoot) | `:354` |
| 3 | the playground convars + the respawn convars | `:356-359` |
| 4 | `mp_restartgame 1` — so the new freezetime/roundtime/respawn take effect on a fresh round | `:362` |
| 5 | `ensure_live_round(...)` — the machine joins a team, ends the warmup, waits for `LIVE` | `:363-364` |
| 6 | `add_bots(per_team=…)` | `:366` |
| 7 | `bot_stop 1` | `:368` |
| 8 | observe → return (the screen is `buy-time`) | `:375-384` |

Two things are wrong for the operator's purposes:

* **the team join is late** — it is a side effect of step 5 (driving all the
  way to a live round), so the local player is on a team only at the very end
  of the setup;
* **the scenario returns during the freeze/buy window** — steps 6 and 7
  (the bots) happen *after* the round went live, and adding the bots plus
  `bot_stop` puts the world back into a state the state machine reads as
  `buy-time`. The operator then waits by hand.

## 3. What to build

### 3.1 The order

```
load the map
  → JOIN THE TEAM as early as the screen allows        (new: the earliest step)
  → the playground convars + the respawn convars
  → mp_restartgame 1        (the convars take effect on a fresh round)
  → wait for the round to go live
  → add the bots, bot_stop 1
  → WAIT FOR THE ROUND TO BE LIVE AGAIN               (new: the scenario's last act)
  → return
```

* **The team join first.** The state machine already has the transition
  (`scripts/scenario/screens.py:896-898`: `TEAM_SELECT → FREEZE` and
  `SPECTATOR → FREEZE` via `jointeam <2|3>`) — **reuse the machine's own
  join**, do not write a second `jointeam` command somewhere else: the team
  choice must stay in one place. A casual server **auto-assigns** the team
  when the round spawns, so the join may be a no-op — that is fine, and the
  result must say which happened (see §3.3).
* **The convars still go before the restart** — otherwise the round's
  freezetime, roundtime and respawn rules are the old ones. This is an
  invariant; do not reorder it.
* **The bots still go after the round is live** — that is where the current
  order has them, and the operator asked for the setup to *finish* with a live
  round, not to start with one.

### 3.2 The last wait — the point of the task

After the bots and `bot_stop 1`, the scenario must **not return** until the
round is live again.

* Use the state machine (`screens.Machine(...).ensure(screens.Screen.LIVE, ...)`)
  or `awaiting.wait_round_started` (which waits for `round_start` **and**
  `round_freeze_end`) — whichever the code's own conventions favour; do not
  invent a third way of asking the same question.
* The wait must have a **timeout** (the round time is 60 minutes; the freeze
  window is seconds — a generous but finite timeout, e.g. 120 s) and must
  **raise `SessionError` with an actionable message** when it expires. The
  scenario must never hang.
* The message must say what it was waiting for and what to do (the same
  wording the neighbouring helpers use — look at `ensure_map`'s and
  `ensure_live_round`'s messages).

### 3.3 What the result must say

The existing keys keep their meaning (`map`, `players`, `teams`, `convars`,
`local_name`, `local_alive`, `screen`). Add:

| key | what |
|---|---|
| `screen` | the **live** screen when the scenario returns (it is `buy-time` today) |
| `round` | the compact round state (`awaiting.round_state(game)`) — the phase, the recent `round_start` / `round_freeze_end` events |
| `waited_s` | how long the final wait took, in seconds (rounded) |
| `team_joined` | how the local player got onto a team: `"joined"` (the scenario's own `jointeam`) or `"auto"` (the server assigned it) — the operator asked to know the team was there |

The result must be **honest**: if the round did not go live, the scenario
raises rather than returning a `buy-time` screen with a cheerful dict.

### 3.4 The knobs

`ensure_bot_match` already takes `map_name`, `per_team`, `respawn`,
`bot_stop`, `verify_movable`, `timeout`, `map_timeout`. Add a knob for the
**final wait's timeout** (default ~120 s) and, if you need one, for the team
to join. Every other caller of `ensure_bot_match` must keep working
(`scripts/scenario/scenarios/bot_match.py` and anything else that calls it —
**grep for it**).

## 4. The test that must be able to fail

`scripts/scenario/tests/` has the scenario's unit tests. The new behaviour
needs tests that **fail without the fix**:

* the **order**: the team is joined **before** the convars and the restart,
  and the bots are added **after** the live wait (a fake `Game` that records
  the calls);
* the **final wait**: the scenario does not return until the round is live —
  a fake whose screen stays `buy-time` must make the scenario **raise**, not
  return;
* the **timeout**: the wait is bounded;
* the **result**: `screen` is the live screen, `round` and `waited_s` and
  `team_joined` are present and honest (`team_joined` is `"auto"` when the
  server assigned the team).

The test must be registered with Meson (`meson.build`'s `scenario_suites`) so
it runs in the verify loop, and it must be **mypy clean** (`python
scripts/typecheck.py` is the gate).

## 5. The rules

- Python, **mypy strict**, the repository's style (4-space indent,
  `snake_case`, typed, no `Any` unless it is unavoidable and explained).
- No new dependencies; the state machine and `awaiting` are the vocabulary.
- The scenario must remain **observation-only** apart from the console
  commands it already issues (the convars, the restart, the team join, the
  bots) — it must not write anything into the payload's settings.
- `docs/control-harness.md` and `docs/operator-live-check.md` say what the
  scenario does — **update them** if the flow changes what the operator sees.

## 6. File ownership

You own `scripts/scenario/awaiting.py` (`ensure_bot_match` and the helpers it
needs), `scripts/scenario/scenarios/bot_match.py` (the knobs and the doc
string), `scripts/scenario/tests/test_bot_match.py` (new or extended) and
`meson.build` (the suite registration), plus `docs/control-harness.md` and
`docs/operator-live-check.md` if the flow changes.

You do **not** own the payload (`payload/**`), the wire, the features, or any
other scenario's file. `scripts/scenario/screens.py` (the state machine) is
**shared**: if you need a change there, say so in your report and keep it to
the minimum — the team-join transition already exists.

## 7. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t31-bot-match-live\.orchestration\env.ps1
meson compile -C C:\Users\gpmpo\cs2-build\t31-bot-match-live\build
meson test   -C C:\Users\gpmpo\cs2-build\t31-bot-match-live\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t31-bot-match-live\build-payload"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

## 8. What the report must state

1. **The order** — the before and after, and why each step is where it is.
2. **The final wait** — what it waits for, the timeout, and the message when
   it expires.
3. **The tests** — the four cases, and the proof that each fails without the
   fix (break it, watch it fail, revert).
4. **The result** — the keys and their meaning, and what a real run prints
   (if you can run it: the game may be live on this machine — the operator
   drives it; the MCP scenario `cs2_run_scenario({name:"bot-match", ...})` is
   how it is run).
5. What is still open.
6. The file list and the commit hashes.
