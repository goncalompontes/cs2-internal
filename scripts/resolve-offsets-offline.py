#!/usr/bin/env python3
"""Resolve the curated offsets **offline** - from the module files on disk.

The payload's offsets live in ``calibration/offsets.json``, and refreshing
that file used to mean running the game: ``scripts/calibrate-live.py``
attaches to ``cs2.exe`` and re-resolves the curated signatures in the
process' memory.  The **offsets** do not need the process, though: the dumper
has a ``--file`` mode that parses the PE on disk, maps it to RVA addressing
and runs the very same ``analysis::resolve_known`` - so the same signatures
resolve to the same RVAs with no game running at all.

This script makes that path first class:

* it asks the **dumper** which modules have curated signatures
  (``--list-signature-modules``, derived from the signature registry itself,
  so a signature added for a new module needs no edit here);
* it finds each module's file in the game's two trees (``game/bin/win64``
  and ``game/csgo/bin/win64`` - ``client.dll`` is in the ``csgo`` tree,
  ``engine2.dll`` and ``scenesystem.dll`` in the ``game`` tree);
* it resolves every module offline (``--file <path> --resolve-offsets --json
  --module <module>``);
* it **merges** the results and writes ``calibration/offsets.json`` in the
  same shape the live calibration writes - **atomically**: the file is only
  written after every module has been resolved, so a failure half-way leaves
  the previous dump untouched;
* it **names every signature that did not resolve**.  A signature that
  silently stops matching is the whole point of the exercise: the feature
  that needs it is dead until somebody looks.

**A dump that lost a signature is not a dump.**  A signature that did not
resolve means an offset the payload cannot use, so the write is **refused**
(exit code 1, the previous dump untouched) unless the operator says
``--allow-unresolved`` on purpose.  The same rule holds for a module that
failed and for a resolution that does not cover the module's signatures.

What it does **not** do: ``interfaces.json``, ``schema.json`` and
``schema_system.json`` come from the **live** process (its ``CreateInterface``
registry and its schema system), so they still need
``scripts/calibrate-live.py`` and a running game.  This script leaves them
untouched and says so.

Usage:

    python scripts/resolve-offsets-offline.py [--game-dir DIR] [--dump-dir DIR] [--check]
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import TypedDict, cast

#: The repository root (the script lives in ``scripts/``).
ROOT = Path(__file__).resolve().parent.parent

#: The game's module trees, relative to the game's root.  The layout differs
#: per module: ``client.dll`` is in the ``csgo`` tree, ``engine2.dll`` and
#: ``scenesystem.dll`` in the ``game`` tree - so **both** are searched.
GAME_MODULE_DIRS = ("game/bin/win64", "game/csgo/bin/win64")

#: The Steam folder name of the game (Steam appid 730).
GAME_FOLDER_NAME = "Counter-Strike Global Offensive"

#: The environment variable that overrides the discovered game directory.
ENV_GAME_DIR = "CS2_GAME_DIR"

#: The environment variable that overrides the discovered Steam directory.
ENV_STEAM = "STEAM"

#: The dumper's "this signature did not match" line (on stderr).
UNRESOLVED_RE = re.compile(r"^\s*unresolved:\s*(\S+)\s*$")

#: The dumper's "the module I looked the signatures up under" line (stderr).
MODULE_RE = re.compile(r"^\s*module:\s*(\S+)\s*$")

#: The dumper's "how many signatures the registry holds for that module"
#: line (on stderr).
SIGNATURE_COUNT_RE = re.compile(r"^\s*signatures:\s*(\d+)\s*$")

#: The prefix every line of this script's report carries.
PREFIX = "resolve-offsets-offline:"

#: The dumps this script refuses to touch: they need the live process.
LIVE_ONLY_DUMPS = ("interfaces.json", "schema.json", "schema_system.json")


class OffsetEntry(TypedDict):
    """One resolved offset - the shape the calibration dumps carry."""

    name: str
    module: str
    rva: int
    source: str


#: How a dumper invocation is run.  The tests pass a fake, so the decision
#: logic can be driven without the dumper and without the game.
Runner = Callable[[list[str]], "subprocess.CompletedProcess[str]"]


class OfflineError(Exception):
    """A failure the script reports as a message, not a traceback."""


@dataclass(frozen=True)
class ModuleResolution:
    """What one module's offline resolution produced."""

    module: str
    path: Path | None
    offsets: list[OffsetEntry]
    unresolved: list[str]
    error: str | None = None


def _run(args: list[str]) -> subprocess.CompletedProcess[str]:
    """Run a command and capture its output."""
    return subprocess.run(args, capture_output=True, text=True, check=False)


def load_gen_sdk() -> ModuleType:
    """The SDK generator, loaded by path (its file name is not a module name).

    The dumper is a **cargo** artifact, and where cargo puts it depends on
    ``CARGO_TARGET_DIR`` (a worktree moves the whole target directory out of
    the source tree).  ``scripts/gen-sdk.py`` already resolves that - it was
    fixed for exactly this in T24 - so the resolution is **reused** here
    instead of being copied: one convention, not three.
    """
    path = ROOT / "scripts" / "gen-sdk.py"
    if not path.is_file():
        raise OfflineError(f"the SDK generator is missing: {path}")
    spec = importlib.util.spec_from_file_location("gen_sdk", path)
    if spec is None or spec.loader is None:
        raise OfflineError(f"the SDK generator cannot be loaded: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_dumper(root: Path = ROOT) -> Path | None:
    """The dumper binary, where cargo put it - or ``None``."""
    generator = load_gen_sdk()
    finder = cast(
        "Callable[[Path], Path | None]",
        getattr(generator, "find_dumper", None),
    )
    if finder is None:
        raise OfflineError("the SDK generator does not know how to find the dumper")
    found = finder(root)
    return Path(found) if found is not None else None


def build_dumper(root: Path = ROOT) -> Path:
    """The dumper binary, building it (release) if cargo has not."""
    binary = find_dumper(root)
    if binary is not None:
        return binary
    print(f"{PREFIX} building cs2-dumper (release)", file=sys.stderr)
    try:
        subprocess.run(
            [
                "cargo",
                "build",
                "--release",
                "--manifest-path",
                str(root / "external" / "dumper" / "Cargo.toml"),
            ],
            check=True,
        )
    except OSError as error:
        raise OfflineError(f"cargo could not be run: {error}") from error
    except subprocess.CalledProcessError as error:
        raise OfflineError(f"the dumper could not be built: {error}") from error
    binary = find_dumper(root)
    if binary is None:
        raise OfflineError("the dumper is not where cargo puts it")
    return binary


def parse_json_array(text: str) -> list[object]:
    """The JSON array in the dumper's output.

    The dumper prints a one-line banner before its JSON in the file mode
    (``file client.dll - image_base=...``), so the array is looked up rather
    than assumed to be the whole output.
    """
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if not line.lstrip().startswith("["):
            continue
        try:
            value: object = json.loads("\n".join(lines[index:]))
        except json.JSONDecodeError:
            continue
        if isinstance(value, list):
            return value
    raise OfflineError("the dumper did not answer with a JSON array")


def parse_json_object(text: str) -> dict[str, object]:
    """The JSON object in the dumper's output."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if not line.lstrip().startswith("{"):
            continue
        try:
            value: object = json.loads("\n".join(lines[index:]))
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            return value
    raise OfflineError("the dumper did not answer with a JSON object")


def dumper_modules(dumper: Path, runner: Runner = _run) -> list[str]:
    """The modules that have curated signatures, as the **dumper** reports them.

    The list is not hard-coded here: it comes from the signature registry
    (``analysis::known_signatures``), so a signature added for a new module
    (the chams' ``scenesystem.dll``) is picked up with no edit.
    """
    completed = runner([str(dumper), "--list-signature-modules"])
    if completed.returncode != 0:
        raise OfflineError(
            "the dumper failed to list the signature modules: "
            + (completed.stderr.strip() or f"exit {completed.returncode}")
        )
    values = parse_json_array(completed.stdout)
    modules: list[str] = []
    for value in values:
        if not isinstance(value, str):
            raise OfflineError("the dumper did not answer with a JSON array of module names")
        modules.append(value)
    return modules


def dumper_signature_counts(dumper: Path, runner: Runner = _run) -> dict[str, int]:
    """How many curated signatures the registry holds, per module.

    The count is the **cross-check** against a resolution: a module that
    resolved fewer signatures than the registry holds for it was not really
    scanned (a wrong file, a module name that does not match), and that has
    to be loud rather than a dump full of plausible-looking wrong RVAs.

    A dumper that does not know the flag (an older binary) answers with
    nothing: the cross-check is then skipped, and the caller says so.
    """
    completed = runner([str(dumper), "--list-signature-counts"])
    if completed.returncode != 0:
        return {}
    try:
        values = parse_json_object(completed.stdout)
    except OfflineError:
        return {}
    counts: dict[str, int] = {}
    for module, value in values.items():
        if not isinstance(value, int):
            raise OfflineError("the dumper answered with a malformed signature count")
        counts[module] = value
    return counts


def module_search_dirs(game_dir: Path) -> list[Path]:
    """The directories the modules are searched in."""
    return [game_dir / relative for relative in GAME_MODULE_DIRS]


def find_module_file(game_dir: Path, module: str) -> Path | None:
    """The module's file, in whichever tree holds it - or ``None``."""
    for directory in module_search_dirs(game_dir):
        candidate = directory / module
        if candidate.is_file():
            return candidate
    return None


def parse_unresolved(stderr: str) -> list[str]:
    """The names the dumper reported as not matching."""
    names: list[str] = []
    for line in stderr.splitlines():
        match = UNRESOLVED_RE.match(line)
        if match is not None:
            names.append(match.group(1))
    return names


def parse_dumper_report(stderr: str) -> tuple[str | None, int | None]:
    """What the dumper said about the module it resolved.

    The dumper names the module it looked the signatures up under and how
    many signatures the registry holds for it.  Both are cross-checks: a
    dumper that resolved a *different* module (an older binary that takes
    the name from the path) would otherwise answer with nothing at all and
    say so quietly.
    """
    module: str | None = None
    count: int | None = None
    for line in stderr.splitlines():
        match = MODULE_RE.match(line)
        if match is not None:
            module = match.group(1)
            continue
        match = SIGNATURE_COUNT_RE.match(line)
        if match is not None:
            count = int(match.group(1))
    return module, count


def parse_offset_entries(text: str, module: str) -> list[OffsetEntry]:
    """The resolved offsets of one module, in the dump's shape."""
    values = parse_json_array(text)
    entries: list[OffsetEntry] = []
    for value in values:
        if not isinstance(value, dict):
            raise OfflineError(f"{module}: the dumper did not answer with a list of offsets")
        name = value.get("name")
        rva = value.get("rva")
        source = value.get("source", "signature")
        if not isinstance(name, str) or not isinstance(rva, int) or not isinstance(source, str):
            raise OfflineError(f"{module}: the dumper answered with a malformed offset")
        entries.append({"name": name, "module": module, "rva": rva, "source": source})
    return entries


def resolve_module(
    dumper: Path,
    module: str,
    path: Path,
    runner: Runner = _run,
) -> ModuleResolution:
    """Resolve one module's curated signatures from its file on disk.

    The result is only accepted when it **adds up**: the dumper must have
    looked the signatures up under the module that was asked for, and it
    must have reported every one of them (resolved or not).  A module that
    resolved *nothing* is a failure, not an empty success - that is the
    silent case where the dump would carry offsets from the wrong file.
    """
    completed = runner(
        [
            str(dumper),
            "--file",
            str(path),
            "--resolve-offsets",
            "--json",
            "--module",
            module,
        ]
    )
    if completed.returncode != 0:
        return ModuleResolution(
            module,
            path,
            [],
            [],
            error=completed.stderr.strip() or f"the dumper exited with {completed.returncode}",
        )
    offsets = parse_offset_entries(completed.stdout, module)
    unresolved = parse_unresolved(completed.stderr)
    reported, count = parse_dumper_report(completed.stderr)
    if reported is not None and reported.casefold() != module.casefold():
        return ModuleResolution(
            module,
            path,
            [],
            [],
            error=f"the dumper resolved {reported}, not {module}",
        )
    if not offsets and not unresolved:
        return ModuleResolution(
            module,
            path,
            [],
            [],
            error="the dumper resolved nothing - the module's name does not match its file",
        )
    if count is not None and count != len(offsets) + len(unresolved):
        return ModuleResolution(
            module,
            path,
            [],
            [],
            error=(
                f"the dumper scanned {len(offsets) + len(unresolved)} of "
                f"{count} signature(s)"
            ),
        )
    return ModuleResolution(module, path, offsets, unresolved)


def resolve_all(
    dumper: Path,
    game_dir: Path,
    modules: Sequence[str],
    runner: Runner = _run,
) -> list[ModuleResolution]:
    """Resolve every module the dumper named."""
    resolutions: list[ModuleResolution] = []
    for module in modules:
        path = find_module_file(game_dir, module)
        if path is None:
            searched = ", ".join(str(directory) for directory in module_search_dirs(game_dir))
            resolutions.append(
                ModuleResolution(module, None, [], [], error=f"not found in {searched}")
            )
            continue
        resolutions.append(resolve_module(dumper, module, path, runner))
    return resolutions


def merge_offsets(resolutions: Sequence[ModuleResolution]) -> list[OffsetEntry]:
    """Every module's entries, in one list - the dump's shape.

    **Every** module contributes its entries: a module whose resolution came
    back empty contributes none, but it is not dropped - it stays in the
    report (see :func:`format_report`), so "this module resolved nothing"
    is visible rather than silent.
    """
    merged: list[OffsetEntry] = []
    for resolution in resolutions:
        merged.extend(resolution.offsets)
    return merged


def format_report(resolutions: Sequence[ModuleResolution]) -> list[str]:
    """The per-module report: how many resolved, and which ones did not."""
    lines: list[str] = []
    for resolution in resolutions:
        if resolution.error is not None:
            lines.append(f"{resolution.module}: FAILED - {resolution.error}")
            continue
        lines.append(f"{resolution.module}: {len(resolution.offsets)} offset(s) [{resolution.path}]")
        for name in resolution.unresolved:
            lines.append(f"unresolved: {resolution.module}: {name}")
    return lines


def coverage_problems(
    resolutions: Sequence[ModuleResolution],
    counts: Mapping[str, int],
) -> list[str]:
    """The modules whose resolution did not cover the module's signatures.

    The registry's count is the yardstick: a module that reported fewer
    signatures than the registry holds for it was not really scanned, and
    the offsets it produced cannot be trusted.
    """
    problems: list[str] = []
    for resolution in resolutions:
        if resolution.error is not None:
            continue
        expected = counts.get(resolution.module)
        if expected is None:
            continue
        covered = len(resolution.offsets) + len(resolution.unresolved)
        if covered != expected:
            problems.append(
                f"{resolution.module}: {covered} of {expected} signature(s) were scanned"
            )
    return problems


def sweep_temporaries(path: Path) -> None:
    """The temporary files a killed run left behind.

    ``write_offsets`` removes its own temporary file when it fails, but a
    power loss between the temporary file and the replace leaves one in the
    calibration directory.  The next run sweeps them: they are not a dump.
    """
    prefix = path.name + "."
    try:
        candidates = list(path.parent.iterdir())
    except OSError:
        return
    for candidate in candidates:
        if not candidate.name.startswith(prefix) or not candidate.name.endswith(".tmp"):
            continue
        try:
            candidate.unlink()
        except OSError:
            pass


def write_offsets(path: Path, offsets: Sequence[OffsetEntry]) -> None:
    """Write the dump **atomically**: a temporary file, then a replace.

    The dump is never half-written: the whole list is written to a temporary
    file in the same directory and moved over the old one, so a failure
    leaves the previous dump exactly as it was.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    sweep_temporaries(path)
    text = json.dumps(list(offsets), indent=2)
    handle, temporary_name = tempfile.mkstemp(
        dir=str(path.parent),
        prefix=path.name + ".",
        suffix=".tmp",
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(text)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def read_offsets(path: Path) -> list[OffsetEntry]:
    """The offsets of an existing dump."""
    try:
        value: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise OfflineError(f"the dump cannot be read: {path} ({error})") from error
    if not isinstance(value, list):
        raise OfflineError(f"the dump is not a list of offsets: {path}")
    entries: list[OffsetEntry] = []
    for item in value:
        if not isinstance(item, dict):
            raise OfflineError(f"the dump is not a list of offsets: {path}")
        name = item.get("name")
        module = item.get("module")
        rva = item.get("rva")
        source = item.get("source")
        if (
            not isinstance(name, str)
            or not isinstance(module, str)
            or not isinstance(rva, int)
            or not isinstance(source, str)
        ):
            raise OfflineError(f"the dump holds a malformed offset: {path}")
        entries.append({"name": name, "module": module, "rva": rva, "source": source})
    return entries


def _index_entries(entries: Sequence[OffsetEntry]) -> tuple[dict[str, OffsetEntry], list[str]]:
    """The entries by name, and the names that appear twice.

    A duplicate name is **not** silently dropped: the payload looks an offset
    up by name, so two entries with one name are a defect the comparison has
    to name.
    """
    index: dict[str, OffsetEntry] = {}
    duplicates: list[str] = []
    for entry in entries:
        name = entry["name"]
        if name in index:
            duplicates.append(name)
            continue
        index[name] = entry
    return index, duplicates


def compare_offsets(
    existing: Sequence[OffsetEntry],
    resolved: Sequence[OffsetEntry],
) -> list[str]:
    """The differences between the shipped dump and the offline resolution.

    Every field of an entry counts: the name, the module, the RVA **and**
    the source.  A duplicate name on either side is named too - it is not
    silently dropped from the comparison.
    """
    shipped, shipped_duplicates = _index_entries(existing)
    fresh, fresh_duplicates = _index_entries(resolved)
    differences: list[str] = []
    for name in shipped_duplicates:
        differences.append(f"duplicate: {name} (in the existing dump)")
    for name in fresh_duplicates:
        differences.append(f"duplicate: {name} (in the offline resolution)")
    for name, entry in fresh.items():
        other = shipped.get(name)
        if other is None:
            differences.append(
                f"added: {name} (module {entry['module']}, rva 0x{entry['rva']:x})"
            )
            continue
        changes: list[str] = []
        if other["rva"] != entry["rva"]:
            changes.append(f"rva 0x{other['rva']:x} -> 0x{entry['rva']:x}")
        if other["module"] != entry["module"]:
            changes.append(f"module {other['module']} -> {entry['module']}")
        if other["source"] != entry["source"]:
            changes.append(f"source {other['source']} -> {entry['source']}")
        if changes:
            differences.append(f"changed: {name}: " + ", ".join(changes))
    for name, entry in shipped.items():
        if name not in fresh:
            differences.append(
                f"missing: {name} (module {entry['module']}, rva 0x{entry['rva']:x})"
            )
    return differences


def registry_steam_roots() -> list[Path]:
    """The Steam installations the **registry** names.

    Steam can live anywhere (``D:\\Steam``), and the registry is where it
    records where it lives - the two standard paths are only defaults.  The
    module is imported at run time: it is Windows-only, and the script has
    to work (and skip this) on a host that has no such registry.
    """
    try:
        winreg = importlib.import_module("winreg")
    except ImportError:
        return []
    roots: list[Path] = []
    keys = (
        (r"Software\Valve\Steam", "SteamPath"),
        (r"Software\Valve\Steam", "SteamExe"),
        (r"Software\Wow6432Node\Valve\Steam", "InstallPath"),
    )
    for key_name, value_name in keys:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_name) as key:
                value, _kind = winreg.QueryValueEx(key, value_name)
        except OSError:
            continue
        if not isinstance(value, str) or not value:
            continue
        path = Path(value)
        root = path.parent if path.is_file() else path
        if root not in roots:
            roots.append(root)
    return roots


def steam_roots(env: Mapping[str, str]) -> list[Path]:
    """The Steam installations to look in.

    ``%STEAM%`` first (the operator's own answer), then the two standard
    install paths, then the installations the **registry** names - Steam is
    not always installed in one of the two.
    """
    roots: list[Path] = []
    candidates: list[str] = []
    override = env.get(ENV_STEAM)
    if override:
        candidates.append(override)
    candidates.append(r"C:\Program Files (x86)\Steam\steam.exe")
    candidates.append(r"C:\Program Files\Steam\steam.exe")
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file():
            roots.append(path.parent)
        elif path.is_dir():
            roots.append(path)
    for root in registry_steam_roots():
        if root not in roots:
            roots.append(root)
    return roots


def steam_library_paths(steam_root: Path) -> list[Path]:
    """The Steam library folders, from ``libraryfolders.vdf``.

    The Steam library path is **not** fixed - the game lives in
    ``F:\\SteamLibrary\\...`` on some machines and under the Steam
    installation on others - so the file is the thing to read.
    """
    vdf = steam_root / "steamapps" / "libraryfolders.vdf"
    if not vdf.is_file():
        return []
    text = vdf.read_text(encoding="utf-8", errors="replace")
    paths: list[Path] = []
    for match in re.finditer(r'"path"\s+"([^"]+)"', text):
        # The VDF escapes the backslashes: "F:\\SteamLibrary".
        paths.append(Path(match.group(1).replace("\\\\", "\\")))
    return paths


def discover_game_dir(env: Mapping[str, str] | None = None) -> Path | None:
    """The game's root, discovered from the Steam library folders.

    The Steam library path is not fixed, so the default is **discovered**:
    the Steam installation (``STEAM``, or the standard install) names its
    library folders in ``libraryfolders.vdf``, and the game is the ``730``
    app in one of them.  ``CS2_GAME_DIR`` overrides the search.
    """
    environment = os.environ if env is None else env
    override = environment.get(ENV_GAME_DIR)
    if override:
        candidate = Path(override)
        if candidate.is_dir():
            return candidate
    for root in steam_roots(environment):
        for library in [root, *steam_library_paths(root)]:
            candidate = library / "steamapps" / "common" / GAME_FOLDER_NAME
            if (candidate / "game" / "bin" / "win64" / "engine2.dll").is_file():
                return candidate
    return None


def build_parser() -> argparse.ArgumentParser:
    """The command line."""
    parser = argparse.ArgumentParser(
        prog="resolve-offsets-offline.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "Resolve the curated offsets offline - from the module files on disk, "
            "with no game running."
        ),
        epilog=(
            "The **offsets** (calibration/offsets.json) are all this script needs: the\n"
            "dumper resolves the very same curated signatures from the module's file\n"
            "on disk that the live calibration resolves in the process' memory.\n"
            "\n"
            "The **interfaces** and the **schema** (calibration/interfaces.json,\n"
            "calibration/schema.json, calibration/schema_system.json) come from the\n"
            "live process - its CreateInterface registry and its schema system - so\n"
            "they still need scripts/calibrate-live.py and a running game.  This\n"
            "script leaves them untouched.\n"
            "\n"
            "The game directory defaults to the Steam install discovered through\n"
            "libraryfolders.vdf (the Steam library path is not fixed - the game lives\n"
            "in F:\\SteamLibrary\\... on some machines).  The installation itself is\n"
            "found through %STEAM%, the two standard paths and the registry.\n"
            "CS2_GAME_DIR overrides the search; --game-dir overrides that.\n"
            "\n"
            "With --check nothing is written: the resolution is compared with the\n"
            "existing dump and the exit code says whether they differ.\n"
            "\n"
            "A signature that did not resolve is an offset the payload cannot use, so\n"
            "the dump is **not** written (the previous one stays) and the exit code\n"
            "is 1.  --allow-unresolved writes the degraded dump anyway, on purpose.\n"
        ),
    )
    parser.add_argument(
        "--game-dir",
        default=None,
        help="the game's root (default: discovered from the Steam library folders)",
    )
    parser.add_argument(
        "--dump-dir",
        default=None,
        help="where calibration/offsets.json lives (default: the repository's calibration/)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="compare with the existing dump and write nothing; non-zero when they differ",
    )
    parser.add_argument(
        "--allow-unresolved",
        action="store_true",
        help=(
            "write the dump even when a signature did not resolve "
            "(the dump is degraded: the payload cannot use the missing offsets)"
        ),
    )
    parser.add_argument(
        "--dumper",
        default=None,
        help="the dumper binary (default: where cargo put it)",
    )
    return parser


def main(argv: list[str] | None = None, runner: Runner | None = None) -> int:
    """Resolve the offsets; return the exit code.

    The runner is a parameter so the tests can drive the whole CLI without
    the dumper and without the game.
    """
    args = build_parser().parse_args(argv)
    active: Runner = _run if runner is None else runner

    game_dir = Path(args.game_dir) if args.game_dir else discover_game_dir()
    if game_dir is None:
        print(
            f"{PREFIX} the game's directory was not found; pass --game-dir "
            "(or set CS2_GAME_DIR)",
            file=sys.stderr,
        )
        return 1
    dump_dir = Path(args.dump_dir) if args.dump_dir else ROOT / "calibration"
    dump_path = dump_dir / "offsets.json"

    try:
        dumper = Path(args.dumper) if args.dumper else build_dumper()
        if not dumper.is_file():
            raise OfflineError(f"the dumper is not there: {dumper}")
        modules = dumper_modules(dumper, active)
        counts = dumper_signature_counts(dumper, active)
        resolutions = resolve_all(dumper, game_dir, modules, active)
    except OfflineError as error:
        print(f"{PREFIX} {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"{PREFIX} the dumper could not be built: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"{PREFIX} the dumper could not be run: {error}", file=sys.stderr)
        return 1

    print(f"{PREFIX} game: {game_dir}", file=sys.stderr)
    print(f"{PREFIX} dumper: {dumper}", file=sys.stderr)
    for line in format_report(resolutions):
        print(f"{PREFIX} {line}", file=sys.stderr)
    print(
        f"{PREFIX} {', '.join(LIVE_ONLY_DUMPS)} are untouched - they still need the "
        "live calibration (scripts/calibrate-live.py) and a running game",
        file=sys.stderr,
    )
    if not counts:
        print(
            f"{PREFIX} the dumper does not report the signature counts; "
            "the coverage cross-check is skipped",
            file=sys.stderr,
        )

    resolved = merge_offsets(resolutions)
    unresolved = [name for resolution in resolutions for name in resolution.unresolved]
    failed = [resolution for resolution in resolutions if resolution.error is not None]
    problems = coverage_problems(resolutions, counts)

    if args.check:
        if not dump_path.is_file():
            print(f"{PREFIX} there is no dump to compare with: {dump_path}", file=sys.stderr)
            return 1
        try:
            existing = read_offsets(dump_path)
        except OfflineError as error:
            print(f"{PREFIX} {error}", file=sys.stderr)
            return 1
        differences = compare_offsets(existing, resolved)
        if differences:
            print(f"{PREFIX} {dump_path} differs from the offline resolution:", file=sys.stderr)
            for difference in differences:
                print(f"{PREFIX}   {difference}", file=sys.stderr)
            return 1
        print(f"{PREFIX} no difference: {dump_path} matches the offline resolution "
              f"({len(resolved)} offset(s))", file=sys.stderr)
        if unresolved or failed or problems:
            # The dump matches, but a signature did not resolve - that is
            # still a defect: it is an offset the payload cannot use.
            for problem in problems:
                print(f"{PREFIX}   {problem}", file=sys.stderr)
            return 1
        return 0

    if failed:
        print(
            f"{PREFIX} {len(failed)} module(s) did not resolve; nothing was written",
            file=sys.stderr,
        )
        return 1
    if problems:
        print(
            f"{PREFIX} the resolution does not cover every signature; "
            "nothing was written",
            file=sys.stderr,
        )
        for problem in problems:
            print(f"{PREFIX}   {problem}", file=sys.stderr)
        return 1
    if unresolved and not args.allow_unresolved:
        print(
            f"{PREFIX} {len(unresolved)} signature(s) did not resolve; nothing was "
            "written - a dump without them is a dump the payload cannot use "
            "(--allow-unresolved writes it anyway)",
            file=sys.stderr,
        )
        return 1

    previous: list[OffsetEntry] = []
    if dump_path.is_file():
        try:
            previous = read_offsets(dump_path)
        except OfflineError:
            previous = []
    changes = compare_offsets(previous, resolved) if previous else []
    if changes:
        print(f"{PREFIX} the dump changes:", file=sys.stderr)
        for change in changes:
            print(f"{PREFIX}   {change}", file=sys.stderr)

    try:
        write_offsets(dump_path, resolved)
    except OSError as error:
        print(f"{PREFIX} the dump could not be written: {error}", file=sys.stderr)
        return 1
    if unresolved:
        print(
            f"{PREFIX} wrote {len(resolved)} offset(s) to {dump_path} - DEGRADED: "
            f"{len(unresolved)} signature(s) did not resolve "
            f"({', '.join(unresolved)}); the payload cannot use those offsets",
            file=sys.stderr,
        )
    else:
        print(f"{PREFIX} wrote {len(resolved)} offset(s) to {dump_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except OfflineError as error:  # pragma: no cover - the safety net
        print(f"{PREFIX} {error}", file=sys.stderr)
        sys.exit(1)
