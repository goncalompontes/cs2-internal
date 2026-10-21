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
    and it protects nothing.

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
import os
import re
import struct
import subprocess
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
    """The mixed key of a module: what the call site xors the descriptor with."""
    key = 0
    for part in entry.key_parts:
        key ^= part
    return mix64(key)


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
    address into another register).
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
        if (rva - run) // 8 == entry.table_size:
            descriptor = descriptor_after(image, rva, entry, mixed, slot)
            if descriptor is not None:
                candidates.append(run)
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
    three key constants, the mixer is the one of the plugin, and the index
    selects the entry of the table.
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

    The number of the calls it left alone is returned too: the operator
    sees the CRT's symbols that the image still calls directly.
    """
    direct: dict[str, str] = {}
    for entry in coverage:
        for symbol, reason in entry.direct_callees().items():
            direct.setdefault(symbol, reason)

    failures: list[tuple[str, str]] = []
    excepted: list[str] = []
    left_alone = 0
    for symbol, reason in sorted(direct.items()):
        if symbol not in symbols:
            # An import or a CRT symbol the payload does not own: it names
            # no aim function, and it is left alone.
            left_alone += 1
            continue
        if reason == SRET_REASON:
            excepted.append(symbol)
            continue
        failures.append((symbol, reason))

    if failures:
        names = ", ".join(f"{symbol} ({reason})" for symbol, reason in failures)
        raise Failure(
            f"{len(failures)} cross-TU callees that a protected function calls directly are "
            f"the payload's own code: {names}. Add them to the policy's `cross_module` list"
        )
    if excepted:
        print(
            f"check_payload: {len(excepted)} cross-TU callees stay direct because of the "
            "`sret` rule (this task does not narrow it): " + ", ".join(excepted)
        )
    return len(excepted), left_alone


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
    policy = args.policy
    if policy is None:
        env = os.environ.get("CS2_PROTECT_CONFIG", "").strip()
        policy = Path(env) if env else None
    if coverage_dir is None and policy is not None:
        # The same default the plugin computes (the digest of the policy
        # path), so that the two find the same directory without the build
        # having to pass anything around.
        coverage_dir = default_coverage_dir(str(policy))
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
        else:
            print("check_payload: no coverage given: the number of the rewrites is not checked")
    except Failure as error:
        print(f"check_payload: {error}", file=sys.stderr)
        return 1

    print("check_payload: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
