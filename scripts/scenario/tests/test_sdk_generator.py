"""Unit tests for the SDK generator's dumper-path resolution.

``scripts/gen-sdk.py`` renders the C++ SDK headers by running the Rust
dumper, and the dumper binary is a **cargo** artifact: cargo puts it in
``external/dumper/target/release/`` — unless ``CARGO_TARGET_DIR`` moves the
whole target directory somewhere else.  A worktree (or any build that keeps
cargo's output out of the source tree) sets that variable, and the generator
has to follow it: otherwise it looks for the binary where cargo did *not*
put it, and the generation writes **empty** SDK headers.  A green build
against an empty SDK is worse than a red one.

These tests are **independent of the build**: they drive the generator's own
functions, and its ``main`` with a faked ``subprocess``, so no cargo build is
needed and nothing is written into the source tree.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import os
import pathlib
import tempfile
import unittest
import unittest.mock
from typing import Any, Iterator

#: The repository root (``scripts/scenario/tests`` -> ``scripts`` -> root).
ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The generator (its name is not a module name, so it is loaded by path).
GENERATOR = ROOT / "scripts" / "gen-sdk.py"


def _load_generator() -> Any:
    """The generator module, loaded from its path.

    The module is loaded dynamically (its file name is not importable), so
    its functions are reached through ``Any``: the checker cannot see them.
    """
    spec = importlib.util.spec_from_file_location("gen_sdk", GENERATOR)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GENERATOR_MODULE = _load_generator()


@contextlib.contextmanager
def _cargo_target_dir(value: str | None) -> Iterator[None]:
    """The environment with ``CARGO_TARGET_DIR`` set — or removed.

    ``None`` removes the variable, so the "cargo did not move its target
    directory" case is the *real* unset case and not an empty string.
    """
    with unittest.mock.patch.dict(os.environ):
        if value is None:
            os.environ.pop("CARGO_TARGET_DIR", None)
        else:
            os.environ["CARGO_TARGET_DIR"] = value
        yield


class _FakeSubprocess:
    """The generator's ``subprocess``, faked: it records the commands.

    It replaces the module's own ``subprocess`` name, so nothing is spawned
    and no dumper is ever run — the test only asks *which* binary the
    generator would have run.
    """

    def __init__(self) -> None:
        self.commands: list[list[str]] = []

    def run(self, command: list[str], check: bool = False) -> None:
        self.commands.append(list(command))


class TestDumperPath(unittest.TestCase):
    """The resolution: where the dumper binary is."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def test_the_path_follows_cargo_target_dir(self) -> None:
        """With the variable set, the binary is in cargo's target directory."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            expected = target / "release" / self.generator.dumper_exe_name()
            self.assertEqual(
                self.generator.dumper_binary(ROOT, {"CARGO_TARGET_DIR": str(target)}),
                expected,
            )

    def test_the_path_is_the_crate_target_without_cargo_target_dir(self) -> None:
        """Without the variable, the binary is where it always was."""
        expected = (ROOT / "external" / "dumper" / "target" / "release"
                    / self.generator.dumper_exe_name())
        self.assertEqual(self.generator.dumper_binary(ROOT, {}), expected)

    def test_an_empty_cargo_target_dir_is_treated_as_unset(self) -> None:
        """An empty value moves nothing: the crate's own target directory."""
        expected = (ROOT / "external" / "dumper" / "target" / "release"
                    / self.generator.dumper_exe_name())
        self.assertEqual(
            self.generator.dumper_binary(ROOT, {"CARGO_TARGET_DIR": ""}),
            expected,
        )

    def test_the_process_environment_is_the_default(self) -> None:
        """Without an explicit environment, the process' own one is read."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            expected = target / "release" / self.generator.dumper_exe_name()
            with _cargo_target_dir(str(target)):
                self.assertEqual(self.generator.dumper_binary(ROOT), expected)

    def test_the_release_profile_is_the_one_used(self) -> None:
        """The release build is the one the generator builds and runs."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            path = self.generator.dumper_binary(
                ROOT, {"CARGO_TARGET_DIR": str(target)})
            self.assertEqual(path.parent.name, "release")


class TestGeneratorUsesTheResolvedPath(unittest.TestCase):
    """The generator runs the binary it resolved — not a fixed path."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def _run(self, target: pathlib.Path | None) -> list[list[str]]:
        """Run ``main`` with a faked ``subprocess``; return the commands.

        ``target`` is the ``CARGO_TARGET_DIR`` to set, or ``None`` to leave
        the variable unset.
        """
        fake = _FakeSubprocess()
        self.generator.subprocess = fake
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            dumps = work / "calibration"
            dumps.mkdir()
            (dumps / "interfaces.json").write_text("{}", encoding="utf-8")
            out = work / "sdk"
            out.mkdir()
            stamp = work / "sdk.stamp"

            value = None if target is None else str(target)
            with _cargo_target_dir(value):
                # The generator reports on stderr; the test does not need it.
                with contextlib.redirect_stderr(io.StringIO()):
                    code = self.generator.main(
                        [str(dumps), str(out), str(stamp)])
            self.assertEqual(code, 0)
            self.assertTrue(stamp.is_file())
        return fake.commands

    def test_it_runs_the_binary_where_cargo_put_it(self) -> None:
        """The binary in cargo's target directory is the one that is run."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            binary = target / "release" / self.generator.dumper_exe_name()
            binary.parent.mkdir(parents=True)
            binary.write_text("", encoding="utf-8")

            commands = self._run(target)

            # The binary is there, so the dumper is **not** rebuilt ...
            self.assertEqual(len(commands), 1)
            # ... and the one that is run is cargo's.
            self.assertEqual(commands[0][0], str(binary))

    def test_it_builds_the_dumper_into_cargo_target_dir(self) -> None:
        """The on-demand build stays, and the binary is still cargo's."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            binary = target / "release" / self.generator.dumper_exe_name()

            commands = self._run(target)

            # The binary is missing, so cargo builds it ...
            self.assertGreaterEqual(len(commands), 2)
            self.assertEqual(commands[0][0], "cargo")
            self.assertIn("--release", commands[0])
            # ... and the binary that is run afterwards is the one cargo put
            # in its target directory.
            self.assertEqual(commands[-1][0], str(binary))

    def test_it_runs_the_crate_binary_without_cargo_target_dir(self) -> None:
        """Without the variable the binary is in the crate's target dir."""
        commands = self._run(None)
        expected = (ROOT / "external" / "dumper" / "target" / "release"
                    / self.generator.dumper_exe_name())
        self.assertEqual(commands[-1][0], str(expected))


if __name__ == "__main__":
    unittest.main(verbosity=2)
