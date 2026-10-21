#!/usr/bin/env python3
"""The differential test of the string protection.

The pass encrypts the literals the policy selects and decodes them at run
time. A mistake does not crash: it decodes the wrong bytes, and the program
carries on with the wrong string. The test therefore builds the same fixture
twice -- once with the pass and once without -- loads both and compares what
they return.

The fixture is a **DLL**, not an EXE: the key is derived from the instance
handle, and only the OS loader calls `DllMain` with the real handle. The
fixture therefore mirrors the payload: it defines `cs2_protect_instance_base`
and publishes its own handle in its `DllMain`.

The test also checks the image: the plaintext of a protected literal is in
the unprotected DLL and **not** in the protected one. That is the check that
fails if the protection silently did nothing.

Usage: str_diff_test.py --clang-cl FILE --plugin FILE --fixture FILE
                       --policy FILE --work DIR
"""
from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

#: The exit code CTest reads as "skipped".
SKIP = 77

#: The literals the policy protects. The test checks that they are in the
#: unprotected image and not in the protected one.
PROTECTED_LITERALS = ("healthy", "shared", "formatted", "four", "twoblok")

#: The literals the policy does not protect: the denied one, the one the
#: policy does not select, and the one the gate refuses because its address
#: escapes into a global initializer. All three have to be in **both**
#: images.
UNPROTECTED_LITERALS = ("deniedxx", "untouched", "escaped")

#: The number of literals the policy protects. The report has to name each
#: of them as protected: a pass that protected fewer of them would leave a
#: literal in the image, and the image check would not see it.
EXPECTED_PROTECTED = len(PROTECTED_LITERALS)

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


def run(
    command: list[str], environment: dict[str, str], vcvars: Path | None
) -> subprocess.CompletedProcess[str]:
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
            ["cmd", "/c", str(script)], capture_output=True, text=True, env=environment
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
    """Builds the fixture as a DLL, with the pass or without it."""
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(coverage)
    command = [
        str(clang_cl),
        "/nologo",
        "/std:c++latest",
        "/O2",
        "/EHsc",
        "/GR-",
        "/MT",
        "/LD",
        f"/Fe{output}",
        str(source),
    ]
    if plugin is not None:
        command.append(f"/clang:-fpass-plugin={plugin}")
    return run(command, environment, vcvars)


def probe(dll: Path, text: bytes) -> int:
    """Loads the DLL and calls its exported probe.

    The loader calls `DllMain`, so the fixture publishes its own instance
    handle before the probe runs -- exactly like the payload.
    """
    library = ctypes.WinDLL(str(dll))
    function = library.probe
    function.restype = ctypes.c_ulonglong
    function.argtypes = [ctypes.c_char_p]
    return int(function(text))


def probe_blocks(dll: Path, text: bytes) -> int:
    """Loads the DLL and calls its exported two-block probe.

    The probe uses one literal in the entry block and in another block: the
    decode has to run before both uses, or the entry-block use reads an
    uninitialised stack slot and the two builds disagree.
    """
    library = ctypes.WinDLL(str(dll))
    function = library.probe_blocks
    function.restype = ctypes.c_ulonglong
    function.argtypes = [ctypes.c_char_p]
    return int(function(text))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-cl", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)

    if not args.plugin.is_file():
        print(f"str_diff_test: the plugin '{args.plugin}' is not built: skipped")
        return SKIP

    work = args.work
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    vcvars = find_vcvars()
    if vcvars is None:
        print("str_diff_test: no vcvars64.bat found: the fixture needs the MSVC toolchain")
        return 1

    try:
        plain = work / "plain.dll"
        protected = work / "protected.dll"

        built = compile_fixture(
            args.clang_cl, args.fixture, plain, None, args.policy, work / "coverage-plain", vcvars
        )
        check(
            built.returncode == 0,
            f"the unprotected build failed:\n{built.stdout}\n{built.stderr}",
        )

        built = compile_fixture(
            args.clang_cl, args.fixture, protected, args.plugin, args.policy,
            work / "coverage", vcvars
        )
        check(
            built.returncode == 0,
            f"the protected build failed:\n{built.stdout}\n{built.stderr}",
        )

        # The pass has to have done something: the report says how many
        # literals it protected, and the two images have to differ.
        check("str:" in built.stderr, f"the pass did not report a string:\n{built.stderr}")
        protected_count = built.stderr.count(": protected")
        check(
            protected_count == EXPECTED_PROTECTED,
            f"the pass reported {protected_count} protected literals, expected "
            f"{EXPECTED_PROTECTED}:\n{built.stderr}",
        )
        check(
            plain.read_bytes() != protected.read_bytes(),
            "the protected image is identical to the unprotected one: the pass did nothing",
        )

        # The image: the plaintext of a protected literal is in the
        # unprotected image and **not** in the protected one.
        plain_bytes = plain.read_bytes()
        protected_bytes = protected.read_bytes()
        for literal in PROTECTED_LITERALS:
            needle = literal.encode("ascii")
            check(
                needle in plain_bytes,
                f"the unprotected image does not hold '{literal}': the test would be vacuous",
            )
            check(
                needle not in protected_bytes,
                f"the protected image still holds the plaintext of '{literal}'",
            )
        for literal in UNPROTECTED_LITERALS:
            needle = literal.encode("ascii")
            check(
                needle in plain_bytes,
                f"the unprotected image does not hold '{literal}': the test would be vacuous",
            )
            check(
                needle in protected_bytes,
                f"the protected image does not hold '{literal}': the gate changed a literal it "
                "had refused",
            )

        # The two runs have to agree.
        for text in (b"healthy", b"something else", b""):
            first = probe(plain, text)
            second = probe(protected, text)
            check(
                first == second,
                f"the two builds disagree on '{text.decode('ascii', 'replace')}': "
                f"{first} and {second}",
            )

        # The two-block probe: one literal, two uses, one of them in the
        # entry block. A decode that is emitted after the use of the entry
        # block leaves that use reading an uninitialised stack slot, and the
        # two builds disagree -- silently, because nothing crashes.
        for text in (b"x", b"y", b""):
            first = probe_blocks(plain, text)
            second = probe_blocks(protected, text)
            check(
                first == second,
                f"the two builds disagree on the two-block probe "
                f"'{text.decode('ascii', 'replace')}': {first} and {second}",
            )
    except Failure as error:
        print(f"str_diff_test: {error}", file=sys.stderr)
        return 1

    print("str_diff_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
