"""Scenario: observe the payload's **local animfix** in a live round.

The local animfix (``docs/re/animfix.md``) makes the **server's** animation win
for the local player: the server replicates a part of the AG2 graph inputs of
``CCS2PawnGraphController``, the client's own prediction rewrites the very
same slots, and the prediction is the only writer **through the setter** — so
without the fix the server's values never reach the graph. The animfix
captures the server's values at the netvar codec and puts them into the
graph's parameters where the graph is evaluated.

The scenario switches the animfix on (it is off by default, and that is the
only write it does), watches the payload's own state, and switches it **off**
again when it is done — the feature is left exactly as it found it.

What it reports:

* the animfix is on and it **acted** (``animfix_acted``),
* the **re-apply** counter climbs (``animfix_applies``) — the graph's
  parameters were corrected,
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
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..session import Game, SessionError

WATCH = 6.0     # how long to observe the animfix doing its thing
SAMPLE = 0.1
#: The animfix must have corrected the graph at least this often per second
#: while the watch window runs. The server ticks at 64 Hz, so the animfix
#: gets a chance on every tick; a tenth of that is already "it is working".
MIN_APPLY_RATE = 1.0


def run(game: Game, *, watch: float = WATCH, map_name: str = "de_dust2",
        setup: bool = True) -> dict[str, Any]:
    """Watch the local animfix in a live round.

    The animfix is switched on (it is off by default) and the payload's own
    counters and aim-yaw readings are sampled for ``watch`` seconds. It is
    switched off again at the end.
    """
    if setup:
        awaiting.ensure_live_round(game, map_name=map_name, verify_movable=False)

    # The feature is off by default; the scenario switches it on (the one
    # write it does) so the operator can see what it does — and off again
    # when it is done, so the round is left as it was found. The switch-off
    # is in a `finally`: a pipe hiccup inside the watch loop must not leave
    # the animfix on for the rest of the round.
    game.animfix(True)

    status = game.input()
    if not status.get("animfix"):
        game.animfix(False)
        raise SessionError("the payload did not accept the animfix command")

    captures_before = int(status.get("animfix_captures") or 0)
    applies_before = int(status.get("animfix_applies") or 0)
    skips_before = int(status.get("animfix_skips") or 0)
    deltas: list[float] = []
    acted_samples = 0
    final = status

    try:
        deadline = time.monotonic() + watch
        while time.monotonic() < deadline:
            live = game.input()
            if live.get("animfix_acted"):
                acted_samples += 1
            delta = live.get("animfix_aim_yaw_delta")
            if delta is not None:
                deltas.append(float(delta))
            time.sleep(SAMPLE)
        final = game.input()
    finally:
        # Leave the feature as it was found.
        game.animfix(False)

    observed_captures = int(final.get("animfix_captures") or 0) - captures_before
    observed_applies = int(final.get("animfix_applies") or 0) - applies_before
    observed_skips = int(final.get("animfix_skips") or 0) - skips_before
    ready = bool(final.get("animfix_ready"))
    animfix_status = str(final.get("animfix_status") or "")

    result: dict[str, Any] = {
        "screen": str(awaiting.screen(game)),
        "animfix": bool(final.get("animfix")),
        "animfix_ready": ready,
        "animfix_status": animfix_status,
        "animfix_acted": bool(final.get("animfix_acted")),
        "captures_during_window": observed_captures,
        "applies_during_window": observed_applies,
        "skips_during_window": observed_skips,
        # The refusals, by reason — so "the codec has not run yet" is
        # distinguishable from "every capture aged out" (a stalled
        # connection) and from "the capture was already used".
        "skips_no_capture": int(final.get("animfix_skip_no_capture") or 0),
        "skips_stale": int(final.get("animfix_skip_stale") or 0),
        "skips_applied": int(final.get("animfix_skip_applied") or 0),
        "skips_controller": int(final.get("animfix_skip_controller") or 0),
        # The server's snapshot rate: the staleness bound is measured
        # against it, so this is how old the server's values may be.
        "snapshot_interval_ms": int(final.get("animfix_snapshot_interval_ms") or 0),
        # The values the engine's own guard refused. It should be zero.
        "rejected": int(final.get("animfix_rejected") or 0),
        "acted_samples": acted_samples,
        "data_valid": bool(final.get("animfix_data_valid")),
        "server_aim_yaw": final.get("animfix_server_aim_yaw"),
        "predicted_aim_yaw": final.get("animfix_predicted_aim_yaw"),
        # The **divergence**: the signed difference between the server's aim
        # yaw and the client's predicted one, in degrees. It is what the
        # animfix removes, and it is only non-zero while the player turns.
        "aim_yaw_delta": final.get("animfix_aim_yaw_delta"),
        "max_abs_aim_yaw_delta": round(max((abs(d) for d in deltas), default=0.0), 2),
    }

    if not ready:
        # The hooks are not installed: the animfix cannot work, and the
        # payload says why (`animfix_status`). That is a **skipped** run, not
        # a failure — the feature is simply not available in this build.
        result["verdict"] = (f"the animfix is on but its hooks are not installed: {animfix_status}"
                             if animfix_status else
                             "the animfix is on but its hooks are not installed")
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
            f"graph (applies +{observed_applies}, skips +{observed_skips}, "
            f"no capture {result['skips_no_capture']}, aged out "
            f"{result['skips_stale']}, already used {result['skips_applied']}, "
            f"controller changed {result['skips_controller']}): {result}"
        )

    rate = observed_applies / watch if watch > 0 else float(observed_applies)
    result["apply_rate"] = round(rate, 1)
    result["verdict"] = (
        f"the animfix acted: {observed_captures} captures, {observed_applies} graph "
        f"writes corrected ({rate:.1f}/s), {observed_skips} refused; the server's aim "
        f"yaw {result['server_aim_yaw']} vs the client's predicted "
        f"{result['predicted_aim_yaw']} (divergence {result['aim_yaw_delta']} deg, "
        f"peak {result['max_abs_aim_yaw_delta']} deg)"
    )
    if rate < MIN_APPLY_RATE:
        result["verdict"] += (
            f" — the correction rate is below {MIN_APPLY_RATE}/s: the animfix is only "
            "just keeping up"
        )
    if result["max_abs_aim_yaw_delta"] < 0.5:
        result["verdict"] += (
            " — the divergence is ~0: the player did not turn during the window, "
            "so there was nothing for the animfix to correct (turn in-game and "
            "re-run to see it)"
        )
    return result
