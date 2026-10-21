#!/usr/bin/env python3
"""The IR-level test of the protection pass.

The pass rewrites the calls of a selected function into indirect dispatches
through an encrypted descriptor. A mistake shows up as a *wrong call target*,
not as a crash, so the test asserts on the shape of the rewritten IR:

  * the direct call to the target is gone;
  * an indirect call is present;
  * the caller's function type is unchanged (the ABI is preserved by
    construction, and the test proves it);
  * the table global exists and holds the target pointer;
  * the descriptor global exists and its bytes are not the plain index;
  * decoding the descriptor really yields the index of the target (the test
    recomputes the key from the three constants of the image and the mixer);
  * the calls that must be skipped -- a variadic callee, a callee with an
    `sret` parameter, a `musttail` call -- are untouched.

Usage: ir_test.py --opt FILE --plugin FILE --fixture FILE --policy FILE --work DIR
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# The cipher is shared with the payload check: a copy of the mixer in two
# places is a mixer that can drift, and the test would decode the descriptor
# with a different mixer than the pass encrypted it with.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from protect_checks import (  # noqa: E402
    MASK64,
    MIX_MULTIPLIER,
    MIX_XOR,
    mix64,
    signed64,
)


class Failure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise Failure(what)


def body_of(ir: str, function: str) -> str:
    """The body of one function of the IR.

    The body ends at the closing brace of the function: the metadata of the
    module carries the strings of the pass, and they are not part of the
    function.
    """
    marker = f"define i32 @{function}(i32 %x)"
    start = ir.find(marker)
    if start < 0:
        raise Failure(f"the function '{function}' is not in the output IR")
    body = []
    for line in ir[start:].splitlines():
        body.append(line)
        if line == "}":
            break
    return "\n".join(body)


def run_opt(opt: Path, plugin: Path, fixture: Path, policy: Path, work: Path) -> str:
    """Runs the pass over the fixture and returns the output IR."""
    work.mkdir(parents=True, exist_ok=True)
    output = work / "calls.out.ll"
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(work / "coverage")
    command = [
        str(opt),
        f"-load-pass-plugin={plugin}",
        "-passes=cs2-protect",
        "-S",
        str(fixture),
        "-o",
        str(output),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, env=environment)
    if completed.returncode != 0:
        raise Failure(f"opt failed:\n{completed.stderr}")
    return output.read_text(encoding="utf-8", errors="replace")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        ir = run_opt(args.opt, args.plugin, args.fixture, args.policy, args.work)
    except Failure as error:
        print(f"ir_test: {error}", file=sys.stderr)
        return 1

    try:
        check_ir(ir)
    except Failure as error:
        print(f"ir_test: {error}", file=sys.stderr)
        print("--- the output IR ---", file=sys.stderr)
        print(ir, file=sys.stderr)
        return 1

    print("ir_test: ok")
    return 0


def check_ir(ir: str) -> None:
    """The assertions of the IR test."""
    caller = body_of(ir, "caller")

    # The direct call is gone, and an indirect call took its place.
    check("call i32 @target(" not in caller, "the direct call to the target is still there")
    check(
        re.search(r"call i32 %\d+\(i32 %x\)", caller) is not None,
        "there is no indirect call in the caller",
    )

    # The function type of the caller is unchanged: the ABI is preserved by
    # construction, and the test proves it.
    check(
        "define i32 @caller(i32 %x)" in ir,
        "the function type of the caller changed",
    )

    # The table holds the target, and the descriptor holds an encrypted index.
    table = re.search(
        r"@cs2\.protect\.table = private unnamed_addr constant \[(\d+) x ptr\] \[([^\]]*)\]", ir
    )
    check(table is not None, "the table global is missing")
    assert table is not None
    table_size = int(table.group(1))
    check(table_size >= 1, "the table is empty")
    check("@target" in table.group(2), "the table does not hold the target")

    descriptor = re.search(
        r"@cs2\.protect\.desc = private unnamed_addr constant \[(\d+) x i64\] \[([^\]]*)\]", ir
    )
    check(descriptor is not None, "the descriptor global is missing")
    assert descriptor is not None
    check(int(descriptor.group(1)) >= 1, "the descriptor is empty")

    values = [int(value) for value in re.findall(r"i64 (-?\d+)", descriptor.group(2))]
    check(len(values) == int(descriptor.group(1)), "the descriptor does not match its size")
    check(
        all(value != 0 for value in values),
        "the descriptor holds the plain index: it is not encrypted",
    )

    # The key is split into three constants; the test rebuilds it and decodes
    # the descriptor, so a wrong index is caught even when the shape is right.
    parts = []
    for name in ("key0", "key1", "key2"):
        found = re.search(rf"@cs2\.protect\.{name} = private unnamed_addr constant i64 (-?\d+)", ir)
        check(found is not None, f"the constant '{name}' is missing")
        assert found is not None
        parts.append(int(found.group(1)) & MASK64)
    check(len(set(parts)) == 3, "the key is not split into three different constants")
    key = mix64(parts[0] ^ parts[1] ^ parts[2])

    entries = re.findall(r"ptr (@\S+)", table.group(2))
    for value in values:
        index = (value & MASK64) ^ key
        check(index < len(entries), "the decoded index is outside the table")
        check(
            entries[index] == "@target",
            f"the decoded index points at '{entries[index]}' and not at the target",
        )

    # The calls that must be skipped are untouched.
    check(
        "call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)" in caller,
        "the variadic call was rewritten",
    )
    check(
        "call void @sret_target(ptr sret({ i64, i64, i64 }) %out, i32 %x)" in caller,
        "the call with an sret parameter was rewritten",
    )
    check(
        "musttail call i32 @musttail_target(i32 %x)" in caller,
        "the musttail call was rewritten",
    )

    # The decode is inline: the caller makes exactly the four calls it made
    # before, and none of them is a call to a shared helper (a helper would
    # add a stack frame to every call site).
    calls = re.findall(r"\bcall\b", caller)
    check(len(calls) == 4, f"the caller makes {len(calls)} calls and not four")
    symbols = set(re.findall(r"@[A-Za-z0-9_.$]+\(", "\n".join(caller.splitlines()[1:])))
    check(
        symbols == {"@variadic_target(", "@sret_target(", "@musttail_target("},
        f"the caller calls an unexpected symbol: {sorted(symbols)}",
    )

    # The decode reads the key, the constants of the mixer and the
    # descriptor with *volatile* loads, and that is load-bearing: the key is
    # a compile-time key, so the whole dispatch is a compile-time constant,
    # and a compiler that folds it leaves a direct call behind -- the
    # protection is void and the program still behaves the same. The
    # differential test checks the machine code; this test checks that the
    # barrier is still there.
    volatile_loads = re.findall(r"load volatile i64", caller)
    check(
        len(volatile_loads) == 6,
        f"the decode has {len(volatile_loads)} volatile loads and not six",
    )

    # The constants of the mixer live in globals and not in the instruction
    # stream. The X86 backend of this LLVM mis-materialises a 64-bit
    # immediate in this shape (it loses a bit of it), and a wrong constant
    # is a wrong call target -- not a crash. A constant in a global is
    # written by the object writer, so it reaches the image bit for bit.
    for name, value in (("mixmul", MIX_MULTIPLIER), ("mixxor", MIX_XOR)):
        found = re.search(rf"@cs2\.protect\.{name} = private unnamed_addr constant i64 (-?\d+)", ir)
        check(found is not None, f"the constant '{name}' is missing")
        assert found is not None
        check(
            int(found.group(1)) & MASK64 == value,
            f"the constant '{name}' does not hold the value of the mixer",
        )
        signed = signed64(value)
        check(
            re.search(rf"\b{signed}\b", caller) is None,
            f"the constant '{name}' is an immediate of the caller: the backend may "
            "materialise it wrongly",
        )

    # The GEP into the table is not `inbounds`: the index is a value the
    # image computes at run time, and an out-of-range index would then be
    # poison by the letter of the reference -- and the call through it would
    # be undefined behaviour instead of a wrong address.
    check(
        "getelementptr inbounds ptr, ptr @cs2.protect.table" not in caller,
        "the GEP into the table promises `inbounds`",
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
