# S4 — the state machine: more screens, more capabilities, more interactions (Python)

**Goal:** make the screen state machine a **complete** model of what the game can
be doing, so an agent (or a scenario) can ask "what is the game doing?" and
"what may I do right now?" and get a truthful, typed answer — and can *drive*
the game to a state it needs.

**File ownership (you may ONLY touch these):**

- `scripts/scenario/screens.py`
- `scripts/scenario/awaiting.py`
- `scripts/scenario/session.py`
- `scripts/scenario/tests/test_screens.py`
- `scripts/scenario/tests/test_state_machine.py` *(new)*
- `scripts/scenario/__init__.py` (re-exports only)

**Do NOT touch** `protocol.py` or `control.py` — a parallel task (S3) owns those
and you consume its types. If you need a type that does not exist, **stop and
report it**.

**S3 has landed** (commit `854ef30`), so its types are available:
`cs2.scenario.protocol` exports the `TypedDict`s (`StateSnapshotDict`,
`PlayerSnapshotDict`, `RulesSnapshotDict`, `LevelSnapshotDict`, …), the
`Literal`s, the recursive `WaitExpr`, the `ControlTransport` `Protocol`, and
the `ControlError` / `ProtocolError` exceptions. `Control.state()` now returns a
**validated** `StateSnapshotDict` and `Control.ping()` a validated
`PingResult` — so the `dict[str, Any]` annotations on `Game.state()` and the
other `Game` wrappers are now **wrong** and must be widened (a `TypedDict` is
not a `dict[str, Any]` to a type checker). That is part of your §6 work.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`spec-s3.md` (so you know what the types are called), then
`docs/control-harness.md` §"The screen state machine".

---

## 1. What is there now

`scripts/scenario/screens.py` (649 lines) has:

- `Screen(str, enum.Enum)` — **9** values: `NOT_READY`, `NOT_IN_GAME`,
  `TEAM_SELECT`, `SPECTATOR`, `WARMUP`, `FREEZE`, `LIVE`, `DEAD`, `ROUND_END`.
- `Can(str, enum.Enum)` — **8** capabilities: `STATE`, `LOOK`, `MOVE`, `JUMP`,
  `ATTACK`, `RELOAD`, `BUY`, `TELEPORT`.
- `GameState` — a frozen dataclass: the screen, the raw snapshot, the recent
  event names, the notes; plus the accessors (`valid`, `in_game`, `rules_loaded`,
  `team`, `alive`, `life_state`, `on_ground`, `origin`, `players`,
  `in_real_map`, `buy_time`, `round_over`, `capabilities()`, `can()`, `why()`,
  `require()`, `summary()`, `as_dict()`).
- `CAPABILITIES: dict[Screen, tuple[Can, ...]]` and `TRANSITIONS:
  tuple[Transition, ...]` — **data**, so the `screens` scenario can print them
  and a test can assert they stay in sync with the code.
- `deduce(raw, events) -> GameState` — the decision order is
  `NOT_READY → NOT_IN_GAME → TEAM_SELECT → SPECTATOR → WARMUP → DEAD → FREEZE →
  ROUND_END → LIVE`.
- `Machine` — the driver: `observe`, `screen`, `require`, `ensure(target)`,
  `ensure_live`, plus the per-transition actions (`_load_map`, `_join_team`,
  `_end_warmup`, `_restart_round`, `_wait_change`).

`scripts/scenario/awaiting.py` (482 lines) is the readiness layer, and
`scripts/scenario/session.py` (276 lines) is the session façade (`Game`) over
`Control`.

## 2. The data you have

The screen is derived from the payload's snapshot, whose blocks are typed in
`protocol.py`:

- `rules`: `loaded`, `match_started`, `freeze`, `warmup`, `game_restart`,
  **`bomb_planted`**, **`bomb_dropped`**, `t_cant_buy`, `ct_cant_buy`,
  **`game_phase`**, `round_time`, `freeze_time`, `round_win_status`,
  **`round_win_reason`**, `total_rounds_played`, `match_start_time`,
  `round_start_time`, `restart_round_time`, `game_start_time`,
  `warmup_period_start`, **`time_until_next_phase`**.
- `level`: `up`, `proxy`, `rules_global`, `identities`, **`local_connected`**.
- `local` / `players[]`: `team`, `alive`, `life_state`, `connected`,
  `defusing`, `has_bomb`, `in_buy_zone`, `scoped`, `helmet`, `defuser`,
  `money`, `health`, …
- the **event stream** (`round_start`, `round_end`, `round_freeze_end`,
  `round_mvp`, `announce_phase_end`, `player_death`, `player_hurt`,
  `player_spawned`, `bomb_planted`, `bomb_defused`, `bomb_exploded`,
  `bomb_dropped`, `bomb_pickup`, `item_purchase`, `begin_new_match`,
  `client_disconnect`, …).

**Note:** the payload's rules block has **no pause signal** and no explicit
"loading" flag. Say in your report which of the new screens you could derive
**from the existing data** and which need a payload change (do not make the
payload change yourself — report it).

## 3. The new screens

Add at least these to `Screen`, with the **decision order** you choose and
documented:

| Screen | Meaning | Derived from |
|---|---|---|
| `LOADING` | a level is in flight (the map is being loaded / the world is being set up) | `level.up` transitions, or the payload's state going invalid while a level was up |
| `DISCONNECTED` | the client lost the server | `level.local_connected == 0`, or a `client_disconnect` event |
| `BUY_TIME` | the buy window is open in a live round | `rules.time_until_next_phase`, the `*_cant_buy` flags, the `round_start` → `round_freeze_end` window |
| `PLANTED` | the bomb is down | `rules.bomb_planted` |
| `DEFUSING` | the local player is defusing | `local.defusing` |
| `OVERTIME` | the match is in overtime | `rules.game_phase` (the game phase value for overtime — **confirm** it, do not guess) |
| `HALF_TIME` | the match is between halves | `rules.game_phase` (confirm) |
| `PAUSED` | the game is paused | **probably not derivable** — say so |

Rules:

- The **decision order matters** and must be documented in the module docstring
  (the current one documents the 9-value order). A dead player must still read
  as `DEAD` (the existing test `test_dead_beats_freeze_and_round_end` pins
  that) — but a **planted bomb** is a *round* fact, not a *player* fact, so
  decide explicitly whether `PLANTED` outranks `DEAD` and say why.
- Each new screen needs a **row in `CAPABILITIES`**.
- Each new screen needs at least one **transition** (or be explicitly documented
  as terminal). The existing test `test_every_screen_is_reachable_or_terminal`
  enforces this — keep it passing.
- The `Screen` values are **kebab-case strings** (`"round-end"`), and `Screen`
  is a `str` mixin so `Screen.LIVE == "live"`. Keep that.

## 4. The new capabilities

Add to `Can`, each with a **real** meaning, and add them to the `CAPABILITIES`
rows where they are true:

| Capability | Meaning | How it is done today |
|---|---|---|
| `PLANT` | plant the bomb (needs the C4, the buy zone and the plant site) | `exec "plant"` / the `+use` button |
| `DEFUSE` | defuse the bomb | `exec "defuse"` / `+use` |
| `DROP` | drop the current weapon | `exec "drop"` |
| `INSPECT` | inspect the weapon | `exec "lastinv"` |
| `SWITCH_WEAPON` | switch to a slot (`1`…`5`, or the weapon name) | `exec "slotN"` / `+<weapon>` |
| `GRENADE` | throw a grenade | `exec "+hegrenade"` |
| `SCORE` | read the scoreboard | **read-only** — a new `SCORE` capability that is a *read*, like `STATE` |
| `RECONNECT` | reconnect to a server | `exec "reconnect"` |

`SCORE` is a new kind of capability: it is always available (like `STATE`).
Decide whether the model needs a separate "read-only" concept, or whether
`SCORE` simply appears in every screen's row. Say which, and why.

## 5. More interactions — the `Machine` can drive the new screens

`Machine.ensure(target)` currently drives: `NOT_IN_GAME → TEAM_SELECT`,
`TEAM_SELECT → FREEZE`, `WARMUP → FREEZE`, `FREEZE → LIVE`, `ROUND_END → LIVE`,
`DEAD → LIVE`, `NOT_READY → TEAM_SELECT`, `LIVE → FREEZE`.

Add the **actions** the new screens need, as `Transition.action` values (the
existing test `test_transition_actions_exist` checks the action names against
a `KNOWN_ACTIONS` set — extend that set):

- `wait_bomb_planted` — wait for the bomb to be planted.
- `wait_defuse` / `wait_defused` — wait for the defuse to start / finish.
- `wait_buy_time` — wait for the buy window.
- `wait_round_start` (already there) — extend it to cover the post-plant
  restart.
- `reconnect` — reconnect to the server.
- `wait_load` — wait for the level to finish loading.

Each action must be implemented in `Machine` (the private `_do_*` helpers), and
each must be **safe**: an action that is not applicable in the current state
must not fire the game.

## 6. The `Game` façade — finish the typing

`scripts/scenario/session.py`'s `Game` has **six completely unannotated**
methods (a review finding): `state_machine`, `screen`, `game_state`,
`require`, `ensure_screen`, and the `aim`/`rage`/`heatmap` delegators use
`**kwargs: Any`. Annotate them:

```python
def state_machine(self, *, map_name: str = "de_dust2", debug: bool = True) -> "Machine": ...
def screen(self) -> Screen: ...
def game_state(self) -> GameState: ...
def require(self, capability: Can | str) -> GameState: ...
def ensure_screen(self, target: Screen, *, map_name: str = "de_dust2", **kwargs: Any) -> GameState: ...
```

Also: `Game` should satisfy the `ControlTransport` `Protocol` from `protocol.py`
(S3) — add a runtime check in the test.

**The import cycle** — `screens.py` imports `Game` from `session.py`, and
`session.py` imports the machine **lazily** inside the methods (to break the
cycle). Keep that pattern; do not create a cycle.

## 7. The tests

`scripts/scenario/tests/test_screens.py` is the existing test file (189 lines,
`unittest`). It must keep passing, extended with:

- a fixture per **new** screen (the `FIXTURES` table maps a screen → a
  representative snapshot; the new screens need entries, and
  `test_each_screen_is_recognised` will then cover them).
- a test per new screen's **deduction** (the exact rules block / local state
  that produces it).
- a test that the **decision order** is what you documented — e.g. that
  `PLANTED` outranks (or does not outrank) `DEAD`, whichever you chose.
- a test that the **capability table is not merely self-consistent**: the
  existing `test_table_matches_the_deduction` only proves `can()` agrees with
  the table. Add a test that asserts the **content** — e.g. that `LIVE` has
  `MOVE`, that `DEAD` has only `STATE`, that `PLANTED` has `PLANT` but not
  `MOVE`.
- a test for each new transition's action (that the action name is known and
  that the action is a no-op in a state where it does not apply).
- the new `tests/test_state_machine.py`: the `Machine`'s **pure** logic — the
  decision table, the capability resolution, the transition graph's
  reachability — **without** a live game. The `Machine` currently has **zero**
  unit tests (only the live `screens` scenario exercises it). Extract whatever
  is pure enough to test and test it.

## 8. Quality bar

- `from __future__ import annotations`; `snake_case`; `SCREAMING_SNAKE`
  constants; docstrings that explain *why*.
- The module docstring must be updated: it is the **documentation of the
  screens** (the "Screens" section) and it is now wrong.
- The `screens` **scenario** (`scripts/scenario/scenarios/screens.py`) prints
  the tables — make sure it still works with the new screens (it is not in your
  ownership; if it breaks, **report it** and say what it needs).
- **Do not change the wire format** or the payload.

## 9. Verification

- `python -m unittest discover -s scripts\scenario\tests -t scripts` → green,
  with **more** tests than before.
- `python scripts\run-scenario.py --list` → still lists the scenarios.
- **Prove the new tests can fail**: break one deduction rule on purpose, watch
  the test fail, revert.
- Commit in logical pieces (`feat(scenario): …`).

## 10. Report

- The full new screen list with the **decision order** and the derivation of
  each.
- The new capabilities and where they are true.
- The new transitions and their actions.
- **Which of the new screens you could not derive from the existing data**, and
  exactly what payload change each would need.
- The `Machine` logic you made testable, and the test count before/after.
