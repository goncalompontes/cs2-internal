"""Extract `resource/*.gameevents` from the CS2 VPKs and list the event names.

The client loads event definitions from `resource/core.gameevents`,
`resource/game.gameevents` and `resource/mod.gameevents` (see
`CGameEventManager::Init` in client.dll). This reads them straight out of the
game's VPK archives so the harness's event names *and field lists* are verified
against the build rather than hand-written.

Usage:
    python scripts/extract-gameevents.py [--game-dir DIR] [--out FILE] [--emit-header FILE]
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

VPK_MAGIC = 0x55AA1234
ARCHIVE_INLINE = 0x7FFF

# Declared field type -> accessor kind:
#   string -> GetString, bool -> GetBool, float -> GetFloat, player_* ->
#   GetPlayerIndex (slot 15), everything else (int/short/byte/uint64) -> GetInt.
FIELD_KINDS = {
    "string": "s",
    "bool": "b",
    "float": "f",
    "player_pawn": "p",
    "player_controller": "p",
    "player_controller_and_pawn": "p",
}
NUMERIC_KINDS = {
    "int", "uint64", "short", "byte", "long", "local",
}


def _cstr(buf: bytes, pos: int) -> tuple[str, int]:
    end = buf.index(b"\0", pos)
    return buf[pos:end].decode("utf-8", "replace"), end + 1


def read_tree(dir_vpk: Path) -> dict[str, tuple[int, int, bytes]]:
    """Map "path/name.ext" -> (archive_index, entry_offset, entry_length, preload)."""
    buf = dir_vpk.read_bytes()
    magic, version, tree_size, _data_size, md5_size, other_size, sig_size = struct.unpack_from(
        "<7I", buf, 0
    )
    if magic != VPK_MAGIC or version != 2:
        raise ValueError(f"{dir_vpk}: not a VPK v2 (magic={magic:#x} version={version})")
    data_offset = 28 + tree_size + md5_size + other_size + sig_size
    entries: dict[str, tuple[int, int, bytes]] = {}
    pos = 28
    end = 28 + tree_size
    while pos < end:
        ext, pos = _cstr(buf, pos)
        if ext == "":
            break
        while True:
            path, pos = _cstr(buf, pos)
            if path == "":
                break
            while True:
                name, pos = _cstr(buf, pos)
                if name == "":
                    break
                _crc, preload_bytes, archive, offset, length, term = struct.unpack_from(
                    "<IHHIIH", buf, pos
                )
                pos += 18
                assert term == 0xFFFF, "bad VPK entry terminator"
                preload = buf[pos : pos + preload_bytes]
                pos += preload_bytes
                key = f"{path}/{name}.{ext}"
                if archive == ARCHIVE_INLINE:
                    entries[key] = (archive, data_offset + offset, length, preload)
                else:
                    entries[key] = (archive, offset, length, preload)
    return entries


def read_entry(dir_vpk: Path, entry: tuple[int, int, bytes]) -> bytes:
    archive, offset, length, preload = entry
    if archive == ARCHIVE_INLINE:
        buf = dir_vpk.read_bytes()
        return preload + buf[offset : offset + length]
    chunk = dir_vpk.with_name(dir_vpk.name.replace("_dir.vpk", f"_{archive:03d}.vpk"))
    with chunk.open("rb") as handle:
        handle.seek(offset)
        return preload + handle.read(length)


def _strip_comments(text: str) -> str:
    out: list[str] = []
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            newline = text.find("\n", index)
            index = len(text) if newline == -1 else newline + 1
        elif text.startswith("/*", index):
            close = text.find("*/", index + 2)
            index = len(text) if close == -1 else close + 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def _tokenize(text: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    index = 0
    while index < len(text):
        char = text[index]
        if char in " \t\r\n":
            index += 1
        elif char in "{}":
            tokens.append((char, char))
            index += 1
        elif char == '"':
            end = index + 1
            while end < len(text) and text[end] != '"':
                end += 2 if text[end] == "\\" else 1
            tokens.append(("str", text[index + 1 : end]))
            index = end + 1
        else:
            end = index
            while end < len(text) and text[end] not in ' \t\r\n{}"':
                end += 1
            tokens.append(("str", text[index:end]))
            index = end
    return tokens


def parse_events(data: bytes) -> dict[str, list[tuple[str, str]]]:
    """Return {event_name: [(field_name, declared_type), ...]} in file order."""
    tokens = _tokenize(_strip_comments(data.decode("utf-8", "replace")))
    pos = 0

    def parse_block() -> list[tuple[str, object]]:
        nonlocal pos
        assert tokens[pos][0] == "{"
        pos += 1
        entries: list[tuple[str, object]] = []
        while pos < len(tokens) and tokens[pos][0] != "}":
            key = tokens[pos][1]
            pos += 1
            if pos < len(tokens) and tokens[pos][0] == "{":
                entries.append((key, parse_block()))
            else:
                entries.append((key, tokens[pos][1] if pos < len(tokens) else ""))
                pos += 1
        if pos < len(tokens):
            pos += 1  # closing brace
        return entries

    # Root: a single key followed by a block.
    while pos < len(tokens) and tokens[pos][0] != "{":
        pos += 1
    root = parse_block()
    events: dict[str, list[tuple[str, str]]] = {}
    for key, value in root:
        if isinstance(value, list):
            events[key] = [(name, kind) for name, kind in value if isinstance(kind, str)]
    return events


def emit_header(path: str, names: list[str], fields: list[tuple[str, str, str]]) -> None:
    lines = [
        "// Generated by scripts/extract-gameevents.py — do not edit by hand.",
        "//",
        "// The game-event names and field lists the target build defines, read from",
        "// its own resource/{core,game,mod}.gameevents. `is_known_event` lets the",
        "// harness reject a typo instead of silently waiting for an event that can",
        "// never fire; `event_fields` gives each event's fields and the accessor kind",
        f"// to read them with ('i' int, 'f' float, 'b' bool, 's' string) — {len(names)}",
        "// events, " + str(len(fields)) + " fields in build 25815307. Regenerate after",
        "// a game update.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <string_view>",
        "",
        "namespace cs2::control {",
        "",
        f"inline constexpr std::array<std::string_view, {len(names)}> KNOWN_EVENTS = {{{{",
    ]
    for name in names:
        lines.append(f'    "{name}",')
    lines += [
        "}};",
        "",
        "// True when `name` is a game event this build defines.",
        "[[nodiscard]] inline auto is_known_event(std::string_view name) noexcept -> bool {",
        "  for (const std::string_view known : KNOWN_EVENTS) {",
        "    if (known == name) {",
        "      return true;",
        "    }",
        "  }",
        "  return false;",
        "}",
        "",
        "// One declared field: the accessor kind is 'i' int, 'f' float, 'b' bool, 's' string.",
        "struct EventFieldDef {",
        "  std::string_view event;",
        "  std::string_view name;",
        "  char kind;",
        "};",
        "",
        f"inline constexpr std::array<EventFieldDef, {len(fields)}> KNOWN_EVENT_FIELDS = {{{{",
    ]
    for event, name, kind in fields:
        lines.append(f'    {{ "{event}", "{name}", \'{kind}\' }},')
    lines += [
        "}};",
        "",
        "// The declared fields of `event` (contiguous in KNOWN_EVENT_FIELDS), or",
        "// {nullptr, 0} for an event with none.",
        "struct EventFieldRange {",
        "  const EventFieldDef* data;",
        "  std::size_t size;",
        "};",
        "",
        "[[nodiscard]] inline auto event_fields(std::string_view event) noexcept -> EventFieldRange {",
        "  for (std::size_t i = 0; i < KNOWN_EVENT_FIELDS.size(); ++i) {",
        "    if (KNOWN_EVENT_FIELDS[i].event == event) {",
        "      std::size_t end = i;",
        "      while (end < KNOWN_EVENT_FIELDS.size() && KNOWN_EVENT_FIELDS[end].event == event) {",
        "        ++end;",
        "      }",
        "      return {&KNOWN_EVENT_FIELDS[i], end - i};",
        "    }",
        "  }",
        "  return {nullptr, 0};",
        "}",
        "",
        "}  // namespace cs2::control",
        "",
    ]
    Path(path).write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", default=r"F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game")
    parser.add_argument("--out", default=None, help="write the result as JSON")
    parser.add_argument("--emit-header", default=None,
                        help="write a C++ header of the verified names + fields")
    args = parser.parse_args(argv)

    game_dir = Path(args.game_dir)
    targets = ["resource/core.gameevents", "resource/game.gameevents", "resource/mod.gameevents"]
    found: dict[str, dict[str, list[tuple[str, str]]]] = {}
    for dir_vpk in sorted(game_dir.rglob("*_dir.vpk")):
        try:
            tree = read_tree(dir_vpk)
        except Exception as error:  # noqa: BLE001 - report and continue
            print(f"  skip {dir_vpk.name}: {error}", file=sys.stderr)
            continue
        for target in targets:
            if target in tree and target not in found:
                events = parse_events(read_entry(dir_vpk, tree[target]))
                found[target] = events
                fields = sum(len(v) for v in events.values())
                print(f"{target}: {len(events)} events, {fields} fields")

    kinds: dict[str, str] = {}
    # The same event can be defined in several files (`mod.gameevents` *extends*
    # `core.gameevents`); union the fields, keeping first-seen order.
    merged: dict[str, list[tuple[str, str]]] = {}
    unknown_types: dict[str, int] = {}
    for events in found.values():
        for name, declared in events.items():
            fields = merged.setdefault(name, [])
            seen = {existing for existing, _ in fields}
            for field, declared_type in declared:
                if field not in seen:
                    fields.append((field, declared_type))
                    seen.add(field)
                if declared_type in FIELD_KINDS:
                    kinds[field] = FIELD_KINDS[declared_type]
                elif declared_type in NUMERIC_KINDS:
                    kinds[field] = "i"
                else:
                    unknown_types[declared_type] = unknown_types.get(declared_type, 0) + 1

    names = sorted(merged)
    flat: list[tuple[str, str, str]] = []
    for name in names:
        for field, declared_type in merged[name]:
            if declared_type in FIELD_KINDS:
                kind = FIELD_KINDS[declared_type]
            elif declared_type in NUMERIC_KINDS:
                kind = "i"
            else:
                kind = "i"
            flat.append((name, field, kind))

    print(f"\ntotal distinct event names: {len(names)}; fields: {len(flat)}")
    if unknown_types:
        print("unmapped declared types (treated as int):", json.dumps(unknown_types, sort_keys=True))
    if args.emit_header:
        emit_header(args.emit_header, names, flat)
        print(f"wrote {args.emit_header} ({len(names)} names, {len(flat)} fields)")
    if args.out:
        Path(args.out).write_text(
            json.dumps(
                {"sources": {k: {e: f for e, f in v.items()} for k, v in found.items()},
                 "names": names},
                indent=2,
            ),
            encoding="utf-8",
        )
        print(f"wrote {args.out}")
    return 0 if names else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
