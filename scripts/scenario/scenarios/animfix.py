"""Scenario: observe the payload's **local animfix** in a live round.

The local animfix (``docs/re/animfix.md``) makes the **server's** animation win
for the local player: the server replicates a part of the AG2 graph inputs of
``CCS2PawnGraphController``, the client's own prediction rewrites the very
same slots, and the prediction is the only writer — so without the fix the
server's values never reach the graph. The animfix captures the server's
values at the netvar codec and hands them to the graph instead.

The scenario is **observation-only** apart from the one command it needs (it
turns the feature on, because the feature is off by default and the operator
has to see what it does). It waits for a live round, switches the animfix on,
and then watches the payload's own state:

* the animfix is on and it **acted** (``animfix_acted``),
* the **re-apply** counter climbs (``animfix_applies``) — the graph writes the
  payload corrected,
* the **captures** climb (``animfix_captures``) — the server's values arrive,
* the **server's** aim yaw and the **client's predicted** aim yaw are both
  reported (``animfix_server_aim_yaw`` / ``animfix_predicted_aim_yaw``) and
  their difference (``animfix_aim_yaw_delta``) is the **divergence** the fix
  removes.

The player has to **turn** for the divergence to be visible: with a still
camera the server's and the client's aim yaw agree, and the animfix has
nothing to correct. The scenario therefore reports the divergence rather than
requiring a non-zero one, and it fails only when the feature is on and does
**not** act at all.

Nothing else is written: no ``exec``, no settings change, no movement.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

WATCH = 6.0     # how long to observe the animfix doing its thing
SAMPLE = 0.1
#: The payload must have corrected the graph at least this often per second
#: while the watch window runs (the server ticks at 64 Hz, so the animfix
#: gets a chance on every tick).
MIN_APPLY_RATE = 1.0


def run(game: Game, *, watch: float = WATCH, map_name: str = "de_dust2",
        setup: bool = True) -> dict[str, Any]:
    """Watch the local animfix in a live round.

    The animfix is switched on (it is off by default) and the payload's own
    counters and aim-yaw readings are sampled for ``watch`` seconds.
    """
    if setup:
        awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    before = game.input()
    # The feature is off by default; the scenario switches it on (the one
    # write it does) so the operator can see what it does.
    game.animfix(True)

    status = game.input()
    if not status.get("animfix"):
        raise SessionError("the payload did not accept the animfix command")

    captures_before = int(status.get("animfix_captures") or 0)
    applies_before = int(status.get("animfix_applies") or 0)
    skips_before = int(status.get("animfix_skips") or 0)
    deltas: list[float] = []
    acted_samples = 0

    deadline = time.monotonic() + watch
    while time.monotonic() < deadline:
        live = game.input()
        if live.get("animfix_acted"):
            acted_samples += 1
        deltas.append(float(live.get("animfix_aim_yaw_delta") or 0.0))
        time.sleep(SAMPLE)

    status = game.input()
    captures_after = int(status.get("animfix_captures") or 0)
    applies_after = int(status.get("animfix_applies") or 0)
    skips_after = int(status.get("animfix_skips") or 0)
    observed_captures = captures_after - captures_before
    observed_applies = applies_after - applies_before
    observed_skips = skips_after - skips_before

    result: dict[str, Any] = {
        "screen": str(awaiting.screen(game)),
        "animfix": bool(status.get("animfix")),
        "animfix_ready": bool(status.get("animfix_ready")),
        "animfix_acted": bool(status.get("animfix_acted")),
        "captures_during_window": observed_captures,
        "applies_during_window": observed_applies,
        "skips_during_window": observed_skips,
        "acted_samples": acted_samples,
        "server_aim_yaw": round(float(status.get("animfix_server_aim_yaw") or 0.0), 2),
        "predicted_aim_yaw": round(float(status.get("animfix_predicted_aim_yaw") or 0.0), 2),
        # The **divergence**: the signed difference between the server's aim
        # yaw and the client's predicted one, in degrees. It is what the
        # animfix removes, and it is only non-zero while the player turns.
        "aim_yaw_delta": round(float(status.get("animfix_aim_yaw_delta") or 0.0), 2),
        "max_abs_aim_yaw_delta": round(max((abs(d) for d in deltas), default=0.0), 2),
    }

    if not result["animfix_ready"]:
        result["verdict"] = ("the animfix is on but its hooks are not installed: the "
                             "client.dll signatures (animfix_graph_codec / "
                             "animfix_param_setter) did not resolve in this build")
        result["skipped"] = True
        return result

    if observed_captures == 0:
        raise SessionError(
            "the animfix is on but the payload never captured the server's graph "
            f"inputs (captures +{observed_captures}, applies +{observed_applies}): "
            f"{result}"
        )

    if observed_applies == 0:
        raise SessionError(
            "the animfix captured the server's values but never corrected the "
            f"graph (applies +{observed_applies}, skips +{observed_skips}): {result}"
        )

    result["verdict"] = (
        f"the animfix acted: {observed_captures} captures, {observed_applies} graph "
        f"writes corrected, {observed_skips} refused; the server's aim yaw "
        f"{result['server_aim_yaw']} vs the client's predicted {result['predicted_aim_yaw']} "
        f"(divergence {result['aim_yaw_delta']} deg, peak "
        f"{result['max_abs_aim_yaw_delta']} deg)"
    )
    if result["max_abs_aim_yaw_delta"] < 0.5:
        result["verdict"] += (
            " — the divergence is ~0: the player did not turn during the window, "
            "so there was nothing for the animfix to correct (turn in-game and "
            "re-run to see it)"
        )
    return result
