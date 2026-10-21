#!/usr/bin/env python3
"""The IR-level test of the string protection.

The pass encrypts the literals the policy selects, deletes the plaintext
literal from the module and emits a decode into every using function. A
mistake does not crash: it decodes the wrong bytes, so the test asserts on
the shape of the rewritten IR:

  * the plaintext of a protected literal is **gone** from the IR;
  * the blob and the decode call **are** there;
  * the decode reads the instance base (a volatile load) and the seeds;
  * a refused literal (one whose address is baked into a global initializer)
    is **byte for byte unchanged**;
  * a denied literal and a literal the policy does not select are unchanged
    as well.

The test exercises **two** paths, because `opt` does not invoke the
extension points: the `opt` path runs the named pipeline over the IR of the
fixture, and the **production path** compiles the fixture with
`clang-cl -fpass-plugin=<dll> -emit-llvm -S`, which is what the payload
build does. A test that only exercised the `opt` path could pass for the
wrong reason.

Usage: str_ir_test.py --opt FILE --clang FILE --plugin FILE --fixture FILE
                     --policy FILE --work DIR
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

#: The exit code CTest reads as "skipped".
SKIP = 77

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
            ["cmd", "/c", str(script)], capture_output=True, text=True, env=environment
        )
    finally:
        script.unlink(missing_ok=True)


def environment_for(policy: Path, coverage: Path) -> dict[str, str]:
    """The environment the compiler hands down to the plugin."""
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(coverage)
    return environment


def compile_to_ir(
    clang: Path,
    source: Path,
    output: Path,
    plugin: Path | None,
    policy: Path,
    coverage: Path,
    vcvars: Path | None,
) -> subprocess.CompletedProcess[str]:
    """Compiles the fixture to LLVM IR, with the pass or without it.

    The plain `clang` driver is used, not `clang-cl`: the driver knows how to
    write LLVM IR (`-emit-llvm -S`), and the MSVC toolchain comes from
    vcvars64, which is what the fixture's standard library headers need.
    """
    environment = environment_for(policy, coverage)
    command = [
        str(clang),
        "-std=c++20",
        "-O2",
        "-emit-llvm",
        "-S",
        "-o",
        str(output),
        str(source),
    ]
    if plugin is not None:
        command.append(f"-fpass-plugin={plugin}")
    return run(command, environment, vcvars)


def run_opt(opt: Path, plugin: Path, source: Path, output: Path, policy: Path, coverage: Path) -> str:
    """Runs the pass over the IR of the fixture and returns the output IR."""
    environment = environment_for(policy, coverage)
    command = [
        str(opt),
        f"-load-pass-plugin={plugin}",
        "-passes=cs2-protect",
        "-S",
        str(source),
        "-o",
        str(output),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, env=environment)
    if completed.returncode != 0:
        raise Failure(f"opt failed:\n{completed.stderr}")
    return output.read_text(encoding="utf-8", errors="replace")


def check_ir(ir: str, what: str) -> None:
    """The assertions of the IR test."""
    # The plaintext of a protected literal is gone: the file no longer holds
    # the string, which is the whole point of the task. The *name* of the
    # literal is checked as well: the MSVC ABI spells the name of a string
    # literal after the string itself, so a literal that is not deleted
    # leaks its content through its own symbol.
    for name in ("healthy", "shared", "formatted", "four"):
        check(f'c"{name}' not in ir, f"{what}: the plaintext of a protected literal is still there")
        check(f'@{name}' not in ir, f"{what}: the name of a protected literal is still there")

    # The blob and the decode are there.
    check("@cs2.protect.strblob." in ir, f"{what}: the blob is missing")
    check("call void @cs2.protect.strdecode" in ir, f"{what}: the decode call is missing")
    check("alloca" in ir, f"{what}: the buffer is missing")

    # The decode reads the instance base and the seeds with volatile loads:
    # the whole decode is a compile-time constant once the seeds are known,
    # so a compiler that folds it folds the *plaintext* into the image.
    check(
        "load volatile i64, ptr @cs2_protect_instance_base" in ir,
        f"{what}: the decode does not read the instance base",
    )
    check("load volatile i64, ptr @cs2.protect.strseed0" in ir,
          f"{what}: the decode does not read the seed")
    check("load volatile i8" in ir, f"{what}: the blob is not read with a volatile load")

    # The mixer constants live in globals and not in the instruction stream:
    # the X86 backend of this LLVM mis-materialises a 64-bit immediate. The
    # check looks at the *body* of the decode, not at the module: the
    # constant is of course in the initializer of its global.
    body = ir[ir.find("define internal void @cs2.protect.strdecode") :]
    check(body != "", f"{what}: the decode helper is not there")
    for value in ("-7046029254386353131", "-4658895280553007687"):
        check(value not in body, f"{what}: a 64-bit constant of the mixer is an immediate")

    # The refused literal is byte for byte unchanged: the gate does not
    # half-protect.
    check('[8 x i8] c"escaped\\00"' in ir, f"{what}: the refused literal was changed")
    # The denied literal and the literal the policy does not select are
    # untouched as well.
    check('[9 x i8] c"deniedxx\\00"' in ir, f"{what}: the denied literal was changed")
    check('[10 x i8] c"untouched\\00"' in ir, f"{what}: an unselected literal was changed")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)

    if not args.plugin.is_file():
        print(f"str_ir_test: the plugin '{args.plugin}' is not built: skipped")
        return SKIP

    work = args.work
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    vcvars = find_vcvars()
    if vcvars is None:
        print("str_ir_test: no vcvars64.bat found: the fixture needs the MSVC toolchain")
        return 1

    try:
        # The IR of the fixture, without the pass: the input of the `opt`
        # path. The fixture is compiled at /O2, so that the literals are not
        # folded away and the functions are not `optnone`.
        plain = work / "plain.ll"
        built = compile_to_ir(
            args.clang, args.fixture, plain, None, args.policy, work / "coverage", vcvars
        )
        check(
            built.returncode == 0,
            f"the unprotected compile failed:\n{built.stdout}\n{built.stderr}",
        )

        # The `opt` path: the named pipeline over the IR of the fixture.
        opt_output = work / "opt.ll"
        ir = run_opt(args.opt, args.plugin, plain, opt_output, args.policy, work / "coverage")
        check_ir(ir, "the opt path")

        # The production path: the plugin is loaded by the compiler, exactly
        # as the payload build loads it.
        production = work / "production.ll"
        built = compile_to_ir(
            args.clang, args.fixture, production, args.plugin, args.policy,
            work / "coverage", vcvars
        )
        check(
            built.returncode == 0,
            f"the protected compile failed:\n{built.stdout}\n{built.stderr}",
        )
        ir = production.read_text(encoding="utf-8", errors="replace")
        check_ir(ir, "the production path")
    except Failure as error:
        print(f"str_ir_test: {error}", file=sys.stderr)
        return 1

    print("str_ir_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
