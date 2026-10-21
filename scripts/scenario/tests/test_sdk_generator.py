"""Unit tests for the SDK generator's dumper-path resolution.

``scripts/gen-sdk.py`` renders the C++ SDK headers by running the Rust
dumper, and the dumper binary is a **cargo** artifact: cargo puts it in
``external/dumper/target/release/`` — unless ``CARGO_TARGET_DIR`` moves the
whole target directory somewhere else.  A worktree (or any build that keeps
cargo's output out of the source tree) sets that variable, and the generator
has to follow it: otherwise it looks for the binary where cargo did *not*
put it, and the generation writes **empty** SDK headers.  A green build
against an empty SDK is worse than a red one.

The same is true of the **live** path: the live calibration runs the dumper
too, so it has to be given the binary the generator resolved — and its
failure has to fail the build, not be swallowed into an empty SDK.

These tests are **independent of the build**: they drive the generator's own
functions, and its ``main`` with a faked ``subprocess``, so no cargo build is
needed and nothing is written into the source tree.  The one exception is
``TestCargoLayout``, which asks cargo itself where it puts a binary — the
question "is the release profile the one cargo uses?" cannot be answered
against the script.

    python -m unittest discover -s scripts/scenario/tests -t scripts
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
import unittest.mock
from collections.abc import Callable, Iterator
from typing import Any, cast

#: The repository root (``scripts/scenario/tests`` -> ``scripts`` -> root).
ROOT = pathlib.Path(__file__).resolve().parents[3]

#: The generator (its name is not a module name, so it is loaded by path).
GENERATOR = ROOT / "scripts" / "gen-sdk.py"

#: Cargo, if it is installed (the layout test needs it).
CARGO = shutil.which("cargo")


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


@contextlib.contextmanager
def _working_directory(path: pathlib.Path) -> Iterator[None]:
    """The process' working directory, moved there and back."""
    previous = os.getcwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def _host_triple() -> str | None:
    """The rust target triple of this host, if rustc is installed."""
    rustc = shutil.which("rustc")
    if rustc is None:
        return None
    completed = subprocess.run([rustc, "-vV"], capture_output=True, text=True,
                               timeout=120)
    if completed.returncode != 0:
        return None
    for line in completed.stdout.splitlines():
        if line.startswith("host: "):
            return line.removeprefix("host: ").strip()
    return None


def _drop(artifact: pathlib.Path) -> Callable[[list[str]], None]:
    """A faked cargo build that drops the artifact where cargo would."""
    def build(command: list[str]) -> None:
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text("", encoding="utf-8")
    return build


class _FakeSubprocess:
    """The generator's ``subprocess``, faked: it records the commands.

    It replaces the module's own ``subprocess`` name, so nothing is spawned
    and no dumper is ever run — the test only asks *which* binary the
    generator would have run.  ``live_exit_code`` is the exit code the faked
    live calibration reports; ``on_build`` is what the faked cargo build
    does (it drops the artifact, so the generator finds it afterwards).
    """

    def __init__(self, live_exit_code: int = 0,
                 on_build: Callable[[list[str]], None] | None = None) -> None:
        self.commands: list[list[str]] = []
        self.working_directories: list[str] = []
        self.live_exit_code = live_exit_code
        self.on_build = on_build

    def run(self, command: list[str], check: bool = False, **_: Any) -> Any:
        self.commands.append(list(command))
        # The working directory the command would have run in: cargo resolves
        # a relative ``CARGO_TARGET_DIR`` against it.
        self.working_directories.append(os.getcwd())
        if command and command[0] == "cargo" and self.on_build is not None:
            self.on_build(list(command))
        live = any("calibrate-live.py" in part for part in command)
        code = self.live_exit_code if live else 0
        return subprocess.CompletedProcess[list[str]](list(command), code)


class TestDumperPath(unittest.TestCase):
    """The resolution: where the dumper binary is."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def _candidate(self, env: dict[str, str]) -> pathlib.Path:
        """The path the generator looks at, for that environment."""
        return cast(pathlib.Path,
                    self.generator.dumper_candidates(ROOT, env)[0])

    def test_the_path_follows_cargo_target_dir(self) -> None:
        """With the variable set, the binary is in cargo's target directory."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            expected = target / "release" / self.generator.dumper_exe_name()
            self.assertEqual(
                self._candidate({"CARGO_TARGET_DIR": str(target)}),
                expected,
            )

    def test_the_path_is_the_crate_target_without_cargo_target_dir(self) -> None:
        """Without the variable, the binary is where it always was."""
        expected = (ROOT / "external" / "dumper" / "target" / "release"
                    / self.generator.dumper_exe_name())
        self.assertEqual(self._candidate({}), expected)

    def test_an_empty_cargo_target_dir_is_treated_as_unset(self) -> None:
        """An empty value moves nothing: the crate's own target directory."""
        expected = (ROOT / "external" / "dumper" / "target" / "release"
                    / self.generator.dumper_exe_name())
        self.assertEqual(self._candidate({"CARGO_TARGET_DIR": ""}), expected)

    def test_the_process_environment_is_the_default(self) -> None:
        """Without an explicit environment, the process' own one is read."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            expected = target / "release" / self.generator.dumper_exe_name()
            with _cargo_target_dir(str(target)):
                self.assertEqual(
                    self.generator.dumper_candidates(ROOT)[0], expected)

    def test_a_target_triple_moves_the_binary_into_its_own_directory(self) -> None:
        """Cargo builds a triple into ``<target>/<triple>/release/``."""
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / "cargo-target"
            expected = (target / "x86_64-pc-windows-msvc" / "release"
                        / self.generator.dumper_exe_name())
            self.assertEqual(
                self._candidate({
                    "CARGO_TARGET_DIR": str(target),
                    "CARGO_BUILD_TARGET": "x86_64-pc-windows-msvc",
                }),
                expected,
            )

    def test_a_relative_target_dir_stays_relative(self) -> None:
        """A relative ``CARGO_TARGET_DIR`` is cargo's rule: relative to the cwd.

        The path stays relative on purpose — cargo resolves it against the
        working directory, and so must everything that looks the binary up.
        """
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            with _working_directory(work):
                path = self._candidate({"CARGO_TARGET_DIR": "cargo-target"})
                self.assertFalse(path.is_absolute())
                self.assertEqual(
                    path,
                    pathlib.Path("cargo-target") / "release"
                    / self.generator.dumper_exe_name(),
                )
                self.assertEqual(
                    path.resolve(),
                    work / "cargo-target" / "release"
                    / self.generator.dumper_exe_name(),
                )


class TestGeneratorUsesTheResolvedPath(unittest.TestCase):
    """The generator runs the binary it resolved — not a fixed path."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def _run(self, work: pathlib.Path, target: pathlib.Path | None,
             on_build: Callable[[list[str]], None] | None = None,
             root: pathlib.Path | None = None) -> _FakeSubprocess:
        """Run ``main`` with a faked ``subprocess``; return the fake.

        ``target`` is the ``CARGO_TARGET_DIR`` to set, or ``None`` to leave
        the variable unset; ``on_build`` is what the faked cargo build does;
        ``root`` replaces the repository root the generator works in.
        """
        fake = _FakeSubprocess(on_build=on_build)
        dumps = work / "calibration"
        dumps.mkdir()
        (dumps / "interfaces.json").write_text("{}", encoding="utf-8")
        out = work / "sdk"
        out.mkdir()
        stamp = work / "sdk.stamp"

        value = None if target is None else str(target)
        # The fake is installed for the call only: the generator module is a
        # singleton, so a fake that outlived the call would leak into every
        # test that runs after this one.
        with unittest.mock.patch.object(self.generator, "subprocess", fake):
            with unittest.mock.patch.object(self.generator, "ROOT", root or ROOT):
                with _cargo_target_dir(value):
                    # The generator reports on stderr; the test does not need it.
                    with contextlib.redirect_stderr(io.StringIO()):
                        code = self.generator.main(
                            [str(dumps), str(out), str(stamp)])
        self.assertEqual(code, 0)
        self.assertTrue(stamp.is_file())
        return fake

    def _crate_binary(self, root: pathlib.Path) -> pathlib.Path:
        """Where the dumper is when cargo did not move its target directory."""
        return (root / "external" / "dumper" / "target" / "release"
                / cast(str, self.generator.dumper_exe_name()))

    def test_it_runs_the_binary_where_cargo_put_it(self) -> None:
        """The binary in cargo's target directory is the one that is run."""
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            target = work / "cargo-target"
            binary = target / "release" / self.generator.dumper_exe_name()
            binary.parent.mkdir(parents=True)
            binary.write_text("", encoding="utf-8")

            commands = self._run(work, target).commands

            # The binary is there, so the dumper is **not** rebuilt ...
            self.assertEqual(len(commands), 1)
            # ... and the one that is run is cargo's.
            self.assertEqual(commands[0][0], str(binary))

    def test_it_builds_the_dumper_into_cargo_target_dir(self) -> None:
        """The on-demand build stays, and the binary is still cargo's."""
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            target = work / "cargo-target"
            binary = target / "release" / self.generator.dumper_exe_name()

            commands = self._run(work, target, on_build=_drop(binary)).commands

            # The binary is missing, so cargo builds it ...
            self.assertGreaterEqual(len(commands), 2)
            self.assertEqual(commands[0][0], "cargo")
            self.assertIn("--release", commands[0])
            # ... the **dumper crate** is the one that is built ...
            self.assertIn("--manifest-path", commands[0])
            self.assertEqual(
                commands[0][commands[0].index("--manifest-path") + 1],
                str(ROOT / "external" / "dumper" / "Cargo.toml"),
            )
            # ... and the binary that is run afterwards is the one cargo put
            # in its target directory.
            self.assertEqual(commands[-1][0], str(binary))

    def test_it_runs_the_crate_binary_without_cargo_target_dir(self) -> None:
        """Without the variable the binary is in the crate's target dir."""
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            root = work / "repo"
            binary = self._crate_binary(root)

            commands = self._run(work, None, on_build=_drop(binary),
                                 root=root).commands

            self.assertEqual(commands[-1][0], str(binary))

    def test_the_fake_is_not_left_behind(self) -> None:
        """The faked ``subprocess`` does not outlive the call.

        The generator module is a module-level singleton: a fake that stays
        installed would silently answer every later test.
        """
        before = self.generator.subprocess
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            root = work / "repo"
            binary = self._crate_binary(root)
            self._run(work, None, on_build=_drop(binary), root=root)
        self.assertIs(self.generator.subprocess, before)

    def test_the_cargo_build_inherits_the_working_directory(self) -> None:
        """Cargo resolves a relative target dir against the cwd — so the
        generator must not move the cwd between resolving and building."""
        def build(command: list[str]) -> None:
            # The faked cargo build drops the artifact where cargo would:
            # relative to the working directory, like a relative target dir.
            binary = (pathlib.Path("cargo-target") / "release"
                      / self.generator.dumper_exe_name())
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_text("", encoding="utf-8")

        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            with _working_directory(work):
                fake = self._run(work, pathlib.Path("cargo-target"),
                                 on_build=build)
            self.assertTrue(fake.commands)
            for used in fake.working_directories:
                self.assertEqual(used, str(work))

    def test_a_missing_dumper_after_the_build_is_an_error(self) -> None:
        """A build that produced no binary is a sentence, not a traceback."""
        fake = _FakeSubprocess()
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            dumps = work / "calibration"
            dumps.mkdir()
            out = work / "sdk"
            out.mkdir()
            with unittest.mock.patch.object(self.generator, "subprocess", fake):
                with _cargo_target_dir(str(work / "cargo-target")):
                    with contextlib.redirect_stderr(io.StringIO()) as stderr:
                        code = self.generator.main([str(dumps), str(out)])
            self.assertEqual(code, 1)
            self.assertIn("gen-sdk:", stderr.getvalue())
            # The dumper was not run: no headers were written at all.
            self.assertEqual(len(fake.commands), 1)


class TestLiveCalibration(unittest.TestCase):
    """The live path: the calibration gets the binary, and its failure is loud."""

    def setUp(self) -> None:
        self.generator = GENERATOR_MODULE

    def _run(self, live_exit_code: int) -> tuple[int, _FakeSubprocess, pathlib.Path]:
        """Run ``main --live`` with a faked ``subprocess``.

        The dumper binary is put where cargo would put it, so the generator
        does not build anything.  Returns the exit code, the fake and the
        binary the generator was expected to use.
        """
        fake = _FakeSubprocess(live_exit_code=live_exit_code)
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            target = work / "cargo-target"
            binary = target / "release" / self.generator.dumper_exe_name()
            binary.parent.mkdir(parents=True)
            binary.write_text("", encoding="utf-8")
            dumps = work / "calibration"
            dumps.mkdir()
            out = work / "sdk"
            out.mkdir()
            with unittest.mock.patch.object(self.generator, "subprocess", fake):
                with _cargo_target_dir(str(target)):
                    with contextlib.redirect_stderr(io.StringIO()):
                        code = self.generator.main(
                            ["--live", str(dumps), str(out)])
        return code, fake, binary

    def test_the_live_calibration_gets_the_resolved_binary(self) -> None:
        """The live script is told where the dumper is.

        It resolves the binary on its own otherwise, and would look in the
        wrong place: the calibration would fail, and the failure was
        swallowed into an empty SDK.
        """
        code, fake, binary = self._run(0)
        self.assertEqual(code, 0)
        live = [command for command in fake.commands
                if any("calibrate-live.py" in part for part in command)]
        self.assertEqual(len(live), 1)
        self.assertIn("--bin", live[0])
        self.assertEqual(live[0][live[0].index("--bin") + 1], str(binary))

    def test_a_failed_live_calibration_fails_the_build(self) -> None:
        """A green build with an empty SDK is worse than a red one."""
        code, fake, _ = self._run(1)
        self.assertNotEqual(code, 0)
        # The dumper was not run: no empty SDK headers were written.
        self.assertEqual(len(fake.commands), 1)


@unittest.skipIf(CARGO is None, "cargo is not installed")
class TestCargoLayout(unittest.TestCase):
    """The layout, observed from cargo itself.

    The question "is the release profile the one cargo puts the binary in?"
    cannot be answered against the script — only against cargo.  These tests
    build a throwaway crate and look at where cargo put *its* artifact.

    The crate has a **library** target: a library needs no linker, so the
    test does not depend on the MSVC environment being set up.
    """

    def _release_dirs(self, work: pathlib.Path, target: pathlib.Path,
                      *extra: str) -> list[pathlib.Path]:
        """Build the throwaway crate; return the directories cargo used.

        The directory is the interesting part: the profile directory
        (``release/``) and, for a triple build, the triple's own directory.
        """
        crate = work / "crate"
        (crate / "src").mkdir(parents=True)
        (crate / "Cargo.toml").write_text(
            '[package]\nname = "layout-probe"\nversion = "0.0.0"\n'
            'edition = "2021"\n\n[lib]\npath = "src/lib.rs"\n',
            encoding="utf-8",
        )
        (crate / "src" / "lib.rs").write_text("pub fn probe() {}\n",
                                               encoding="utf-8")
        environment = dict(os.environ)
        environment["CARGO_TARGET_DIR"] = str(target)
        environment.pop("CARGO_BUILD_TARGET", None)
        cargo = CARGO
        assert cargo is not None, "the class is skipped without cargo"
        completed = subprocess.run(
            [cargo, "build", "--release", "--offline", *extra],
            cwd=crate, capture_output=True, text=True, timeout=600,
            env=environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        artifacts = sorted(target.glob("**/release/liblayout_probe.rlib"))
        self.assertTrue(artifacts, f"cargo built nothing in {target}")
        return [artifact.parent for artifact in artifacts]

    def test_cargo_puts_the_release_binary_in_the_release_directory(self) -> None:
        """The release profile is the directory cargo puts the binary in."""
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            target = work / "cargo-target"
            observed = self._release_dirs(work, target)
            self.assertEqual(len(observed), 1)
            resolved = GENERATOR_MODULE.dumper_candidates(
                ROOT, {"CARGO_TARGET_DIR": str(target)})[0]
            self.assertEqual(resolved.parent, observed[0])

    def test_cargo_puts_a_triple_build_in_its_own_directory(self) -> None:
        """A triple build lands in ``<target>/<triple>/release/``."""
        triple = _host_triple()
        if triple is None:
            self.skipTest("rustc is not installed")
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            target = work / "cargo-target"
            observed = self._release_dirs(work, target, "--target", triple)
            self.assertEqual(len(observed), 1)
            self.assertEqual(observed[0], target / triple / "release")
            resolved = GENERATOR_MODULE.dumper_candidates(
                ROOT, {"CARGO_TARGET_DIR": str(target),
                       "CARGO_BUILD_TARGET": triple})[0]
            self.assertEqual(resolved.parent, observed[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
