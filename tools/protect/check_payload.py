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
    the image;
  * no protected function calls the payload's own code **directly**: the
    cross-TU callees that stayed direct are compared with the symbols the
    payload's own libraries define (read with the SDK's `llvm-nm`);
  * every `cross_module` entry of the policy names a symbol the payload
    defines: an entry that names a symbol that is gone is a stale entry,
    and it protects nothing;
  * the direct calls the payload's own objects still make to the
    `cross_module` symbols are **counted**: the routing rewrites the calls
    of the protected functions only, and the calls of the other functions
    stay direct. The count is a measurement, not a rule.

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
import re
import struct
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

from protect_checks import (
    Coverage,
    Failure,
    DispatchSite,
    Mixer,
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

#: The reason a cross-TU callee may stay direct without failing the build.
#:
#: The `sret` rule is not narrowed by this task (that is the other half of
#: the cross-TU work): a callee that returns a big aggregate keeps its
#: `sret` parameter, and the call stays direct. The check names those
#: callees instead of failing on them.
SRET_REASON = "indirect_parameter"

#: The reasons a `cross_module` entry cannot route a callee.
#:
#: The routing replaces the callee of a call with a value read from the
#: table of the caller. It cannot reproduce a variadic call, a tail call or
#: a call to an `optnone` function, so a callee with one of those
#: properties stays direct -- and no `cross_module` entry will ever route
#: it. The advice the check gives has to say so: an advice that says "add
#: it to the list" sends the operator in a circle.
UNROUTABLE_REASONS = {
    "variadic_callee": "the callee is variadic",
    "musttail": "the call is a tail call",
    "optnone_callee": "the callee is `optnone`",
}

#: A symbol of a `llvm-nm` listing: "0000000000000000 T ?solve@aim@cs2@@…".
NM_SYMBOL = re.compile(r"^(?:[0-9a-fA-F]+)?\s+(?P<type>[A-Za-z])\s+(?P<name>\S+)\s*$")


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


def check_constants(image: PeImage, mixers: list[Mixer]) -> None:
    """The constants of the mixer have to be in the read-only data.

    The call site reads them from the globals of the image, so the value the
    dispatch uses is the value the pass encrypted the descriptor with. A
    constant that the backend materialised as an instruction immediate
    instead can be a *different* one -- the X86 backend of this LLVM loses a
    bit of a 64-bit immediate in the shape the transform emits -- and the
    dispatch would then reach a wrong address without a crash. The constant
    is therefore in the data of the image, and the data holds it exactly.

    The constants are **per build**: the coverage names them, and the image
    has to hold the ones the coverage names.
    """
    data = image.section_of(".rdata")
    check(data is not None, "the image has no read-only data section")
    assert data is not None
    blob = image.data[data.raw_offset : data.raw_offset + data.raw_size]
    for mixer in mixers:
        for name, expected in (("the multiplier", mixer.multiplier),
                               ("the xor constant", mixer.xor_constant)):
            needle = expected.to_bytes(8, "little")
            check(
                needle in blob,
                f"{name} (0x{expected:016x}) is not in the read-only data of the image: the call "
                "site does not read it from a global, and the backend may have materialised it "
                "as a different value",
            )


def check_rotations(image: PeImage, sites: list[DispatchSite],
                    coverage: list[Coverage]) -> None:
    """The rotation of every dispatch has to be the one of **its own module**.

    The multiplier and the xor constant live in the data of the image, but the
    **rotation** is the one value of the mixer that is in the instruction
    stream (`rolq $imm8`, or the `D1` form the backend folds a rotation by
    one into). It is what makes the `.text` of two builds with two seeds
    differ, and the check has to prove that the image's instruction stream
    uses the rotation the pass encrypted with.

    The modules of one build have **different** mixers, so the rotation of a
    dispatch has to be the one of the module the dispatch belongs to -- and
    not one of the rotations of the build: with four modules that would be a
    one-in-four test. The module is found the way the decode finds it, by the
    constants the dispatch reads.
    """
    for entry in coverage:
        if entry.table_size == 0:
            # A module with nothing protected: it has no `protect` line, and
            # no rotation to check.
            continue
        rotate = entry.mix_rotate
        if rotate < 1 or rotate > 63:
            raise Failure(
                f"the coverage '{entry.path.name}' names the rotation {rotate}, and it is not in "
                "[1, 63]: a rotation of 0 makes the `lshr` by 64 of the decode undefined "
                "behaviour"
            )
    for site in sites:
        if site.rotation is None:
            raise Failure(
                f"the dispatch at 0x{site.address:x} has no rotation in front of it: the "
                "instruction stream does not hold the rotation of the mixer"
            )
        if site.rotation < 1 or site.rotation > 63:
            raise Failure(
                f"the dispatch at 0x{site.address:x} rotates by {site.rotation}, and it is not "
                "in [1, 63]"
            )
        found = entry_of_site(image, site, coverage)
        if found is None:
            raise Failure(
                f"the dispatch at 0x{site.address:x} has no coverage: the image and the coverage "
                "do not belong to the same build"
            )
        check(
            site.rotation == found.mix_rotate,
            f"the dispatch at 0x{site.address:x} rotates by {site.rotation}, and the module it "
            f"belongs to ('{found.path.name}') encrypted with {found.mix_rotate}: the image does "
            "not use the rotation the pass encrypted with",
        )


def check_layout(coverage: list[Coverage]) -> None:
    """Every module with more than one definition has to have moved one.

    The permutation of the function list is what makes the `.text` of two
    builds with two seeds differ. A module that has more than one definition
    and moved none is a module the pass did not permute -- and the report of
    the pass is the evidence the check reads.
    """
    for entry in coverage:
        if not entry.layout_present:
            # A coverage of an older build: it has no layout line, and the
            # check cannot say anything about the layout.
            continue
        if entry.layout_functions < 2:
            continue
        check(
            entry.layout_moved > 0,
            f"the coverage '{entry.path.name}' names {entry.layout_functions} definitions and "
            f"{entry.layout_moved} moved: the layout of the module was not permuted",
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
        found = entry_of_site(image, site, coverage)
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
            "in this image: the table entry does not hold the callee. If the entry names "
            "an import, the table holds the import's IAT slot instead of the function, "
            "and the dispatch calls the slot: a `cross_module` entry must not name an "
            "import",
        )
        total += 1
    return total


def entry_of_site(image: PeImage, site: DispatchSite, coverage: list[Coverage]) -> Coverage | None:
    """The coverage entry of the module a dispatch belongs to.

    The module is identified by the key the dispatch reads: the three key
    constants are the ones of the module, and they are the only ones that
    differ from module to module.
    """
    for entry in coverage:
        if entry.table_size == 0 or not entry.key_parts:
            continue
        if not site_reads(image, site, entry.mix_multiplier):
            continue
        if not site_reads(image, site, entry.mix_xor):
            continue
        if not all(site_reads(image, site, part) for part in entry.key_parts):
            continue
        return entry
    return None


def mixed_key(entry: Coverage) -> int:
    """The mixed key of a module: what the call site xors the descriptor with.

    The mixer is the one of the module: the coverage names it, and it is
    the one the pass encrypted the descriptor with.
    """
    key = 0
    for part in entry.key_parts:
        key ^= part
    return mix64(key, entry.mixer())


def tables_of(image: PeImage, entry: Coverage, slot: int) -> list[int]:
    """The tables of the module of a coverage entry.

    The table of the transform is a constant array of `table_size` function
    pointers in the read-only data, and the descriptor of the module follows
    it: the descriptor holds the encrypted indices of the rewritten call
    sites, and the slot the dispatch reads is one of them. The linker may
    leave a gap between the two, so the descriptor is looked for in the
    slots that follow the table.

    The table is found by its **content** and not by the register the
    dispatch calls through: the register allocation is the backend's
    business, and a linear scan of the disassembly does not model the
    control flow (a table loaded in a loop head is used by a dispatch that
    the scan reaches by another path, and a tail jump copies the table's
    address into another register). It is the **fallback** of the check:
    the register the dispatch calls through is tracked back to the `leaq`
    that loaded it, and that table is the module's own table (see
    `resolve_site`).

    A run of code addresses in the read-only data may be **longer** than the
    table: the linker may put two tables next to each other, or a global
    that holds a code address right after the table. Every start of the
    table inside the run is therefore tried, and the descriptor that
    follows it decides which one it is.
    """
    data = image.section_of(".rdata")
    if data is None:
        return []
    mixed = mixed_key(entry)
    candidates: list[int] = []
    run: int | None = None
    for offset in range(0, data.raw_size - 7, 8):
        rva = data.virtual_address + offset
        if image.is_code_address(image.u64_at_rva(rva)):
            if run is None:
                run = rva
            continue
        if run is None:
            continue
        for start in range(run, rva, 8):
            if start + 8 * entry.table_size > rva:
                break
            descriptor = descriptor_after(image, start + 8 * entry.table_size, entry, mixed, slot)
            if descriptor is not None:
                candidates.append(start)
        run = None
    return candidates


def descriptor_after(image: PeImage, table_end: int, entry: Coverage, mixed: int,
                     slot: int) -> int | None:
    """The descriptor of the module that follows a table, or None.

    The descriptor is a run of `descriptor_size` slots whose values decode
    to valid indices of the table, and the slot the dispatch reads is one of
    them. The linker may leave a gap between the table and the descriptor,
    so the slots that follow the table are tried in turn.
    """
    for gap in range(0, 8):
        start = table_end + 8 * gap
        if start > slot:
            break
        if not start <= slot < start + 8 * entry.descriptor_size:
            continue
        if descriptor_at(image, start, entry, mixed):
            return start
    return None


def descriptor_at(image: PeImage, base: int, entry: Coverage, mixed: int) -> bool:
    """Whether the slots at `base` are the descriptor of the module.

    Every slot of the descriptor holds an encrypted index, and the index is
    inside the table: `value ^ mixed < table_size`.
    """
    if entry.descriptor_size <= 0:
        return False
    for index in range(entry.descriptor_size):
        try:
            value = image.u64_at_rva(base + 8 * index)
        except Failure:
            return False
        if (value ^ mixed) >= entry.table_size:
            return False
    return True


def resolve_site(image: PeImage, site: DispatchSite, entry: Coverage) -> int | None:
    """The address the dispatch at a site calls, or None when it does not decode.

    The decode is the one the call site performs: the key is the xor of the
    three key constants, the mixer is the one of the plugin -- the one the
    coverage names, which is the one the pass encrypted the descriptor with
    -- and the index selects the entry of the table.
    """
    mixed = mixed_key(entry)

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

    # The **tracked** table is the primary source: the register the dispatch
    # calls through was loaded with the address of the table of the module,
    # and the dispatch reads *that* table. The link between the dispatch and
    # its own table is what the content scan gives up, and the scan is only
    # the fallback for the sites where the tracking failed (the backend may
    # copy the address of the table into another register, and then the
    # `leaq` the tracking looks for is not there).
    if site.table is not None:
        try:
            return image.u64_at_rva(site.table + 8 * index)
        except Failure:
            pass
    for table in tables_of(image, entry, descriptor):
        try:
            return image.u64_at_rva(table + 8 * index)
        except Failure:
            continue
    return None


def reported_rewrites(coverage: list[Coverage]) -> int:
    """The number of the calls the pass reports having rewritten."""
    return sum(entry.rewritten for entry in coverage)


# --- The payload's own code --------------------------------------------------


def find_nm(objdump: Path) -> Path | None:
    """The `llvm-nm` of the SDK the objdump came from.

    The `llvm-nm` is in the same directory as the `llvm-objdump` the check
    is given, so no build wiring changes: the check finds the tool of the
    SDK it already knows.
    """
    for name in ("llvm-nm.exe", "llvm-nm"):
        candidate = objdump.parent / name
        if candidate.is_file():
            return candidate
    return None


def library_symbols(nm: Path, directory: Path) -> set[str]:
    """The symbols the payload's own libraries define.

    The libraries are the ones in the build directory the check runs in:
    the check is part of the build, and the build directory is where they
    are.
    """
    symbols: set[str] = set()
    libraries = sorted(directory.rglob("*.lib")) + sorted(directory.rglob("*.a"))
    for library in libraries:
        completed = subprocess.run([str(nm), str(library)], capture_output=True, text=True)
        if completed.returncode != 0:
            continue
        for line in completed.stdout.splitlines():
            matched = NM_SYMBOL.match(line)
            if matched is None:
                continue
            if matched.group("type") == "U":
                # An undefined symbol: the library does not define it.
                continue
            symbols.add(matched.group("name"))
    return symbols


#: The header of a relocation block of `llvm-objdump -r`:
#: "RELOCATION RECORDS FOR [.text]:".
RELOCATION_SECTION = re.compile(r"^RELOCATION RECORDS FOR \[(?P<section>[^\]]+)\]:\s*$")

#: A relocation of `llvm-objdump -r`:
#: "0000000000000088 IMAGE_REL_AMD64_REL32    ??2@YAPEAX_K@Z".
RELOCATION = re.compile(
    r"^(?P<offset>[0-9a-fA-F]{16})\s+(?P<kind>IMAGE_REL_AMD64_\w+)\s+(?P<symbol>\S+)\s*$"
)

#: The relocation of a direct call (and of a direct jump) on x64.
IMAGE_REL_AMD64_REL32 = "IMAGE_REL_AMD64_REL32"

#: How many objects are read in one `objdump` call. The command line has a
#: limit, and one call per object would cost a process per file.
OBJECT_BATCH = 40


def direct_calls(objdump: Path, directory: Path, symbols: set[str]) -> dict[str, int]:
    """The direct calls the payload's own objects still make to the symbols.

    The scan is at the **object** level: a linked image has no relocation
    left for an internal call, so the object file is the only place where
    the direct edges are still visible. A REL32 relocation in `.text`
    against a symbol of the list is a direct call (or a direct jump) to it.

    The count is a **measurement**, not a rule: the pass rewrites the calls
    of the *selected* functions, and the calls the other functions make are
    still direct. The number is what the operator needs to know what the
    routing hides and what it does not.
    """
    if not symbols:
        return {}
    objects = sorted(directory.rglob("*.obj"))
    if not objects:
        return {}
    counts: dict[str, int] = {}
    for batch in _batched(objects, OBJECT_BATCH):
        section = ""
        for line in _relocations(objdump, batch).splitlines():
            header = RELOCATION_SECTION.match(line)
            if header is not None:
                section = header.group("section")
                continue
            if section != ".text":
                continue
            matched = RELOCATION.match(line)
            if matched is None:
                continue
            if matched.group("kind") != IMAGE_REL_AMD64_REL32:
                continue
            symbol = matched.group("symbol")
            if symbol not in symbols:
                continue
            counts[symbol] = counts.get(symbol, 0) + 1
    return counts


def _batched(items: list[Path], size: int) -> list[list[Path]]:
    """The items in batches of `size`."""
    return [items[index:index + size] for index in range(0, len(items), size)]


def _relocations(objdump: Path, objects: list[Path]) -> str:
    """The relocations of the objects, as `llvm-objdump -r` prints them.

    Only the `.text` section is asked for: the relocations of the other
    sections are the data's, and there are many more of them.
    """
    command = [str(objdump), "-r", "--section=.text"]
    completed = subprocess.run([*command, *[str(item) for item in objects]],
                               capture_output=True, text=True)
    if completed.returncode == 0:
        return completed.stdout
    # An objdump that does not know the `--section` flag: the section is in
    # the output anyway, and the reader skips the ones that are not `.text`.
    completed = subprocess.run([str(objdump), "-r", *[str(item) for item in objects]],
                               capture_output=True, text=True)
    if completed.returncode != 0:
        raise Failure(
            f"objdump failed on the objects of '{objects[0].parent}':\n{completed.stderr}"
        )
    return completed.stdout


def report_direct_calls(coverage: list[Coverage], objdump: Path, directory: Path) -> int:
    """The direct calls the image still makes to the `cross_module` symbols.

    The `cross_module` list is the operator's declaration of "these cross-TU
    edges must not be readable", and the routing rewrites the calls of the
    **protected** functions. The *same* symbols are still called directly by
    the functions the pass does not protect, and those edges are still in
    the image.

    The check counts them and prints the number: the routing's claim is
    about the edges of the protected functions, and the operator has to see
    what is left. The count is a measurement, not a rule -- the direct
    calls of the unprotected functions are the payload's business, and the
    task does not ask the pass to rewrite them.
    """
    symbols = {symbol for entry in coverage for _pattern, symbol in entry.cross_symbols()}
    if not symbols:
        return 0
    counts = direct_calls(objdump, directory, symbols)
    total = sum(counts.values())
    if not total:
        return 0
    names = ", ".join(f"{symbol} ({count})" for symbol, count in sorted(counts.items()))
    print(
        f"check_payload: {total} direct calls to a cross_module symbol are still in the "
        f"payload's own objects: {names}. The pass rewrites the calls of the protected "
        "functions only: the calls of the functions it does not protect stay direct"
    )
    return total


def check_cross_module_entries(coverage: list[Coverage], symbols: set[str]) -> None:
    """Every `cross_module` entry has to name a symbol the payload defines.

    The entry is the operator's declaration of "this cross-TU edge must not
    be readable". An entry that names a symbol the payload does not define
    is a **stale** entry: the function is gone (or was never there), and
    the entry protects nothing. The build has to say so.
    """
    stale: list[tuple[str, str]] = []
    for entry in coverage:
        for pattern, symbol in entry.cross_symbols():
            if symbol in symbols:
                continue
            stale.append((pattern, symbol))
    if not stale:
        return
    names = ", ".join(f"{pattern} ({symbol})" for pattern, symbol in sorted(set(stale)))
    raise Failure(
        f"{len(set(stale))} cross_module entries name a symbol the payload's own libraries "
        f"do not define: {names}. The entries are stale: add the symbol to the payload, or "
        "take the entry out of the policy"
    )


def check_direct_callees(coverage: list[Coverage], symbols: set[str]) -> tuple[int, int]:
    """No protected function may call the payload's own code directly.

    The cross-TU callees that stayed direct are the ones the coverage's
    `direct` lines name. A symbol the payload's own libraries define is
    the payload's own code, and the edge is still a direct call in the
    image.

    The `sret` edges are excepted: this task does not narrow the `sret`
    rule, so a callee that returns a big aggregate keeps its direct call.
    The check names those callees instead of failing on them.

    The exception is decided per **(module, symbol)** pair, not per symbol:
    the coverage of one module is the evidence of that module, and two
    modules may disagree about a callee. A symbol that one module excepts
    and another module refuses for another reason is a failure, whatever
    the order the coverage files are read in.

    The number of the calls it left alone is returned too: the operator
    sees the CRT's symbols that the image still calls directly.
    """
    # The reason is a property of the (module, callee) pair.
    reasons: dict[tuple[str, str], str] = {}
    for entry in coverage:
        module = entry.module or entry.path.name
        for symbol, reason in entry.direct_callees().items():
            reasons[(module, symbol)] = reason

    # The reasons of one symbol, over every module that mentions it.
    per_symbol: dict[str, list[str]] = {}
    for (_module, symbol), reason in reasons.items():
        per_symbol.setdefault(symbol, []).append(reason)

    failures: list[tuple[str, str]] = []
    excepted: list[str] = []
    left_alone = 0
    for symbol in sorted(per_symbol):
        module_reasons = per_symbol[symbol]
        if symbol not in symbols:
            # An import or a CRT symbol the payload does not own: it names
            # no aim function, and it is left alone.
            left_alone += 1
            continue
        # The reasons are sorted, so the one that is named does not depend
        # on the order the coverage files were read in.
        refused = sorted({reason for reason in module_reasons if reason != SRET_REASON})
        if refused:
            failures.append((symbol, refused[0]))
            continue
        excepted.append(symbol)

    if failures:
        names = ", ".join(f"{symbol} ({reason})" for symbol, reason in failures)
        advice = " ".join(dict.fromkeys(advice_for(reason) for _symbol, reason in failures))
        raise Failure(
            f"{len(failures)} cross-TU callees that a protected function calls directly are "
            f"the payload's own code: {names}. {advice}"
        )
    if excepted:
        print(
            f"check_payload: {len(excepted)} cross-TU callees stay direct because of the "
            "`sret` rule (this task does not narrow it): " + ", ".join(excepted)
        )
    return len(excepted), left_alone


def advice_for(reason: str) -> str:
    """What the operator has to do about a callee that stayed direct.

    The advice depends on the reason, because the reasons are not the same
    kind of thing:

    * a callee the policy does not name is refused with `external_callee`,
      and the `cross_module` list routes it -- the operator has to add it;
    * a callee the policy **does** name may still stay direct, because the
      routing cannot reproduce a variadic call, a tail call or a call to an
      `optnone` function. No `cross_module` entry will ever route it, and
      an advice that says "add it to the list" sends the operator in a
      circle.
    """
    if reason in UNROUTABLE_REASONS:
        return (
            f"The routing cannot bypass `{reason}` ({UNROUTABLE_REASONS[reason]}): the edge "
            "stays direct, and no `cross_module` entry will route it."
        )
    return (
        "A callee that is not in the policy's `cross_module` list is refused with "
        "`external_callee`, and the list routes it: add it to the list."
    )


# --- The protected strings ---------------------------------------------------


def image_runs(data: bytes) -> list[bytes]:
    """The NUL-terminated byte sequences of an image.

    A string literal is a NUL-terminated sequence of bytes in the data of the
    image, so splitting the image on the NUL bytes yields the candidates.

    The **duplicates** are dropped: the scan below only asks whether a
    literal is in the image, and the same run twice is the same answer.
    """
    return list({part for part in data.split(b"\0") if part})


def printable(byte: int) -> bool:
    """Whether a byte is a printable ASCII character."""
    return 0x20 <= byte < 0x7F


def nul_positions(data: bytes) -> list[int]:
    """The positions of the NUL bytes that can **split a string**.

    The scan needs them: a window that spans a NUL is not inside a run, and
    a literal whose content holds an embedded NUL lives in such a window.

    The image holds half a million NUL bytes -- alignment padding, GUIDs,
    other binary data -- and the windows around every one of them would cost
    two minutes per build. A NUL that splits a **string** has a printable
    byte on both sides; the binary data does not.
    """
    positions: list[int] = []
    index = data.find(b"\0")
    while index >= 0:
        if 0 < index < len(data) - 1 and printable(data[index - 1]) and printable(data[index + 1]):
            positions.append(index)
        index = data.find(b"\0", index + 1)
    return positions


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


def scan_literals(data: bytes, runs: list[bytes], by_length: dict[int, set[str]],
                  seed: int | None, forms: DigestForms) -> tuple[set[str], set[str]]:
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

    The scan also looks at the windows that **span** a NUL: a literal whose
    content holds an embedded NUL is not inside a run (the run is cut at the
    NUL), and a scan that only looks at the runs calls it absent -- the same
    mistake the prefix-only scan made, one level down.
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
                    raw = sha256(salt + piece).digest()
                    if raw in wanted:
                        name = raw.hex().upper()
                        if offset == 0 and size == length:
                            whole.add(name)
                        else:
                            part.add(name)
                if forms.plain:
                    raw = sha256(piece).digest()
                    if raw in wanted:
                        name = raw.hex().upper()
                        if offset == 0 and size == length:
                            whole.add(name)
                        else:
                            part.add(name)

        # The windows that span a NUL. A literal with an embedded NUL is cut
        # in two by the run split, so the run scan cannot see it.
        for position in nul_positions(data):
            first = position - length + 1
            if first < 0:
                first = 0
            for start in range(first, position + 1):
                piece = data[start:start + length]
                if len(piece) < length:
                    continue
                if forms.salted:
                    raw = sha256(salt + piece).digest()
                else:
                    raw = sha256(piece).digest()
                if raw in wanted:
                    part.add(raw.hex().upper())
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
    whole, part = scan_literals(protected.data, runs, by_length, seed, forms)
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
            f"(digest {digest[:12]}): the protection did nothing",
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
                    f"check_payload:   digest {digest[:12]}: refused in another module: that "
                    "module's copy of the literal survives the link"
                )
                continue
            print(
                f"check_payload:   digest {digest[:12]}: the bytes are the tail of another "
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
        plain_whole, plain_part = scan_literals(unprotected.data, plain_runs, by_length,
                                         seed, forms)
        missing = everywhere - (plain_whole | plain_part)
        if missing:
            check(
                False,
                f"the unprotected image does not hold the plaintext of {len(missing)} protected "
                f"strings (digest {sorted(missing)[0][:12]}): the check would be vacuous",
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

        # The coverage comes first: the constants of the mixer are per
        # build, and the check has to know them to tell a dispatch from a
        # switch table jump and to decode the descriptor.
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

        # The mixers the coverage names: the values the pass encrypted the
        # descriptors with.
        mixers = [entry.mixer() for entry in files if entry.table_size > 0]
        mixer_values = {value for mixer in mixers for value in (mixer.multiplier,
                                                                mixer.xor_constant)}

        protected_sites = find_dispatch_sites(disassemble(args.objdump, args.protected),
                                              protected.image_base, protected, mixer_values)
        if unprotected is not None:
            plain_sites = find_dispatch_sites(disassemble(args.objdump, args.unprotected),
                                              unprotected.image_base, unprotected)
            print(f"check_payload: the protected image has {len(protected_sites)} encrypted "
                  f"dispatches, the unprotected one {len(plain_sites)}")
            check(len(plain_sites) == 0, "the unprotected image has an encrypted dispatch")
        else:
            print(f"check_payload: the protected image has {len(protected_sites)} encrypted "
                  "dispatches")

        if mixers:
            check_constants(protected, mixers)
            check_rotations(protected, protected_sites, files)

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
            check_layout(files)

            # The cross-TU edges: a protected function must not call the
            # payload's own code directly, and a `cross_module` entry must
            # name a symbol the payload still defines.
            #
            # The rules need the payload's own symbol set, and it is read
            # with the SDK's `llvm-nm` over the payload's own libraries --
            # the ones in the build directory the check runs in. When they
            # cannot be found, the check fails: a rule that cannot be
            # evaluated must not be reported as passed.
            nm = find_nm(args.objdump)
            if nm is None:
                raise Failure(
                    f"the llvm-nm of the LLVM SDK was not found next to the objdump "
                    f"('{args.objdump}'): the check cannot tell the payload's own code from "
                    "the CRT's, and a rule that cannot be evaluated must not be reported as "
                    "passed"
                )
            build_directory = Path.cwd()
            symbols = library_symbols(nm, build_directory)
            if not symbols:
                raise Failure(
                    f"the payload's own libraries were not found in the build directory "
                    f"('{build_directory}'): the check cannot tell the payload's own code "
                    "from the CRT's, and a rule that cannot be evaluated must not be "
                    "reported as passed"
                )
            check_cross_module_entries(files, symbols)
            excepted, left_alone = check_direct_callees(files, symbols)
            print(
                f"check_payload: {len(symbols)} symbols of the payload's own libraries; "
                f"{excepted} cross-TU callees stay direct because of the `sret` rule, and "
                f"{left_alone} direct calls name a symbol the payload does not own"
            )

            # What the routing does **not** hide: the same symbols are still
            # called directly by the functions the pass does not protect. The
            # count is a measurement, not a rule, and the operator has to see
            # it: the `cross_module` list is about the edges of the protected
            # functions.
            report_direct_calls(files, args.objdump, build_directory)

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
