"""The screen/round state machine the scenarios gate on.

A scenario that fires input or reads a mechanic is only meaningful in a handful
of the screens CS2 can be in, and most of the past failures were "the test ran in
the wrong screen" (team-select, warmup, freeze, dead, main menu). This module
turns the raw payload state into one **screen**, the **capabilities** that screen
has, and the **transitions** that move between them, so a scenario can either

* ``machine.ensure(Screen.LIVE)`` — drive the game into a state, or
* ``machine.require(Can.MOVE)`` — fail with a precise reason if it is not usable.

Where the signals come from
---------------------------

Raw state (``state`` command) plus the payload's new authoritative
``C_CSGameRules`` block (``rules``, ``docs/re/subtick.md`` neighbour
``docs/re/client-dll.md`` §"`game_rules`"):

=====================  ==========================================================
signal                 meaning
=====================  ==========================================================
``in_game``            **the main-screen rule**: the level's
                       ``C_CSGameRulesProxy`` entity exists. False at the main
                       menu and while a level loads, true from team-select
                       onwards. The ``game_rules`` global is readable at the
                       main menu too (a default object), so ``rules.loaded``
                       alone cannot be the test.
``in_map``             a local player snapshot exists (true at the main menu too).
``rules.loaded``       the ``game_rules`` global was readable — **true at the
                       main menu as well**; a fact to report, not the level test.
``rules.warmup``       ``m_bWarmupPeriod`` — warmup (no round structure yet).
``rules.freeze``       ``m_bFreezePeriod`` — round started, players frozen.
``rules.round_win_*``  ``m_iRoundWinStatus``/``m_eRoundWinReason`` — a winner is
                       set ⇒ the round is over (buy/loss gap before the next).
``rules.t_cant_buy``   ``m_bTCantBuy`` — the T buy window has closed.
``local.team``         0 = no team (team-select/spectator screen), 1 = spectator,
                       2 = T, 3 = CT.
``local.alive``        ``life_state == 0`` — dead players ignore input.
=====================  ==========================================================

The payload's rules block is the ground truth; the game **event** stream
(`round_start`, `round_freeze_end`, `round_end`) is used only as a fallback for a
build whose rules are unreadable.

Screens
-------

``NOT_READY``    no readable target state (engine booting / level teardown).
``NOT_IN_GAME``  the main screen: no level's rules-proxy entity (``in_game`` false).
``TEAM_SELECT``  level up, no team yet (input ignored; ``jointeam`` moves on).
``SPECTATOR``    level up, spectating.
``WARMUP``       in a team, before the round structure starts.
``FREEZE``       round started, freeze period — input ignored until it ends.
``LIVE``         playable: alive, in a team, round running, not frozen.
``DEAD``         in a team, not alive — input ignored until respawn.
``ROUND_END``    a winner is set; movement/shots do nothing until the next round.

Capabilities (per screen) and the transitions live in ``CAPABILITIES`` /
``TRANSITIONS`` at the bottom of this file; both are printed by the ``screens``
scenario so the table cannot silently drift from the code.
"""

from __future__ import annotations

import enum
import time
from dataclasses import dataclass, field
from typing import Any, Iterable

from .session import Game, SessionError


class Screen(str, enum.Enum):
    """The single screen the game is in."""

    NOT_READY = "not-ready"
    NOT_IN_GAME = "not-in-game"
    TEAM_SELECT = "team-select"
    SPECTATOR = "spectator"
    WARMUP = "warmup"
    FREEZE = "freeze"
    LIVE = "live"
    DEAD = "dead"
    ROUND_END = "round-end"

    def __str__(self) -> str:  # pragma: no cover - display only
        return self.value


class Can(str, enum.Enum):
    """Operations a scenario may perform, gated by the current screen."""

    STATE = "state"          # read the snapshot (always safe)
    LOOK = "look"            # write view angles (`setang` / sub-tick angles)
    MOVE = "move"            # analog movement (usercmd or console `+forward`)
    JUMP = "jump"
    ATTACK = "attack"        # fire / use a weapon
    RELOAD = "reload"
    BUY = "buy"
    TELEPORT = "teleport"    # setpos/noclip (sv_cheats; needs a live pawn)

    def __str__(self) -> str:  # pragma: no cover - display only
        return self.value


# Teams per the game's own numbering.
T, CT, SPECTATOR_TEAM, NO_TEAM = 2, 3, 1, 0

#: How long after injection `exec` is assumed safe even if the engine has not
#: exposed a main-menu local player yet (see `Machine._engine_up`). The observed
#: crash was a console line issued within ~2 s of injection.
EXEC_GRACE_MS = 15_000

#: Which capabilities each screen has. Kept as data so the ``screens`` scenario
#: can print it and a test can assert it stays in sync with ``GameState.capabilities``.
CAPABILITIES: dict[Screen, tuple[Can, ...]] = {
    Screen.NOT_READY: (Can.STATE,),
    Screen.NOT_IN_GAME: (Can.STATE,),
    Screen.TEAM_SELECT: (Can.STATE,),
    Screen.SPECTATOR: (Can.STATE, Can.LOOK),
    Screen.WARMUP: (Can.STATE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK, Can.RELOAD,
                    Can.TELEPORT),
    Screen.FREEZE: (Can.STATE, Can.LOOK, Can.BUY, Can.TELEPORT),
    Screen.LIVE: (Can.STATE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK, Can.RELOAD,
                  Can.BUY, Can.TELEPORT),
    Screen.DEAD: (Can.STATE,),
    Screen.ROUND_END: (Can.STATE, Can.BUY),
}


@dataclass(frozen=True)
class GameState:
    """One observation of the game, reduced to a screen + capabilities."""

    screen: Screen
    raw: dict[str, Any]
    events: tuple[str, ...] = ()
    notes: tuple[str, ...] = ()

    # --- convenience accessors (all read the raw payload state) -----------
    @property
    def valid(self) -> bool:
        return bool(self.raw.get("valid"))

    @property
    def in_game(self) -> bool:
        """A level is up (the main-screen rule): false at the main menu."""
        return _level_up(self.raw, self.events)

    @property
    def rules_loaded(self) -> bool:
        """Whether the `game_rules` global was readable — **true at the main
        menu too**, so it is a fact to report, not the level test."""
        return bool(self.rules.get("loaded"))

    @property
    def in_map(self) -> bool:
        return bool(self.raw.get("in_map"))

    @property
    def level(self) -> dict[str, Any]:
        return self.raw.get("level") or {}

    @property
    def rules(self) -> dict[str, Any]:
        return self.raw.get("rules") or {}

    @property
    def local(self) -> dict[str, Any]:
        return self.raw.get("local") or {}

    @property
    def team(self) -> int:
        return int(self.local.get("team") or 0)

    @property
    def alive(self) -> bool:
        return bool(self.local.get("alive"))

    @property
    def life_state(self) -> int:
        return int(self.local.get("life_state") or 0)

    @property
    def on_ground(self) -> bool:
        return bool(self.local.get("on_ground"))

    @property
    def origin(self) -> dict[str, float] | None:
        return self.local.get("origin")

    @property
    def players(self) -> int:
        return len(self.raw.get("players") or [])

    @property
    def in_real_map(self) -> bool:
        """A real level with the local player spawned in it."""
        origin = self.origin or {}
        nonzero = any(origin.get(axis, 0.0) for axis in ("x", "y", "z"))
        return bool(self.in_game and (nonzero or self.team in (T, CT)))

    @property
    def buy_time(self) -> bool:
        """Whether the local team's buy window is still open (rules-driven)."""
        if not self.in_game or not self.alive:
            return False
        if self.team == T:
            return not bool(self.rules.get("t_cant_buy"))
        if self.team == CT:
            return not bool(self.rules.get("ct_cant_buy"))
        return False

    @property
    def round_over(self) -> bool:
        """A winner is set (rules) or the newest round event is ``round_end``."""
        return bool(self.rules.get("round_win_status")) or _round_ended(self.events)

    # --- the gating API ---------------------------------------------------
    def capabilities(self) -> dict[Can, tuple[bool, str]]:
        """Every capability with ``(allowed, reason)`` — the reason is the *why*."""
        out: dict[Can, tuple[bool, str]] = {}
        for cap in Can:
            out[cap] = self._capability(cap)
        return out

    def _capability(self, cap: Can) -> tuple[bool, str]:
        allowed = cap in CAPABILITIES.get(self.screen, ())
        if cap is Can.BUY:
            # Buy needs the buy window as well as the screen: LIVE before the
            # window closes, or the post-round gap.
            if allowed and not self.buy_time:
                return False, f"{self.screen}: buy window closed ({self._buy_reason()})"
            return allowed, self._reason(cap)
        return allowed, self._reason(cap)

    def _buy_reason(self) -> str:
        team = {T: "t_cant_buy", CT: "ct_cant_buy"}.get(self.team)
        if team is None:
            return "no team"
        return f"{team}={self.rules.get('t_cant_buy' if self.team == T else 'ct_cant_buy')}"

    def _reason(self, cap: Can) -> str:
        if cap in CAPABILITIES.get(self.screen, ()):
            return f"{self.screen} allows {cap}"
        return f"{self.screen} does not allow {cap}"

    def can(self, capability: Can | str) -> bool:
        cap = Can(capability)
        allowed, _ = self._capability(cap)
        return allowed

    def why(self, capability: Can | str) -> str:
        cap = Can(capability)
        allowed, reason = self._capability(cap)
        return ("ok: " if allowed else "blocked: ") + reason

    def require(self, capability: Can | str) -> None:
        """Raise :class:`SessionError` unless ``capability`` is available."""
        if not self.can(capability):
            raise SessionError(f"cannot {capability}: {self.why(capability)} — {self.summary()}")

    def summary(self) -> str:
        """A one-line description for failures and logs."""
        rules = self.rules
        bits = [
            f"screen={self.screen}",
            f"valid={self.valid}",
            f"in_game={self.in_game}",
            f"team={self.team}",
            f"alive={self.alive}",
            f"life_state={self.life_state}",
            f"players={self.players}",
        ]
        if self.in_game:
            bits += [
                f"warmup={bool(rules.get('warmup'))}",
                f"freeze={bool(rules.get('freeze'))}",
                f"match_started={bool(rules.get('match_started'))}",
                f"win_status={rules.get('round_win_status')}",
                f"game_phase={rules.get('game_phase')}",
            ]
        if self.notes:
            bits.append("notes=" + ",".join(self.notes))
        return " ".join(bits)

    def as_dict(self) -> dict[str, Any]:
        """A JSON-friendly view (what goes into a scenario result)."""
        return {
            "screen": str(self.screen),
            "summary": self.summary(),
            "valid": self.valid,
            "in_game": self.in_game,
            "rules_loaded": self.rules_loaded,
            "in_map": self.in_map,
            "in_real_map": self.in_real_map,
            "level": self.level,
            "team": self.team,
            "alive": self.alive,
            "life_state": self.life_state,
            "on_ground": self.on_ground,
            "buy_time": self.buy_time,
            "round_over": self.round_over,
            "players": self.players,
            "rules": self.rules,
            "events": list(self.events),
            "notes": list(self.notes),
            "capabilities": {
                str(cap): {"allowed": allowed, "reason": reason}
                for cap, (allowed, reason) in self.capabilities().items()
            },
        }


def _level_up(raw: dict[str, Any], events: Iterable[str] = ()) -> bool:
    """Whether a level is up — **the main-screen rule** (measured 2026-10-15).

    Everything that looks like a level signal fails at the main menu, so the rule
    is the *local player being in the level*:

    ==========================  ==================  ==================
    signal                      main screen         in a level
    ==========================  ==================  ==================
    ``rules.loaded``            true (default obj)  true
    ``level.up`` (proxy entity) true                true
    identities in the world     258 / 382           726
    ``local.connected``         0                   0
    ``rules.match_started``     false               true
    ``rules.game_start_time``   0                   30.1
    local ``origin``            (0,0,0)             real
    local ``team``              0                   2/3
    ==========================  ==================  ==================

    The menu is a level *world* (it has entities, a `C_CSGameRulesProxy` and a
    readable `game_rules` global holding a default `warmup = freeze = true`
    object) but the local player is **not spawned**: ``origin = (0,0,0)``, no
    team, no weapon, ``life_state != 0``. A real level puts the local at a
    position or on a team. Team-select may have no team yet, so a running match
    (``match_started``, a non-zero match/game time, rounds played, or any game
    event) also counts.

    The canonical alternative — `CNetworkGameClient::m_bIsBackgroundMap` with
    `m_nSignonState >= SIGNONSTATE_FULL` (cs2-dumper #111; the menu is a
    *background* map, which is why the signon state alone is 6 in the menu) — needs
    a new signature and is the documented follow-up; and
    `CBasePlayerController::m_iConnected` is 0 on this build even in-game. See
    `docs/re/client-dll.md` §"Am I in a game?".
    """
    local = raw.get("local") or {}
    if not raw.get("valid") or not local:
        return False
    origin = local.get("origin") or {}
    if any(origin.get(axis, 0.0) for axis in ("x", "y", "z")):
        return True
    if int(local.get("team") or 0) in (T, CT):
        return True
    rules = raw.get("rules") or {}
    if (rules.get("match_started") or float(rules.get("game_start_time") or 0.0) > 0.0
            or int(rules.get("total_rounds_played") or 0) > 0):
        return True
    return any(events)


def _round_ended(events: tuple[str, ...]) -> bool:
    """Whether the newest round boundary in ``events`` (oldest → newest) is an end.

    The fallback for a build whose `rules` cannot be read: walk back to the most
    recent ``round_start``/``round_end`` and see which side of it we are on.
    """
    for name in reversed(events):
        if name == "round_end":
            return True
        if name == "round_start":
            return False
    return False


# --- deduction ------------------------------------------------------------
def deduce(raw: dict[str, Any], events: Iterable[str] = ()) -> GameState:
    """Reduce a payload ``state`` snapshot to a :class:`GameState`.

    The order matters and encodes the screens:

    1. no readable state → ``NOT_READY`` (engine booting / level teardown);
    2. no level up (``in_game`` false — the main screen) → ``NOT_IN_GAME``;
    3. no team → ``TEAM_SELECT`` (also the unassigned screen right after a map);
    4. team 1 → ``SPECTATOR``;
    5. warmup → ``WARMUP``;
    6. not alive → ``DEAD`` (input is ignored whatever the phase);
    7. freeze period → ``FREEZE``;
    8. a winner is set → ``ROUND_END``;
    9. otherwise → ``LIVE``.
    """
    event_names = tuple(events)  # oldest -> newest (the payload's order)

    if not raw.get("valid"):
        # The engine has no readable local state yet: it is still booting after
        # injection, or a level teardown is in flight (the `map` command clears
        # `valid` for a moment). The only correct action is to **wait** — `exec`
        # in this state faults inside the engine's Cbuf path.
        return GameState(Screen.NOT_READY, raw, event_names,
                         ("no readable local state — engine booting or level "
                          "teardown; do not exec",))

    if not _level_up(raw, event_names):
        notes = ["main screen (no level)"]
        if raw.get("rules", {}).get("loaded"):
            notes.append("game_rules global readable — the menu object, not a level")
        if (raw.get("level") or {}).get("up"):
            notes.append("rules-proxy entity exists — the menu world has one too")
        local = raw.get("local") or {}
        if local:
            notes.append(f"local placeholder (origin {(local.get('origin') or {}).get('x', 0):.0f},"
                         f" team {local.get('team')}, weapon '{local.get('weapon')}')")
        return GameState(Screen.NOT_IN_GAME, raw, event_names, tuple(notes))

    team = int((raw.get("local") or {}).get("team") or 0)
    if team == NO_TEAM:
        return GameState(Screen.TEAM_SELECT, raw, event_names,
                         ("level up, no team yet",))
    if team == SPECTATOR_TEAM:
        return GameState(Screen.SPECTATOR, raw, event_names, ("team=1",))

    rules = raw.get("rules") or {}
    local = raw.get("local") or {}
    alive = bool(local.get("alive"))
    warmup = bool(rules.get("warmup"))
    freeze = bool(rules.get("freeze"))
    win_status = int(rules.get("round_win_status") or 0)

    if warmup:
        return GameState(Screen.WARMUP, raw, event_names, ("warmup period",))
    if not alive:
        return GameState(Screen.DEAD, raw, event_names,
                         (f"life_state={local.get('life_state')}",))
    if freeze:
        return GameState(Screen.FREEZE, raw, event_names, ("freeze period",))
    if win_status != 0 or _round_ended(event_names):
        return GameState(Screen.ROUND_END, raw, event_names,
                         (f"winner set (m_iRoundWinStatus={win_status})"
                          if win_status != 0 else "round_end event",))
    return GameState(Screen.LIVE, raw, event_names, ("round live",))


def observe(game: Game, *, events: int = 6) -> GameState:
    """One round-trip: read ``state`` (+ recent events) and deduce the screen."""
    raw = game.state()
    names: list[str] = []
    if events:
        for event in game.events(limit=events).get("events", []):
            name = event.get("name")
            if name:
                names.append(str(name))
    return deduce(raw, names)


# --- transitions ----------------------------------------------------------
@dataclass(frozen=True)
class Transition:
    """One edge of the machine: what to do to leave ``frm`` for ``to``."""

    frm: Screen
    to: Screen
    action: str
    detail: str
    note: str = ""


#: The edges. ``action`` names a step :class:`Machine` knows how to run; the
#: ``screens`` scenario prints this table so it stays honest.
TRANSITIONS: tuple[Transition, ...] = (
    Transition(Screen.NOT_IN_GAME, Screen.TEAM_SELECT, "load_map",
               "game_type 0; game_mode 0; map <name>",
               "the rules global is null until a level loads — do not re-issue "
               "while a load is in flight or it restarts forever"),
    Transition(Screen.TEAM_SELECT, Screen.FREEZE, "join_team", "jointeam <2|3>",
               "input is ignored with no team; a casual server also auto-assigns"),
    Transition(Screen.SPECTATOR, Screen.FREEZE, "join_team", "jointeam <2|3>"),
    Transition(Screen.WARMUP, Screen.FREEZE, "end_warmup",
               "mp_warmup_end; mp_restartgame 1"),
    Transition(Screen.FREEZE, Screen.LIVE, "wait_freeze_end",
               "wait for m_bFreezePeriod to clear (round_freeze_end)"),
    Transition(Screen.ROUND_END, Screen.LIVE, "wait_round_start",
               "wait for the next round_start"),
    Transition(Screen.DEAD, Screen.LIVE, "wait_respawn",
               "wait for the respawn, or mp_restartgame 1 (cheat)"),
    Transition(Screen.NOT_READY, Screen.TEAM_SELECT, "load_map",
               "wait for the engine, then `game_type 0; game_mode 0; map <name>`",
               "no readable local state: engine booting, or the main screen after "
               "a disconnect; `exec` is unsafe only before the engine is up"),
    Transition(Screen.LIVE, Screen.FREEZE, "restart_round",
               "mp_restartgame 1", "used to re-roll into a fresh round"),
)

#: Screens a scenario can drive the game into with :meth:`Machine.ensure`.
TARGETS: tuple[Screen, ...] = (Screen.NOT_IN_GAME, Screen.TEAM_SELECT, Screen.WARMUP,
                               Screen.FREEZE, Screen.LIVE)


class Machine:
    """Drives and inspects the screen state machine for one session."""

    def __init__(self, game: Game, *, map_name: str = "de_dust2",
                 debug: bool = True) -> None:
        self.game = game
        self.map_name = map_name
        self.debug = debug
        self.log: list[dict[str, Any]] = []
        self.state: GameState | None = None
        #: Last "is the level still progressing" marker (see `_settled`).
        self._marker: tuple[int, bool, int] | None = None

    # --- observation ------------------------------------------------------
    def observe(self, *, events: int = 6) -> GameState:
        self.state = observe(self.game, events=events)
        return self.state

    def _assert_payload_has_rules(self) -> None:
        """Fail loudly when the injected payload predates the level/rules probes.

        The machine's screen deduction is authoritative only with the payload's
        ``level`` (the `C_CSGameRulesProxy` main-screen rule) and ``rules``
        (round phase) blocks; an older payload would look like "main screen"
        forever and every ``ensure`` would loop on ``load_map``.
        """
        raw = self.game.state()
        missing = [key for key in ("level", "rules") if key not in raw]
        if missing:
            raise SessionError(
                f"the payload has no {missing} block (it predates the screen state "
                "machine): rebuild it and re-inject once — "
                "`meson compile -C build-payload`, then relaunch the game "
                "(docs/handoff.md §2/§3)"
            )

    def screen(self) -> Screen:
        return self.observe().screen

    def require(self, capability: Can | str, *, events: int = 6) -> GameState:
        """Assert a capability is available *right now*; return the observation."""
        self._assert_payload_has_rules()
        state = self.observe(events=events)
        state.require(capability)
        return state

    def _note(self, action: str, state: GameState, detail: str = "") -> None:
        entry = {"t": round(time.monotonic(), 1), "screen": str(state.screen),
                 "action": action, "detail": detail, "state": state.summary()}
        self.log.append(entry)
        if self.debug:
            print(f"[machine] {state.screen} -> {action} {detail}".rstrip())

    # --- transitions ------------------------------------------------------
    def _engine_up(self) -> bool:
        """Whether it is safe to ``exec``.

        ``exec`` faults inside the engine's Cbuf path when issued too early after
        a launch/injection (`docs/handoff.md` §"Live validation notes"), so every
        action that emits a console line goes through this guard. Two signals,
        either of which is enough:

        * the engine exposes the main-menu local player (``valid && in_map``), or
        * the payload has been injected for ``EXEC_GRACE_MS`` (its own uptime, so
          a slow boot on any host is covered).

        A readable ``valid`` alone is **not** enough: the observed crash happened
        with a readable menu local a couple of seconds after injection.
        """
        raw = self.game.state()
        if raw.get("valid") and raw.get("in_map"):
            return True
        uptime = int(self.game.ping().get("uptime_ms") or 0)
        return uptime >= EXEC_GRACE_MS

    def _load_map(self, timeout: float, load_budget: float) -> bool:
        """Issue ``map`` and wait for a level, never re-issuing mid-load.

        A ``map`` command drops the client out of the level and loads the next
        one: re-issuing while a load is in flight **restarts** the load, so a load
        slower than the re-issue window never completes (observed 2026-10-15).
        Each attempt gets ``load_budget`` seconds and the budget is refreshed
        while the state still shows progress (players/events changing).
        """
        deadline = time.monotonic() + timeout
        markers: tuple[int, int, bool] | None = None
        while time.monotonic() < deadline:
            if not self._engine_up():
                time.sleep(0.5)
                continue
            self.game.exec("sv_cheats 1")
            self.game.exec(f"game_type 0; game_mode 0; map {self.map_name}")
            attempt_end = min(deadline, time.monotonic() + load_budget)
            markers = self._progress_markers()
            while time.monotonic() < attempt_end:
                state = self.observe()
                # `NOT_READY` is the moment a `map` command clears the local state
                # (the level teardown) — still part of the load, not a success.
                if state.screen not in (Screen.NOT_IN_GAME, Screen.NOT_READY):
                    return True
                current = self._progress_markers()
                if current != markers:
                    markers = current
                    attempt_end = min(deadline, time.monotonic() + load_budget)
                time.sleep(0.5)
        return False

    def _progress_markers(self) -> tuple[int, int, bool]:
        state = self.state or self.observe()
        return (state.players, len(state.events), state.in_game)

    def _join_team(self, team: int) -> None:
        if not self._engine_up():
            return
        self.game.exec("sv_cheats 1")
        self.game.exec(f"jointeam {team}")
        # `jointeam` without an argument picks a team; with one it may need the
        # follow-up in this build, so also try the bare form.
        self.game.exec("jointeam")

    def _end_warmup(self) -> None:
        if not self._engine_up():
            return
        self.game.exec("sv_cheats 1")
        self.game.exec("mp_warmup_end")
        self.game.exec("mp_freezetime 0")
        self.game.exec("mp_restartgame 1")

    def _restart_round(self) -> None:
        if not self._engine_up():
            return
        self.game.exec("sv_cheats 1")
        self.game.exec("mp_freezetime 0")
        self.game.exec("mp_restartgame 1")

    def _wait_change(self, screens: tuple[Screen, ...], timeout: float) -> bool:
        """Wait until the screen is no longer any of ``screens``."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.observe().screen
            if screen not in screens:
                return True
            time.sleep(0.5)
        return False

    def _settled(self, state: GameState) -> bool:
        """Whether the level is no longer visibly *progressing*.

        A level that is still connecting/loading reports `team=0` with a growing
        player list — indistinguishable from the team-select screen by fields
        alone — so actions wait for the loading progress (player count / whether
        the local is in the level / the team) to stop changing. The screen's own
        target check runs before this, so reaching ``LIVE`` returns immediately.
        """
        marker = (state.players, state.in_real_map, state.team)
        changed = self._marker is not None and marker != self._marker
        self._marker = marker
        return not changed

    def ensure(self, target: Screen, *, timeout: float = 300.0,
               load_budget: float = 60.0, join_team: int = T,
               verify_movable: bool = False, pause_bots: bool = True) -> GameState:
        """Drive the game into ``target`` (see :data:`TRANSITIONS`).

        Optional assertions on top of the screen:

        ``verify_movable``
            after reaching ``LIVE``, confirm a console ``+forward`` actually moves
            the player (the liveness gate: warmup/freeze/dead all look live).
        ``pause_bots``
            freeze the bots while setting the round up (they otherwise move the
            world under the test).
        """
        from . import awaiting  # local import: awaiting builds on this module

        self._assert_payload_has_rules()
        deadline = time.monotonic() + timeout

        # Pre-flight: the engine must accept queued console lines before any
        # action runs, otherwise the first `exec` takes the game down.
        engine_deadline = min(deadline, time.monotonic() + 90.0)
        while not self._engine_up() and time.monotonic() < engine_deadline:
            time.sleep(1.0)
        if not self._engine_up():
            raise SessionError(
                "the engine never exposed a local player (no main menu) within "
                f"90s — is the game in session 1 and injected? ({self.observe().summary()})"
            )
        while time.monotonic() < deadline:
            state = self.observe()
            if state.screen is target:
                if target is Screen.LIVE and verify_movable:
                    if awaiting.is_movable(self.game):
                        return state
                    self._note("verify_movable", state, "not movable yet — restarting")
                    self._restart_round()
                    continue
                return state

            # Only act on a *settled* screen: a level that is still connecting
            # reports team 0 / no players and looks like the team-select screen.
            if state.screen is not Screen.NOT_READY and not self._settled(state):
                self._note("settle", state, "level still progressing — waiting")
                time.sleep(0.5)
                continue

            if pause_bots and state.in_game:
                self.game.exec("bot_stop 1")

            if state.screen in (Screen.NOT_IN_GAME, Screen.NOT_READY):
                if not self._engine_up():
                    self._note("wait", state, "engine not ready for console lines yet")
                    time.sleep(0.5)
                    continue
                self._note("load_map", state, f"map {self.map_name}")
                if not self._load_map(timeout=max(1.0, deadline - time.monotonic()),
                                      load_budget=load_budget):
                    break
            elif state.screen in (Screen.TEAM_SELECT, Screen.SPECTATOR):
                # A casual/offline server **auto-assigns** the team when the round
                # spawns (the normal end of a map load), so wait for that first and
                # only fall back to `jointeam` (the real team-select screen).
                self._note("wait_team", state, "waiting for the automatic assignment")
                if not self._wait_change((Screen.TEAM_SELECT, Screen.SPECTATOR), timeout=10.0):
                    self._note("join_team", state, f"jointeam {join_team}")
                    self._join_team(join_team)
                    self._wait_change((Screen.TEAM_SELECT, Screen.SPECTATOR), timeout=10.0)
            elif state.screen is Screen.WARMUP:
                self._note("end_warmup", state)
                self._end_warmup()
            elif state.screen is Screen.FREEZE:
                self._note("wait_freeze_end", state)
                self._wait_change((Screen.FREEZE,),
                                  timeout=min(20.0, max(1.0, deadline - time.monotonic())))
            elif state.screen is Screen.ROUND_END:
                self._note("wait_round_start", state)
                self.game.wait_for(event="round_start",
                                   timeout=min(30.0, max(1.0, deadline - time.monotonic())))
            elif state.screen is Screen.DEAD:
                self._note("wait_respawn", state)
                if not self.game.wait_for(local_alive=True,
                                          timeout=min(20.0, max(1.0, deadline - time.monotonic()))
                                          ).get("matched"):
                    self._restart_round()
            elif state.screen is Screen.NOT_READY:
                self._note("wait", state, "no readable state (engine booting / teardown)")
                time.sleep(0.5)
            else:  # pragma: no cover - LIVE handled above
                break

        current = self.state.screen if self.state else Screen.NOT_READY
        raise SessionError(
            f"could not reach {target} within {timeout:.0f}s (at {current}); "
            f"steps: {[entry['action'] for entry in self.log][-6:]}"
        )

    def ensure_live(self, **kwargs: Any) -> GameState:
        """``ensure(Screen.LIVE)`` with the movement liveness gate on by default."""
        kwargs.setdefault("verify_movable", True)
        return self.ensure(Screen.LIVE, **kwargs)
