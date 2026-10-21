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

When the capture never happens, the scenario does **not** report a bare
"never captured": the **codec counters** say which case it was — the hooked
codec never ran (H1), it ran for somebody else (H2), or the local controller
could not be resolved (H2).

The player has to **turn** for the divergence to be visible: with a still
camera the server's and the client's aim yaw agree, and the animfix has
nothing to correct. The scenario therefore reports the divergence rather than
requiring a non-zero one, and it fails only when the feature is on and does
**not** act at all — or when its **duty cycle** (the fraction of the client
anim ticks on which the correction won) falls below ``MIN_DUTY_CYCLE``, which
is the oscillation at the server's tick rate the animfix exists to remove.
"""

from __future__ import annotations

import time
from typing import Any

from .. import awaiting
from ..protocol import InputStatus
from ..session import Game, SessionError

WATCH = 6.0     # how long to observe the animfix doing its thing
SAMPLE = 0.1
#: The floor for the *duty cycle*: the fraction of the client anim ticks on
#: which the correction won. It is 1.0 on a regular connection; below that
#: the pose alternates between the server's and the client's aim yaw at the
#: server's tick rate, and the operator has to know. It is measured over the
#: **watch window** (the counters are cumulative, and the two of them have
#: different windows: the correction counter only moves while the animfix is
#: on).
MIN_DUTY_CYCLE = 0.9


def diagnose_codec(calls: int, other_controller: int, not_local: int,
                   local_calls: int) -> str:
    """What the codec counters **prove**, in one line.

    The animfix's capture is filtered by the local controller: the codec must
    have run *for the local player*. The counters say which of the two
    possible failures it is —

    * no call at all ⇒ **H1**: the hooked codec never runs (not for the local
      player and not for anybody else), so the local player's data comes from
      another codec, and
    * calls, but none of them the local player's ⇒ **H2**: the codec runs, but
      not for the local player — or the comparison against the local
      controller is wrong.

    The same classification as the payload's (``cs2/control/animfix.hpp``'s
    ``classify_codec``), so the scenario and the payload cannot disagree.
    """
    if calls <= 0:
        return ("H1: the hooked codec never ran — the local player's graph "
                "inputs are decoded by another codec")
    if local_calls <= 0 and not_local > 0:
        return ("H2: the local controller could not be resolved — the "
                "comparison could not be made")
    if local_calls <= 0:
        return ("H2: the codec ran, but never for the local controller — it "
                "decodes somebody else's graph inputs")
    return "the codec ran for the local controller"


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
    # when it is done, so the round is left as it was found.
    #
    # **The switch-on is inside the `try` as well**: a failure on the first
    # read (a pipe hiccup) must not leave the animfix on for the rest of the
    # round either.
    deltas: list[float] = []
    acted_samples = 0
    captures_before = 0
    applies_before = 0
    skips_before = 0
    client_ticks_before = 0
    enabled_client_ticks_before = 0
    corrected_ticks_before = 0
    final: InputStatus = {}

    try:
        game.animfix(True)
        status = game.input()
        if not status.get("animfix"):
            raise SessionError("the payload did not accept the animfix command")
        captures_before = int(status.get("animfix_captures") or 0)
        applies_before = int(status.get("animfix_applies") or 0)
        skips_before = int(status.get("animfix_skips") or 0)
        client_ticks_before = int(status.get("animfix_client_ticks") or 0)
        enabled_client_ticks_before = int(status.get("animfix_enabled_client_ticks") or 0)
        corrected_ticks_before = int(status.get("animfix_corrected_ticks") or 0)
        final = status
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

    # The **codec counters**: what the hooked codecs did. They are the
    # diagnosis of a capture that never happened — the difference between
    # "the codec never ran" and "the codec ran, but not for the local
    # player" — and the per-codec split says which of the class's two decode
    # paths the engine used.
    codec_calls = int(final.get("animfix_codec_calls") or 0)
    codec_other_controller = int(final.get("animfix_codec_other_controller") or 0)
    codec_not_local = int(final.get("animfix_codec_not_local") or 0)
    codec_local_calls = int(final.get("animfix_codec_local_calls") or 0)
    codec_diagnosis = diagnose_codec(codec_calls, codec_other_controller,
                                     codec_not_local, codec_local_calls)

    # The **duty cycle**: the client anim ticks on which the correction won,
    # over the client anim ticks it could have won.
    #
    # Both are **deltas over the watch window**, and the denominator is the
    # client anim ticks that ran **while the animfix was on**. The payload's
    # counters are cumulative, and the two of them have **different windows**:
    # the correction counter only moves while the animfix is on, and the
    # driver's own client-tick counter climbs since the payload started (the
    # animfix is off by default). Dividing the two absolute counters would
    # therefore report a duty cycle of 0 % for a feature that wins every tick
    # — the quotient is diluted by the payload's uptime.
    if "animfix_enabled_client_ticks" in final:
        observed_client_ticks = (int(final.get("animfix_enabled_client_ticks") or 0)
                                 - enabled_client_ticks_before)
    else:
        # An older payload: the client-tick delta over the window is the same
        # thing (the animfix is on for the whole window).
        observed_client_ticks = (int(final.get("animfix_client_ticks") or 0)
                                 - client_ticks_before)
    observed_corrected_ticks = (int(final.get("animfix_corrected_ticks") or 0)
                                 - corrected_ticks_before)
    # The duty cycle is only meaningful when the payload reported client ticks
    # at all: a payload that never saw one cannot work, and that is a
    # different failure (reported above).
    measurable = observed_client_ticks > 0
    duty = (observed_corrected_ticks / observed_client_ticks) if measurable else 0.0

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
        # connection).
        "skips_no_capture": int(final.get("animfix_skip_no_capture") or 0),
        "skips_stale": int(final.get("animfix_skip_stale") or 0),
        "skips_controller": int(final.get("animfix_skip_controller") or 0),
        # The graph parameters the animfix could not write: a failure of its
        # own, so it is distinguishable from "the controller does not
        # answer".
        "skips_write_failed": int(final.get("animfix_skip_write_failed") or 0),
        # The anim ticks the driver ran, and the client ones (the correction
        # is written on the client tick: the server tick of a listen server
        # evaluates the server's own copies of the entities).
        "anim_ticks": int(final.get("animfix_anim_ticks") or 0),
        "client_ticks": int(final.get("animfix_client_ticks") or 0),
        # The **duty cycle**: the client anim ticks on which the correction
        # won, and the client anim ticks it could have won — both as
        # **deltas over the watch window**. Below 100 % the pose alternates
        # between the server's and the client's aim yaw at the server's tick
        # rate.
        "corrected_ticks": int(final.get("animfix_corrected_ticks") or 0),
        "corrected_ticks_during_window": observed_corrected_ticks,
        "client_ticks_during_window": observed_client_ticks,
        # The server's snapshot rate: the staleness bound is measured
        # against it, so this is how old the server's values may be.
        "snapshot_interval_ms": int(final.get("animfix_snapshot_interval_ms") or 0),
        # The values the engine's own guard refused. It should be zero.
        "rejected": int(final.get("animfix_rejected") or 0),
        # The **codec counters** and the diagnosis they settle.
        "codec_calls": codec_calls,
        "codec_other_controller": codec_other_controller,
        "codec_not_local": codec_not_local,
        "codec_local_calls": codec_local_calls,
        "codec_diagnosis": codec_diagnosis,
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
        # **A diagnosis, not a symptom.** The bare "never captured" error
        # said what happened; this one says *why*, with the counters that
        # prove it: the codec never ran (H1), it ran for somebody else
        # (H2), or the local controller could not be resolved (H2).
        raise SessionError(
            "the animfix is on but the payload never captured the server's graph "
            f"inputs (captures +{observed_captures}, applies +{observed_applies}); "
            f"{codec_diagnosis} — codec calls {codec_calls}, "
            f"other controller {codec_other_controller}, local controller "
            f"unresolved {codec_not_local}, local calls {codec_local_calls}: "
            f"{result}"
        )

    if observed_applies == 0:
        raise SessionError(
            "the animfix captured the server's values but never corrected the "
            f"graph (applies +{observed_applies}, skips +{observed_skips}, "
            f"no capture {result['skips_no_capture']}, aged out "
            f"{result['skips_stale']}, controller changed "
            f"{result['skips_controller']}): {result}"
        )

    rate = observed_applies / watch if watch > 0 else float(observed_applies)
    result["apply_rate"] = round(rate, 1)
    result["duty_cycle"] = round(duty, 4) if measurable else None
    result["verdict"] = (
        f"the animfix acted: {observed_captures} captures, {observed_applies} graph "
        f"writes corrected ({rate:.1f}/s), {observed_skips} refused; the server's aim "
        f"yaw {result['server_aim_yaw']} vs the client's predicted "
        f"{result['predicted_aim_yaw']} (divergence {result['aim_yaw_delta']} deg, "
        f"peak {result['max_abs_aim_yaw_delta']} deg)"
    )
    if measurable and duty < MIN_DUTY_CYCLE:
        # The duty cycle is the number the animfix exists for: below the
        # floor the pose alternates between the server's and the client's aim
        # yaw at the server's tick rate. It is a **failure**, not a note.
        raise SessionError(
            f"the animfix's duty cycle is only {duty:.1%} over the watch window "
            f"(below {MIN_DUTY_CYCLE:.0%}): {observed_corrected_ticks} of "
            f"{observed_client_ticks} client anim ticks carried the server's value — "
            f"the pose alternates between the server's and the client's aim yaw at "
            f"the server's tick rate: {result}"
        )
    if result["max_abs_aim_yaw_delta"] < 0.5:
        result["verdict"] += (
            " — the divergence is ~0: the player did not turn during the window, "
            "so there was nothing for the animfix to correct (turn in-game and "
            "re-run to see it)"
        )
    return result
