"""Readiness helpers for scenarios — map, round, alive and movable state.

The payload can only be driven usefully once the game is in a *started* round
with an alive, movable local player. Getting there is fiddly (the game boots to
the main menu, ``mp_restartgame`` leads to a warmup then a freeze period, a dead
player ignores input, ...), so the sequence lives here once instead of being
re-derived in every scenario.

The **screen state machine** lives in :mod:`.screens`: it reduces the payload
state to one screen (main menu / team-select / warmup / freeze / live / dead /
round-end), says which capabilities that screen has, and drives the transitions.
These helpers are the thin, backwards-compatible face of it — ``screen``,
``state``, ``require`` and ``ensure`` are the machine-first API that new
scenarios should use::

    from . import awaiting

    awaiting.ensure(game, Screen.LIVE)        # drive the machine to a live round
    game.require("move")                      # fail with the *reason* if not live
    result = awaiting.measure_move(game, via="usercmd", key="w", hold=1.0)

All helpers take the ``Game`` session and raise :class:`~.session.SessionError`
on a hard failure (or return ``False`` for the ``wait_*`` predicates/timeouts).
"""

from __future__ import annotations

import math
import time
from typing import Any

from . import screens
from .control import ControlError
from .session import Game, SessionError

DEFAULT_MAP = "de_dust2"
# A console `+forward` (or a working usercmd `forward`) moves well over this.
DEFAULT_MIN_UNITS = 40.0
# Frames are ~1/64 s; poll a little faster than a command round-trip.
_POLL_S = 0.05


# --- the state machine ----------------------------------------------------
def state(game: Game, *, events: int = 6) -> screens.GameState:
    """The deduced screen + capabilities for the current game state."""
    return screens.observe(game, events=events)


def screen(game: Game) -> screens.Screen:
    """The current :class:`~.screens.Screen`."""
    return state(game).screen


def require(game: Game, capability: screens.Can | str) -> screens.GameState:
    """Assert a capability is available now, with a precise reason if not."""
    observation = state(game)
    observation.require(capability)
    return observation


def machine(game: Game, *, map_name: str = DEFAULT_MAP,
            debug: bool = True) -> screens.Machine:
    """A :class:`~.screens.Machine` bound to this session."""
    return screens.Machine(game, map_name=map_name, debug=debug)


def ensure(game: Game, target: screens.Screen, *, map_name: str = DEFAULT_MAP,
           **kwargs: Any) -> screens.GameState:
    """Drive the game into ``target`` (see :class:`~.screens.Machine`)."""
    return screens.Machine(game, map_name=map_name).ensure(target, **kwargs)


# --- state readers --------------------------------------------------------
def local(game: Game) -> dict[str, Any]:
    """The local player snapshot (empty dict when there is none)."""
    return game.state().get("local") or {}


def origin(game: Game) -> dict[str, float] | None:
    """The local player's world origin, or ``None``."""
    return local(game).get("origin")


def on_ground(game: Game) -> bool:
    """Whether the local player is on the ground (defaults ``True`` when unknown)."""
    return bool(local(game).get("on_ground", True))


def alive(game: Game) -> bool:
    return bool(local(game).get("alive", False))


def in_map(game: Game) -> bool:
    """The payload's ``in_map`` (a local player exists) — **true at the main menu**."""
    return bool(game.state().get("in_map", False))


def in_real_map(game: Game) -> bool:
    """A local player in an actual level (not the main-menu placeholder).

    The menu exposes a local with origin ``(0,0,0)`` and team ``0``; a real map
    has a non-zero origin or a team (2 = T, 3 = CT).
    """
    player = local(game)
    if not player:
        return False
    origin = player.get("origin") or {}
    nonzero = any(origin.get(axis, 0.0) for axis in ("x", "y", "z"))
    return nonzero or player.get("team") in (2, 3)


def displacement(a: dict[str, float], b: dict[str, float]) -> float:
    """Horizontal distance between two origins (ignores Z)."""
    return math.hypot(b["x"] - a["x"], b["y"] - a["y"])


def playable(game: Game) -> bool:
    """True when the local player is alive *and* on a team (T/CT).

    False on the **team-select screen** (``team == 0``), as a spectator
    (``team == 1``) or when dead — the states in which input is ignored.
    """
    player = local(game)
    return bool(player.get("alive")) and player.get("team") in (2, 3)


def wait_playable(game: Game, timeout: float = 60.0) -> bool:
    """Wait until :func:`playable` holds (payload-side ``playable`` predicate)."""
    return bool(game.wait_for(playable=True, timeout=timeout).get("matched"))


# --- waiting --------------------------------------------------------------
def wait_on_ground(game: Game, timeout: float = 5.0) -> bool:
    """Wait until the local player is grounded. Returns ``matched``."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if on_ground(game):
            return True
        time.sleep(_POLL_S)
    return on_ground(game)


def wait_airborne(game: Game, timeout: float = 5.0) -> bool:
    """Wait until the local player leaves the ground. Returns ``matched``."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not on_ground(game):
            return True
        time.sleep(_POLL_S)
    return not on_ground(game)


def wait_alive(game: Game, timeout: float = 60.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if alive(game):
            return True
        time.sleep(0.25)
    return alive(game)


def wait_round_started(game: Game, timeout: float = 120.0) -> bool:
    """Wait for ``round_start`` *and* ``round_freeze_end`` (a live, unfrozen round)."""
    started = game.wait_for(event="round_start", timeout=timeout).get("matched")
    unfrozen = game.wait_for(event="round_freeze_end", timeout=timeout).get("matched")
    return bool(started and unfrozen)


# --- establishing a usable session ---------------------------------------
# A level load is slow — measured ~22 s from `map` to a spawned player on a cold
# start with `-vulkan` + the overlay (2026-10-15) — and **re-issuing `map`
# restarts the load**. A re-issue window shorter than the load therefore
# livelocks: the load never completes and the loop re-issues forever. Wait out a
# whole load budget, extended while the level is still making progress, and only
# re-issue when nothing has changed for the full budget.
LOAD_BUDGET = 60.0


def ensure_map(game: Game, map_name: str = DEFAULT_MAP, timeout: float = 180.0,
               load_budget: float = LOAD_BUDGET) -> None:
    """Load ``map_name`` (a casual offline match) if no real map is loaded yet.

    Thin wrapper over :meth:`screens.Machine.ensure` for ``TEAM_SELECT``: a
    ``map`` command drops the client out of the level and loads the next one, so
    re-issuing it while a load is in flight **restarts** the load (a load slower
    than the re-issue window then never finishes — observed live 2026-10-15).
    ``load_budget`` is how long one attempt may take without progress before the
    command is re-issued.
    """
    if in_real_map(game):
        return
    state_ = screens.Machine(game, map_name=map_name).ensure(
        screens.Screen.TEAM_SELECT, timeout=timeout, load_budget=load_budget)
    if not state_.in_game:
        raise SessionError(f"map {map_name!r} did not load within {timeout:.0f}s")


def is_movable(game: Game, *, hold: float = 1.0, min_units: float = DEFAULT_MIN_UNITS) -> bool:
    """True when an engine ``+forward`` actually moves the local player.

    This is the liveness gate: a dead player, the main menu, warmup/freeze and a
    not-yet-simulating server all look like "input works" but produce no motion.
    """
    before = origin(game)
    game.exec("+forward")
    time.sleep(hold)
    game.exec("-forward")
    after = origin(game)
    if before is None or after is None:
        return False
    return displacement(before, after) >= min_units


def wait_movable(game: Game, timeout: float = 60.0, *,
                 hold: float = 1.0, min_units: float = DEFAULT_MIN_UNITS) -> bool:
    """Wait until the local player can move (see :func:`is_movable`)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if is_movable(game, hold=hold, min_units=min_units):
            return True
        time.sleep(1.0)
    return False


def ensure_live_round(game: Game, *, map_name: str = DEFAULT_MAP, pause_bots: bool = True,
                      timeout: float = 300.0, verify_movable: bool = True) -> screens.GameState:
    """Bring the game to a started round with an alive, movable local player.

    Delegates to the state machine: it loads ``map_name`` if no level is up,
    joins a team, ends warmup/freeze, restarts the round, and waits for
    ``LIVE`` — optionally verifying that a console ``+forward`` actually moves
    the player (``verify_movable``), which is the liveness gate that catches a
    not-yet-simulating server. Raises :class:`SessionError` on timeout.
    """
    return screens.Machine(game, map_name=map_name).ensure(
        screens.Screen.LIVE, timeout=timeout, pause_bots=pause_bots,
        verify_movable=verify_movable)


def round_state(game: Game, *, events: int = 8) -> dict[str, Any]:
    """A compact snapshot of the local/round state for diagnostics.

    Use it to see *why* an input test might be ignored: a dead player, no team
    (spectator / team-select), the warmup/freeze period, no weapon, etc.
    ``screen`` is the state machine's deduction, ``capabilities`` says which
    operations are available (and why not), and ``recent_events`` shows the tail
    of the game-event stream (``round_start`` / ``round_freeze_end`` /
    ``round_end``).
    """
    snapshot = game.state()
    local = snapshot.get("local") or {}
    names = [e.get("name") for e in game.events(limit=events).get("events", [])]
    observation = screens.deduce(snapshot, names)
    blocked = {
        str(cap): reason
        for cap, (allowed, reason) in observation.capabilities().items()
        if not allowed
    }
    return {
        "screen": str(observation.screen),
        "valid": snapshot.get("valid"),
        "in_game": snapshot.get("in_game"),
        "in_map": snapshot.get("in_map"),
        "alive": local.get("alive"),
        "team": local.get("team"),
        "life_state": local.get("life_state"),
        "on_ground": local.get("on_ground"),
        "weapon": local.get("weapon"),
        "clip": local.get("clip"),
        "players": len(snapshot.get("players", [])),
        "rules": snapshot.get("rules") or {},
        "blocked": blocked,
        "recent_events": names[-6:],
    }


# --- measurement ----------------------------------------------------------
def _spot_distance(a: dict[str, float] | None, b: dict[str, float] | None) -> float:
    """Horizontal distance between an origin and a target spot (``inf`` if nil)."""
    if not a or not b:
        return float("inf")
    return math.hypot(a.get("x", 0.0) - b.get("x", 0.0), a.get("y", 0.0) - b.get("y", 0.0))


def recenter(game: Game, point: dict[str, float] | None, *, tolerance: float = 2.0,
             timeout: float = 3.0) -> dict[str, Any]:
    """Teleport the player to ``point`` and wait for the move to **land**.

    ``setpos`` is queued and applied on a later frame, so reading the origin
    straight afterwards returns the *old* position: that race made the 2026-10-15
    matrix report stale ``before`` positions and mix teleports into the
    displacements. The engine may also resolve the request to a nearby *standing*
    spot (30-40 units off in that run), so success is "the position has stopped
    changing" rather than an exact match, and the achieved distance is reported.
    The walk is always measured relative to where it actually starts.

    Returns ``{applied, distance, origin}``.
    """
    if point is None:
        return {"applied": False, "distance": None, "origin": origin(game)}
    start = origin(game)
    if _spot_distance(start, point) <= tolerance:
        return {"applied": True, "distance": 0.0, "origin": start}
    game.teleport(point["x"], point["y"], point.get("z"))
    deadline = time.monotonic() + timeout
    previous = start
    stable = 0
    while time.monotonic() < deadline:
        current = origin(game)
        if (current is not None and previous is not None
                and _spot_distance(current, previous) <= 0.5):
            stable += 1
            if stable >= 3:  # ~150 ms without a change: the teleport has landed
                return {"applied": True,
                        "distance": round(_spot_distance(current, point), 1),
                        "origin": current}
        else:
            stable = 0
        previous = current
        time.sleep(0.05)
    current = origin(game)
    return {"applied": False,
            "distance": round(_spot_distance(current, point), 1) if current else None,
            "origin": current}


def speed_2d(game: Game) -> float:
    """The local player's horizontal speed (``0`` when unknown)."""
    velocity = (local(game).get("velocity") or {})
    return math.hypot(velocity.get("x", 0.0), velocity.get("y", 0.0))


def settle(game: Game, *, timeout: float = 3.0, max_speed: float = 5.0,
           require_ground: bool = True, poll: float = 0.05) -> bool:
    """Wait until the local player is standing still (and grounded).

    A walk leaves momentum behind and a teleport does **not** clear velocity, so
    without this wait the next measurement starts with the previous test's motion
    bleeding into it (and a player walked off a ledge keeps falling, which also
    ruins the horizontal reading). Returns whether it settled.
    """
    deadline = time.monotonic() + timeout
    while True:
        player = local(game)
        velocity = player.get("velocity") or {}
        still = math.hypot(velocity.get("x", 0.0), velocity.get("y", 0.0)) <= max_speed
        grounded = bool(player.get("on_ground")) if require_ground else True
        if still and grounded:
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(poll)


def prepare(game: Game, spot: dict[str, float] | None = None, *,
            timeout: float = 3.0) -> dict[str, Any]:
    """Recentre to ``spot`` and wait for the player to be still and grounded.

    The pair is what makes a measurement start from a known, quiet state: position
    (``recenter``, waiting for the teleport to land) **and** zero velocity
    (``settle``, so a previous walk's momentum does not bleed into the next
    reading). Returns ``{applied, distance, settled, speed, origin}`` so a caller
    can see when a reading was taken from a moving or off-target start.
    """
    moved = recenter(game, spot)
    settled = settle(game, timeout=timeout)
    return {"applied": moved.get("applied"), "distance": moved.get("distance"),
            "settled": settled, "speed": round(speed_2d(game), 2),
            "origin": origin(game)}


def pick_clear_heading(game: Game, *, headings: int = 8, hold: float = 0.5,
                       pitch: float = 0.0, home: dict[str, float] | None = None,
                       require: str | None = "move") -> dict[str, Any]:
    """Find the yaw with the most clearance and aim there.

    A movement test is only meaningful if the player can actually walk: a spawn
    corner, a wall or a prop turns "the write did nothing" into "the walk was
    blocked" (the exact ambiguity that cost a run on 2026-10-15). This sweeps
    ``headings`` evenly-spaced yaws, holds the engine's own console ``+forward``
    for ``hold`` seconds at each, and keeps the one that moved furthest — so the
    subsequent ``usercmd`` measurement starts from open space whatever the map.

    Every heading starts from **the same spot**: ``home`` when given (the caller's
    fixed test position — e.g. the operator's chosen open area), otherwise the
    position the sweep started in. ``require`` is re-checked per heading: a sweep
    measured during freeze or round-end is worthless (freeze ignores input *and*
    resets view angles, so `setang` never converges) — those headings are recorded
    with the reason and skipped. The sweep also stops once the player dies.

    Returns ``{yaw, moved, best, all, readings, home}`` (``moved`` is the best
    displacement in units, ``readings`` how many headings produced one); the best
    yaw is left applied via ``setang``.
    """
    step = 360.0 / max(1, headings)
    if home is None:
        home = origin(game)
    results: list[dict[str, Any]] = []
    for index in range(max(1, headings)):
        yaw = (step * index) % 360.0
        try:
            if not alive(game):
                results.append({"yaw": yaw, "moved": 0.0, "error": "player died"})
                break
            if require is not None:
                observation = screens.observe(game)
                if not observation.can(require):
                    results.append({"yaw": yaw, "moved": 0.0,
                                    "error": observation.why(require)})
                    continue
            recenter(game, home)
            settle(game)
            game.look(yaw=yaw, pitch=pitch)
            before = origin(game)
            game.press("w", hold=hold)  # console `+forward`, the known-good path
            after = origin(game)
            moved = displacement(before, after) if (before and after) else 0.0
            results.append({"yaw": yaw, "moved": round(moved, 1)})
        except (SessionError, ControlError) as error:
            # A dead player ignores `setang` and a live round can kill us mid-sweep;
            # that heading simply has no measurement.
            results.append({"yaw": yaw, "moved": 0.0, "error": str(error)[:120]})
    if not results:
        raise SessionError("clearance sweep produced no measurements (player dead?)")
    best = max(results, key=lambda item: item["moved"])
    readings = sum(1 for item in results if "error" not in item)
    game.look(yaw=best["yaw"], pitch=pitch)
    return {"yaw": best["yaw"], "moved": best["moved"], "best": best, "all": results,
            "readings": readings, "home": home}


def measure_move(game: Game, *, key: str = "w", hold: float = 1.0,
                 via: str | None = None, yaw: float | None = None,
                 pitch: float | None = 0.0) -> dict[str, Any]:
    """Hold ``key`` for ``hold`` seconds and return the horizontal displacement.

    ``via`` selects the backend (``None`` = the engine console, ``"usercmd"`` =
    the payload's `CreateMove` hook). ``yaw``/``pitch`` optionally ``setang`` the
    view first so the movement axis is unambiguous. Returns
    ``{before, after, moved}``.
    """
    if yaw is not None or pitch is not None:
        game.look(yaw=yaw, pitch=pitch)
    before = origin(game)
    game.press(key, hold=hold, via=via)
    after = origin(game)
    moved = displacement(before, after) if (before and after) else 0.0
    return {"before": before, "after": after, "moved": round(moved, 1)}
