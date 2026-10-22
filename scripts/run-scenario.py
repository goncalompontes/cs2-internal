#!/usr/bin/env python3
"""Run a programmatic CS2 scenario against the payload (ADR-0020).

The runner launches the game through the ``CS2Launch`` scheduled task, injects
``payload.dll``, connects to the control channel, runs the named scenario, and
tears down. Any failure propagates as a non-zero exit with a traceback.

Usage:
  python scripts/run-scenario.py --list
  python scripts/run-scenario.py join-bot-match
  python scripts/run-scenario.py join-bot-match --map de_inferno --bots 4
  python scripts/run-scenario.py join-bot-match --no-launch --keep-running
"""

from __future__ import annotations

import argparse
import json
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from scenario import SessionError, cs2_session  # noqa: E402
from scenario.scenarios import SCENARIOS  # noqa: E402


def _coerce(value: str) -> object:
    """Best-effort CLI value coercion: bool, int, float, else string."""
    lowered = value.lower()
    if lowered in ("true", "false"):
        return lowered == "true"
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("scenario", nargs="?", help="scenario name (see --list)")
    parser.add_argument("--list", action="store_true", help="list scenarios and exit")
    parser.add_argument("--no-launch", action="store_true",
                        help="attach to an already-running game instead of launching")
    parser.add_argument("--no-inject", action="store_true",
                        help="do not inject the payload (assume it is loaded)")
    parser.add_argument("--keep-running", action="store_true",
                        help="leave the game running on exit")
    parser.add_argument("--pipe", help="override the control pipe name")
    parser.add_argument("--game-timeout", type=float, default=180.0,
                        help="seconds to wait for the game / client.dll")
    parser.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                        help="scenario keyword argument (repeatable)")
    args = parser.parse_args(argv)

    if args.list or not args.scenario:
        for name in sorted(SCENARIOS):
            print(name)
        return 0 if args.list else 2

    run = SCENARIOS.get(args.scenario)
    if run is None:
        print(f"unknown scenario: {args.scenario!r} (try --list)", file=sys.stderr)
        return 2

    kwargs: dict[str, object] = {}
    for item in args.set:
        key, separator, value = item.partition("=")
        if not separator:
            print(f"--set expects KEY=VALUE, got {item!r}", file=sys.stderr)
            return 2
        kwargs[key] = _coerce(value)

    try:
        with cs2_session(
            launch=not args.no_launch,
            inject_payload=not args.no_inject,
            keep_running=args.keep_running,
            pipe_name=args.pipe,
            game_timeout=args.game_timeout,
        ) as game:
            result = run(game, **kwargs)
    except (SessionError, Exception):  # noqa: BLE001 - report any failure verbatim
        traceback.print_exc()
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
