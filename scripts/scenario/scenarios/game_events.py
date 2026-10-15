"""Smoke scenario: capture game events through the payload listener.

Seeds an offline bot match and asserts the payload's ``IGameEventListener2``
(registered with the client's ``CGameEventManager``; see
``docs/re/client-dll.md`` §"Game events") sees a ``round_start``. This is the
acceptance scenario for the harness's event support (ADR-0020).
"""

from __future__ import annotations

from typing import Any

from .. import awaiting
from ..session import Game, SessionError


def run(game: Game, *, map_name: str = "de_dust2", bots: int = 2,
        map_timeout: float = 180.0, event_timeout: float = 120.0) -> dict[str, Any]:
    """Load ``map_name``, start a round and await ``round_start``."""
    game.exec("sv_cheats 1")
    awaiting.ensure_map(game, map_name=map_name, timeout=map_timeout)

    ping = game.ping()
    if not ping.get("events_ready"):
        raise SessionError("payload event listener not ready (client.dll manager unresolved)")

    # Bots so a real round actually begins.
    for command in ["bot_add_t", "bot_add_ct"][:bots]:
        game.exec(command)

    # `wait_for(event=...)` subscribes on demand and matches a *new* event.
    started = game.wait_for(event="round_start", timeout=event_timeout)
    if not started.get("matched"):
        raise SessionError(f"'round_start' not observed within {event_timeout:.0f}s")

    seen = game.events(limit=64)
    names = [event["name"] for event in seen["events"]]
    if "round_start" not in names:
        raise SessionError(f"event ring did not capture round_start: {names!r}")

    # The accessors must have produced at least one event carrying arguments.
    with_args = [event for event in seen["events"] if event.get("args")]
    if not with_args:
        raise SessionError(f"no captured event carried arguments: {seen['events']!r}")

    # Exercise `event_args` matching: `round_start`'s arguments are the match
    # settings, so the next round repeats them.
    round_args = next((event["args"] for event in seen["events"]
                       if event["name"] == "round_start" and event.get("args")), None)
    matched_args = None
    if round_args:
        again = game.wait_for(event="round_start", event_args=round_args, timeout=event_timeout)
        if not again.get("matched"):
            raise SessionError(
                f"event_args {round_args!r} did not match a later round_start within "
                f"{event_timeout:.0f}s")
        matched_args = round_args

    return {
        "map": map_name,
        "events_ready": True,
        "count": seen["count"],
        "recent": names,
        "args_example": with_args[-1],
        "event_args_matched": matched_args,
    }
