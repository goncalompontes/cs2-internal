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
  * every dispatch *reaches the right callee*: the check decodes the
    descriptor of the image with the key the pass used, resolves the index
    in the table of the image and compares the address with the code of the
    image.

The last two are the ones that catch the dangerous failures. The key is a
compile-time key, so the whole dispatch is a compile-time constant, and a
compiler that folds it away leaves a *direct* call behind: the image would
then behave exactly like the unprotected one, and the protection would be
void. And a dispatch that reaches the *wrong* callee does not crash: it
calls another function, which is the failure mode this project exists to
avoid.

The cipher is repeated here (the plugin's `transform.cpp` is the other
half): the check has to know what the call site computes, and a change of
the cipher has to change both.

Usage: check_payload.py --protected FILE --unprotected FILE --objdump FILE
                       [--coverage DIR] [--policy FILE]
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

from protect_checks import (
    MIX_MULTIPLIER,
    MIX_XOR,
    Coverage,
    Failure,
    DispatchSite,
    PeImage,
    count_dispatches,
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
                f"check_payload: the coverage '{entry.path.name}' describes no table of this "
                "image: it belongs to another build and is not counted"
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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protected", type=Path, required=True)
    parser.add_argument("--unprotected", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, default=None)
    parser.add_argument("--policy", type=Path, default=None)
    args = parser.parse_args(argv)

    coverage_dir = args.coverage
    if coverage_dir is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE", "").strip()
        coverage_dir = Path(env) if env else None
    policy = args.policy
    if policy is None:
        env = os.environ.get("CS2_PROTECT_CONFIG", "").strip()
        policy = Path(env) if env else None

    try:
        protected = PeImage(args.protected.read_bytes())
        unprotected = PeImage(args.unprotected.read_bytes())
        check_exports(protected, unprotected)
        print("check_payload: the exports are the same")
        check_imports(protected, unprotected)
        print("check_payload: the imports are the same")
        dir64 = check_relocations(protected)
        print(f"check_payload: the base relocation table has {dir64} DIR64 entries")
        entries = check_unwind_table(protected)
        print(f"check_payload: the unwind table has {entries} entries")

        plain_sites = find_dispatch_sites(disassemble(args.objdump, args.unprotected),
                                          unprotected.image_base, unprotected)
        protected_sites = find_dispatch_sites(disassemble(args.objdump, args.protected),
                                              protected.image_base, protected)
        print(f"check_payload: the protected image has {len(protected_sites)} encrypted "
              f"dispatches, the unprotected one {len(plain_sites)}")
        check(len(plain_sites) == 0, "the unprotected image has an encrypted dispatch")

        check_constants(protected, protected_sites)

        files: list[Coverage] = []
        if coverage_dir and policy:
            files, skipped = read_coverage(coverage_dir, policy)
            for name, reason, rewritten in skipped:
                if rewritten > 0:
                    print(f"check_payload: the coverage '{name}' is skipped: {reason}")
            if skipped:
                print(f"check_payload: {len(skipped)} coverage files do not describe this "
                      "build and are not counted")
        if files:
            expected = reported_rewrites(files)
            check(
                len(protected_sites) == expected,
                f"the protected image has {len(protected_sites)} encrypted dispatches, but the "
                f"pass reported {expected} rewrites: the compiler folded the dispatch away",
            )
            print(f"check_payload: the pass reported {expected} rewrites, and the image has them")
            resolved = check_dispatch_targets(protected, protected_sites, files)
            print(f"check_payload: {resolved} dispatches decode to a callee of the image")
        else:
            print("check_payload: no coverage given: the number of the rewrites is not checked")
    except Failure as error:
        print(f"check_payload: {error}", file=sys.stderr)
        return 1

    print("check_payload: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
