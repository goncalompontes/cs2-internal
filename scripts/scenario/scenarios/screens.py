"""Scenario: exercise and document the screen state machine.

The harness's other scenarios assume the game is in a *live* round. This one
makes that assumption checkable: it prints the machine's tables (screens →
capabilities, transitions), drives the game into ``LIVE``, kills the local
player to observe ``DEAD``, and drives it back — asserting the capability gate
(the operations ``DEAD`` must refuse) rather than just the raw fields.

It is the live half of ``scripts/scenario/screens.py``: the tables below are the
documentation, the run is the evidence. A failure here means the *machine* is
wrong (or the payload's ``rules`` block is unreadable), not the mechanic under
test.
"""

from __future__ import annotations

import time
from typing import Any

from .. import screens
from ..session import Game, SessionError

MAP = "de_dust2"
DEATH_TIMEOUT = 8.0


def _tables() -> dict[str, Any]:
    return {
        "screens": [str(screen) for screen in screens.Screen],
        "capabilities": {
            str(screen): [str(cap) for cap in caps]
            for screen, caps in screens.CAPABILITIES.items()
        },
        "transitions": [
            {"from": str(edge.frm), "to": str(edge.to), "action": edge.action,
             "detail": edge.detail}
            for edge in screens.TRANSITIONS
        ],
    }


def _observe(machine: screens.Machine, label: str) -> dict[str, Any]:
    state = machine.observe()
    print(f"[screens] {label}: {state.summary()}")
    return {"label": label, **state.as_dict()}


def run(game: Game, *, map_name: str = MAP, kill: bool = True,
        main_screen: bool = True, timeout: float = 300.0) -> dict[str, Any]:
    """Walk the machine: main screen → LIVE, observe, (kill → DEAD), back to LIVE.

    The first step is the **main-screen rule** test: ``disconnect`` returns the
    game to the main menu, where the ``game_rules`` global is still readable (a
    default object) but no level's rules-proxy entity exists — so ``in_game`` must
    be false. That is the rule every other scenario's gating depends on.
    """
    machine = game.state_machine(map_name=map_name)
    report: dict[str, Any] = {"tables": _tables(), "observations": []}

    if main_screen:
        game.exec("disconnect")
        # The main screen presents as `not-in-game` (a menu local exists) or
        # `not-ready` (no local at all after the disconnect) — both are "no level".
        want = (screens.Screen.NOT_IN_GAME, screens.Screen.NOT_READY)
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            if machine.observe().screen in want:
                break
            time.sleep(0.5)
        menu = _observe(machine, "main-screen")
        report["observations"].append(menu)
        report["main_screen_rule"] = {
            "in_game": menu["in_game"],
            "rules_loaded": bool((menu.get("rules") or {}).get("loaded")),
            "level": menu.get("level"),
        }
        if menu["screen"] not in (str(s) for s in want):
            raise SessionError(
                f"after `disconnect` the machine reports {menu['screen']}, expected "
                f"one of {[str(s) for s in want]} ({menu['summary']}) — if the game "
                "is still in a level, `disconnect` did not take effect"
            )
        if menu["in_game"]:
            raise SessionError(
                "the main-screen rule says a level is up while the machine reports "
                f"{menu['screen']}: {menu['summary']}"
            )

    observation = machine.ensure(screens.Screen.LIVE, timeout=timeout,
                                 verify_movable=True)
    report["observations"].append(_observe(machine, "live"))
    observation.require(screens.Can.MOVE)
    observation.require(screens.Can.ATTACK)

    if kill:
        # `DEAD` is the screen every input test silently fails in, so prove the
        # machine catches it. `kill` needs `sv_cheats 1` (the machine sets it).
        game.exec("sv_cheats 1; kill; kill")
        deadline = time.monotonic() + DEATH_TIMEOUT
        while time.monotonic() < deadline:
            state = machine.observe()
            if state.screen in (screens.Screen.DEAD, screens.Screen.ROUND_END):
                break
            time.sleep(0.25)
        dead = _observe(machine, "after-kill")
        report["observations"].append(dead)
        if dead["screen"] == str(screens.Screen.DEAD):
            blocked = dead["capabilities"]["move"]["allowed"]
            if blocked:
                raise SessionError(
                    "the machine allows MOVE while DEAD — the capability table "
                    f"is wrong ({dead['summary']})"
                )
            report["dead_blocks_move"] = True
        else:
            # The round can end under us (the bots keep playing): that is a valid
            # screen too, just not the one this step wanted to prove.
            report["dead_blocks_move"] = None
            report["note"] = f"could not reach DEAD (saw {dead['screen']})"

    machine.ensure(screens.Screen.LIVE, timeout=timeout, verify_movable=True)
    report["observations"].append(_observe(machine, "live-again"))
    report["steps"] = [entry["action"] for entry in machine.log]
    report["final"] = machine.state.as_dict() if machine.state else {}
    return report
