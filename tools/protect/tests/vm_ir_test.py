#!/usr/bin/env python3
"""The IR-level test of the virtualizer's front end.

The front end of the virtualizer decides whether a selected function *could*
be virtualized, lowers it to the bytecode of the virtual machine and encodes
that bytecode. It does not rewrite the function: the analysis is a verdict,
and the verdict is what the operator needs before paying for the 5-50x.

The test asserts three things:

  * the analysis does not touch the IR: the module that comes out of the
    analysis is the module that went in, byte for byte (the two are printed
    by the same printer, so the comparison is meaningful);
  * the report and the coverage carry the verdict of every function of the
    fixture, with the reason of every refusal;
  * the production path (the compiler loads the plugin and the extension
    points fire) says the same thing, and the body of the selected function
    is the body of the unprotected build.

Usage: vm_ir_test.py --opt FILE --clang-cl FILE --plugin FILE --fixture FILE
                     --source FILE --policy FILE --production-policy FILE --work DIR
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

#: The verdict of every function of the fixture: the verdict and the reason
#: of a refusal ("" when the function is virtualizable).
EXPECTED: dict[str, tuple[str, str]] = {
    "vm_leaf": ("virtualizable", ""),
    "vm_swap": ("virtualizable", ""),
    "vm_float": ("refused", "unsupported_type"),
    "vm_variadic": ("refused", "variadic_callee"),
    "vm_indirect": ("refused", "indirect_call"),
    "vm_eh": ("refused", "eh_pad"),
    "vm_call": ("virtualizable", ""),
    "vm_memcpy": ("refused", "dynamic_memcpy"),
    "vm_memcpy_constant": ("virtualizable", ""),
}

#: One line of the report: the verdict of one function.
REPORT = re.compile(
    r"\[cs2-protect\].*: vm (?P<symbol>\S+)(?: \([^)]*\))? "
    r"(?P<verdict>virtualizable|refused)(?P<rest>.*)$"
)

#: One line of the coverage: the verdict of one function.
COVERAGE = re.compile(r"^vm (?P<symbol>\S+) (?P<fields>.*)$")


class Failure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise Failure(what)


def run(command: list[str], environment: dict[str, str], cwd: Path | None = None) \
    -> subprocess.CompletedProcess[str]:
    """Runs a command and returns its result."""
    return subprocess.run(command, capture_output=True, text=True, env=environment, cwd=cwd)


def environment_for(policy: Path, coverage: Path) -> dict[str, str]:
    """The environment the plugin reads."""
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(coverage)
    return environment


def verdicts_of_report(report: str) -> dict[str, tuple[str, str]]:
    """The verdict of every function the report names."""
    verdicts: dict[str, tuple[str, str]] = {}
    for line in report.splitlines():
        matched = REPORT.search(line)
        if matched is None:
            continue
        reason = ""
        found = re.search(r"reason=(\S+)", matched.group("rest"))
        if found is not None:
            reason = found.group(1)
        verdicts[matched.group("symbol")] = (matched.group("verdict"), reason)
    return verdicts


def verdicts_of_coverage(coverage: Path) -> dict[str, dict[str, str]]:
    """The verdict of every function the coverage names."""
    verdicts: dict[str, dict[str, str]] = {}
    for line in coverage.read_text(encoding="utf-8", errors="replace").splitlines():
        matched = COVERAGE.match(line)
        if matched is None:
            continue
        fields: dict[str, str] = {}
        for part in matched.group("fields").split():
            name, _, value = part.partition("=")
            fields[name] = value
        verdicts[matched.group("symbol")] = fields
    return verdicts


def coverage_files(directory: Path) -> list[Path]:
    """The coverage files of a directory."""
    return sorted(directory.glob("*.cov"))


def check_verdicts(verdicts: dict[str, tuple[str, str]], what: str,
                   expected: dict[str, tuple[str, str]] | None = None) -> None:
    """The verdict of every function of the fixture."""
    if expected is None:
        expected = EXPECTED
    for symbol, (verdict, reason) in expected.items():
        check(symbol in verdicts, f"{what}: the verdict of '{symbol}' is missing")
        assert symbol in verdicts
        found_verdict, found_reason = verdicts[symbol]
        check(
            found_verdict == verdict,
            f"{what}: the verdict of '{symbol}' is '{found_verdict}' and not '{verdict}'",
        )
        if reason:
            check(
                found_reason == reason,
                f"{what}: the refusal of '{symbol}' is '{found_reason}' and not '{reason}'",
            )


def body_of(ir: str, function: str) -> str:
    """The body of one function of the IR.

    The body is the lines between the opening brace of the definition and
    its closing brace: the attributes of the definition and the metadata of
    the module are not part of it.
    """
    matched = re.search(rf"^define .*@{function}\(.*\) .*\{{$", ir, re.MULTILINE)
    check(matched is not None, f"the function '{function}' is not in the IR")
    assert matched is not None
    body: list[str] = []
    for line in ir[matched.end():].splitlines():
        if line == "}":
            break
        body.append(line)
    return "\n".join(body)


def check_read_only(opt: Path, plugin: Path, fixture: Path, policy: Path, work: Path) -> None:
    """The analysis does not touch the IR."""
    baseline = work / "baseline.ll"
    analysed = work / "analysed.ll"

    completed = run(
        [str(opt), "-S", str(fixture), "-o", str(baseline)],
        environment_for(policy, work / "coverage-empty"),
    )
    check(completed.returncode == 0, f"opt failed:\n{completed.stderr}")

    completed = run(
        [
            str(opt),
            f"-load-pass-plugin={plugin}",
            "-passes=cs2-protect-vm",
            "-S",
            str(fixture),
            "-o",
            str(analysed),
        ],
        environment_for(policy, work / "coverage"),
    )
    check(completed.returncode == 0, f"opt failed:\n{completed.stderr}")

    check(
        baseline.read_bytes() == analysed.read_bytes(),
        "the analysis changed the module: it has to be read-only",
    )
    check_verdicts(verdicts_of_report(completed.stderr), "the report")


def check_full_pipeline(opt: Path, plugin: Path, fixture: Path, policy: Path, work: Path) -> None:
    """The named pipeline reports the verdict and writes it to the coverage."""
    output = work / "full.ll"
    coverage = work / "coverage-full"
    completed = run(
        [
            str(opt),
            f"-load-pass-plugin={plugin}",
            "-passes=cs2-protect",
            "-S",
            str(fixture),
            "-o",
            str(output),
        ],
        environment_for(policy, coverage),
    )
    check(completed.returncode == 0, f"opt failed:\n{completed.stderr}")
    check_verdicts(verdicts_of_report(completed.stderr), "the report of the pipeline")

    files = coverage_files(coverage)
    check(len(files) == 1, f"the coverage holds {len(files)} files and not one")
    verdicts = verdicts_of_coverage(files[0])
    check_verdicts(
        {symbol: (fields.get("verdict", ""), fields.get("reason", ""))
         for symbol, fields in verdicts.items()},
        "the coverage",
    )
    for symbol, fields in verdicts.items():
        check("slots" in fields, f"the coverage of '{symbol}' has no slot count")
        check("frame" in fields, f"the coverage of '{symbol}' has no frame size")
        check("bytecode" in fields, f"the coverage of '{symbol}' has no bytecode size")
        check("instructions" in fields, f"the coverage of '{symbol}' has no instruction count")
        check("calls" in fields, f"the coverage of '{symbol}' has no call count")


def check_production(clang_cl: Path, plugin: Path, source: Path, policy: Path, work: Path) -> None:
    """The production path: the compiler loads the plugin."""
    plain = work / "plain.ll"
    protected = work / "protected.ll"

    # The compiler writes the IR next to the source, so the source is
    # copied into the work directory: the tree of the repository is not
    # written to by a test.
    local = work / source.name
    shutil.copyfile(source, local)

    reports: dict[str, str] = {}
    for output, with_plugin in ((plain, False), (protected, True)):
        command = [
            str(clang_cl),
            "/nologo",
            "/std:c++latest",
            "/O2",
            "/EHsc",
            "/GR-",
            "/MT",
            "/clang:-emit-llvm",
            "/clang:-S",
            str(local),
        ]
        if with_plugin:
            command.insert(-1, f"/clang:-fpass-plugin={plugin}")
        completed = run(command, environment_for(policy, work / f"coverage-{output.stem}"), work)
        check(
            completed.returncode == 0,
            f"the {'protected' if with_plugin else 'unprotected'} build failed:\n"
            f"{completed.stdout}\n{completed.stderr}",
        )
        reports[output.stem] = completed.stderr
        # The compiler writes the IR into the working directory, with the
        # name of the source.
        produced = work / local.with_suffix(".ll").name
        check(produced.is_file(), f"the compiler wrote no IR: {produced} is missing")
        shutil.move(str(produced), str(output))

    protected_ir = protected.read_text(encoding="utf-8", errors="replace")
    plain_ir = plain.read_text(encoding="utf-8", errors="replace")

    # The analysis ran: the extension points fire in the production path,
    # and the verdict is in the report and in the coverage. A test that
    # only exercised the `opt` path could pass for the wrong reason.
    check_verdicts(verdicts_of_report(reports["protected"]), "the report of the production path",
                   {"vm_leaf": ("virtualizable", "")})
    files = coverage_files(work / "coverage-protected")
    check(len(files) == 1, f"the coverage holds {len(files)} files and not one")
    coverage = verdicts_of_coverage(files[0])
    check("vm_leaf" in coverage, "the coverage of the production path has no verdict")
    if "vm_leaf" in coverage:
        check(
            coverage["vm_leaf"].get("verdict") == "virtualizable",
            "the coverage of the production path has the wrong verdict",
        )

    # The analysis adds nothing to the module: there is no bytecode, and no
    # interpreter.
    check("cs2.vm" not in protected_ir, "the analysis added a bytecode global")
    check("cs2.protect.table" not in protected_ir, "the analysis added a table")

    # The body of the selected function is the body of the unprotected
    # build: the analysis is read-only.
    check(
        body_of(protected_ir, "vm_leaf") == body_of(plain_ir, "vm_leaf"),
        "the analysis changed the body of the selected function",
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", type=Path, required=True)
    parser.add_argument("--clang-cl", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--production-policy", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)

    work = args.work
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    try:
        check_read_only(args.opt, args.plugin, args.fixture, args.policy, work)
        check_full_pipeline(args.opt, args.plugin, args.fixture, args.policy, work)
        check_production(args.clang_cl, args.plugin, args.source, args.production_policy, work)
    except Failure as error:
        print(f"vm_ir_test: {error}", file=sys.stderr)
        return 1

    print("vm_ir_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
