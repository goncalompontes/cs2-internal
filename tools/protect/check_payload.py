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
    in the unprotected one.

The last check is the one that catches the dangerous failure: the key is a
compile-time key, so the whole dispatch is a compile-time constant, and a
compiler that folds it away leaves a *direct* call behind. The image would
then behave exactly like the unprotected one, and the protection would be
void.

Usage: check_payload.py --protected FILE --unprotected FILE --objdump FILE
                       [--coverage DIR] [--policy FILE]
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

#: The relocations the mapper applies. 10 is IMAGE_REL_BASED_DIR64.
IMAGE_REL_BASED_DIR64 = 10

#: The exception directory of a PE (the x64 unwind table).
EXCEPTION_DIRECTORY = 3

#: The base relocation directory of a PE.
BASE_RELOCATION_DIRECTORY = 5


class Failure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the check with a message that says what did not hold."""
    if not condition:
        raise Failure(what)


# --- The PE image ------------------------------------------------------------


class Section:
    """One section of a PE image."""

    def __init__(self, name: str, virtual_address: int, virtual_size: int, raw_offset: int,
                 raw_size: int) -> None:
        self.name = name
        self.virtual_address = virtual_address
        self.virtual_size = virtual_size
        self.raw_offset = raw_offset
        self.raw_size = raw_size

    def contains_rva(self, rva: int) -> bool:
        """Whether an RVA falls in this section."""
        return self.virtual_address <= rva < self.virtual_address + max(self.virtual_size,
                                                                       self.raw_size)


class PeImage:
    """The parts of a PE image the checks need."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        if data[:2] != b"MZ":
            raise Failure("the file is not a PE image")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise Failure("the file is not a PE image")
        coff = pe_offset + 4
        self.machine = struct.unpack_from("<H", data, coff)[0]
        number_of_sections = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        optional = coff + 20
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic == 0x20B:
            self.image_base = struct.unpack_from("<Q", data, optional + 24)[0]
            directories = optional + 112
        else:
            self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
            directories = optional + 96
        number_of_directories = struct.unpack_from("<I", data, optional + 108 if magic == 0x20B
                                                  else optional + 92)[0]

        self.sections: list[Section] = []
        table = optional + optional_size
        for index in range(number_of_sections):
            header = table + index * 40
            name = data[header : header + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, header + 8
            )
            self.sections.append(Section(name, virtual_address, virtual_size, raw_offset,
                                         raw_size))

        self.directories: list[tuple[int, int]] = []
        for index in range(min(number_of_directories, 16)):
            self.directories.append(
                struct.unpack_from("<II", data, directories + index * 8)
            )

    def directory(self, index: int) -> tuple[int, int]:
        """The (rva, size) of a data directory."""
        if index >= len(self.directories):
            return (0, 0)
        return self.directories[index]

    def section_of(self, name: str) -> Section | None:
        """The section with a name."""
        for section in self.sections:
            if section.name == name:
                return section
        return None

    def bytes_at_rva(self, rva: int, size: int) -> bytes:
        """The bytes of an RVA range."""
        for section in self.sections:
            if section.contains_rva(rva):
                offset = section.raw_offset + (rva - section.virtual_address)
                return self.data[offset : offset + size]
        raise Failure(f"the RVA 0x{rva:x} is not in any section")


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


def count_dispatches(objdump: Path, binary: Path) -> int:
    """The number of encrypted dispatches in a binary.

    A dispatch is an indirect call through the table: `call *(%rX,%rY,8)`.
    The direct call that the pass replaced is gone, so the count is the
    number of the call sites the pass rewrote.
    """
    completed = subprocess.run([str(objdump), "-d", str(binary)], capture_output=True, text=True)
    if completed.returncode != 0:
        raise Failure(f"objdump failed on '{binary}':\n{completed.stderr}")
    return len(re.findall(r"(?:callq|jmpq)\s+\*\(%r[a-z0-9]+,%r[a-z0-9]+,8\)", completed.stdout))


def reported_rewrites(coverage: Path, policy: Path) -> int:
    """The number of the calls the pass reports having rewritten.

    The coverage files carry one line per selected function with the number
    of the rewritten calls (`fn <symbol> rewritten=<n> skipped=<n>`).
    """
    if not policy.is_file():
        return 0
    digest = hashlib.sha256(policy.read_bytes()).hexdigest().upper()
    total = 0
    for path in sorted(coverage.glob("*.cov")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if f"policy-sha256 {digest}" not in text:
            continue
        for line in text.splitlines():
            if not line.startswith("fn "):
                continue
            for part in line.split():
                if part.startswith("rewritten="):
                    total += int(part.split("=", 1)[1])
    return total


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protected", type=Path, required=True)
    parser.add_argument("--unprotected", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, default=None)
    parser.add_argument("--policy", type=Path, default=None)
    args = parser.parse_args(argv)

    coverage = args.coverage
    if coverage is None:
        env = os.environ.get("CS2_PROTECT_COVERAGE", "").strip()
        coverage = Path(env) if env else None
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

        plain_dispatches = count_dispatches(args.objdump, args.unprotected)
        protected_dispatches = count_dispatches(args.objdump, args.protected)
        print(f"check_payload: the protected image has {protected_dispatches} encrypted dispatches, "
              f"the unprotected one {plain_dispatches}")
        check(plain_dispatches == 0, "the unprotected image has an encrypted dispatch")
        if coverage is not None and policy is not None:
            expected = reported_rewrites(coverage, policy)
            check(
                protected_dispatches == expected,
                f"the protected image has {protected_dispatches} encrypted dispatches, but the "
                f"pass reported {expected} rewrites: the compiler folded the dispatch away",
            )
            print(f"check_payload: the pass reported {expected} rewrites, and the image has them")
        else:
            print("check_payload: no coverage given: the number of the rewrites is not checked")
    except Failure as error:
        print(f"check_payload: {error}", file=sys.stderr)
        return 1

    print("check_payload: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
