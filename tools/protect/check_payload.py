#!/usr/bin/env python3
"""The payload-level checks of the protection pass.

The pass rewrites the calls of a selected function into indirect dispatches
through an encrypted descriptor. The image has to survive that:

  * the exports and the imports are the same as the unprotected ones (the
    mapper resolves the imports by name, and the payload's exports are its
    contract);
  * the `.reloc` section is there with DIR64 entries (the mapper applies
    them; without them the image is mapped at the wrong place);
  * the exception directory (`.pdata`) is there and well formed (the mapper
    registers the unwind table and treats its absence as fatal);
  * the number of the encrypted dispatches in the protected image is the
    number of the calls the pass reports having rewritten -- and it is zero
    in the unprotected one;
  * every dispatch **decodes to a callee of the image**: the check decodes
    the descriptor of the image with the key the pass used, resolves the
    index in the table of the image, and checks that the address is code of
    the image.

The last two are the ones that catch the dangerous failures. The key is a
compile-time key, so the whole dispatch is a compile-time constant, and a
compiler that folds it away leaves a *direct* call behind: the image would
then behave exactly like the unprotected one, and the protection would be
void. And a dispatch that reaches a wrong address does not crash: it calls
another function, which is the failure mode this project exists to avoid.

What the last check proves is exactly this: the descriptor, decoded with
the key the pass used, names an entry of the table of the image, and that
entry is code of the image. It does **not** compare the entry with the
callee of the original call site (the plugin does not record it), so a
table that holds the wrong function would pass it.

The cipher is repeated here (the plugin's `transform.cpp` is the other
half): the check has to know what the call site computes, and a change of
the cipher has to change both.

Usage: check_payload.py --protected FILE --objdump FILE [--unprotected FILE]
                       [--coverage DIR] [--policy FILE] [--source-root DIR]

The unprotected image is optional: without it the checks that compare the
two images (the exports, the imports and the "the unprotected image has no
dispatch") are skipped, and the rest of the checks still run.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path
from typing import NamedTuple

from protect_checks import (
    MIX_MULTIPLIER,
    MIX_XOR,
    Coverage,
    Failure,
    DispatchSite,
    PeImage,
    count_dispatches,
    default_coverage_dir,
    disassemble,
    find_dispatch_sites,
    mix64,
    read_coverage,
)

#: The relocations the mapper applies. 10 is IMAGE_REL_BASED_DIR64.
IMAGE_REL_BASED_DIR64 = 10

#: The exception directory of a PE (the x64 unwind table).
EXCEPTION_DIRECTORY = 3

#: The base relocation directory of a PE.
BASE_RELOCATION_DIRECTORY = 5


def check(condition: bool, what: str) -> None:
    """Fails the check with a message that says what did not hold."""
    if not condition:
        raise Failure(what)


# --- The checks --------------------------------------------------------------


def check_exports(protected: PeImage, unprotected: PeImage) -> None:
    """The exports have to be the same."""
    first = exports_of(protected)
    second = exports_of(unprotected)
    check(
        first == second,
        f"the exports differ: {sorted(first - second)} only in the protected image, "
        f"{sorted(second - first)} only in the unprotected one",
    )


def exports_of(image: PeImage) -> set[str]:
    """The names of the exported symbols."""
    rva, size = image.directory(0)
    if rva == 0 or size == 0:
        return set()
    blob = image.bytes_at_rva(rva, size)
    names: set[str] = set()
    count = struct.unpack_from("<I", blob, 24)[0]
    for index in range(count):
        entry = 40 + index * 4
        name_rva = struct.unpack_from("<I", blob, entry)[0]
        if name_rva == 0:
            continue
        end = blob.find(b"\0", 0)
        raw = image.bytes_at_rva(name_rva, 256)
        names.add(raw.split(b"\0", 1)[0].decode("ascii", "replace"))
    return names


def check_imports(protected: PeImage, unprotected: PeImage) -> None:
    """The imports have to be the same."""
    first = imports_of(protected)
    second = imports_of(unprotected)
    check(
        first == second,
        f"the imports differ: {sorted(first - second)} only in the protected image, "
        f"{sorted(second - first)} only in the unprotected one",
    )


def imports_of(image: PeImage) -> set[str]:
    """The names of the imported symbols."""
    rva, size = image.directory(1)
    if rva == 0 or size == 0:
        return set()
    blob = image.bytes_at_rva(rva, size)
    names: set[str] = set()
    offset = 0
    while offset + 20 <= len(blob):
        descriptor = blob[offset : offset + 20]
        if descriptor == b"\0" * 20:
            break
        name_rva = struct.unpack_from("<I", descriptor, 12)[0]
        if name_rva:
            raw = image.bytes_at_rva(name_rva, 256)
            names.add(raw.split(b"\0", 1)[0].decode("ascii", "replace"))
        offset += 20
    return names


def check_relocations(image: PeImage) -> int:
    """The base relocation table has to be there, with DIR64 entries."""
    rva, size = image.directory(BASE_RELOCATION_DIRECTORY)
    check(rva != 0 and size != 0, "the image has no base relocation directory")
    blob = image.bytes_at_rva(rva, size)
    dir64 = 0
    offset = 0
    while offset + 8 <= len(blob):
        page_rva, block_size = struct.unpack_from("<II", blob, offset)
        if block_size < 8:
            break
        entries = (block_size - 8) // 2
        for index in range(entries):
            entry = struct.unpack_from("<H", blob, offset + 8 + index * 2)[0]
            kind = entry >> 12
            if kind == IMAGE_REL_BASED_DIR64:
                dir64 += 1
        offset += block_size
    check(dir64 > 0, "the base relocation table has no DIR64 entry")
    return dir64


def check_unwind_table(image: PeImage) -> int:
    """The x64 unwind table has to be there, and well formed."""
    rva, size = image.directory(EXCEPTION_DIRECTORY)
    check(rva != 0 and size != 0, "the image has no exception directory")
    check(size % 12 == 0, "the exception directory is not a multiple of 12 bytes")
    blob = image.bytes_at_rva(rva, size)
    entries = 0
    text = image.section_of(".text")
    for offset in range(0, len(blob), 12):
        begin, end, unwind = struct.unpack_from("<III", blob, offset)
        if begin == 0 and end == 0:
            continue
        entries += 1
        check(begin < end, f"the unwind entry {entries} has an empty range")
        check(
            text is not None and text.contains_rva(begin),
            f"the unwind entry {entries} does not start in the code",
        )
        check(unwind != 0, f"the unwind entry {entries} has no unwind information")
    check(entries > 0, "the exception directory is empty")
    return entries


def check_constants(image: PeImage, sites: list[DispatchSite]) -> None:
    """The constants of the mixer have to be in the read-only data.

    The call site reads them from the globals of the image, so the value the
    dispatch uses is the value the pass encrypted the descriptor with. A
    constant that the backend materialised as an instruction immediate
    instead can be a *different* one -- the X86 backend of this LLVM loses a
    bit of a 64-bit immediate in the shape the transform emits -- and the
    dispatch would then reach a wrong address without a crash. The constant
    is therefore in the data of the image, and the data holds it exactly.
    """
    data = image.section_of(".rdata")
    check(data is not None, "the image has no read-only data section")
    assert data is not None
    blob = image.data[data.raw_offset : data.raw_offset + data.raw_size]
    for name, expected in (("the multiplier", MIX_MULTIPLIER), ("the xor constant", MIX_XOR)):
        needle = expected.to_bytes(8, "little")
        check(
            needle in blob,
            f"{name} (0x{expected:016x}) is not in the read-only data of the image: the call "
            "site does not read it from a global, and the backend may have materialised it "
            "as a different value",
        )


def site_reads(image: PeImage, site: DispatchSite, expected: int) -> bool:
    """Whether the dispatch reads the given constant from the image.

    The constant has to be read from a global of the image: a constant that
    the backend materialised as an instruction immediate can be a *different*
    value (the X86 backend of this LLVM loses a bit of a 64-bit immediate in
    this shape), and a wrong constant means a wrong call target.
    """
    for reference in site.references:
        try:
            if image.u64_at_rva(reference) == expected:
                return True
        except Failure:
            continue
    return False


def check_dispatch_targets(image: PeImage, sites: list[DispatchSite],
                           coverage: list[Coverage]) -> int:
    """Every dispatch has to reach the callee the descriptor names.

    The check decodes the descriptor of the image with the key the pass used
    and resolves the index in the table of the image: the address the
    dispatch calls has to be the address of the table entry, and that address
    has to be code. A dispatch that reaches a wrong address is the failure
    the pass exists to prevent, and it does not crash: it calls another
    function.

    The coverage files carry the key of every module; a file that does not
    match any table of the image belongs to another build and is not
    evidence about this one.
    """
    matched: dict[int, Coverage] = {}

    for entry in coverage:
        if entry.table_size == 0 or not entry.key_parts:
            continue
        for site in sites:
            if site.table is None or site.table in matched:
                continue
            if not site_reads(image, site, entry.mix_multiplier):
                continue
            if not site_reads(image, site, entry.mix_xor):
                continue
            if not all(site_reads(image, site, part) for part in entry.key_parts):
                continue
            matched[site.table] = entry
            break

    for entry in coverage:
        if entry.rewritten > 0 and entry.path not in {item.path for item in matched.values()}:
            print(
                f"check_payload: the coverage '{entry.path.name}' names no table of this "
                "image: the dispatches of that module are not in it"
            )

    total = 0
    for site in sites:
        found: Coverage | None = matched.get(site.table) if site.table is not None else None
        if found is None:
            raise Failure(
                f"the dispatch at 0x{site.address:x} has no coverage: the image and the "
                "coverage do not belong to the same build"
            )
        resolved = resolve_site(image, site, found)
        check(
            resolved is not None,
            f"the dispatch at 0x{site.address:x} does not decode to a callee of the image",
        )
        assert resolved is not None
        check(
            image.is_code_address(resolved),
            f"the dispatch at 0x{site.address:x} calls 0x{resolved:x}, which is not code "
            "in this image",
        )
        total += 1
    return total


def resolve_site(image: PeImage, site: DispatchSite, entry: Coverage) -> int | None:
    """The address the dispatch at a site calls, or None when it does not decode.

    The decode is the one the call site performs: the key is the xor of the
    three key constants, the mixer is the one of the plugin, and the index
    selects the entry of the table.
    """
    key = 0
    for part in entry.key_parts:
        key ^= part
    mixed = mix64(key)

    if site.table is None:
        return None

    # The descriptor: the reference whose value decodes to an index of the
    # table.
    descriptor = None
    for reference in site.references:
        try:
            value = image.u64_at_rva(reference)
        except Failure:
            continue
        index = value ^ mixed
        if index < entry.table_size:
            descriptor = reference
            break
    if descriptor is None:
        return None

    index = image.u64_at_rva(descriptor) ^ mixed
    if index >= entry.table_size:
        return None
    try:
        return image.u64_at_rva(site.table + 8 * index)
    except Failure:
        return None


def reported_rewrites(coverage: list[Coverage]) -> int:
    """The number of the calls the pass reports having rewritten."""
    return sum(entry.rewritten for entry in coverage)


# --- The protected strings ---------------------------------------------------


def image_runs(data: bytes) -> list[bytes]:
    """The NUL-terminated byte sequences of an image.

    A string literal is a NUL-terminated sequence of bytes in the data of the
    image, so splitting the image on the NUL bytes yields the candidates.

    The **duplicates** are dropped: the scan below only asks whether a
    literal is in the image, and the same run twice is the same answer.
    """
    return list({part for part in data.split(b"\0") if part})


#: The text the proof of the salt is computed over. It is the plugin's
#: constant (tools/protect/src/strings.cpp).
SALT_PROOF_TEXT = b"cs2.protect.salt.proof"


def build_seed(policy: Path | None) -> int | None:
    """The seed of the build: the environment's, else the policy's.

    The digests of the literals are salted with it, so the check needs it to
    recognise a literal in the image. A build without a seed has no salt,
    and the digests are unsalted.
    """
    text = os.environ.get("CS2_PROTECT_SEED", "").strip()
    if not text and policy is not None and policy.is_file():
        try:
            document = json.loads(policy.read_text(encoding="utf-8", errors="replace"))
        except (OSError, ValueError):
            document = {}
        seed = document.get("seed") if isinstance(document, dict) else None
        if isinstance(seed, str):
            text = seed.strip()
        elif isinstance(seed, int):
            text = f"0x{seed:x}"
    if not text:
        return None
    digits = text
    if len(digits) > 2 and digits[0] == "0" and digits[1] in "xX":
        digits = digits[2:]
    try:
        # The seed is hexadecimal, and only its low 64 bits count.
        return int(digits, 16) & 0xFFFFFFFFFFFFFFFF
    except ValueError:
        return None


def salt_proof(seed: int | None) -> str:
    """The proof that a seed is the seed of the build."""
    if seed is None:
        return ""
    return hashlib.sha256(seed.to_bytes(8, "little") + SALT_PROOF_TEXT).hexdigest().upper()


class DigestForms(NamedTuple):
    """Which forms of the digest the coverage of this build holds."""

    #: The coverage names the salt: its digests are `sha256(seed || content)`.
    salted: bool
    #: The coverage names no salt: its digests are the unsalted `sha256(content)`.
    plain: bool


def digest_forms(coverage: list[Coverage]) -> DigestForms:
    """Which forms of the digest the coverage of this build holds.

    A coverage that names the salt carries **salted** digests, and a coverage
    of an older build carries the unsalted ones. The scan computes only the
    forms the coverage holds: the scan is the expensive part of this check,
    and a digest that cannot match is wasted work.
    """
    salted = False
    plain = False
    for entry in coverage:
        if not entry.strings:
            continue
        if entry.str_salt:
            salted = True
        else:
            plain = True
    return DigestForms(salted, plain)


def scan_literals(runs: list[bytes], by_length: dict[int, set[str]], seed: int | None,
                  forms: DigestForms) -> tuple[set[str], set[str]]:
    """The wanted literals the image holds: as a whole run, and inside a run.

    The scan is at **every offset** of every run. A literal that is in the
    image as a substring *is* in the image, and the MSVC ABI merges a literal
    with a longer literal that **ends with it**: the bytes of the deleted
    literal survive as the tail of the longer one, and a scan that only looks
    at the start of the runs cannot see it. A scan that cannot see it counts
    it as absent, and the number it publishes is the number it did not look
    at.

    Two sets are returned, and they are not the same failure:

    * the literals that are in the image as a **whole run** -- a
      NUL-terminated string of their own: a copy of the literal itself;
    * the literals that are only **inside** a run -- as its prefix or as its
      tail: the bytes of another string of the image.
    """
    whole: set[str] = set()
    part: set[str] = set()
    salt = seed.to_bytes(8, "little") if seed is not None else b""
    sha256 = hashlib.sha256
    for length, digests in by_length.items():
        wanted: set[bytes] = set()
        for digest in digests:
            try:
                wanted.add(bytes.fromhex(digest))
            except ValueError:
                continue
        if not wanted:
            continue
        for run in runs:
            size = len(run)
            if size < length:
                continue
            for offset in range(size - length + 1):
                piece = run[offset:offset + length]
                if forms.salted:
                    digest = sha256(salt + piece).digest()
                    if digest in wanted:
                        name = digest.hex().upper()
                        if offset == 0 and size == length:
                            whole.add(name)
                        else:
                            part.add(name)
                if forms.plain:
                    digest = sha256(piece).digest()
                    if digest in wanted:
                        name = digest.hex().upper()
                        if offset == 0 and size == length:
                            whole.add(name)
                        else:
                            part.add(name)
    return whole, part


def protected_literals(coverage: list[Coverage]) -> tuple[dict[int, set[str]], set[str], int]:
    """The digests of the literals the pass protected, and the refused ones.

    The digests are grouped by length: a run is only hashed with the lengths
    that are actually wanted. The third value is the number of literals the
    coverage records, protected or refused: a coverage with no literal at
    all is a coverage of a build without the string protection.
    """
    by_length: dict[int, set[str]] = {}
    refused: set[str] = set()
    total = 0
    for entry in coverage:
        for literal in entry.strings:
            total += 1
            digest = literal.digest
            if not digest:
                continue
            if literal.state == "protected":
                by_length.setdefault(literal.length, set()).add(digest)
            else:
                refused.add(digest)
    return by_length, refused, total


def check_protected_strings(protected: PeImage, unprotected: PeImage | None,
                             coverage: list[Coverage], policy: Path | None) -> int:
    """The plaintext of the protected strings has to be out of the image.

    The check is a byte scan: the coverage carries the length and the digest
    of every protected literal (never the content), and the scan hashes the
    candidates of the image and compares. The scan is at **every offset** of
    every run of the image, because a literal that is in the image as a
    substring is in the image -- the MSVC ABI merges a literal with a longer
    literal that ends with it, and the bytes of the deleted literal survive
    as the tail of the longer one.

    The number the check reports is the number the scan produced: the
    literals whose plaintext is not in the protected image, and the ones
    whose plaintext **is**. A literal that is protected in one module and
    **refused** in another is legitimately still in the image -- the gate
    does not half-protect -- and a literal whose bytes survive as the tail
    of another string of the image is greppable too. Both are reported with
    their coverage hash, and neither is a failure: the pass did its work on
    the literal it protected, and the two causes are properties of the ABI
    and of the payload's data model, not of the pass.

    What **is** a failure is a literal that is in the image as a whole
    string and that nobody refused: that is the pass's own copy, and it
    means the protection did nothing.
    """
    by_length, refused, total = protected_literals(coverage)
    if not by_length:
        # The policy may select nothing, in which case there is nothing to
        # check. A policy that selects strings and protected none is a
        # failure -- unless the coverage has no string section at all, which
        # means the coverage is from a build without the string protection.
        if not policy_selects_strings(policy):
            print("check_payload: no protected string in the coverage: the strings are not checked")
            return 0
        if total == 0:
            check(
                False,
                "the policy selects strings, but the coverage of this build carries no string "
                "section: the coverage is from a build without the string protection, and the "
                "strings are not checked",
            )
        check(
            False,
            "the policy selects strings, but no literal was protected: the protection did "
            "nothing",
        )

    # The salt: the digests of the literals are salted with the seed of the
    # build, and the coverage does not carry the salt. A check with the
    # wrong salt would find nothing in the image and would report a
    # protection that works, so the proof is verified first.
    seed = build_seed(policy)
    for entry in coverage:
        if not entry.str_salt:
            continue
        if entry.str_salt != "build-seed":
            check(
                False,
                f"the coverage is salted with '{entry.str_salt}', which this check does not "
                "know: the strings cannot be checked",
            )
        if seed is None:
            check(
                False,
                "the coverage is salted with the seed of the build, and this check does not "
                "have the seed: set CS2_PROTECT_SEED, or give the check the policy that "
                "carries it",
            )
        if entry.str_salt_proof and entry.str_salt_proof != salt_proof(seed):
            check(
                False,
                "the seed of this check is not the seed of the build: the digests of the "
                "coverage cannot be matched against the image",
            )

    forms = digest_forms(coverage)
    runs = image_runs(protected.data)
    everywhere = {digest for digests in by_length.values() for digest in digests}
    whole, part = scan_literals(runs, by_length, seed, forms)
    survivors = (whole | part) & everywhere
    absent = everywhere - survivors

    # A literal that is protected in one module and refused in another is
    # legitimately still in the image: the refused module keeps its copy,
    # and the linker keeps the COMDAT. A literal that nobody refused and
    # that is in the image as a **whole string** is a failure: that is the
    # pass's own copy, and it means the pass did not remove it. A literal
    # that is only *inside* another string of the image is greppable, but
    # the pass cannot remove another string's bytes -- it is reported.
    for digest in sorted(whole - refused):
        check(
            False,
            f"the protected image still holds the plaintext of a protected string "
            f"(sha256 {digest[:12]}): the protection did nothing",
        )

    print(f"check_payload: {len(absent)} of the {len(everywhere)} protected strings are absent "
          "from the protected image")
    if survivors:
        print(f"check_payload: {len(survivors)} of the {len(everywhere)} protected strings are "
              "still in the protected image")
        refused_elsewhere = survivors & refused
        merged = survivors - refused_elsewhere
        print(f"check_payload:   {len(refused_elsewhere)} of them are refused in another module: "
              "that module's copy of the literal survives the link")
        print(f"check_payload:   {len(merged)} of them are protected in every module: their "
              "bytes survive as the tail of another string of the image (the MSVC ABI merges a "
              "literal with a longer one that ends with it)")
        for digest in sorted(survivors):
            if digest in refused:
                print(
                    f"check_payload:   sha256 {digest[:12]}: refused in another module: that "
                    "module's copy of the literal survives the link"
                )
                continue
            print(
                f"check_payload:   sha256 {digest[:12]}: the bytes are the tail of another "
                "string of the image: the pass cannot remove another string's bytes"
            )

    if unprotected is not None:
        plain_runs = image_runs(unprotected.data)
        # The same scan as on the protected side, at every offset: the
        # unprotected image has to hold the plaintext of every protected
        # literal, or the comparison would be vacuous for that literal. A
        # scan that only looks at the start of the runs would fail a build
        # whose image holds the plaintext as the tail of another string --
        # a verdict that depends on the layout of the image and not on the
        # protection.
        plain_whole, plain_part = scan_literals(plain_runs, by_length, seed, forms)
        missing = everywhere - (plain_whole | plain_part)
        check(
            not missing,
            f"the unprotected image does not hold the plaintext of {len(missing)} protected "
            f"strings (sha256 {sorted(missing)[0][:12]}): the check would be vacuous",
        )
        print(f"check_payload: the unprotected image holds the plaintext of all the "
              f"{len(everywhere)} protected strings")

    return len(absent)


def policy_selects_strings(policy: Path | None) -> bool:
    """Whether the policy's `strings` section selects anything."""
    if policy is None or not policy.is_file():
        return False
    try:
        document = json.loads(policy.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return False
    strings = document.get("strings")
    return isinstance(strings, list) and bool(strings)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protected", type=Path, required=True)
    parser.add_argument("--unprotected", type=Path, default=None)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, default=None)
    parser.add_argument("--policy", type=Path, default=None)
    parser.add_argument("--source-root", type=Path, default=None)
    args = parser.parse_args(argv)

    coverage_dir = args.coverage
    if coverage_dir is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE", "").strip()
        coverage_dir = Path(env) if env else None
    # The policy, and the **spelling** the plugin was given: the coverage
    # directory is keyed by the path of the policy, and the plugin hashes the
    # environment's string -- not the path a reader builds out of it, which
    # spells the forward slashes as backslashes on Windows. The two have to
    # compute the same directory, so the key is the environment's spelling.
    policy_env = os.environ.get("CS2_PROTECT_CONFIG", "").strip()
    policy = args.policy
    if policy is None and policy_env:
        policy = Path(policy_env)
    if coverage_dir is None and policy is not None:
        # The same default the plugin computes (the digest of the policy
        # path), so that the two find the same directory without the build
        # having to pass anything around.
        coverage_dir = default_coverage_dir(policy_env or str(policy))
    source_root = args.source_root
    if source_root is None:
        env = os.environ.get("CS2_PROTECT_SOURCE_ROOT", "").strip()
        source_root = Path(env) if env else None

    try:
        protected = PeImage(args.protected.read_bytes())
        unprotected: PeImage | None = None
        if args.unprotected is not None:
            unprotected = PeImage(args.unprotected.read_bytes())
            check_exports(protected, unprotected)
            print("check_payload: the exports are the same")
            check_imports(protected, unprotected)
            print("check_payload: the imports are the same")
        else:
            print("check_payload: no unprotected image given: the exports and the imports are "
                  "not compared")
        dir64 = check_relocations(protected)
        print(f"check_payload: the base relocation table has {dir64} DIR64 entries")
        entries = check_unwind_table(protected)
        print(f"check_payload: the unwind table has {entries} entries")

        protected_sites = find_dispatch_sites(disassemble(args.objdump, args.protected),
                                              protected.image_base, protected)
        if unprotected is not None:
            plain_sites = find_dispatch_sites(disassemble(args.objdump, args.unprotected),
                                              unprotected.image_base, unprotected)
            print(f"check_payload: the protected image has {len(protected_sites)} encrypted "
                  f"dispatches, the unprotected one {len(plain_sites)}")
            check(len(plain_sites) == 0, "the unprotected image has an encrypted dispatch")
        else:
            print(f"check_payload: the protected image has {len(protected_sites)} encrypted "
                  "dispatches")

        check_constants(protected, protected_sites)

        files: list[Coverage] = []
        if coverage_dir and policy:
            read = read_coverage(coverage_dir, policy, source_root)
            for name, reason in read.malformed:
                print(f"check_payload: the coverage '{name}' is malformed: {reason}",
                      file=sys.stderr)
            for name, reason, rewritten in read.skipped:
                if rewritten > 0:
                    print(f"check_payload: the coverage '{name}' is skipped: {reason}")
            if read.skipped:
                print(f"check_payload: {len(read.skipped)} coverage files do not describe this "
                      "build and are not counted")
            check(
                not read.malformed,
                "a coverage file of this policy is malformed: the build's evidence is not "
                "trustworthy",
            )
            files = read.files
            check(
                bool(files),
                f"no coverage file of this policy describes this build (the directory is "
                f"'{coverage_dir}'): the number of the rewrites cannot be checked. The "
                "coverage of a build is written by the plugin while it compiles; re-run the "
                "Meson configure step and the build.",
            )
            expected = reported_rewrites(files)
            check(
                len(protected_sites) == expected,
                f"the protected image has {len(protected_sites)} encrypted dispatches, but the "
                f"pass reported {expected} rewrites: the compiler folded the dispatch away",
            )
            print(f"check_payload: the pass reported {expected} rewrites, and the image has them")
            resolved = check_dispatch_targets(protected, protected_sites, files)
            print(f"check_payload: {resolved} dispatches decode to a callee of the image")
            check_protected_strings(protected, unprotected, files, policy)
        else:
            print("check_payload: no coverage given: the number of the rewrites is not checked")
            check_protected_strings(protected, unprotected, [], policy)
    except Failure as error:
        print(f"check_payload: {error}", file=sys.stderr)
        return 1

    print("check_payload: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
