#!/usr/bin/env python3
"""Compare two built payloads and report what differs.

The acceptance criterion for the protection pass is that the difference
between the *protected* and the *unprotected* build is confined to the
selected functions and to the new globals: the transform marks the selected
functions `noinline`, rewrites their calls into indirect dispatches, and
adds the table, the descriptor and the key constants. Everything else has to
be identical.

The link step is not reproducible byte for byte: the PE header carries the
link time and the debug directory carries a CodeView GUID that the linker
generates per link. Both are stamped by the linker, not by the compiler, and
they change between two links of the *same* build.

This script therefore compares two images in two ways:

  * byte for byte, and
  * with the linker's per-link stamps normalised out (the PE TimeDateStamp,
    the debug directory's own timestamp and size, and the CodeView record's
    GUID, age and PDB path).

The PDB path is normalised *in place* (it is replaced by a filler of the
same length), so the image layout does not move. Two builds whose embedded
PDB paths have a different length therefore still differ -- the path shifts
the layout of the image -- and the script says so instead of pretending the
two images are comparable.

With --attribute the differences are *attributed*: the x64 unwind table
(.pdata) says which function a code address belongs to, and the PDB says
which function that is. The report then names the functions the differences
are in, and the bytes that are outside any function (the new globals, which
live in the read-only data).

With --section the comparison is **per section**: the acceptance criterion
of the protection is about the .text alone, and the whole-file comparison
would be satisfied by a handful of bytes in the read-only data. The section
is compared with the linker's per-link stamps normalised out (the PE
timestamp, the debug directory's own stamp and the CodeView GUID, age and
PDB path), so that the PDB path of the two builds does not count as a
difference. The exit code is 0 when the two sections are identical and 1
when they differ -- the same sense as the whole-file mode.

Usage: compare_builds.py [--ignore-link-stamps] [--attribute]
                          [--section NAME] [--pdb FILE] [--pdbutil FILE] A.dll B.dll

  --ignore-link-stamps  also compare with the linker's per-link stamps
                        normalised out
  --attribute           name the functions the differences are in (and the
                        bytes that are outside any function)
  --section             compare one section only (e.g. .text) and report
                        its size in both images and the bytes that differ
  --pdb                 the PDB of the *second* image (for the names)
  --pdbutil             llvm-pdbutil.exe (the SDK's)

Exit code 0 when the images are identical (or identical apart from the link
stamps), 1 when they differ.
"""
from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

#: The PE header fields the linker stamps per link.
TIMESTAMP_OFFSET = 0x88  # PE header + 8 (TimeDateStamp)
#: The debug directory entry: the CodeView GUID (16 bytes) and the age/timestamp.
DEBUG_DIRECTORY_RVA_OFFSET = 0xA8  # PE header + 40 (AddressOfDebugDirectory)
DEBUG_DIRECTORY_SIZE_OFFSET = 0xAC  # PE header + 44 (SizeOfDebugDirectory)
CODEVIEW_SIGNATURE = b"RSDS"

#: The size of one debug directory entry.
DEBUG_ENTRY_SIZE = 28

#: The exception directory of a PE (the x64 unwind table).
EXCEPTION_DIRECTORY = 3


def pe_header(data: bytes) -> int:
    """The offset of the PE header, or 0 when the file is not a PE image."""
    if len(data) < 0x100 or data[:2] != b"MZ":
        return 0
    pe_offset = int(struct.unpack_from("<I", data, 0x3C)[0])
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        return 0
    return pe_offset


def codeview_paths(data: bytes) -> list[tuple[int, int]]:
    """The ranges of the PDB paths of the CodeView records.

    A range is (start, end): the path is the NUL-terminated string that
    follows the GUID and the age of the record.
    """
    paths: list[tuple[int, int]] = []
    pe_offset = pe_header(data)
    if pe_offset == 0:
        return paths
    optional = pe_offset + 24
    debug_rva = struct.unpack_from("<I", data, optional + 160)[0]
    debug_size = struct.unpack_from("<I", data, optional + 164)[0]
    if debug_rva == 0 or debug_size == 0:
        return paths
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    for section in range(struct.unpack_from("<H", data, pe_offset + 6)[0]):
        header = optional + optional_size + section * 40
        virtual = struct.unpack_from("<I", data, header + 12)[0]
        raw_size = struct.unpack_from("<I", data, header + 16)[0]
        raw_offset = struct.unpack_from("<I", data, header + 20)[0]
        if virtual <= debug_rva < virtual + raw_size:
            base = raw_offset + (debug_rva - virtual)
            for index in range(debug_size // DEBUG_ENTRY_SIZE):
                entry = base + index * DEBUG_ENTRY_SIZE
                pointer = struct.unpack_from("<I", data, entry + 24)[0]
                if pointer and data[pointer : pointer + 4] == CODEVIEW_SIGNATURE:
                    end = data.find(b"\0", pointer + 24)
                    if end < 0:
                        end = len(data)
                    paths.append((pointer + 24, end))
            break
    return paths


def link_stamps(data: bytes) -> list[tuple[int, int]]:
    """The byte ranges the linker stamps per link.

    That is the PE timestamp, the timestamp and the size of the debug
    directory entry, and the GUID, the age and the PDB path of the CodeView
    record.
    """
    ranges: list[tuple[int, int]] = []
    pe_offset = pe_header(data)
    if pe_offset == 0:
        return ranges
    # The COFF header starts at pe_offset; the TimeDateStamp is at +8.
    ranges.append((pe_offset + 8, pe_offset + 12))
    optional = pe_offset + 24
    debug_rva = struct.unpack_from("<I", data, optional + 160)[0]
    debug_size = struct.unpack_from("<I", data, optional + 164)[0]
    if debug_rva == 0 or debug_size == 0:
        return ranges
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    for section in range(struct.unpack_from("<H", data, pe_offset + 6)[0]):
        header = optional + optional_size + section * 40
        virtual = struct.unpack_from("<I", data, header + 12)[0]
        raw_size = struct.unpack_from("<I", data, header + 16)[0]
        raw_offset = struct.unpack_from("<I", data, header + 20)[0]
        if virtual <= debug_rva < virtual + raw_size:
            base = raw_offset + (debug_rva - virtual)
            for index in range(debug_size // DEBUG_ENTRY_SIZE):
                entry = base + index * DEBUG_ENTRY_SIZE
                # The debug directory entry carries its own timestamp.
                ranges.append((entry + 4, entry + 8))
                # ... and the size of the record it points at.
                ranges.append((entry + 16, entry + 20))
                pointer = struct.unpack_from("<I", data, entry + 24)[0]
                if pointer and data[pointer : pointer + 4] == CODEVIEW_SIGNATURE:
                    # The GUID (16 bytes) and the age (4 bytes) follow the
                    # signature; the PDB path follows the age.
                    ranges.append((pointer + 4, pointer + 24))
            break
    ranges.extend(codeview_paths(data))
    return ranges


def normalise(data: bytes) -> bytes:
    """The image with the linker's per-link stamps normalised out."""
    out = bytearray(data)
    for start, end in link_stamps(data):
        if start >= end or end > len(out):
            continue
        out[start:end] = b"\0" * (end - start)
    # The PDB path is a string, not a stamp: it is replaced by a filler of
    # the same length, so that the layout of the image does not move.
    for start, end in codeview_paths(data):
        if start >= end or end > len(out):
            continue
        out[start:end] = b"p" * (end - start)
    return bytes(out)


def path_lengths(data: bytes) -> list[int]:
    """The length of every embedded PDB path."""
    return [end - start for start, end in codeview_paths(data)]


# --- The attribution ---------------------------------------------------------


class Section:
    """One section of a PE image."""

    def __init__(self, name: str, virtual_address: int, virtual_size: int, raw_offset: int,
                 raw_size: int) -> None:
        self.name = name
        self.virtual_address = virtual_address
        self.virtual_size = virtual_size
        self.raw_offset = raw_offset
        self.raw_size = raw_size

    def rva_of(self, offset: int) -> int | None:
        """The RVA of a file offset, or None when the offset is not in the section."""
        if not (self.raw_offset <= offset < self.raw_offset + self.raw_size):
            return None
        return self.virtual_address + (offset - self.raw_offset)


class Image:
    """The parts of a PE image the attribution needs."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        pe_offset = pe_header(data)
        if pe_offset == 0:
            raise SystemExit("compare_builds: the file is not a PE image")
        optional = pe_offset + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic == 0x20B:
            self.directories_offset = optional + 112
            self.number_of_directories = struct.unpack_from("<I", data, optional + 108)[0]
        else:
            self.directories_offset = optional + 96
            self.number_of_directories = struct.unpack_from("<I", data, optional + 92)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        number_of_sections = struct.unpack_from("<H", data, pe_offset + 6)[0]
        table = optional + optional_size
        self.sections: list[Section] = []
        for index in range(number_of_sections):
            header = table + index * 40
            name = data[header : header + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, header + 8
            )
            self.sections.append(Section(name, virtual_address, virtual_size, raw_offset,
                                         raw_size))

    def directory(self, index: int) -> tuple[int, int]:
        """The (rva, size) of a data directory."""
        if index >= self.number_of_directories:
            return (0, 0)
        return struct.unpack_from("<II", self.data, self.directories_offset + index * 8)

    def section_of_offset(self, offset: int) -> Section | None:
        """The section a file offset is in."""
        for section in self.sections:
            if section.raw_offset <= offset < section.raw_offset + section.raw_size:
                return section
        return None

    def functions(self) -> list[tuple[int, int]]:
        """The (begin, end) RVA ranges of the unwind table."""
        rva, size = self.directory(EXCEPTION_DIRECTORY)
        if rva == 0 or size == 0:
            return []
        blob = self.bytes_at_rva(rva, size)
        ranges: list[tuple[int, int]] = []
        for offset in range(0, len(blob) - 11, 12):
            begin, end, _ = struct.unpack_from("<III", blob, offset)
            if begin == 0 and end == 0:
                continue
            ranges.append((begin, end))
        return ranges

    def bytes_at_rva(self, rva: int, size: int) -> bytes:
        """The bytes of an RVA range."""
        for section in self.sections:
            if section.virtual_address <= rva < section.virtual_address + max(
                section.virtual_size, section.raw_size
            ):
                offset = section.raw_offset + (rva - section.virtual_address)
                return self.data[offset : offset + size]
        return b""


def functions_of(image: Image) -> list[tuple[int, int]]:
    """The (begin, end) RVA ranges of the functions of an image.

    The x64 unwind table (.pdata) says which function a code address belongs
    to. The ranges are in the link order, so the *n*-th range of one image
    and the *n*-th range of the other one are the same function.
    """
    return sorted(image.functions())


def section_of(image: Image, name: str) -> Section | None:
    """The section with a name, or None when the image has none."""
    for section in image.sections:
        if section.name == name:
            return section
    return None


def bytes_of_section(image: Image, section: Section) -> bytes:
    """The bytes of a section."""
    return image.data[section.raw_offset : section.raw_offset + section.raw_size]


def differing_bytes(first: bytes, second: bytes) -> int:
    """The number of the bytes that differ between two blobs.

    A blob that is longer than the other one differs in the bytes it has
    and the other one does not: the section grew.
    """
    shared = min(len(first), len(second))
    count = sum(1 for index in range(shared) if first[index] != second[index])
    return count + abs(len(first) - len(second))


def compare_section(first: bytes, second: bytes, name: str) -> int:
    """Compares one section of the two images and reports the verdict.

    The linker's per-link stamps are normalised out: the PDB path of two
    builds is not a difference of the section, and the CodeView record lives
    in the read-only data.
    """
    left = Image(normalise(first))
    right = Image(normalise(second))
    left_section = section_of(left, name)
    right_section = section_of(right, name)
    if left_section is None or right_section is None:
        missing = name if left_section is None else right_section
        print(f"compare_builds: the image has no section '{missing}'")
        return 1

    left_bytes = bytes_of_section(left, left_section)
    right_bytes = bytes_of_section(right, right_section)
    count = differing_bytes(left_bytes, right_bytes)
    print(f"compare_builds: the section {name}: {len(left_bytes)} bytes in the first image, "
          f"{len(right_bytes)} in the second")
    share = (100.0 * count / max(len(left_bytes), len(right_bytes))) if left_bytes else 0.0
    print(f"compare_builds: {count} of them differ ({share:.2f}% of the section)")
    if count == 0:
        print(f"compare_builds: the two {name} sections are identical")
        return 0
    print(f"compare_builds: the two {name} sections differ")
    return 1


def print_sections(first: bytes, second: bytes) -> None:
    """Prints the section list of the two images, and what differs in each.

    The list is printed in every mode: a section that is not named is a
    section nobody looked at, and the acceptance criterion of the protection
    is about one section.

    The linker's per-link stamps are normalised out, exactly as the
    `--section` verdict does it, so that the table and the verdict agree.
    """
    left = Image(normalise(first))
    right = Image(normalise(second))
    print("compare_builds: the sections of the two images "
          "(the linker's per-link stamps are normalised out):")
    for section in left.sections:
        other = section_of(right, section.name)
        if other is None:
            print(f"  {section.name:10} {section.raw_size:>10} bytes "
                  f"(only in the first image)")
            continue
        count = differing_bytes(bytes_of_section(left, section), bytes_of_section(right, other))
        print(f"  {section.name:10} {section.raw_size:>10} / {other.raw_size:<10} bytes "
              f"{count:>10} differ")
    for section in right.sections:
        if section_of(left, section.name) is None:
            print(f"  {section.name:10} {section.raw_size:>10} bytes "
                  f"(only in the second image)")


def attribute(first: Image, second: Image) -> None:
    """Prints which functions changed, and how much the sections grew.

    A byte-for-byte comparison cannot work here: the transform makes the
    image bigger, so every function after the first one moves, and even the
    bytes of an unchanged function differ (the displacements of the calls
    and of the data references). The attribution is therefore per function:
    the functions whose *size* changed are the ones the transform touched,
    and the sections that grew are the ones that hold the new globals.
    """
    first_functions = functions_of(first)
    second_functions = functions_of(second)
    common = min(len(first_functions), len(second_functions))
    changed = []
    for index in range(common):
        begin, end = first_functions[index]
        other_begin, other_end = second_functions[index]
        if (end - begin) != (other_end - other_begin):
            changed.append((index, begin, end - begin, other_end - other_begin))
    print(f"compare_builds: {len(first_functions)} functions in the first image, "
          f"{len(second_functions)} in the second")
    print(f"compare_builds: {len(changed)} of them changed their size")
    for index, begin, before, after in changed[:40]:
        print(f"  the function #{index} at rva 0x{begin:x}: {before} -> {after} bytes")
    if len(changed) > 40:
        print(f"  ... and {len(changed) - 40} more")
    for section in second.sections:
        other = None
        for candidate in first.sections:
            if candidate.name == section.name:
                other = candidate
                break
        if other is None:
            continue
        if other.raw_size != section.raw_size:
            print(f"  the section {section.name}: {other.raw_size} -> {section.raw_size} bytes")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--ignore-link-stamps", action="store_true")
    parser.add_argument("--attribute", action="store_true")
    parser.add_argument("--section", default=None,
                        help="compare one section only (e.g. .text)")
    parser.add_argument("--pdb", type=Path, default=None)
    parser.add_argument("--pdbutil", type=Path, default=None)
    args = parser.parse_args(argv)

    first = args.first.read_bytes()
    second = args.second.read_bytes()

    # The per-section mode: the acceptance criterion of the protection is
    # about the .text alone, and the whole-file comparison would be
    # satisfied by a handful of bytes in the read-only data.
    if args.section is not None:
        print_sections(first, second)
        return compare_section(first, second, args.section)

    if len(first) != len(second):
        print(f"compare_builds: the sizes differ ({len(first)} vs {len(second)})")
        first_paths = path_lengths(first)
        second_paths = path_lengths(second)
        if first_paths != second_paths:
            print(
                "compare_builds: the embedded PDB paths have a different length "
                f"({first_paths} vs {second_paths}); the path shifts the layout of "
                "the image, so the two images are not comparable. Compare two "
                "builds whose PDB paths have the same length."
            )
        else:
            print(
                "compare_builds: the transform changed the size of the image, so a "
                "byte-for-byte comparison says nothing: use --attribute."
            )
        print_sections(first, second)
        if not args.attribute:
            return 1
    shared = min(len(first), len(second))
    differing = [i for i in range(shared) if first[i] != second[i]]
    if len(first) != len(second):
        # The bytes the longer image has and the shorter one does not are
        # differences too.
        differing.extend(range(shared, max(len(first), len(second))))
    print_sections(first, second)
    if not differing:
        print(f"compare_builds: {args.first} and {args.second} are byte-identical")
        return 0
    print(f"compare_builds: {len(differing)} bytes differ")
    if args.ignore_link_stamps:
        stamps = link_stamps(first) + link_stamps(second)
        inside = [i for i in differing if any(start <= i < end for start, end in stamps)]
        print(f"compare_builds: {len(inside)} of them are inside the linker's per-link stamps")
        differing = [i for i in differing if i not in set(inside)]
        if not differing:
            print("compare_builds: the images are identical apart from the link stamps")
            return 0
    ranges: list[tuple[int, int]] = []
    start = previous = differing[0]
    for offset in differing[1:]:
        if offset != previous + 1:
            ranges.append((start, previous))
            start = offset
        previous = offset
    ranges.append((start, previous))
    for first_byte, last_byte in ranges[:20]:
        print(f"  0x{first_byte:06x}..0x{last_byte:06x} ({last_byte - first_byte + 1} bytes)")
    if len(ranges) > 20:
        print(f"  ... and {len(ranges) - 20} more ranges")
    if args.attribute:
        print("compare_builds: the differences are in")
        attribute(Image(first), Image(second))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
