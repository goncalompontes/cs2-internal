#!/usr/bin/env python3
"""The differential test of the protection pass.

The pass rewrites the calls of a selected function into indirect dispatches
through an encrypted descriptor. A mistake does not crash: it calls the
*wrong* function, so the test builds the same fixture twice -- once with the
pass and once without -- runs both and compares their output *and* their
exit code.

The fixture calls enough functions through the rewritten path (and one
variadic callee, which the pass must skip) that a mis-dispatch changes the
result.

The build needs the MSVC toolchain (the static CRT of the fixture), so the
driver sets up vcvars64 the way scripts/build-protect-plugin.py does.

Usage: diff_test.py --clang-cl FILE --plugin FILE --fixture FILE --policy FILE --work DIR
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

VSWHERE = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
KNOWN_VCVARS = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)


class Failure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise Failure(what)


def find_vcvars() -> Path | None:
    """The vcvars64 script of the Visual Studio installation."""
    if VSWHERE.is_file():
        try:
            output = subprocess.run(
                [str(VSWHERE), "-latest", "-property", "installationPath"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            output = ""
        if output:
            script = Path(output) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
            if script.is_file():
                return script
    return KNOWN_VCVARS if KNOWN_VCVARS.is_file() else None


def run(command: list[str], environment: dict[str, str], vcvars: Path | None) -> subprocess.CompletedProcess[str]:
    """Runs a command, under vcvars64 when the shell has no MSVC toolchain.

    The command goes through a temporary batch file: cmd.exe does not
    understand the quoting that Python uses for an argument that contains
    quotes of its own.
    """
    if vcvars is None or (os.environ.get("INCLUDE") and os.environ.get("LIB")):
        return subprocess.run(command, capture_output=True, text=True, env=environment)

    with tempfile.NamedTemporaryFile(
        "w", suffix=".bat", delete=False, encoding="utf-8", newline="\r\n"
    ) as batch:
        batch.write("@echo off\n")
        batch.write(f'call "{vcvars}" >nul 2>&1\n')
        batch.write(subprocess.list2cmdline(command) + "\n")
        batch.write("exit /b %ERRORLEVEL%\n")
        script = Path(batch.name)
    try:
        return subprocess.run(
            ["cmd", "/c", str(script)],
            capture_output=True,
            text=True,
            env=environment,
        )
    finally:
        script.unlink(missing_ok=True)


def compile_fixture(
    clang_cl: Path,
    source: Path,
    output: Path,
    plugin: Path | None,
    policy: Path,
    coverage: Path,
    vcvars: Path | None,
) -> subprocess.CompletedProcess[str]:
    """Builds the fixture, with the pass or without it."""
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(coverage)
    command = [
        str(clang_cl),
        "/nologo",
        "/std:c++latest",
        "/O2",
        # The payload is built with /EHsc, and so is the fixture: the
        # selected function has an EH edge, and the pass must skip the
        # `invoke` and leave the plain calls alone.
        "/EHsc",
        "/GR-",
        "/MT",
        f"/Fe{output}",
        str(source),
    ]
    if plugin is not None:
        command.append(f"/clang:-fpass-plugin={plugin}")
    return run(command, environment, vcvars)


def count_dispatches(objdump: Path, binary: Path) -> int:
    """The number of encrypted dispatches in a binary.

    A dispatch is an indirect call through the table: `call *(%rX,%rY,8)`.
    The direct call that the pass replaced is gone, so the count is the
    number of call sites the pass rewrote.
    """
    completed = subprocess.run(
        [str(objdump), "-d", str(binary)], capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise Failure(f"objdump failed on '{binary}':\n{completed.stderr}")
    return len(re.findall(r"(?:callq|jmpq)\s+\*\(%r[a-z0-9]+,%r[a-z0-9]+,8\)", completed.stdout))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-cl", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        check_result(args)
    except Failure as error:
        print(f"diff_test: {error}", file=sys.stderr)
        return 1

    print("diff_test: ok")
    return 0


def check_result(args: argparse.Namespace) -> None:
    """The assertions of the differential test."""
    work = args.work
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    vcvars = find_vcvars()
    check(vcvars is not None, "no vcvars64.bat found: the fixture needs the MSVC toolchain")

    plain = work / "plain.exe"
    protected = work / "protected.exe"

    built = compile_fixture(
        args.clang_cl, args.fixture, plain, None, args.policy, work / "coverage-plain", vcvars
    )
    check(
        built.returncode == 0,
        f"the unprotected build failed:\n{built.stdout}\n{built.stderr}",
    )

    built = compile_fixture(
        args.clang_cl, args.fixture, protected, args.plugin, args.policy, work / "coverage", vcvars
    )
    check(
        built.returncode == 0,
        f"the protected build failed:\n{built.stdout}\n{built.stderr}",
    )

    # The pass has to have done something: the report says how many calls it
    # rewrote, and the two binaries have to differ.
    check(
        "rewritten=" in built.stderr,
        f"the pass did not report a rewrite:\n{built.stderr}",
    )
    rewritten = 0
    for line in built.stderr.splitlines():
        if "rewritten=" in line:
            for part in line.split():
                if part.startswith("rewritten="):
                    rewritten = max(rewritten, int(part.split("=", 1)[1]))
    check(rewritten > 0, "the pass rewrote no call at all")
    check(
        plain.read_bytes() != protected.read_bytes(),
        "the protected binary is identical to the unprotected one: the pass did nothing",
    )

    # The dispatch has to be in the *machine code*. The key is a compile-time
    # key, so the whole dispatch is a compile-time constant: a compiler that
    # folds it away leaves a direct call behind, the program still behaves
    # the same, and the protection is void. The differential check below
    # would not notice -- this one does.
    plain_dispatches = count_dispatches(args.objdump, plain)
    protected_dispatches = count_dispatches(args.objdump, protected)
    check(
        protected_dispatches == plain_dispatches + rewritten,
        f"the protected binary has {protected_dispatches} dispatches and the unprotected one "
        f"{plain_dispatches}, but the pass reported {rewritten} rewrites: the compiler folded "
        "the dispatch away",
    )

    # The two runs have to agree.
    plain_run = subprocess.run([str(plain)], capture_output=True, text=True)
    protected_run = subprocess.run([str(protected)], capture_output=True, text=True)
    check(
        plain_run.stdout == protected_run.stdout,
        f"the output differs:\n--- unprotected\n{plain_run.stdout}\n--- protected\n{protected_run.stdout}",
    )
    check(
        plain_run.returncode == protected_run.returncode,
        f"the exit code differs: {plain_run.returncode} and {protected_run.returncode}",
    )
    check(plain_run.stdout != "", "the fixture printed nothing: the test would be vacuous")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

