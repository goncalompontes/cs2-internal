"""Scenario: probe the sub-tick / analog movement seams.

``subtick-move`` asserts the *historic* write (raw axes + command floats +
buttons) moves the player — and it does not. This scenario is the diagnostic
counterpart: it first reads back **what the engine's own input writes** (hold the
console ``+forward``/``+jump`` and dump the command's base has-bits/move floats,
the raw axes ``CCSGOInput + 0x270``, the per-controller input entry and the
command's own ``CSubtickMoveStep`` list), then tries each ingredient of the
community movement recipe in turn and reports the displacement of each:

| variant | write |
|---|---|
| ``plain`` | the historic write (raw axes + command floats + both button states) |
| ``quantize`` | ... snapped to the ``sv_quantize_movement_input`` valid set |
| ``quantize+step`` | ... plus the residual on the sub-tick analog channel |
| ``raw_entry`` | ... plus the per-controller input entry ``+0x270`` is derived from |
| ``all`` | all three |

``quantize`` also sets the server-side ``sv_quantize_movement_input`` cvar
(``--set quantize=1``) so the same matrix can be compared with and without
server-side rounding; ``sv_subtick_movement_view_angles`` is forced off so the
sub-tick view angles cannot steer movement and confuse the result
(``docs/re/subtick.md`` §"Community cross-check", UC 729264 / 753644 / 761983).

The **state** claim is the displacement; whether the player *visibly* walked is
still the operator's call (``docs/handoff.md`` §5).
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..control import ControlError
from ..session import Game, SessionError

YAW = 90.0  # +forward is along world +X (Source convention)
PITCH = 0.0
HOLD_SECONDS = 1.0
MIN_MOVE_UNITS = 40.0
FORWARD_SPEED = 450.0
#: The magnitude the **engine itself** writes: a console `+forward` puts
#: ``forwardmove = 1.0`` on the command *and* ``1.0`` on the raw axes
#: (`CCSGOInput + 0x270`), with has-bit ``0x40`` and nothing for the other axes
#: (measured live 2026-10-15, see `engine.forward` in this scenario's result).
#: Everything downstream scales by `cl_forwardspeed`, so this is the unit the
#: write should use; ``450`` (the community `MOVEMENT_SCALE`) is the historic
#: guess the probe compares against.
ENGINE_FORWARD = 1.0
ENGINE_HOLD = 0.6

# The write variants to try, in order. The engine's own input writes `1.0`
# (`ENGINE_FORWARD`) into the raw axes *and* the command's `forwardmove`
# (measured live 2026-10-15), so that is full speed; `0.5` is the smallest
# genuinely *analog* request the engine could consume.
#
# `raw_entry` is deliberately **absent**: writing the per-controller input entry
# (`CCSGOInput + 0xb58 + 0x18`, the "next probe" of the older notes) crashes the
# game with an access violation inside the engine's input path
# (`client.dll + 0xb64a80`, next to `create_move_input`; observed 2026-10-15) and
# the same offset *reads* denormals (`entry_move` below) — the layout does not hold
# for this build. Re-derive it before enabling (`docs/re/subtick.md` §"Actionable
# seams").
VARIANTS: tuple[tuple[str, dict[str, float | bool]], ...] = (
    ("plain-1", {"forward": ENGINE_FORWARD}),
    ("plain-half", {"forward": 0.5}),
    ("quantize-1", {"forward": ENGINE_FORWARD, "quantize": True}),
    ("quantize+step-1", {"forward": ENGINE_FORWARD, "quantize": True, "step": True}),
    ("quantize+step-half", {"forward": 0.5, "quantize": True, "step": True}),
)

# Reported from the payload's `input` diagnostics for each probe.
DIAGNOSTIC_KEYS = (
    "base_has_bits", "base_move", "raw_move", "entry_move", "base_words",
    "cmd_words", "steps", "step_dump", "move_writes", "raw_writes",
    "entry_writes", "step_writes",
)


def _diagnostics(status: dict[str, Any]) -> dict[str, Any]:
    return {key: status.get(key) for key in DIAGNOSTIC_KEYS}


def _engine_convention(game: Game) -> dict[str, Any]:
    """Read what the engine's own input writes, before we write anything.

    The console ``+forward`` path is the one that *does* move the player
    (``docs/handoff.md`` §7), so its command is the ground truth for the has-bit
    values, the analog units and the sub-tick step convention.
    """
    game.exec("+forward")
    time.sleep(ENGINE_HOLD)
    forward = game.input()
    game.exec("-forward")
    game.exec("+jump")
    time.sleep(0.2)
    jump = game.input()
    game.exec("-jump")
    return {"forward": _diagnostics(forward), "jump": _diagnostics(jump)}


def _recenter(game: Game, origin_point: dict[str, float] | None) -> dict[str, Any]:
    """Return the player to the test spot and wait until it is still.

    Position (``recenter``) and zero velocity (``settle``) together: the teleport
    is synchronous (``setpos`` is applied on a later frame) and the settle stops a
    previous walk's momentum bleeding into the next measurement.
    """
    return awaiting.prepare(game, origin_point)


def _measure_console(game: Game, *, hold: float,
                     origin_point: dict[str, float] | None = None) -> dict[str, Any]:
    """The **engine's own** path from the same spot: hold the console ``+forward``.

    This is the baseline that says how far "walking" is at this spot in ``hold``
    seconds — without it a small number cannot be told from a blocked walk or a
    slowed-down write.
    """
    result: dict[str, Any] = {"moved_units": 0.0, "origin": {}}
    try:
        awaiting.require(game, "move")
        quiet = _recenter(game, origin_point)
        before = awaiting.origin(game)
        game.press("w", hold=hold)  # console `+forward`, the known-good path
        after = awaiting.origin(game)
        moved = awaiting.displacement(before, after) if (before and after) else 0.0
        result.update({"moved_units": round(moved, 1), "quiet": quiet,
                       "origin": {"before": before, "after": after}})
    except (SessionError, ControlError) as error:
        result["error"] = str(error)[:160]
    return result


def _measure(game: Game, *, hold: float, forward: float = FORWARD_SPEED,
             origin_point: dict[str, float] | None = None,
             **move_kwargs: bool) -> dict[str, Any]:
    """Hold the ``usercmd`` move with the given write and measure the walk.

    Every variant starts from the **same spot** (``origin_point``): the walks move
    the player, so without recentring the second variant measures a wall the first
    one walked into (observed 2026-10-15 — the entire matrix after the first
    variant read 0.0 from one wedged position).

    A live round can kill the player mid-hold (bots keep shooting); that is
    recorded as an ``error`` rather than aborting the matrix, so a probe run still
    yields the variants it did complete.
    """
    result: dict[str, Any] = {"forward": forward, "moved_units": 0.0, "movers": None,
                              "origin": {}}
    try:
        # Never measure outside a movement-capable screen: freeze/round-end/dead
        # ignore input, so a zero there says nothing about the write.
        awaiting.require(game, "move")
        quiet = _recenter(game, origin_point)
        before = awaiting.origin(game)
        game.move(forward=forward, **move_kwargs)
        time.sleep(hold)
        after = awaiting.origin(game)
        status = game.input()
        moved = awaiting.displacement(before, after) if (before and after) else 0.0
        result.update({
            "moved_units": round(moved, 1),
            "movers": {key: status.get(key) for key in
                       ("move_writes", "raw_writes", "entry_writes", "step_writes",
                        "base_move", "raw_move", "entry_move", "steps", "step_dump")},
            "origin": {"before": before, "after": after},
            "alive": awaiting.alive(game),
            "quiet": quiet,
        })
    except (SessionError, ControlError) as error:
        result["error"] = str(error)[:160]
    finally:
        try:
            game.move(clear=True)
        except (SessionError, ControlError):
            pass
    return result


def run(game: Game, *, hold: float = HOLD_SECONDS, map_name: str = "de_dust2",
        quantize: int = 0, min_units: float = MIN_MOVE_UNITS,
        clear_heading: bool = True, x: float | None = None, y: float | None = None,
        z: float | None = None, strict: bool = True) -> dict[str, Any]:
    """Try every movement write variant and report which (if any) moves.

    ``quantize`` is the server-side ``sv_quantize_movement_input`` value to set
    (0 by default, so the *client* write is the only variable). The **fixed test
    spot** is ``x``/``y``/``z`` when given (the operator's chosen open area) and
    otherwise wherever the player is standing when the probe starts — captured
    *before* anything moves, and teleported back to (waiting for the teleport to
    land **and** for zero velocity) before the sweep, the console baseline and
    every variant, so no walk can be blocked by, or inherit momentum from, the
    previous one. ``clear_heading`` then sweeps the view from that spot and aims
    down the most open direction (``--set clear_heading=false`` to keep ``yaw``
    fixed). With ``strict`` (default) the scenario fails only if **no** variant
    moved the player — the per-variant table is always returned so a partial result
    is readable.
    """
    # `verify_movable=False`: the machine's `LIVE` already requires the round to be
    # unfrozen/alive (the rules block is authoritative), and this scenario does its
    # own clearance sweep below — which also avoids the deadlock where the spawn's
    # fixed facing points at a wall, so the old `+forward` gate would restart the
    # round forever.
    awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    status = game.input("usercmd")
    if not status.get("hooked"):
        raise SessionError(f"usercmd backend not hooked: {status}")
    # The screen machine decides whether movement input is meaningful at all
    # (main menu / team-select / warmup / freeze / dead all swallow it).
    observation = awaiting.require(game, "move")

    # The **fixed test spot**: the operator's chosen open area (`x`/`y`/`z`) or
    # wherever the player is standing now. It is captured *after*
    # `ensure_live_round` (which returns immediately when the round is already
    # live, preserving the operator's placement) and every check below teleports
    # back to it, so a walk can never be blocked by the previous walk.
    if x is not None and y is not None:
        home = {"x": float(x), "y": float(y), "z": z}
        awaiting.recenter(game, home)
    else:
        home = awaiting.origin(game)
    print(f"[movement-probe] test spot {home}", flush=True)

    moved: dict[str, float] = {}
    winners: list[str] = []
    variants: dict[str, Any] = {}
    engine: dict[str, Any] = {}
    state: dict[str, Any] = {}
    heading: dict[str, Any] = {}
    baseline: dict[str, Any] = {}
    try:
        # Isolate the movement write: no server-side rounding beyond what we ask
        # for, and no sub-tick view angles steering the movement. Both cvars go
        # back to their vanilla (on) values at the end.
        game.exec(f"sv_quantize_movement_input {1 if quantize else 0}")
        game.exec("sv_subtick_movement_view_angles 0")

        # A **stable** movement-capable environment. Bots end rounds (and rounds
        # cycle through freeze, which ignores input *and* resets view angles, so
        # every measurement taken there is worthless — that is exactly what the
        # 2026-10-15 sweep run measured). Kick them, then re-assert the screen.
        game.exec("bot_kick")
        time.sleep(1.0)
        awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

        # Aim down the most open direction: an obstructed walk would look exactly
        # like a write that does nothing.
        if clear_heading:
            heading = awaiting.pick_clear_heading(game, home=home)
            print(f"[movement-probe] clear heading yaw={heading['yaw']:.0f} "
                  f"({heading['moved']:.0f} units, readings={heading['readings']}, "
                  f"all={heading['all']})", flush=True)
        else:
            game.look(yaw=YAW, pitch=PITCH)  # the fixed test axis
        state = awaiting.round_state(game)  # what the test depends on
        engine = _engine_convention(game)

        try:
            # Every variant walks from the same open spot (the fixed test spot).
            baseline = _measure_console(game, hold=hold, origin_point=home)
            print(f"[movement-probe] console baseline from {home}: "
                  f"{baseline['moved_units']:.0f} units", flush=True)
            for label, kwargs in VARIANTS:
                variants[label] = _measure(game, hold=hold, origin_point=home,
                                           **kwargs)
        finally:
            game.move(clear=True)

        moved = {label: variant["moved_units"] for label, variant in variants.items()}
        winners = sorted(label for label, units in moved.items() if units >= min_units)
        base_units = baseline.get("moved_units") or 0.0
        ratios = {"console": 1.0} if base_units <= 0 else {
            label: round(units / base_units, 2) for label, units in moved.items()
        }
    finally:
        game.move(clear=True)
        game.exec("sv_quantize_movement_input 1")
        game.exec("sv_subtick_movement_view_angles 1")

    if strict and not winners:
        walks = {
            label: {
                "moved_units": variant["moved_units"],
                "forward": variant["forward"],
                "move_writes": (variant["movers"] or {}).get("move_writes"),
                "raw_writes": (variant["movers"] or {}).get("raw_writes"),
                "step_writes": (variant["movers"] or {}).get("step_writes"),
                "base_move": (variant["movers"] or {}).get("base_move"),
                "raw_move": (variant["movers"] or {}).get("raw_move"),
            }
            for label, variant in variants.items()
        }
        raise SessionError(
            "no movement write moved the player "
            f"({walks}): the command-side seam is not reaching the simulation "
            f"(console baseline={baseline.get('moved_units')} units, "
            f"state={state}, engine={engine.get('forward')})"
        )

    return {
        "quantize_cvar": 1 if quantize else 0,
        "screen": str(observation.screen),
        "test_spot": home,
        "clear_heading": heading.get("best"),
        "console_baseline": baseline.get("moved_units"),
        "console_baseline_quiet": (baseline.get("quiet") or {}).get("settled"),
        "moved_units": moved,
        "moved_vs_console": ratios,
        "quiet": {label: (variant.get("quiet") or {}) for label, variant in variants.items()},
        "winners": winners,
        "variants": variants,
        "engine": engine,
        "state": state,
    }
