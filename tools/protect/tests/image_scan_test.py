#!/usr/bin/env python3
"""The image scan of the protected strings.

The scan looks for the plaintext of a protected literal at **every offset**
of every NUL-terminated run of the image.  That is the whole point of it:
the MSVC ABI merges a literal with a longer literal that **ends with it**
(`movement_thirdperson_fov` holds `thirdperson_fov` at offset 9), so the
bytes of a literal the pass deleted survive as the tail of the longer one.
A scan that only hashes the start of the runs cannot see that leak, and
counts it as absent -- the number it publishes is then the number of
literals it did not look at.

The test drives the predicate of `check_payload.py` itself, over an image
that holds the two shapes:

  * a literal that is a **whole** run of the image;
  * a literal that is only the **tail** of a longer run.

Usage: image_scan_test.py
"""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

# The readers are shared with the checkers, and the checkers put the
# directory of the tool on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_payload  # noqa: E402

#: The seed of the build the test pretends to be.
SEED = 0x0123456789ABCDEF

#: The literals of the fixture.
WHOLE = b"movement_thirdperson_fov"
TAIL = b"thirdperson_fov"
ABSENT = b"not_in_the_image"


class TestFailure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise TestFailure(what)


def salted_digest(content: bytes) -> str:
    """The digest the coverage of a build with a seed carries."""
    return hashlib.sha256(SEED.to_bytes(8, "little") + content).hexdigest().upper()


def plain_digest(content: bytes) -> str:
    """The digest the coverage of an older build carries."""
    return hashlib.sha256(content).hexdigest().upper()


def wanted(*contents: bytes, salted: bool) -> dict[int, set[str]]:
    """The digests of the literals, grouped by length."""
    by_length: dict[int, set[str]] = {}
    for content in contents:
        digest = salted_digest(content) if salted else plain_digest(content)
        by_length.setdefault(len(content), set()).add(digest)
    return by_length


def check_the_scan() -> None:
    """The assertions of the test."""
    # The image: a literal of its own, and a longer literal that ends with
    # the second one -- the shape the MSVC ABI produces.
    image = b"\x00" + WHOLE + b"\x00" + b"xxx" + TAIL + b"\x00" + b"yyy\x00"
    runs = check_payload.image_runs(image)

    # The scan of a build with a seed: the digests are salted.
    forms = check_payload.DigestForms(salted=True, plain=False)
    whole, part = check_payload.scan_literals(
        image, runs, wanted(WHOLE, TAIL, ABSENT, salted=True), SEED, forms
    )
    check(salted_digest(WHOLE) in whole,
          "the scan did not find the literal that is a whole run of the image")
    check(salted_digest(TAIL) not in whole,
          "the scan found the tail of a longer run as a whole run")
    check(salted_digest(TAIL) in part,
          "the scan did not find the literal that is the tail of a longer run")
    check(salted_digest(ABSENT) not in whole and salted_digest(ABSENT) not in part,
          "the scan found a literal that is not in the image")

    # The scan of an older build: the digests are unsalted.
    forms = check_payload.DigestForms(salted=False, plain=True)
    whole, part = check_payload.scan_literals(
        image, runs, wanted(WHOLE, TAIL, ABSENT, salted=False), SEED, forms
    )
    check(plain_digest(WHOLE) in whole,
          "the scan did not find the unsalted literal that is a whole run")
    check(plain_digest(TAIL) in part,
          "the scan did not find the unsalted literal that is the tail of a longer run")

    # The predicate the check uses to tell the two apart: a literal that is
    # in the image as a whole string is the pass's own copy, and a literal
    # that is only inside another string is the bytes of another string.
    check(len(whole) == 1, f"the scan found {len(whole)} whole runs, not 1")
    check(len(part) == 1, f"the scan found {len(part)} literals inside a run, not 1")

    # A literal whose content holds an **embedded NUL**: the run split cuts it
    # in two, so a scan that only looks at the runs cannot see it -- and it
    # would call it absent.
    embedded = b"ab\x00cd"
    image = b"\x00" + WHOLE + b"\x00" + b"zz" + embedded + b"zz\x00"
    runs = check_payload.image_runs(image)
    forms = check_payload.DigestForms(salted=True, plain=False)
    embedded_whole, embedded_part = check_payload.scan_literals(
        image, runs, wanted(embedded, salted=True), SEED, forms
    )
    check(salted_digest(embedded) in embedded_part,
          "the scan did not find the literal with an embedded NUL")
    check(salted_digest(embedded) not in embedded_whole,
          "the scan found the literal with an embedded NUL as a whole run")


def main() -> int:
    try:
        check_the_scan()
    except TestFailure as error:
        print(f"image_scan_test: {error}", file=sys.stderr)
        return 1

    print("image_scan_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
