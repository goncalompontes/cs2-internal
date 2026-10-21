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
``docs/re/client-dll.md`` §"`game_rules"):

=========================  ==========================================================
signal                     meaning
=========================  ==========================================================
``in_game``                **the main-screen rule**: the level's
                           ``C_CSGameRulesProxy`` entity exists. False at the main
                           menu and while a level loads, true from team-select
                           onwards. The ``game_rules`` global is readable at the
                           main menu too (a default object), so ``rules.loaded``
                           alone cannot be the test.
``in_map``                 a local player snapshot exists (true at the main menu too).
``rules.loaded``           the ``game_rules`` global was readable — **true at the
                           main menu as well**; a fact to report, not the level test.
``rules.warmup``           ``m_bWarmupPeriod`` — warmup (no round structure yet).
``rules.freeze``           ``m_bFreezePeriod`` — round started, players frozen.
``rules.round_win_*``      ``m_iRoundWinStatus``/``m_eRoundWinReason`` — a winner is
                           set ⇒ the round is over (buy/loss gap before the next).
``rules.t_cant_buy``       ``m_bTCantBuy`` — the T buy window has closed.
``rules.bomb_planted``     ``m_bBombPlanted`` — the bomb is down.
``rules.game_phase``       ``m_gamePhase`` — the *match* phase (see
                           :data:`GAME_PHASE_HALF_TIME`).
``rules.overtime_playing`` ``m_nOvertimePlaying`` — the match is in overtime.
                           **The payload does not send it yet** (see
                           :data:`PENDING_PAYLOAD_FIELDS`).
``rules.game_paused``      ``m_bGamePaused`` — the game is paused. **The
                           payload does not send it yet** (see
                           :data:`PENDING_PAYLOAD_FIELDS`).
``local.team``             0 = no team (team-select/spectator screen), 1 = spectator,
                           2 = T, 3 = CT.
``local.alive``            ``life_state == 0`` — dead players ignore input.
``local.defusing``         the local player is defusing the bomb.
``local.has_bomb``         the local player carries the C4.
``local.defuser``          the local player carries a defuser.
=========================  ==========================================================

The payload's rules block is the ground truth; the game **event** stream
(``round_start``, ``round_freeze_end``, ``round_end``, ``client_disconnect``, …)
is used as a fallback for a build whose rules are unreadable.

Screens
-------

The screens are mutually exclusive and the **decision order** — the first rule
that matches wins — is the order of the :class:`Screen` enum (and the order
:func:`deduce` applies them in):

1.  ``LOADING``      a level is in flight — the machine asked for a map and the
                     level is not up yet. It is checked **first** because it is
                     the machine's own knowledge: while it holds, neither
                     ``exec`` nor another ``map`` may be issued (the level
                     teardown clears the local state, so a load is otherwise
                     indistinguishable from the main menu).
2.  ``NOT_READY``    no readable target state (engine booting / level teardown).
3.  ``NOT_IN_GAME``  the main screen: no level's rules-proxy entity (``in_game`` false).
4.  ``DISCONNECTED`` the client lost the server: a ``client_disconnect`` event
                     newer than the last round start, while the level is still up.
5.  ``TEAM_SELECT``  level up, no team yet (input ignored; ``jointeam`` moves on).
6.  ``SPECTATOR``    level up, spectating.
7.  ``DEAD``         in a team, not alive — input ignored until respawn.
8.  ``WARMUP``       in a team, before the round structure starts.
9.  ``FREEZE``       round started, freeze period — input ignored until it ends.
10. ``HALF_TIME``    the match is between the halves (``m_gamePhase == 4``).
11. ``OVERTIME``     the match is in overtime (``m_nOvertimePlaying``).
12. ``PAUSED``       the game is paused (``m_bGamePaused``).
13. ``ROUND_END``    a winner is set; movement/shots do nothing until the next round.
14. ``DEFUSING``     the local player is defusing the planted bomb.
15. ``PLANTED``      the bomb is down (the local player is alive).
16. ``BUY_TIME``     the round is running and the buy window is still open.
17. ``LIVE``         playable: alive, in a team, round running, not frozen.

Why the order is what it is
~~~~~~~~~~~~~~~~~~~~~~~~~~~

* **The player fact beats the round fact.** ``DEAD`` (7) sits *before* the
  round-level screens (freeze, round end, planted) **and before the warmup**
  (8): a dead player ignores every input, so "the bomb is down" — or "the
  warmup is running" — is not what a scenario needs to know first. The bomb is
  still reported as a *fact* (``rules.bomb_planted``) and in the notes.
* **The match fact beats the round fact.** ``HALF_TIME``/``OVERTIME``/``PAUSED``
  (10-12) sit before ``ROUND_END`` (13): ``m_iRoundWinStatus`` is *not* reset
  between the halves, so at the halftime the last round's winner is still set —
  reading it as "round end" would hide the halftime for the whole break.
* **The round fact beats the live default.** ``DEFUSING`` (14) is more specific
  than ``PLANTED`` (15) — a defusing player is in a planted round — and both are
  more specific than ``BUY_TIME`` (16), whose buy window is closed once the bomb
  is down anyway.
* **The buy window is its own screen.** ``BUY_TIME`` (16) splits "a running
  round" in two: with the window open the player may still buy, without it the
  round is plain ``LIVE``. A scenario that wants to buy waits for
  ``Screen.BUY_TIME``; a scenario that wants a plain live round waits for
  ``Screen.LIVE`` — and :meth:`Machine.ensure` accepts ``BUY_TIME`` for a
  ``LIVE`` target, because the window may never close (``mp_buytime 9999``).

Screens the payload cannot report yet
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``OVERTIME`` and ``PAUSED`` are read from two fields the **current payload does
not send**: ``m_nOvertimePlaying`` and ``m_bGamePaused``. The deduction is
written against those field names, so the screens light up as soon as the
payload grows them, and until then they are simply never returned (the rules
block has no such key, so the test is false). The payload change each needs is
described in :data:`PENDING_PAYLOAD_FIELDS`.

Capabilities (per screen) and the transitions live in ``CAPABILITIES`` /
``TRANSITIONS`` at the bottom of this file; both are printed by the ``screens``
scenario so the table cannot silently drift from the code.
"""

from __future__ import annotations

import enum
import time
from dataclasses import dataclass
from typing import Any, Callable, Iterable

from .protocol import (
    LevelSnapshotDict,
    PlayerSnapshotDict,
    RulesSnapshotDict,
    StateSnapshotDict,
    Vec3Dict,
)
from .session import Game, SessionError


class Screen(str, enum.Enum):
    """The single screen the game is in.

    The members are declared in the **decision order** (:func:`deduce`): the
    enum order is the documentation of the deduction, and the ``screens``
    scenario prints it.
    """

    LOADING = "loading"
    NOT_READY = "not-ready"
    NOT_IN_GAME = "not-in-game"
    DISCONNECTED = "disconnected"
    TEAM_SELECT = "team-select"
    SPECTATOR = "spectator"
    DEAD = "dead"
    WARMUP = "warmup"
    FREEZE = "freeze"
    HALF_TIME = "half-time"
    OVERTIME = "overtime"
    PAUSED = "paused"
    ROUND_END = "round-end"
    DEFUSING = "defusing"
    PLANTED = "planted"
    BUY_TIME = "buy-time"
    LIVE = "live"

    def __str__(self) -> str:  # pragma: no cover - display only
        return self.value


class Can(str, enum.Enum):
    """Operations a scenario may perform, gated by the current screen."""

    STATE = "state"              # read the snapshot (always safe)
    SCORE = "score"              # read the scoreboard (always safe, like STATE)
    LOOK = "look"                # write view angles (`setang` / sub-tick angles)
    MOVE = "move"                # analog movement (usercmd or console `+forward`)
    JUMP = "jump"
    ATTACK = "attack"            # fire / use a weapon
    RELOAD = "reload"
    BUY = "buy"
    PLANT = "plant"              # plant the bomb (needs the C4 and a plant site)
    DEFUSE = "defuse"            # defuse the planted bomb (needs a defuser)
    DROP = "drop"                # drop the current weapon
    INSPECT = "inspect"          # inspect the current weapon
    SWITCH_WEAPON = "switch-weapon"  # switch to another slot
    GRENADE = "grenade"          # throw a grenade
    TELEPORT = "teleport"        # setpos/noclip (sv_cheats; needs a live pawn)
    RECONNECT = "reconnect"      # reconnect to a server

    def __str__(self) -> str:  # pragma: no cover - display only
        return self.value


#: The capabilities that only *read* the game. They are available in every
#: screen — there is nothing a screen can do to make reading the state or the
#: scoreboard unsafe — so they appear in every row of :data:`CAPABILITIES`.
#: They are listed explicitly (instead of being implied by a "read-only" flag)
#: because the row **is** the documentation the ``screens`` scenario prints:
#: a reader must see that the score is always readable.
READ_ONLY: frozenset[Can] = frozenset({Can.STATE, Can.SCORE})


# Teams per the game's own numbering.
T, CT, SPECTATOR_TEAM, NO_TEAM = 2, 3, 1, 0


#: How long after injection `exec` is assumed safe even if the engine has not
#: exposed a main-menu local player yet (see `Machine._engine_up`). The observed
#: crash was a console line issued within ~2 s of injection.
EXEC_GRACE_MS = 15_000


# --- the match phase (`C_CSGameRules::m_gamePhase`) -------------------------
# The values are confirmed against the shipped client.dll: the client's own
# phase mapper (`FUN_180759960`, which reads `m_gamePhase` at +0x84) sends
# "MatchEnded" for 5 and "HalfTime" for 4. The enum has **no** overtime value —
# CS2 signals overtime with `m_nOvertimePlaying` instead.
GAME_PHASE_WARMUP_ROUND = 0
GAME_PHASE_PLAYING_STANDARD = 1
GAME_PHASE_PLAYING_FIRST_HALF = 2
GAME_PHASE_PLAYING_SECOND_HALF = 3
GAME_PHASE_HALF_TIME = 4
GAME_PHASE_MATCH_ENDED = 5

#: The rules fields the **current payload does not send**, and what each one
#: would give the machine. The deduction already reads them by name, so the
#: screens stay dark until the payload grows the fields.
PENDING_PAYLOAD_FIELDS: dict[str, str] = {
    "overtime_playing": (
        "OVERTIME — `C_CSGameRules::m_nOvertimePlaying` (client.dll +0x90). "
        "The payload's rules block would need a new `int` field read from it."
    ),
    "game_paused": (
        "PAUSED — `CGameRules::m_bGamePaused` (client.dll +0x38, the base "
        "class of the client's `C_CSGameRules`) or the CS-specific "
        "`m_bMatchWaitingForResume` (+0x61). The payload's rules block would "
        "need a new `bool` field read from it."
    ),
}

#: The screens that mean "a level is up" — i.e. everything but the main
#: screen, the load in flight and the disconnected client. The machine uses it
#: to tell "the map loaded" from "the map is still loading".
IN_LEVEL: tuple[Screen, ...] = (
    Screen.TEAM_SELECT, Screen.SPECTATOR, Screen.DEAD, Screen.WARMUP,
    Screen.FREEZE, Screen.HALF_TIME, Screen.OVERTIME, Screen.PAUSED,
    Screen.ROUND_END, Screen.DEFUSING, Screen.PLANTED, Screen.BUY_TIME,
    Screen.LIVE,
)

#: The events that mean "the client lost the server". ``client_disconnect`` is
#: captured by the payload without an explicit subscription
#: (``payload/control/src/events.cpp``), so the screen works out of the box.
DISCONNECT_EVENTS: tuple[str, ...] = ("client_disconnect", "cs_game_disconnected")

#: The events that open a new round (or match). A disconnect *after* one of
#: them belongs to the current round; one before it is history.
ROUND_BOUNDARY_EVENTS: tuple[str, ...] = ("round_start", "begin_new_match")

#: Which capabilities each screen has. Kept as data so the ``screens`` scenario
#: can print it and a test can assert it stays in sync with ``GameState.capabilities``.
#:
#: A row says what the screen *permits*; the runtime facts (the buy window, the
#: C4, the defuser) are checked on top by :meth:`GameState._capability` and are
#: named in the reason.
#:
#: ``buy`` appears in the two rows where the buy window can be open — ``FREEZE``
#: (the pre-round window) and ``BUY_TIME`` (the window of a running round). It
#: is **not** in ``LIVE``: a running round whose window is still open *is* the
#: ``BUY_TIME`` screen, so a ``buy`` there would never be granted.
CAPABILITIES: dict[Screen, tuple[Can, ...]] = {
    Screen.NOT_READY: (Can.STATE, Can.SCORE),
    Screen.NOT_IN_GAME: (Can.STATE, Can.SCORE, Can.RECONNECT),
    Screen.LOADING: (Can.STATE, Can.SCORE),
    Screen.DISCONNECTED: (Can.STATE, Can.SCORE, Can.RECONNECT),
    Screen.TEAM_SELECT: (Can.STATE, Can.SCORE),
    Screen.SPECTATOR: (Can.STATE, Can.SCORE, Can.LOOK),
    Screen.WARMUP: (Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK,
                    Can.RELOAD, Can.DROP, Can.INSPECT, Can.SWITCH_WEAPON,
                    Can.GRENADE, Can.TELEPORT),
    Screen.DEAD: (Can.STATE, Can.SCORE),
    Screen.FREEZE: (Can.STATE, Can.SCORE, Can.LOOK, Can.BUY, Can.TELEPORT),
    Screen.HALF_TIME: (Can.STATE, Can.SCORE, Can.LOOK, Can.TELEPORT),
    Screen.OVERTIME: (Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK,
                      Can.RELOAD, Can.DROP, Can.INSPECT, Can.SWITCH_WEAPON,
                      Can.GRENADE, Can.TELEPORT),
    Screen.PAUSED: (Can.STATE, Can.SCORE, Can.LOOK),
    Screen.ROUND_END: (Can.STATE, Can.SCORE),
    # A defusing player is pinned to the bomb: dropping the weapon or switching
    # away cancels the defuse, so only the reads and the view are left.
    Screen.DEFUSING: (Can.STATE, Can.SCORE, Can.LOOK),
    Screen.PLANTED: (Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK,
                     Can.RELOAD, Can.DROP, Can.INSPECT, Can.SWITCH_WEAPON,
                     Can.GRENADE, Can.DEFUSE, Can.TELEPORT),
    Screen.BUY_TIME: (Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK,
                      Can.RELOAD, Can.DROP, Can.INSPECT, Can.SWITCH_WEAPON,
                      Can.GRENADE, Can.BUY, Can.PLANT, Can.TELEPORT),
    Screen.LIVE: (Can.STATE, Can.SCORE, Can.LOOK, Can.MOVE, Can.JUMP, Can.ATTACK,
                  Can.RELOAD, Can.DROP, Can.INSPECT, Can.SWITCH_WEAPON,
                  Can.GRENADE, Can.PLANT, Can.TELEPORT),
}


@dataclass(frozen=True)
class GameState:
    """One observation of the game, reduced to a screen + capabilities."""

    screen: Screen
    raw: StateSnapshotDict
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
    def level(self) -> LevelSnapshotDict:
        """The level probe block (the payload always writes it)."""
        return self.raw["level"]

    @property
    def rules(self) -> RulesSnapshotDict:
        """The round rules block (the payload always writes it)."""
        return self.raw["rules"]

    @property
    def local(self) -> PlayerSnapshotDict | None:
        """The local player snapshot (``None`` when there is none)."""
        return self.raw.get("local") or None

    @property
    def team(self) -> int:
        local = self.local
        return int(local.get("team") or 0) if local else 0

    @property
    def alive(self) -> bool:
        local = self.local
        return bool(local.get("alive")) if local else False

    @property
    def life_state(self) -> int:
        local = self.local
        return int(local.get("life_state") or 0) if local else 0

    @property
    def on_ground(self) -> bool:
        local = self.local
        return bool(local.get("on_ground")) if local else False

    @property
    def origin(self) -> Vec3Dict | None:
        local = self.local
        return local.get("origin") if local else None

    @property
    def players(self) -> int:
        return len(self.raw.get("players") or [])

    @property
    def in_real_map(self) -> bool:
        """A real level with the local player spawned in it."""
        origin = self.origin
        nonzero = (any(origin.get(axis, 0.0) for axis in ("x", "y", "z"))
                   if origin else False)
        return bool(self.in_game and (nonzero or self.team in (T, CT)))

    @property
    def buy_time(self) -> bool:
        """Whether the local team's buy window is still open (rules-driven)."""
        if not self.in_game or not self.alive:
            return False
        return _buy_window_open(self.rules, self.team)

    @property
    def round_over(self) -> bool:
        """A winner is set (rules) or the newest round event is ``round_end``."""
        return bool(self.rules.get("round_win_status")) or _round_ended(self.events)

    @property
    def bomb_planted(self) -> bool:
        """Whether the bomb is down (``m_bBombPlanted``)."""
        return bool(self.rules.get("bomb_planted"))

    @property
    def defusing(self) -> bool:
        """Whether the local player is defusing the bomb."""
        local = self.local
        return bool(local.get("defusing")) if local else False

    @property
    def has_bomb(self) -> bool:
        """Whether the local player carries the C4."""
        local = self.local
        return bool(local.get("has_bomb")) if local else False

    @property
    def defuser(self) -> bool:
        """Whether the local player carries a defuser."""
        local = self.local
        return bool(local.get("defuser")) if local else False

    @property
    def game_phase(self) -> int:
        """The match phase (``m_gamePhase``)."""
        return int(self.rules.get("game_phase") or 0)

    @property
    def overtime(self) -> bool:
        """Whether the match is in overtime (``m_nOvertimePlaying``).

        The current payload does not send the field — see
        :data:`PENDING_PAYLOAD_FIELDS`.
        """
        return int(self.rules.get("overtime_playing") or 0) > 0

    @property
    def paused(self) -> bool:
        """Whether the game is paused (``m_bGamePaused``).

        The current payload does not send the field — see
        :data:`PENDING_PAYLOAD_FIELDS`.
        """
        return bool(self.rules.get("game_paused"))

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
        if cap is Can.PLANT:
            # Planting needs the C4 in the hand and a bomb that is not down.
            if allowed and not self.can_plant:
                return False, f"{self.screen}: {self._plant_reason()}"
            return allowed, self._reason(cap)
        if cap is Can.DEFUSE:
            # Defusing needs a planted bomb, a defuser and no running defuse.
            if allowed and not self.can_defuse:
                return False, f"{self.screen}: {self._defuse_reason()}"
            return allowed, self._reason(cap)
        return allowed, self._reason(cap)

    @property
    def can_plant(self) -> bool:
        """Whether the local player could plant the bomb right now.

        The *plant site* is not derivable from the payload (the player block
        carries ``in_buy_zone`` but not ``in_bomb_zone``), so the check is
        limited to what the payload reports: the C4 is in hand and the bomb is
        not down yet.
        """
        return self.has_bomb and not self.bomb_planted

    @property
    def can_defuse(self) -> bool:
        """Whether the local player could defuse right now."""
        return self.bomb_planted and self.defuser and not self.defusing

    def _buy_reason(self) -> str:
        team = {T: "t_cant_buy", CT: "ct_cant_buy"}.get(self.team)
        if team is None:
            return "no team"
        return f"{team}={self.rules.get('t_cant_buy' if self.team == T else 'ct_cant_buy')}"

    def _plant_reason(self) -> str:
        if self.bomb_planted:
            return "the bomb is already planted"
        if not self.has_bomb:
            return "the local player does not carry the C4"
        return "ok"

    def _defuse_reason(self) -> str:
        if not self.bomb_planted:
            return "the bomb is not planted"
        if not self.defuser:
            return "the local player has no defuser"
        if self.defusing:
            return "the defuse is already running"
        return "ok"

    def _reason(self, cap: Can) -> str:
        if cap in CAPABILITIES.get(self.screen, ()):
            return f"{self.screen} allows {cap}"
        return f"{self.screen} does not allow {cap}"

    def can(self, capability: Can | str) -> bool:
        cap = self._capability_name(capability)
        allowed, _ = self._capability(cap)
        return allowed

    def why(self, capability: Can | str) -> str:
        cap = self._capability_name(capability)
        allowed, reason = self._capability(cap)
        return ("ok: " if allowed else "blocked: ") + reason

    def require(self, capability: Can | str) -> None:
        """Raise :class:`SessionError` unless ``capability`` is available."""
        if not self.can(capability):
            raise SessionError(f"cannot {capability}: {self.why(capability)} — {self.summary()}")

    def _capability_name(self, capability: Can | str) -> Can:
        """The capability, or a :class:`SessionError` that names the mistake.

        A name the enum does not know is a **caller's** mistake, not an
        internal error: the module's own error says what was asked for and
        what is on offer, so a typo does not come out as a raw ``ValueError``
        from the enum's constructor.
        """
        try:
            return Can(capability)
        except ValueError:
            known = sorted(str(cap) for cap in Can)
            raise SessionError(
                f"unknown capability {capability!r} (known: {known})") from None

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
                f"bomb_planted={self.bomb_planted}",
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
            "bomb_planted": self.bomb_planted,
            "defusing": self.defusing,
            "has_bomb": self.has_bomb,
            "defuser": self.defuser,
            "game_phase": self.game_phase,
            "overtime": self.overtime,
            "paused": self.paused,
            "players": self.players,
            "rules": self.rules,
            "events": list(self.events),
            "notes": list(self.notes),
            "capabilities": {
                str(cap): {"allowed": allowed, "reason": reason}
                for cap, (allowed, reason) in self.capabilities().items()
            },
        }


def _buy_window_open(rules: RulesSnapshotDict, team: int) -> bool:
    """Whether ``team``'s buy window is still open (rules-driven).

    The buy window is per team: ``m_bTCantBuy`` / ``m_bCTCantBuy`` close one
    side's window without touching the other.
    """
    if team == T:
        return not bool(rules.get("t_cant_buy"))
    if team == CT:
        return not bool(rules.get("ct_cant_buy"))
    return False


def _level_settled_up(raw: StateSnapshotDict) -> bool:
    """Whether the *strong* level signals say a level is up.

    The weak signal — "a game event happened" — is deliberately left out: it
    stays true after the level is gone (the event ring keeps the last 64
    events), so it cannot tell "a level is up" from "a level *was* up".
    """
    local = raw.get("local")
    if local:
        origin = local.get("origin")
        if origin is not None and any(origin.get(axis, 0.0)
                                      for axis in ("x", "y", "z")):
            return True
        if int(local.get("team") or 0) in (T, CT):
            return True
    rules = raw["rules"]
    return bool(rules.get("match_started")
                or float(rules.get("game_start_time") or 0.0) > 0.0
                or int(rules.get("total_rounds_played") or 0) > 0)


def _level_up(raw: StateSnapshotDict, events: Iterable[str] = ()) -> bool:
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
    local = raw.get("local")
    if not raw.get("valid") or not local:
        return False
    if _level_settled_up(raw):
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


def _disconnected(events: tuple[str, ...]) -> bool:
    """Whether the client lost the server during the current round.

    Only the events **after** the last round boundary count: the event ring
    keeps 64 events, so a disconnect from an earlier round would otherwise pin
    the screen to "disconnected" forever.
    """
    start = 0
    for position in range(len(events) - 1, -1, -1):
        if events[position] in ROUND_BOUNDARY_EVENTS:
            start = position
            break
    return any(name in DISCONNECT_EVENTS for name in events[start:])


# --- deduction ------------------------------------------------------------
def deduce(raw: StateSnapshotDict, events: Iterable[str | None] = (),
           *, loading: bool = False) -> GameState:
    """Reduce a payload ``state`` snapshot to a :class:`GameState`.

    The order matters and encodes the screens (the module docstring has the
    long form):

    1. a level load is in flight → ``LOADING``;
    2. no readable state → ``NOT_READY`` (engine booting / level teardown);
    3. no level up (``in_game`` false — the main screen) → ``NOT_IN_GAME``;
    4. the server was lost → ``DISCONNECTED``;
    5. no team → ``TEAM_SELECT`` (also the unassigned screen right after a map);
    6. team 1 → ``SPECTATOR``;
    7. warmup → ``WARMUP``;
    8. not alive → ``DEAD`` (input is ignored whatever the phase);
    9. freeze period → ``FREEZE``;
    10. the match is between the halves → ``HALF_TIME``;
    11. the match is in overtime → ``OVERTIME``;
    12. the game is paused → ``PAUSED``;
    13. a winner is set → ``ROUND_END``;
    14. the local player is defusing → ``DEFUSING``;
    15. the bomb is down → ``PLANTED``;
    16. the buy window is open → ``BUY_TIME``;
    17. otherwise → ``LIVE``.
    ``loading`` is the machine's own knowledge: it is set while the machine
    waits for a map it asked for, which is the only way to tell "a level is
    loading" from "the client is at the main menu" (both look like no level).

    ``events`` may carry ``None`` for an event without a name: the names that
    are missing cannot name a screen, so they are dropped here.
    """
    event_names = tuple(name for name in events if name is not None)

    if loading and not _level_up(raw, event_names):
        # A map load is in flight: the teardown cleared the local state, so the
        # level reads as "not up" — the same as the main menu. Without this the
        # machine would read the main screen and issue *another* `map` command,
        # restarting the load forever (observed 2026-10-15).
        #
        # The *weak* level rule is the right one here: it flips back to "a level
        # is up" as soon as the local player exists again, so the screen leaves
        # LOADING exactly when the old code left NOT_IN_GAME — the team-select
        # screen then covers the rest of the spawn, and the machine's settle
        # check waits for the level to stop progressing.
        return GameState(Screen.LOADING, raw, event_names,
                         ("level load in flight",))

    if not raw.get("valid"):
        # The engine has no readable local state yet: it is still booting after
        # injection, or a level teardown is in flight (the `map` command clears
        # `valid` for a moment). The only correct action is to **wait** — `exec`
        # in this state faults inside the engine's Cbuf path.
        return GameState(Screen.NOT_READY, raw, event_names,
                         ("no readable local state — engine booting or level "
                          "teardown; do not exec",))

    local = raw.get("local")
    if not _level_up(raw, event_names):
        notes = ["main screen (no level)"]
        if raw["rules"].get("loaded"):
            notes.append("game_rules global readable — the menu object, not a level")
        level = raw.get("level")
        if level is not None and level.get("up"):
            notes.append("rules-proxy entity exists — the menu world has one too")
        if local:
            origin = local.get("origin")
            x = origin.get("x", 0) if origin else 0
            notes.append(f"local placeholder (origin {x:.0f},"
                         f" team {local.get('team')}, weapon '{local.get('weapon')}')")
        return GameState(Screen.NOT_IN_GAME, raw, event_names, tuple(notes))

    if _disconnected(event_names):
        # The level is still up but the server is gone: the client is about to
        # fall back to the main menu, and the only useful action is to
        # reconnect.
        return GameState(Screen.DISCONNECTED, raw, event_names,
                         ("the client lost the server",))

    team = int(local.get("team") or 0) if local else NO_TEAM
    if team == NO_TEAM:
        return GameState(Screen.TEAM_SELECT, raw, event_names,
                         ("level up, no team yet",))
    if team == SPECTATOR_TEAM:
        return GameState(Screen.SPECTATOR, raw, event_names, ("team=1",))

    rules = raw["rules"]
    alive = bool(local.get("alive")) if local else False
    warmup = bool(rules.get("warmup"))
    freeze = bool(rules.get("freeze"))
    win_status = int(rules.get("round_win_status") or 0)
    phase = int(rules.get("game_phase") or 0)

    if not alive:
        # The player fact beats the round facts: a dead player ignores every
        # input, so "the warmup is running" (or "the bomb is down") is not
        # what a scenario needs to know first.
        return GameState(Screen.DEAD, raw, event_names,
                         (f"life_state={local.get('life_state') if local else 0}",))
    if warmup:
        return GameState(Screen.WARMUP, raw, event_names, ("warmup period",))
    if freeze:
        return GameState(Screen.FREEZE, raw, event_names, ("freeze period",))
    if phase == GAME_PHASE_HALF_TIME:
        # The match phase outranks the round phase: `m_iRoundWinStatus` is not
        # reset between the halves, so the last round's winner is still set
        # during the break.
        return GameState(Screen.HALF_TIME, raw, event_names,
                         (f"game_phase={phase} (between the halves)",))
    if int(rules.get("overtime_playing") or 0) > 0:
        # `m_nOvertimePlaying` — the current payload does not send it yet (see
        # `PENDING_PAYLOAD_FIELDS`), so this never fires with today's payload.
        return GameState(Screen.OVERTIME, raw, event_names,
                         (f"overtime_playing={rules.get('overtime_playing')}",))
    if bool(rules.get("game_paused")):
        # `m_bGamePaused` — the current payload does not send it yet (see
        # `PENDING_PAYLOAD_FIELDS`), so this never fires with today's payload.
        return GameState(Screen.PAUSED, raw, event_names, ("the game is paused",))
    if win_status != 0 or _round_ended(event_names):
        return GameState(Screen.ROUND_END, raw, event_names,
                         (f"winner set (m_iRoundWinStatus={win_status})"
                          if win_status != 0 else "round_end event",))
    defusing = bool(local.get("defusing")) if local else False
    if defusing:
        return GameState(Screen.DEFUSING, raw, event_names, ("the local player is defusing",))
    if bool(rules.get("bomb_planted")):
        return GameState(Screen.PLANTED, raw, event_names, ("the bomb is planted",))
    if _buy_window_open(rules, team):
        return GameState(Screen.BUY_TIME, raw, event_names,
                         ("the buy window is still open",))
    return GameState(Screen.LIVE, raw, event_names, ("round live",))


def observe(game: Game, *, events: int = 6, loading: bool = False) -> GameState:
    """One round-trip: read ``state`` (+ recent events) and deduce the screen."""
    raw = game.state()
    names: list[str] = []
    if events:
        for event in game.events(limit=events).get("events", []):
            name = event.get("name")
            if name:
                names.append(str(name))
    return deduce(raw, names, loading=loading)


# --- transitions ----------------------------------------------------------
@dataclass(frozen=True)
class Transition:
    """One edge of the machine: what to do to leave ``frm`` for ``to``."""

    frm: Screen
    to: Screen
    action: str
    detail: str
    note: str = ""


#: The actions the machine knows. Every action named by a :data:`TRANSITIONS`
#: edge has a ``Machine._do_<action>`` method — the ``test_state_machine``
#: suite pins that, so the table cannot name an action the machine cannot run.
KNOWN_ACTIONS: frozenset[str] = frozenset({
    "wait", "load_map", "join_team", "end_warmup", "restart_round",
    "wait_freeze_end", "wait_round_start", "wait_respawn",
    "wait_load", "wait_bomb_planted", "wait_defuse", "wait_defused",
    "wait_buy_time", "wait_buy_end", "wait_halftime", "wait_overtime",
    "wait_pause", "reconnect", "resume_match",
})


#: The edges. ``action`` names a step :class:`Machine` knows how to run; the
#: ``screens`` scenario prints this table so it stays honest.
TRANSITIONS: tuple[Transition, ...] = (
    Transition(Screen.NOT_IN_GAME, Screen.TEAM_SELECT, "load_map",
               "game_type 0; game_mode 0; map <name>",
               "the rules global is null until a level loads — do not re-issue "
               "while a load is in flight or it restarts forever"),
    Transition(Screen.LOADING, Screen.TEAM_SELECT, "wait_load",
               "wait for the level to come up",
               "the teardown clears the local state, so the load is only visible "
               "as the absence of a level — waiting is the only safe action"),
    Transition(Screen.DISCONNECTED, Screen.LOADING, "reconnect",
               "reconnect",
               "the client lost the server; reconnecting re-enters the loading "
               "phase, so the reconnect edge ends in LOADING"),
    Transition(Screen.TEAM_SELECT, Screen.FREEZE, "join_team", "jointeam <2|3>",
               "input is ignored with no team; a casual server also auto-assigns"),
    Transition(Screen.SPECTATOR, Screen.FREEZE, "join_team", "jointeam <2|3>"),
    Transition(Screen.WARMUP, Screen.FREEZE, "end_warmup",
               "mp_warmup_end; mp_restartgame 1"),
    Transition(Screen.FREEZE, Screen.LIVE, "wait_freeze_end",
               "wait for m_bFreezePeriod to clear (round_freeze_end)"),
    Transition(Screen.LIVE, Screen.BUY_TIME, "wait_buy_time",
               "wait for the buy window to open",
               "the window opens with the round and closes when it expires "
               "(m_bTCantBuy / m_bCTCantBuy)"),
    Transition(Screen.BUY_TIME, Screen.LIVE, "wait_buy_end",
               "wait for the buy window to close"),
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
    Transition(Screen.LIVE, Screen.PLANTED, "wait_bomb_planted",
               "wait for m_bBombPlanted (or the bomb_planted event)"),
    Transition(Screen.PLANTED, Screen.DEFUSING, "wait_defuse",
               "wait for the local player to start defusing"),
    Transition(Screen.DEFUSING, Screen.ROUND_END, "wait_defused",
               "wait for the bomb_defused event",
               "the defuse completes the round — the round end comes with it"),
    Transition(Screen.PLANTED, Screen.LIVE, "wait_round_start",
               "wait for the next round_start",
               "the post-plant restart: the round ends (the bomb exploded or was "
               "defused) and the next round_start opens the next round"),
    Transition(Screen.LIVE, Screen.HALF_TIME, "wait_halftime",
               "wait for m_gamePhase == 4 (or the start_halftime event)"),
    Transition(Screen.HALF_TIME, Screen.LIVE, "wait_round_start",
               "wait for the next round_start (the second half)"),
    Transition(Screen.LIVE, Screen.OVERTIME, "wait_overtime",
               "wait for m_nOvertimePlaying",
               "needs the payload's `overtime_playing` field "
               "(PENDING_PAYLOAD_FIELDS) — with today's payload the edge never "
               "fires"),
    Transition(Screen.OVERTIME, Screen.LIVE, "wait_round_start",
               "wait for the next round_start (the overtime round)"),
    Transition(Screen.LIVE, Screen.PAUSED, "wait_pause",
               "wait for m_bGamePaused",
               "needs the payload's `game_paused` field (PENDING_PAYLOAD_FIELDS) "
               "— with today's payload the edge never fires"),
    Transition(Screen.PAUSED, Screen.LIVE, "resume_match",
               "unpausematch", "the match is paused; resume it to play on"),
)

#: Screens a scenario can drive the game into with :meth:`Machine.ensure`.
TARGETS: tuple[Screen, ...] = (Screen.NOT_IN_GAME, Screen.LOADING,
                               Screen.DISCONNECTED, Screen.TEAM_SELECT,
                               Screen.WARMUP, Screen.FREEZE, Screen.BUY_TIME,
                               Screen.PLANTED, Screen.LIVE)


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
        #: Whether a map load the machine asked for is still in flight. This is
        #: the machine's own knowledge — the payload has no "loading" flag.
        self._loading = False

    # --- observation ------------------------------------------------------
    def observe(self, *, events: int = 6) -> GameState:
        state = observe(self.game, events=events, loading=self._loading)
        if self._loading and state.screen is not Screen.LOADING:
            # The level came up (or the engine is back): the load is over.
            self._loading = False
        self.state = state
        return state

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
        a launch/injection (``docs/handoff.md`` §"Live validation notes"), so every
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

    def _wait_change(self, screens: tuple[Screen, ...], timeout: float) -> bool:
        """Wait until the screen is no longer any of ``screens``."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.observe().screen
            if screen not in screens:
                return True
            time.sleep(0.5)
        return False

    def _wait_until(self, predicate: Callable[[GameState], bool],
                    timeout: float, poll: float = 0.25) -> bool:
        """Wait until ``predicate`` holds for the observed state."""
        deadline = time.monotonic() + timeout
        while True:
            state = self.observe()
            if predicate(state):
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(poll)

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

    # --- the actions ------------------------------------------------------
    # Every action is *safe*: it checks that it applies to the current screen
    # before it touches the game, so a mistimed call cannot fire a console line
    # into a state where it does nothing (or worse, faults).
    def _do_wait(self, timeout: float) -> bool:
        """Wait for the engine to expose a readable state (never ``exec``)."""
        return self._wait_until(lambda state: state.valid, timeout)

    def _do_load_map(self, timeout: float, load_budget: float) -> bool:
        """Issue ``map`` and wait for a level, never re-issued mid-load.

        A ``map`` command drops the client out of the level and loads the next
        one: re-issuing while a load is in flight **restarts** the load, so a load
        slower than the re-issue window never completes (observed 2026-10-15).
        Each attempt gets ``load_budget`` seconds and the budget is refreshed
        while the state still shows progress (players/events changing).
        """
        if self.state is not None and self.state.screen not in (
                Screen.NOT_IN_GAME, Screen.NOT_READY, Screen.LOADING):
            return False  # a level is up — loading another one is not the action
        deadline = time.monotonic() + timeout
        markers: tuple[int, int, bool] | None = None
        while time.monotonic() < deadline:
            if not self._engine_up():
                time.sleep(0.5)
                continue
            self.game.exec("sv_cheats 1")
            self.game.exec(f"game_type 0; game_mode 0; map {self.map_name}")
            self._loading = True
            attempt_end = min(deadline, time.monotonic() + load_budget)
            markers = self._progress_markers()
            while time.monotonic() < attempt_end:
                state = self.observe()
                # The level is up when the screen leaves the "no level" family —
                # `LOADING` is part of it (the load is in flight).
                if state.screen in IN_LEVEL:
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

    def _do_join_team(self, team: int) -> bool:
        """Join a team (``jointeam``) — only when there is no team yet."""
        if self.state is not None and self.state.team not in (NO_TEAM, SPECTATOR_TEAM):
            return False
        if not self._engine_up():
            return False
        self.game.exec("sv_cheats 1")
        self.game.exec(f"jointeam {team}")
        # `jointeam` without an argument picks a team; with one it may need the
        # follow-up in this build, so also try the bare form.
        self.game.exec("jointeam")
        return True

    def _do_end_warmup(self) -> bool:
        """End the warmup — only while the warmup period is running."""
        if self.state is not None and self.state.screen is not Screen.WARMUP:
            return False
        if not self._engine_up():
            return False
        self.game.exec("sv_cheats 1")
        self.game.exec("mp_warmup_end")
        self.game.exec("mp_freezetime 0")
        self.game.exec("mp_restartgame 1")
        return True

    def _do_restart_round(self) -> bool:
        """Re-roll the round (``mp_restartgame 1``) — only inside a level."""
        if self.state is not None and self.state.screen not in IN_LEVEL:
            return False
        if not self._engine_up():
            return False
        self.game.exec("sv_cheats 1")
        self.game.exec("mp_freezetime 0")
        self.game.exec("mp_restartgame 1")
        return True

    def _do_wait_freeze_end(self, timeout: float) -> bool:
        """Wait for the freeze period to clear."""
        return self._wait_change((Screen.FREEZE,), timeout)

    def _do_wait_round_start(self, timeout: float) -> bool:
        """Wait for the next ``round_start``.

        The same action covers the post-plant restart: after the bomb explodes
        (or is defused) the next round opens with a ``round_start``.
        """
        return bool(self.game.wait_for(event="round_start", timeout=timeout).get("matched"))

    def _do_wait_respawn(self, timeout: float) -> bool:
        """Wait for the respawn; fall back to a round restart."""
        if self.game.wait_for(local_alive=True, timeout=timeout).get("matched"):
            return True
        return self._do_restart_round()

    def _do_wait_load(self, timeout: float) -> bool:
        """Wait for the level to come up (never ``exec``)."""
        return self._wait_change((Screen.LOADING, Screen.NOT_READY,
                                  Screen.NOT_IN_GAME), timeout)

    def _do_wait_bomb_planted(self, timeout: float) -> bool:
        """Wait for the bomb to be planted."""
        if self.state is not None and self.state.bomb_planted:
            return True
        return bool(self.game.wait_for(event="bomb_planted", timeout=timeout).get("matched"))

    def _do_wait_defuse(self, timeout: float) -> bool:
        """Wait for the local player to start defusing."""
        return self._wait_until(lambda state: state.defusing, timeout)

    def _do_wait_defused(self, timeout: float) -> bool:
        """Wait for the defuse to finish."""
        if self.state is not None and not self.state.bomb_planted:
            return True  # the bomb is gone — the defuse is over
        return bool(self.game.wait_for(event="bomb_defused", timeout=timeout).get("matched"))

    def _do_wait_buy_time(self, timeout: float) -> bool:
        """Wait for the buy window to open."""
        return self._wait_until(lambda state: state.buy_time, timeout)

    def _do_wait_buy_end(self, timeout: float) -> bool:
        """Wait for the buy window to close."""
        return self._wait_until(lambda state: not state.buy_time, timeout)

    def _do_wait_halftime(self, timeout: float) -> bool:
        """Wait for the match to reach the halftime."""
        return self._wait_until(
            lambda state: state.game_phase == GAME_PHASE_HALF_TIME, timeout)

    def _do_wait_overtime(self, timeout: float) -> bool:
        """Wait for the match to go into overtime.

        Needs the payload's ``overtime_playing`` field (see
        :data:`PENDING_PAYLOAD_FIELDS`).
        """
        return self._wait_until(lambda state: state.overtime, timeout)

    def _do_wait_pause(self, timeout: float) -> bool:
        """Wait for the game to be paused.

        Needs the payload's ``game_paused`` field (see
        :data:`PENDING_PAYLOAD_FIELDS`).
        """
        return self._wait_until(lambda state: state.paused, timeout)

    def _do_reconnect(self) -> bool:
        """Reconnect to the server — only from a disconnected (or menu) screen."""
        if self.state is not None and self.state.screen not in (
                Screen.DISCONNECTED, Screen.NOT_IN_GAME):
            return False
        if not self._engine_up():
            return False
        self.game.exec("reconnect")
        return True

    def _do_resume_match(self) -> bool:
        """Resume a paused match — only while the game is paused."""
        if self.state is not None and self.state.screen is not Screen.PAUSED:
            return False
        if not self._engine_up():
            return False
        self.game.exec("unpausematch")
        return True

    # --- driving ----------------------------------------------------------
    def _reached(self, target: Screen, state: GameState) -> bool:
        """Whether ``state`` satisfies the ``target`` request.

        ``LIVE`` is satisfied by ``BUY_TIME``: a running round whose buy window
        is still open *is* a live round, and the window may never close (the
        playground convars set ``mp_buytime 9999``), so demanding the exact
        ``LIVE`` screen would make ``ensure(LIVE)`` time out there.
        """
        if state.screen is target:
            return True
        return target is Screen.LIVE and state.screen is Screen.BUY_TIME

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
            if self._reached(target, state):
                if target is Screen.LIVE and verify_movable:
                    if awaiting.is_movable(self.game):
                        return state
                    self._note("verify_movable", state, "not movable yet — restarting")
                    self._do_restart_round()
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

            remaining = max(1.0, deadline - time.monotonic())
            if state.screen in (Screen.NOT_IN_GAME, Screen.NOT_READY):
                if not self._engine_up():
                    self._note("wait", state, "engine not ready for console lines yet")
                    time.sleep(0.5)
                    continue
                self._note("load_map", state, f"map {self.map_name}")
                if not self._do_load_map(timeout=remaining, load_budget=load_budget):
                    break
            elif state.screen is Screen.LOADING:
                self._note("wait_load", state, "waiting for the level")
                if not self._do_wait_load(timeout=min(load_budget, remaining)):
                    # The load stalled for a whole budget: re-issue the map, the
                    # same retry the machine does from the main screen (the
                    # attempt window is refreshed while the load progresses, so
                    # a slow load is never restarted).
                    self._note("load_map", state, f"map {self.map_name} (the load stalled)")
                    if not self._do_load_map(timeout=remaining, load_budget=load_budget):
                        break
            elif state.screen is Screen.DISCONNECTED:
                self._note("reconnect", state, "the client lost the server")
                self._do_reconnect()
                self._wait_change((Screen.DISCONNECTED,), timeout=10.0)
            elif state.screen in (Screen.TEAM_SELECT, Screen.SPECTATOR):
                # A casual/offline server **auto-assigns** the team when the round
                # spawns (the normal end of a map load), so wait for that first and
                # only fall back to `jointeam` (the real team-select screen).
                self._note("wait_team", state, "waiting for the automatic assignment")
                if not self._wait_change((Screen.TEAM_SELECT, Screen.SPECTATOR), timeout=10.0):
                    self._note("join_team", state, f"jointeam {join_team}")
                    self._do_join_team(join_team)
                    self._wait_change((Screen.TEAM_SELECT, Screen.SPECTATOR), timeout=10.0)
            elif state.screen is Screen.WARMUP:
                self._note("end_warmup", state)
                self._do_end_warmup()
            elif state.screen is Screen.FREEZE:
                self._note("wait_freeze_end", state)
                self._do_wait_freeze_end(timeout=min(20.0, remaining))
            elif state.screen is Screen.ROUND_END:
                self._note("wait_round_start", state)
                self._do_wait_round_start(timeout=min(30.0, remaining))
            elif state.screen is Screen.PLANTED:
                self._note("wait_round_start", state, "the bomb is down — waiting for the round end")
                self._do_wait_round_start(timeout=min(30.0, remaining))
            elif state.screen is Screen.DEFUSING:
                self._note("wait_defused", state)
                self._do_wait_defused(timeout=min(30.0, remaining))
            elif state.screen is Screen.BUY_TIME:
                self._note("wait_buy_end", state, "the buy window is still open")
                self._do_wait_buy_end(timeout=min(30.0, remaining))
            elif state.screen is Screen.HALF_TIME:
                self._note("wait_round_start", state, "the halftime — waiting for the second half")
                self._do_wait_round_start(timeout=min(30.0, remaining))
            elif state.screen is Screen.OVERTIME:
                self._note("wait_round_start", state, "overtime — waiting for the round")
                self._do_wait_round_start(timeout=min(30.0, remaining))
            elif state.screen is Screen.PAUSED:
                self._note("resume_match", state, "the match is paused")
                self._do_resume_match()
            elif state.screen is Screen.DEAD:
                self._note("wait_respawn", state)
                self._do_wait_respawn(timeout=min(20.0, remaining))
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

    def ensure_team(self, team: int = T, *, timeout: float = 30.0) -> bool:
        """Get the local player onto a team — as early as the screen allows.

        The **only** way into a team is this machine's own ``join_team``
        transition (``TEAM_SELECT``/``SPECTATOR`` → ``FREEZE``, the
        ``jointeam <2|3>`` edge), so a caller that needs a team *early* — the
        bot match joins before its convars — uses this instead of writing its
        own ``jointeam`` line: the team choice stays in one place.

        A casual/offline server **auto-assigns** the team when the round
        spawns, so the player often has one already; that is not a failure and
        the caller can say so (the bot match's ``team_joined``).

        Returns ``True`` when this call's ``jointeam`` put the player on a team
        and ``False`` when the player already had one. Raises
        :class:`SessionError` when no team can be given.
        """
        deadline = time.monotonic() + timeout
        while True:
            state = self.observe()
            if state.team in (T, CT):
                return False  # the team was already there — the server assigned it
            if state.screen in (Screen.TEAM_SELECT, Screen.SPECTATOR):
                break
            if time.monotonic() >= deadline:
                raise SessionError(
                    f"cannot join a team from the {state.screen} screen "
                    f"({state.summary()}) — a team can only be joined from the "
                    "team-select or the spectator screen")
            time.sleep(0.5)
        self._note("join_team", state, f"jointeam {team}")
        if not self._do_join_team(team):
            raise SessionError(
                f"the {state.screen} screen refused the team join "
                f"({state.summary()})")
        remaining = max(1.0, deadline - time.monotonic())
        if not self._wait_until(lambda observed: observed.team in (T, CT), remaining):
            current = self.state.screen if self.state else Screen.NOT_READY
            raise SessionError(
                f"the local player did not join a team within {remaining:.0f}s "
                f"(at {current})")
        return True
