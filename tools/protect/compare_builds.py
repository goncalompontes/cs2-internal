#!/usr/bin/env python3
"""Compare two built payloads and report what differs.

The acceptance criterion for the protection pass is that the *protected*
build is byte-identical to the unprotected one, because the pass only reports
and changes no code. The link step is not reproducible byte for byte: the
PE header carries the link time and the debug directory carries a CodeView
GUID that the linker generates per link. Both are stamped by the linker, not
by the compiler, and they change between two links of the *same* build.

This script therefore compares two images in two ways:

  * byte for byte, and
  * with the linker's per-link stamps zeroed out (the PE TimeDateStamp and
    the CodeView GUID/timestamp in the debug directory).

Usage: compare_builds.py [--ignore-link-stamps] A.dll B.dll

  --ignore-link-stamps  also compare with the linker's per-link stamps
                        zeroed out

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


def link_stamps(data: bytes) -> list[tuple[int, int]]:
    """The byte ranges the linker stamps per link."""
    ranges: list[tuple[int, int]] = []
    if len(data) < 0x100 or data[:2] != b"MZ":
        return ranges
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        return ranges
    # The COFF header starts at pe_offset; the TimeDateStamp is at +8.
    ranges.append((pe_offset + 8, pe_offset + 12))
    optional = pe_offset + 24
    debug_rva = struct.unpack_from("<I", data, optional + 160)[0]
    debug_size = struct.unpack_from("<I", data, optional + 164)[0]
    if debug_rva == 0 or debug_size == 0:
        return ranges
    # The debug directory lives in the image; find its file offset.
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    for section in range(struct.unpack_from("<H", data, pe_offset + 6)[0]):
        header = optional + optional_size + section * 40
        virtual = struct.unpack_from("<I", data, header + 12)[0]
        raw_size = struct.unpack_from("<I", data, header + 16)[0]
        raw_offset = struct.unpack_from("<I", data, header + 20)[0]
        if virtual <= debug_rva < virtual + raw_size:
            base = raw_offset + (debug_rva - virtual)
            for index in range(debug_size // 28):
                entry = base + index * 28
                # The debug directory entry carries its own timestamp.
                ranges.append((entry + 4, entry + 8))
                pointer = struct.unpack_from("<I", data, entry + 24)[0]
                if pointer and data[pointer : pointer + 4] == CODEVIEW_SIGNATURE:
                    # The GUID (16 bytes) and the age (4 bytes) follow the
                    # signature; the PDB path follows the age.
                    ranges.append((pointer + 4, pointer + 24))
            break
    return ranges


def normalise(data: bytes) -> bytes:
    """The image with the linker's per-link stamps zeroed out."""
    out = bytearray(data)
    for start, end in link_stamps(data):
        out[start:end] = b"\0" * (end - start)
    return bytes(out)


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
