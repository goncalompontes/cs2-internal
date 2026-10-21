"""Unit tests for the offline offset resolver.

``scripts/resolve-offsets-offline.py`` refreshes ``calibration/offsets.json``
from the **module files on disk** - no game, no process.  The dumper resolves
the very same curated signatures from a file that it resolves in the live
process' memory, so the offsets do not need the game (the *interfaces* and
the *schema* do - they still come from the live process).

The script's **decision logic** is what these tests drive: which modules are
resolved (the answer comes from the dumper, not from a list in the script),
where each module's file is found, what is merged into the dump, what is
**named** when a signature does not resolve, what ``main()`` does with that
(the dump is **not** written), and what happens to the previous dump when the
write fails half-way.

The dumper is faked (a runner that answers with canned output), so the tests
need neither the game nor a cargo build.  The one exception is the flag test,
which runs the real binary when it is built.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from collections.abc import Callable, Mapping, Sequence
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


def _fake_resolver(
    modules: Sequence[str],
    counts: Mapping[str, int] | None = None,
    resolutions: Mapping[str, subprocess.CompletedProcess[str]] | None = None,
) -> Callable[[list[str]], subprocess.CompletedProcess[str]]:
    """A fake dumper for the **whole** resolver: the module list, the
    registry's counts and the per-module resolutions.

    It is what ``main()`` is driven with: the CLI's decisions (what is
    written, what is refused) are the ones that matter, and they need the
    whole conversation with the dumper, not one call.
    """
    answers: Mapping[str, subprocess.CompletedProcess[str]] = resolutions or {}

    def runner(args: list[str]) -> subprocess.CompletedProcess[str]:
        if "--list-signature-modules" in args:
            return _completed(stdout=json.dumps(list(modules)))
        if "--list-signature-counts" in args:
            if counts is None:
                # An older dumper: it does not know the flag.
                return _completed(stderr="unknown argument", returncode=2)
            return _completed(stdout=json.dumps(dict(counts)))
        for module, response in answers.items():
            if module in args:
                return response
        return _completed(stderr=f"unexpected call: {args}")

    return runner


def _offset(name: str, rva: int, module: str = "client.dll") -> dict[str, object]:
    """One offset, in the dump's shape."""
    return {"name": name, "module": module, "rva": rva, "source": "signature"}


def _resolved(
    module: str,
    offsets: Sequence[dict[str, object]] = (),
    unresolved: Sequence[str] = (),
    signatures: int | None = None,
) -> subprocess.CompletedProcess[str]:
    """What the dumper answers for one module's resolution."""
    stderr = f"  module: {module}\n"
    total = signatures if signatures is not None else len(offsets) + len(unresolved)
    stderr += f"  signatures: {total}\n"
    for name in unresolved:
        stderr += f"  unresolved: {name}\n"
    return _completed(stdout=json.dumps(list(offsets)), stderr=stderr)


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


class TestSignatureCounts(unittest.TestCase):
    """The registry's signature counts - the cross-check against a resolution."""

    def test_the_counts_are_the_dumpers_answer(self) -> None:
        counts = RESOLVER.dumper_signature_counts(
            pathlib.Path("cs2-dumper"),
            _fake_dumper({"--list-signature-counts": _completed(stdout='{"client.dll": 21}')}),
        )
        self.assertEqual({"client.dll": 21}, counts)

    def test_a_dumper_that_does_not_know_the_flag_is_not_an_error(self) -> None:
        counts = RESOLVER.dumper_signature_counts(
            pathlib.Path("cs2-dumper"),
            _fake_dumper({"--list-signature-counts": _completed(stderr="boom", returncode=2)}),
        )
        self.assertEqual({}, counts)


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

    def test_a_module_that_resolved_nothing_is_a_failure(self) -> None:
        # The silent case: the dumper looked the signatures up under another
        # name, so it resolved nothing - and said nothing.
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper({"client.dll": _completed(stdout="[]")}),
        )
        self.assertIsNotNone(resolution.error)
        self.assertEqual([], resolution.offsets)

    def test_a_dumper_that_resolved_another_module_is_a_failure(self) -> None:
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper(
                {
                    "client.dll": _completed(
                        stdout=json.dumps([_offset("a", 1)]),
                        stderr="  module: engine2.dll\n  signatures: 1\n",
                    )
                }
            ),
        )
        self.assertIsNotNone(resolution.error)
        self.assertIn("engine2.dll", str(resolution.error))

    def test_a_resolution_that_misses_a_signature_is_a_failure(self) -> None:
        # The dumper says the module holds three signatures, but it only
        # reported one: the module was not really scanned.
        resolution = RESOLVER.resolve_module(
            pathlib.Path("cs2-dumper"),
            "client.dll",
            pathlib.Path("client.dll"),
            _fake_dumper(
                {
                    "client.dll": _completed(
                        stdout=json.dumps([_offset("a", 1)]),
                        stderr="  module: client.dll\n  signatures: 3\n",
                    )
                }
            ),
        )
        self.assertIsNotNone(resolution.error)
        self.assertIn("3", str(resolution.error))


class TestMerge(unittest.TestCase):
    """The merge keeps **every** module - including one that resolved nothing."""

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
            [
                {"name": "input_service", "module": "engine2.dll",
                 "rva": 100, "source": "signature"},
            ],
            [],
        )
        return [client, engine]

    def test_every_module_keeps_its_entries(self) -> None:
        # Both modules have entries: the merged list has to carry both of
        # them. A merge that keeps only the first module's offsets loses
        # the engine's - and that has to fail here.
        merged = RESOLVER.merge_offsets(self._resolutions())
        self.assertEqual(2, len(merged))
        self.assertEqual(
            {"client.dll", "engine2.dll"},
            {entry["module"] for entry in merged},
        )
        self.assertEqual(
            {"game_entity_system", "input_service"},
            {entry["name"] for entry in merged},
        )

    def test_a_module_that_resolved_nothing_is_not_dropped(self) -> None:
        resolutions = self._resolutions()
        empty = RESOLVER.ModuleResolution(
            "scenesystem.dll",
            pathlib.Path("scenesystem.dll"),
            [],
            ["scene_draw_primitive"],
        )
        report = "\n".join(RESOLVER.format_report([*resolutions, empty]))
        self.assertIn("scenesystem.dll", report)
        self.assertIn("scene_draw_primitive", report)

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

    def test_a_temporary_file_a_killed_run_left_behind_is_swept(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "offsets.json"
            leftover = pathlib.Path(directory) / "offsets.json.abc123.tmp"
            leftover.write_text("half", encoding="utf-8")
            RESOLVER.write_offsets(path, [_offset("game_entity_system", 1)])
            self.assertFalse(leftover.exists())
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

    def test_a_changed_source_is_named(self) -> None:
        shipped = [_offset("game_entity_system", 41003272)]
        fresh = [_offset("game_entity_system", 41003272)]
        fresh[0]["source"] = "manual"
        differences = RESOLVER.compare_offsets(shipped, fresh)
        self.assertEqual(1, len(differences))
        self.assertIn("source", differences[0])

    def test_a_duplicate_name_is_not_silently_dropped(self) -> None:
        shipped = [_offset("game_entity_system", 41003272)]
        fresh = [
            _offset("game_entity_system", 41003272),
            _offset("game_entity_system", 41003300),
        ]
        differences = RESOLVER.compare_offsets(shipped, fresh)
        self.assertEqual(1, len(differences))
        self.assertIn("duplicate", differences[0])
        self.assertIn("game_entity_system", differences[0])


class TestMain(unittest.TestCase):
    """``main()`` - the CLI's decisions: what is written, and what is not.

    These are the tests the reviewer wanted: a signature that did not
    resolve must **not** end up as a deleted entry in the dump with a
    success message, a module that failed must not write anything, and
    ``--check`` must say so with a non-zero exit code.
    """

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.root = pathlib.Path(self._temporary.name)
        self.game = self.root / "Counter-Strike Global Offensive"
        (self.game / "game" / "csgo" / "bin" / "win64").mkdir(parents=True)
        (self.game / "game" / "bin" / "win64").mkdir(parents=True)
        (self.game / "game" / "csgo" / "bin" / "win64" / "client.dll").write_bytes(b"")
        (self.game / "game" / "bin" / "win64" / "engine2.dll").write_bytes(b"")
        self.dump_dir = self.root / "calibration"
        self.dump_dir.mkdir()
        self.dump = self.dump_dir / "offsets.json"
        # The dumper is faked, but the script insists that it is *there*:
        # an empty file is enough.
        self.dumper = self.root / "cs2-dumper.exe"
        self.dumper.write_bytes(b"")

    def _run(
        self,
        runner: Callable[[list[str]], subprocess.CompletedProcess[str]],
        *extra: str,
        dumper: str | None = None,
    ) -> tuple[int, str]:
        """Run the resolver's ``main()``; return the exit code and stderr."""
        arguments = [
            "--game-dir",
            str(self.game),
            "--dump-dir",
            str(self.dump_dir),
            "--dumper",
            dumper if dumper is not None else str(self.dumper),
            *extra,
        ]
        captured = io.StringIO()
        with contextlib.redirect_stderr(captured):
            code = RESOLVER.main(arguments, runner=runner)
        return code, captured.getvalue()

    def _client_resolved(self, *unresolved: str) -> subprocess.CompletedProcess[str]:
        return _resolved(
            "client.dll",
            [_offset("game_entity_system", 41003272)],
            list(unresolved),
            signatures=1 + len(unresolved),
        )

    def test_the_dump_is_written_when_everything_resolves(self) -> None:
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 1},
            {"client.dll": self._client_resolved()},
        )
        code, output = self._run(runner)
        self.assertEqual(0, code)
        self.assertTrue(self.dump.is_file())
        self.assertEqual(
            [_offset("game_entity_system", 41003272)],
            json.loads(self.dump.read_text(encoding="utf-8")),
        )
        self.assertIn("wrote 1 offset(s)", output)

    def test_the_write_is_refused_when_a_signature_did_not_resolve(self) -> None:
        # The failure mode the task exists to prevent: the signature that
        # stopped matching is deleted from the dump and the run reports
        # success.
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 2},
            {"client.dll": self._client_resolved("subtick_step_alloc")},
        )
        code, output = self._run(runner)
        self.assertEqual(1, code)
        self.assertFalse(self.dump.exists())
        self.assertIn("subtick_step_alloc", output)
        self.assertNotIn("wrote", output)

    def test_the_previous_dump_survives_a_refused_write(self) -> None:
        self.dump.write_text("previous", encoding="utf-8")
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 2},
            {"client.dll": self._client_resolved("subtick_step_alloc")},
        )
        code, _output = self._run(runner)
        self.assertEqual(1, code)
        self.assertEqual("previous", self.dump.read_text(encoding="utf-8"))

    def test_the_degraded_dump_can_be_forced_on_purpose(self) -> None:
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 2},
            {"client.dll": self._client_resolved("subtick_step_alloc")},
        )
        code, output = self._run(runner, "--allow-unresolved")
        self.assertEqual(0, code)
        self.assertTrue(self.dump.is_file())
        self.assertIn("DEGRADED", output)
        self.assertIn("subtick_step_alloc", output)

    def test_a_failed_module_writes_nothing(self) -> None:
        runner = _fake_resolver(
            ["client.dll", "engine2.dll"],
            {"client.dll": 1, "engine2.dll": 1},
            {
                "client.dll": self._client_resolved(),
                "engine2.dll": _completed(stderr="boom", returncode=1),
            },
        )
        code, output = self._run(runner)
        self.assertEqual(1, code)
        self.assertFalse(self.dump.exists())
        self.assertIn("did not resolve", output)

    def test_a_module_that_was_not_scanned_writes_nothing(self) -> None:
        # The dumper says the module holds two signatures but only reported
        # one: the offsets cannot be trusted.
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 2},
            {"client.dll": self._client_resolved()},
        )
        code, output = self._run(runner)
        self.assertEqual(1, code)
        self.assertFalse(self.dump.exists())
        self.assertIn("signature(s)", output)

    def test_check_exits_non_zero_when_a_signature_did_not_resolve(self) -> None:
        self.dump.write_text(
            json.dumps([_offset("game_entity_system", 41003272)], indent=2),
            encoding="utf-8",
        )
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 2},
            {"client.dll": self._client_resolved("subtick_step_alloc")},
        )
        code, output = self._run(runner, "--check")
        self.assertEqual(1, code)
        self.assertIn("subtick_step_alloc", output)

    def test_check_reports_no_difference(self) -> None:
        self.dump.write_text(
            json.dumps([_offset("game_entity_system", 41003272)], indent=2),
            encoding="utf-8",
        )
        runner = _fake_resolver(
            ["client.dll"],
            {"client.dll": 1},
            {"client.dll": self._client_resolved()},
        )
        code, output = self._run(runner, "--check")
        self.assertEqual(0, code)
        self.assertIn("no difference", output)
        # The live-only dumps are untouched - and the script says so in the
        # check mode too.
        self.assertIn("interfaces.json", output)

    def test_a_dumper_that_is_not_there_is_a_message_not_a_traceback(self) -> None:
        code, output = self._run(
            _fake_resolver([]),
            dumper=str(self.root / "nowhere" / "cs2-dumper.exe"),
        )
        self.assertEqual(1, code)
        self.assertIn("not there", output)
        self.assertNotIn("Traceback", output)

    def test_a_dumper_that_cannot_be_run_is_a_message_not_a_traceback(self) -> None:
        def runner(args: list[str]) -> subprocess.CompletedProcess[str]:
            raise FileNotFoundError("no such file")

        code, output = self._run(runner)
        self.assertEqual(1, code)
        self.assertNotIn("Traceback", output)
        self.assertIn("could not be run", output)


class TestFlag(unittest.TestCase):
    """The dumper's ``--list-signature-modules`` flag."""

    def test_the_flag_is_in_the_command_line_help(self) -> None:
        dumper = RESOLVER.find_dumper(ROOT)
        if dumper is None:
            self.skipTest(
                "the dumper is not built "
                "(cargo build --release --manifest-path external/dumper/Cargo.toml)"
            )
        completed = subprocess.run(
            [str(dumper), "--help"],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn("--list-signature-modules", completed.stdout)

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

    def test_the_counts_flag_matches_the_modules(self) -> None:
        dumper = RESOLVER.find_dumper(ROOT)
        if dumper is None:
            self.skipTest(
                "the dumper is not built "
                "(cargo build --release --manifest-path external/dumper/Cargo.toml)"
            )
        modules = subprocess.run(
            [str(dumper), "--list-signature-modules"],
            capture_output=True,
            text=True,
            check=True,
        )
        counts = subprocess.run(
            [str(dumper), "--list-signature-counts"],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(
            sorted(json.loads(modules.stdout)),
            sorted(json.loads(counts.stdout)),
        )


if __name__ == "__main__":
    unittest.main()
