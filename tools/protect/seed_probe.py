#!/usr/bin/env python3
"""Prints the seed of the build, or says why there is none.

The seed is **mandatory**. The protection's constants, its keys and its
layout are derived from it, and the fallback -- the module id hashed with
FNV-1a -- is a constant of the payload: two builds without a seed are byte
for byte the same file, and a release that ships it ships a signature.

The plugin refuses to run without a seed (tools/protect/src/config.cpp), and
the payload's wiring refuses to *configure* without one, the way it refuses
to configure without a policy (payload/meson.build): a build that looks
protected and is not is the failure mode this project exists to avoid.

The seed is the environment's `CS2_PROTECT_SEED`, else the policy's `seed`
(the policy is the one `CS2_PROTECT_CONFIG` names). It is printed on
stdout; the exit code is 0 when there is a seed and 1 when there is none,
and the reason is on stderr.

Usage: seed_probe.py
"""
from __future__ import annotations

import json
import os
import sys


def policy_seed(path: str) -> str:
    """The seed of the policy file, or "" when it has none.

    A policy file that cannot be read or parsed has no seed: the plugin
    reports the malformed policy itself, with a better message, and the
    wiring must not hide it behind a seed error.
    """
    if not path:
        return ""
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            document = json.load(handle)
    except (OSError, ValueError):
        return ""
    if not isinstance(document, dict):
        return ""
    seed = document.get("seed")
    if seed is None:
        return ""
    if isinstance(seed, int):
        return f"0x{seed:x}"
    if isinstance(seed, str):
        return seed.strip()
    return ""


def main() -> int:
    seed = os.environ.get("CS2_PROTECT_SEED", "").strip()
    if not seed:
        seed = policy_seed(os.environ.get("CS2_PROTECT_CONFIG", "").strip())
    if not seed:
        print(
            "the build has no seed: set CS2_PROTECT_SEED to a 64-bit hexadecimal value, or put "
            'a "seed" in the policy file',
            file=sys.stderr,
        )
        return 1
    sys.stdout.write(seed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
