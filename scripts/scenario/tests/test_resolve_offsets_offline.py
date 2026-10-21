"""Unit tests for the offline offset resolver.

``scripts/resolve-offsets-offline.py`` refreshes ``calibration/offsets.json``
from the **module files on disk** - no game, no process.  The dumper resolves
the very same curated signatures from a file that it resolves in the live
process' memory, so the offsets do not need the game (the *interfaces* and
the *schema* do - they still come from the live process).

The script's **decision logic** is what these tests drive: which modules are
resolved (the answer comes from the dumper, not from a list in the script),
where each module's file is found, what is merged into the dump, what is
**named** when a signature does not resolve, and what happens to the previous
dump when the write fails half-way.

The dumper is faked (a runner that answers with canned output), so the tests
need neither the game nor a cargo build.  The one exception is the flag test,
which runs the real binary when it is built.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from collections.abc import Callable
from typing import Any

#: The repository root (``scripts/scenario/tests`` -> ``scripts`` -> root).
ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The resolver (its name is not a module name, so it is loaded by path).
SCRIPT = ROOT / "scripts" / "resolve-offsets-offline.py"


def _load_resolver() -> Any:
    """The resolver module, loaded from its path.

    The module is loaded dynamically (its file name is not importable), so
    its functions are reached through ``Any``: the checker cannot see them.
    It is registered in ``sys.modules`` first: the resolver declares a
    dataclass, and ``dataclasses`` looks the defining module up there.
    """
    spec = importlib.util.spec_from_file_location("resolve_offsets_offline", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


RESOLVER = _load_resolver()


def _completed(
    stdout: str = "",
    stderr: str = "",
    returncode: int = 0,
) -> subprocess.CompletedProcess[str]:
    """A finished process, as the dumper runner returns it."""
    return subprocess.CompletedProcess(
        args=["cs2-dumper"],
        returncode=returncode,
        stdout=stdout,
        stderr=stderr,
    )


def _fake_dumper(
    responses: dict[str, subprocess.CompletedProcess[str]],
) -> Callable[[list[str]], subprocess.CompletedProcess[str]]:
    """A fake dumper: it answers by what it finds in the arguments.

    The key is matched against the argument list, so a test can answer
    ``--list-signature-modules`` with one payload and the per-module
    resolution with another.
    """

    def runner(args: list[str]) -> subprocess.CompletedProcess[str]:
        for key, response in responses.items():
            if key in args:
                return response
        return _completed(stderr=f"unexpected call: {args}")

    return runner


def _offset(name: str, rva: int, module: str = "client.dll") -> dict[str, object]:
    """One offset, in the dump's shape."""
    return {"name": name, "module": module, "rva": rva, "source": "signature"}


class TestModuleList(unittest.TestCase):
    """The module list comes from the dumper, not from a list in the script."""

    def test_the_list_is_the_dumpers_answer(self) -> None:
        # ``scenesystem.dll`` is the chams' module: the script has never
        # heard of it, so the answer cannot come from a list inside it.
        answer = json.dumps(["scenesystem.dll", "client.dll"])
        modules = RESOLVER.dumper_modules(
            pathlib.Path("cs2-dumper"),
            _fake_dumper({"--list-signature-modules": _completed(stdout=answer)}),
        )
        self.assertEqual(["scenesystem.dll", "client.dll"], modules)

    def test_a_failing_dumper_is_an_error(self) -> None:
        with self.assertRaises(RESOLVER.OfflineError):
            RESOLVER.dumper_modules(
                pathlib.Path("cs2-dumper"),
                _fake_dumper(
                    {"--list-signature-modules": _completed(stderr="boom", returncode=1)}
                ),
            )

    def test_a_malformed_answer_is_an_error(self) -> None:
        with self.assertRaises(RESOLVER.OfflineError):
            RESOLVER.dumper_modules(
                pathlib.Path("cs2-dumper"),
                _fake_dumper({"--list-signature-modules": _completed(stdout="{}")}),
            )


class TestModuleFile(unittest.TestCase):
    """The module's file is found in either game tree."""

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.game = pathlib.Path(self._temporary.name) / "Counter-Strike Global Offensive"
        (self.game / "game" / "bin" / "win64").mkdir(parents=True)
        (self.game / "game" / "csgo" / "bin" / "win64").mkdir(parents=True)
        (self.game / "game" / "bin" / "win64" / "engine2.dll").write_bytes(b"")
        (self.game / "game" / "csgo" / "bin" / "win64" / "client.dll").write_bytes(b"")

    def test_the_csgo_tree_holds_the_client(self) -> None:
        found = RESOLVER.find_module_file(self.game, "client.dll")
        self.assertEqual(self.game / "game" / "csgo" / "bin" / "win64" / "client.dll", found)

    def test_the_game_tree_holds_the_engine(self) -> None:
        found = RESOLVER.find_module_file(self.game, "engine2.dll")
        self.assertEqual(self.game / "game" / "bin" / "win64" / "engine2.dll", found)

    def test_a_module_that_is_not_there_is_not_found(self) -> None:
        self.assertIsNone(RESOLVER.find_module_file(self.game, "scenesystem.dll"))


class TestResolution(unittest.TestCase):
    """What a module's resolution produces - and what it does not."""

    def test_the_offsets_are_parsed(self) -> None:
        stdout = json.dumps([_offset("game_entity_system", 41003272)])
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper({"client.dll": _completed(stdout=stdout)}),
        )
        self.assertEqual(1, len(resolution.offsets))
        self.assertEqual("game_entity_system", resolution.offsets[0]["name"])
        self.assertEqual(41003272, resolution.offsets[0]["rva"])
        self.assertEqual("client.dll", resolution.offsets[0]["module"])

    def test_a_signature_that_does_not_resolve_is_named(self) -> None:
        stdout = json.dumps([_offset("game_entity_system", 41003272)])
        stderr = "  unresolved: animfix_graph_codec\n  unresolved: animfix_anim_tick\n"
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper({"client.dll": _completed(stdout=stdout, stderr=stderr)}),
        )
        self.assertEqual(["animfix_graph_codec", "animfix_anim_tick"], resolution.unresolved)
        report = "\n".join(RESOLVER.format_report([resolution]))
        self.assertIn("animfix_graph_codec", report)
        self.assertIn("animfix_anim_tick", report)

    def test_a_failing_dumper_is_reported(self) -> None:
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper({"client.dll": _completed(stderr="boom", returncode=1)}),
        )
        self.assertIsNotNone(resolution.error)
        self.assertEqual([], resolution.offsets)
        self.assertIn("FAILED", "\n".join(RESOLVER.format_report([resolution])))


class TestMerge(unittest.TestCase):
    """The merge keeps every module - including one that resolved nothing."""

    def _resolutions(self) -> list[Any]:
        client = RESOLVER.ModuleResolution(
            "client.dll",
            pathlib.Path("client.dll"),
            [
                {"name": "game_entity_system", "module": "client.dll",
                 "rva": 41003272, "source": "signature"},
            ],
            [],
        )
        engine = RESOLVER.ModuleResolution(
            "engine2.dll",
            pathlib.Path("engine2.dll"),
            [],
            ["cbuf_add_text"],
        )
        return [client, engine]

    def test_every_module_keeps_its_entries(self) -> None:
        merged = RESOLVER.merge_offsets(self._resolutions())
        self.assertEqual(1, len(merged))
        self.assertEqual("game_entity_system", merged[0]["name"])

    def test_a_module_that_resolved_nothing_is_not_dropped(self) -> None:
        report = "\n".join(RESOLVER.format_report(self._resolutions()))
        self.assertIn("engine2.dll", report)
        self.assertIn("cbuf_add_text", report)

    def test_the_merged_entries_keep_the_dump_shape(self) -> None:
        merged = RESOLVER.merge_offsets(self._resolutions())
        self.assertEqual(["name", "module", "rva", "source"], list(merged[0].keys()))


class TestWrite(unittest.TestCase):
    """The dump is written atomically and completely."""

    def test_the_write_produces_the_dump(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "offsets.json"
            RESOLVER.write_offsets(path, [_offset("game_entity_system", 41003272)])
            self.assertEqual(
                [_offset("game_entity_system", 41003272)],
                json.loads(path.read_text(encoding="utf-8")),
            )

    def test_a_failure_leaves_the_previous_dump_untouched(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "offsets.json"
            path.write_text("previous", encoding="utf-8")
            with unittest.mock.patch.object(
                RESOLVER.os, "replace", side_effect=OSError("boom")
            ):
                with self.assertRaises(OSError):
                    RESOLVER.write_offsets(path, [_offset("game_entity_system", 1)])
            self.assertEqual("previous", path.read_text(encoding="utf-8"))
            # No half-written temporary file is left behind.
            self.assertEqual(
                ["offsets.json"],
                sorted(entry.name for entry in pathlib.Path(directory).iterdir()),
            )


class TestCheck(unittest.TestCase):
    """The comparison the ``--check`` mode reports on."""

    def test_an_identical_dump_has_no_difference(self) -> None:
        entries = [_offset("game_entity_system", 41003272)]
        self.assertEqual([], RESOLVER.compare_offsets(entries, entries))

    def test_a_moved_offset_is_named(self) -> None:
        shipped = [_offset("game_entity_system", 41003272)]
        fresh = [_offset("game_entity_system", 41003300)]
        differences = RESOLVER.compare_offsets(shipped, fresh)
        self.assertEqual(1, len(differences))
        self.assertIn("game_entity_system", differences[0])
        self.assertIn("0x271a908", differences[0])
        self.assertIn("0x271a924", differences[0])

    def test_an_offset_that_is_missing_is_named(self) -> None:
        shipped = [_offset("game_entity_system", 41003272)]
        differences = RESOLVER.compare_offsets(shipped, [])
        self.assertEqual(1, len(differences))
        self.assertIn("missing", differences[0])
        self.assertIn("game_entity_system", differences[0])


class TestFlag(unittest.TestCase):
    """The dumper's ``--list-signature-modules`` flag."""

    def test_the_flag_is_wired(self) -> None:
        source = (ROOT / "external" / "dumper" / "src" / "main.rs").read_text(
            encoding="utf-8"
        )
        self.assertIn("list_signature_modules", source)

    def test_the_flag_lists_the_curated_modules(self) -> None:
        dumper = RESOLVER.find_dumper(ROOT)
        if dumper is None:
            self.skipTest(
                "the dumper is not built "
                "(cargo build --release --manifest-path external/dumper/Cargo.toml)"
            )
        completed = subprocess.run(
            [str(dumper), "--list-signature-modules"],
            capture_output=True,
            text=True,
            check=True,
        )
        modules = json.loads(completed.stdout)
        self.assertIn("client.dll", modules)
        self.assertIn("engine2.dll", modules)


if __name__ == "__main__":
    unittest.main()
