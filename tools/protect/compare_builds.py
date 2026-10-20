#!/usr/bin/env python3
"""Compare two built payloads and report what differs.

The acceptance criterion for the protection pass is that the *protected*
build is identical to the unprotected one, because the pass only reports and
changes no code. The link step is not reproducible byte for byte: the PE
header carries the link time and the debug directory carries a CodeView GUID
that the linker generates per link. Both are stamped by the linker, not by
the compiler, and they change between two links of the *same* build.

This script therefore compares two images in two ways:

  * byte for byte, and
  * with the linker's per-link stamps normalised out (the PE TimeDateStamp,
    the debug directory's own timestamp and size, and the CodeView record's
    GUID, age and PDB path).

The PDB path is normalised *in place* (it is replaced by a filler of the
same length), so the image layout does not move. Two builds whose embedded
PDB paths have a different length therefore still differ -- the path shifts
the layout of the image -- and the script says so instead of pretending the
two are comparable.

Usage: compare_builds.py [--ignore-link-stamps] A.dll B.dll

  --ignore-link-stamps  also compare with the linker's per-link stamps
                        normalised out

Exit code 0 when the images are identical (or identical apart from the link
stamps), 1 when they differ.
"""
import argparse
import struct
import sys
from pathlib import Path

# The PE header fields the linker stamps per link.
TIMESTAMP_OFFSET = 0x88  # PE header + 8 (TimeDateStamp)
# The debug directory entry: the CodeView GUID (16 bytes) and the age/timestamp.
DEBUG_DIRECTORY_RVA_OFFSET = 0xA8  # PE header + 40 (AddressOfDebugDirectory)
DEBUG_DIRECTORY_SIZE_OFFSET = 0xAC  # PE header + 44 (SizeOfDebugDirectory)
CODEVIEW_SIGNATURE = b"RSDS"

# The size of one debug directory entry.
DEBUG_ENTRY_SIZE = 28


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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--ignore-link-stamps", action="store_true")
    args = parser.parse_args(argv)

    first = args.first.read_bytes()
    second = args.second.read_bytes()
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
        return 1
    differing = [i for i in range(len(first)) if first[i] != second[i]]
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
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
