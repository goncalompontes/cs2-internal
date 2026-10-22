#!/usr/bin/env python3
"""The test of the rotation reader of the payload check.

The **rotation** of the mixer is the one value of the mixer that is in the
instruction stream: the multiplier and the xor constant live in the data of
the image, but the rotation is an immediate of the decode. It is what makes
the `.text` of two builds with two seeds differ, and the payload check has to
read it out of the image to prove that the image uses the rotation the pass
encrypted with.

The X86 backend spells the rotation in **three** ways, and a reader that
knows only one of them cannot see two of the sixty-three legal rotations:

| the rotation | the instruction | the encoding |
|---|---|---|
| 31 | `rolq $0x1f, %rax` | `48 c1 c0 1f` |
| 1 | `rolq %rax` | `48 d1 c0` |
| 63 | `rorq %rax` | `48 d1 c8` |

The second and the third are the ones the backend folds: a rotation by one
has no immediate at all, and a rotation by 63 is a rotation to the right by
one. A build whose module has one of the two rotations therefore produces a
dispatch that a reader of `rolq $imm8` cannot read, and **the build fails
its own check** -- and the seed `0x2222222222222222` of the specification's
acceptance procedure is one of them.

The test drives the reader of the disassembly and the check that uses it, so
that a reader that rots (one that only knows `rolq $imm8` again) fails the
suite instead of failing the build of a good seed.

Usage: rotation_test.py
"""
from __future__ import annotations

import struct
import sys
import tempfile
import shutil
from pathlib import Path

# The readers are shared with the checkers, and the checkers put the
# directory of the tool on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_payload  # noqa: E402
from protect_checks import (  # noqa: E402
    Coverage,
    Failure,
    PeImage,
    find_dispatch_sites,
    rotation_of,
)
from support import (  # noqa: E402
    IMAGE_BASE,
    RDATA_RVA,
    digest_of,
    synthetic_image,
    write_coverage,
)


class TestFailure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise TestFailure(what)


#: The mixer of the synthetic module: the values the coverage names.
MULTIPLIER = 0x3B5E840A0E972AED
XOR_CONSTANT = 0x7674E4E3A0BE4C0A

#: The three parts of the key of the synthetic module.
KEY_PARTS = (0x1111111111111111, 0x2222222222222222, 0x3333333333333333)

#: The RVA of the table of the synthetic module in the `.rdata`.
TABLE_RVA = RDATA_RVA + 0x100


def the_disassembly(rotation: str) -> str:
    """A disassembly with one dispatch and the given rotation in front of it.

    The rotation is the text of the instruction, so that the test can spell
    it the way the backend does: `rolq %rax` for a rotation by one and
    `rorq %rax` for a rotation by 63.
    """
    def at(offset: int, rva: int) -> str:
        """One instruction: its address and the global it reads."""
        return f"   {offset:x}:\tmovq\t0x{rva:x}(%rip),%rax        # 0x{IMAGE_BASE + rva:x}"

    return "\n".join([
        f"   113500:\tleaq\t0x{TABLE_RVA:x}(%rip),%rsi        # 0x{IMAGE_BASE + TABLE_RVA:x}",
        at(113507, RDATA_RVA + 0x00),
        at(113514, RDATA_RVA + 0x08),
        at(113521, RDATA_RVA + 0x10),
        f"   113528:\txorq\t%rcx,%rax",
        f"   113531:\timulq\t0x{RDATA_RVA + 0x18:x}(%rip),%rax        "
        f"# 0x{IMAGE_BASE + RDATA_RVA + 0x18:x}",
        f"   113538:\txorq\t0x{RDATA_RVA + 0x20:x}(%rip),%rax        "
        f"# 0x{IMAGE_BASE + RDATA_RVA + 0x20:x}",
        f"   113545:\t{rotation}",
        at(113547, RDATA_RVA + 0x28),
        "   113554:\tcallq\t*(%rsi,%rax,8)",
    ])


def the_image() -> PeImage:
    """The synthetic image the dispatch reads its constants from."""
    constants = list(KEY_PARTS) + [MULTIPLIER, XOR_CONSTANT, 0x0]
    rdata = bytearray()
    for value in constants:
        rdata += struct.pack("<Q", value)
    # The table of the module, at the RVA the `leaq` names.
    rdata += bytes(TABLE_RVA - RDATA_RVA - len(rdata))
    rdata += struct.pack("<Q", IMAGE_BASE + 0x1000)
    return PeImage(synthetic_image(bytes(rdata)))


def the_coverage(root: Path, rotate: int) -> Coverage:
    """The coverage of the synthetic module, with the given rotation."""
    tree = root / "rotation-tree"
    (tree / "payload").mkdir(parents=True, exist_ok=True)
    source = tree / "payload" / "solver.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")
    policy = root / "rotation-policy.json"
    policy.write_text('{"protect": ["cs2::aim::solve"]}\n', encoding="utf-8")
    coverage = root / "rotation-coverage"
    coverage.mkdir(exist_ok=True)
    path = coverage / "0x1.cov"
    write_coverage(path, digest_of(policy.read_bytes()), source, 7, rotate=rotate,
                   multiplier=MULTIPLIER, xor_constant=XOR_CONSTANT, keys=KEY_PARTS)
    return Coverage(path)


def check_the_reader() -> None:
    """The reader sees the rotation in all three spellings of the backend."""
    cases = (
        ("rolq $0x1f, %rax", 31),
        ("rolq $0x2, %rax", 2),
        ("rolq $0x3e, %rax", 62),
        ("rolq %rax", 1),
        ("rorq %rax", 63),
        ("rorq $0x2, %rax", 62),
        ("rorq $0x1, %rax", 63),
    )
    for text, expected in cases:
        found = rotation_of(text)
        check(found == expected,
              f"the reader read the rotation of '{text}' as {found}, not {expected}")

    # A rotation by a variable count is not a rotation of the mixer, and a
    # rotation of 0 or 64 is not a rotation at all.
    for text in ("rolq %cl, %rax", "rorq %cl, %rax", "rolq $0x0, %rax", "rorq $0x0, %rax"):
        check(rotation_of(text) is None,
              f"the reader read a rotation out of '{text}': it is not one of the mixer's")

    # And an instruction that is not a rotation at all.
    check(rotation_of("movq %rax, %rcx") is None, "the reader read a rotation out of a `movq`")


def check_the_dispatch(root: Path) -> None:
    """The dispatch of the image carries the rotation of its module."""
    image = the_image()
    for text, expected in (("rolq $0x1f, %rax", 31), ("rolq %rax", 1), ("rorq %rax", 63)):
        sites = find_dispatch_sites(the_disassembly(text), image.image_base, image)
        check(len(sites) == 1, f"the reader found {len(sites)} dispatches, not 1")
        site = sites[0]
        check(site.rotation == expected,
              f"the dispatch of '{text}' carries the rotation {site.rotation}, not {expected}")

        # The check of the payload has to accept it: the rotation of the
        # dispatch is the one of the module it belongs to.
        coverage = the_coverage(root, expected)
        check_payload.check_rotations(image, sites, [coverage])

        # And it has to reject a dispatch that rotates by another amount:
        # the modules of one build have different mixers, and the rotation
        # of a dispatch is the one of **its own** module.
        other = the_coverage(root, (expected % 63) + 1)
        try:
            check_payload.check_rotations(image, sites, [other])
        except Failure:
            pass
        else:
            raise TestFailure(
                f"the check accepted a dispatch that rotates by {expected} in a module that "
                f"encrypted with {(expected % 63) + 1}"
            )

    # A dispatch with no rotation in front of it is a failure: the image
    # does not hold the rotation of the mixer at all.
    sites = find_dispatch_sites(the_disassembly("movq %rax, %rcx"), image.image_base, image)
    check(len(sites) == 1, f"the reader found {len(sites)} dispatches, not 1")
    coverage = the_coverage(root, 31)
    try:
        check_payload.check_rotations(image, sites, [coverage])
    except Failure as error:
        check("no rotation" in str(error),
              f"the check did not name the missing rotation: {error}")
    else:
        raise TestFailure("the check accepted a dispatch with no rotation in front of it")


def main() -> int:
    try:
        check_the_reader()
        root = Path(tempfile.mkdtemp(prefix="cs2-protect-rotation-test-"))
        try:
            check_the_dispatch(root)
        finally:
            shutil.rmtree(root, ignore_errors=True)
    except TestFailure as error:
        print(f"rotation_test: {error}", file=sys.stderr)
        return 1

    print("rotation_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
