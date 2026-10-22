#!/usr/bin/env python3
"""The synthetic image and the synthetic coverage the tests share.

The readers of an image need a `.text` (the code addresses of a table have to
be in it) and a `.rdata` (the table, the descriptor and the constants of the
mixer). A real image would do, but it would tie the test to a build; the
synthetic one is exact and it is a dozen lines.

The coverage writer is here for the same reason: the readers of the coverage
and the checks that use them have to agree on the *format*, and one writer is
the only way they can.

The module is imported by the tests, not run.
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

#: The image base of the synthetic image the table test builds.
IMAGE_BASE = 0x180000000

#: The RVA of the `.text` section of the synthetic image.
TEXT_RVA = 0x1000

#: The RVA of the `.rdata` section of the synthetic image.
RDATA_RVA = 0x2000


def digest_of(data: bytes) -> str:
    """The digest of a file, as the plugin spells it (upper case)."""
    return hashlib.sha256(data).hexdigest().upper()


def synthetic_image(rdata: bytes) -> bytes:
    """A PE image with a `.text` section and a `.rdata` section.

    The image is the smallest one the reader of the check accepts: the
    tests of the table lookup need a `.text` (the code addresses of the
    table have to be in it) and a `.rdata` (the table and its descriptor).
    """
    text = bytes(0x100)
    headers = bytearray(0x200)
    headers[0:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x80)
    headers[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<H", headers, coff, 0x8664)        # the machine
    struct.pack_into("<H", headers, coff + 2, 2)         # the number of sections
    struct.pack_into("<H", headers, coff + 16, 0xF0)     # the size of the optional header
    optional = coff + 20
    struct.pack_into("<H", headers, optional, 0x20B)     # PE32+
    struct.pack_into("<Q", headers, optional + 24, IMAGE_BASE)
    struct.pack_into("<I", headers, optional + 108, 16)  # the number of the directories
    table = optional + 0xF0
    sections = ((".text", TEXT_RVA, len(text), len(headers)),
                (".rdata", RDATA_RVA, len(rdata), len(headers) + len(text)))
    for index, (name, rva, size, offset) in enumerate(sections):
        header = table + index * 40
        headers[header:header + len(name)] = name.encode("ascii")
        struct.pack_into("<IIII", headers, header + 8, size, rva, size, offset)
    body = bytearray(headers)
    body += text
    body += rdata
    return bytes(body)


def write_coverage(path: Path, policy_digest: str, source: Path, rewritten: int,
                   vm: str | None = None, layout: str | None = None,
                   cross: str | None = None, direct: str | None = None,
                   cross_rewritten: int = 0, multiplier: int = 0x9E3779B97F4A7C15,
                   xor_constant: int = 0xBF58476D1CE4E5B9, rotate: int = 31,
                   keys: tuple[int, int, int] = (0x1, 0x2, 0x3)) -> None:
    """Writes a coverage file the way the plugin writes it.

    The `vm` line is the verdict of the virtualizer's analysis. It is
    optional: a coverage file of an older build has none, and a reader that
    does not know the section still reads the file. The `layout` line is the
    permutation of the module's function list, and it is optional in the same
    way.

    The `cross` and the `direct` lines are the cross-module routing: the
    state of every `cross_module` entry and the cross-TU callees that
    stayed direct. They are optional in the same way.

    The mixer of the build is named by the `protect` line: the multiplier,
    the xor constant, the rotation and the three parts of the key.
    """
    body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {source.name}\n"
        f"source-path {source.as_posix()}\n"
        f"source-sha256 {digest_of(source.read_bytes())}\n"
        f"protect table=2 descriptor=3 multiplier=0x{multiplier:016x} "
        f"xor=0x{xor_constant:016x} rotate={rotate} "
        f"key=0x{keys[0]:x} key=0x{keys[1]:x} key=0x{keys[2]:x}\n"
        "entry matched cs2::aim::solve\n"
        f"fn ?solve rewritten={rewritten} skipped=0 cross={cross_rewritten}\n"
    )
    if vm is not None:
        body += vm
    if layout is not None:
        body += layout
    if cross is not None:
        body += cross
    if direct is not None:
        body += direct
    path.write_text(body + f"content-sha256 {digest_of(body.encode('utf-8'))}\n",
                    encoding="utf-8")
