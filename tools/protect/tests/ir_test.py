#!/usr/bin/env python3
"""The IR-level test of the protection pass.

The pass rewrites the calls of a selected function into indirect dispatches
through an encrypted descriptor. A mistake shows up as a *wrong call target*,
not as a crash, so the test asserts on the shape of the rewritten IR:

  * the direct call to the target is gone;
  * the direct call to the **cross-TU** callee the policy names is gone too
    (the declaration goes into the caller's own table as a relocatable
    pointer to the external symbol);
  * an indirect call is present;
  * the caller's function type is unchanged (the ABI is preserved by
    construction, and the test proves it);
  * the table global exists and holds the target pointer and the external
    symbol;
  * the descriptor global exists and its bytes are not the plain index;
  * decoding the descriptor really yields the index of the callee (the test
    recomputes the key from the three constants of the image and the mixer);
  * a callee with an `sret` parameter is routed through a **thunk**, and the
    thunk carries the `sret` attribute of the call and the hidden pointer out
    of the cell of the scratch that the `sret` attribute names (in this
    fixture, the first one: the callees are free functions);
  * the calls that must be skipped -- a variadic callee, a `musttail` call --
    are untouched.

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
    Mixer,
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


def body_of_thunk(ir: str, thunk: str) -> str:
    """The body of one thunk of the IR.

    The thunk is the adapter of a struct-returning call: it takes the
    scratch, loads the arguments out of it and calls the callee with the
    `sret` attribute.
    """
    marker = f"define internal i64 @{thunk}("
    start = ir.find(marker)
    if start < 0:
        raise Failure(f"the thunk '{thunk}' is not in the output IR")
    body = []
    for line in ir[start:].splitlines():
        body.append(line)
        if line == "}":
            break
    return "\n".join(body)


def defines_of(ir: str) -> list[str]:
    """The names of the functions the IR defines, in the order of the list.

    The order is the one of the module's function list: the pass permutes
    it, and the object writer emits the functions in that order.
    """
    return re.findall(r"^define\s+.*?@([A-Za-z0-9_.$]+)\(", ir, re.MULTILINE)


def rotation_of(caller: str) -> int | None:
    """The rotation of the mixer, as the decode emits it.

    The decode is a rotation: a `shl` by the rotation and a `lshr` by 64
    minus the rotation. The rotation is the one value of the mixer that is
    in the instruction stream, and it is what makes the `.text` of two
    builds with two seeds differ.
    """
    amounts = [int(value) for value in re.findall(r"\b(?:shl|lshr) i64 %\w+, (\d+)\b", caller)]
    for amount in amounts:
        if 1 <= amount <= 63 and (64 - amount) in amounts:
            return amount
    return None


def run_opt(opt: Path, plugin: Path, fixture: Path, policy: Path, work: Path,
            seed: str | None = None, name: str = "calls") -> str:
    """Runs the pass over the fixture and returns the output IR."""
    work.mkdir(parents=True, exist_ok=True)
    output = work / f"{name}.out.ll"
    environment = dict(os.environ)
    environment["CS2_PROTECT_CONFIG"] = str(policy)
    environment["CS2_PROTECT_COVERAGE"] = str(work / "coverage")
    # The seed of the build: the environment wins over the policy.
    environment.pop("CS2_PROTECT_SEED", None)
    if seed is not None:
        environment["CS2_PROTECT_SEED"] = seed
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
        # The same seed gives the same layout, and another seed gives another
        # one: the permutation is seeded.
        same = run_opt(args.opt, args.plugin, args.fixture, args.policy, args.work,
                       seed="0x0123456789ABCDEF", name="same")
        other = run_opt(args.opt, args.plugin, args.fixture, args.policy, args.work,
                        seed="0xFEDCBA9876543210", name="other")
    except Failure as error:
        print(f"ir_test: {error}", file=sys.stderr)
        return 1

    try:
        check_ir(ir)
        check_layout(ir, same, other)
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

    # The direct calls are gone, and indirect calls took their place: the
    # same-module callee and the cross-TU one the policy names.
    check("call i32 @target(" not in caller, "the direct call to the target is still there")
    check(
        "call i32 @external_target(" not in caller,
        "the direct call to the external callee is still there",
    )
    check(
        len(re.findall(r"call i32 %\d+\(i32 %x\)", caller)) == 2,
        "there are not two indirect calls in the caller",
    )

    # The function type of the caller is unchanged: the ABI is preserved by
    # construction, and the test proves it.
    check(
        "define i32 @caller(i32 %x)" in ir,
        "the function type of the caller changed",
    )

    # The table holds the target, the external symbol and the two `sret`
    # callees, and the descriptor holds an encrypted index.
    table = re.search(
        r"@cs2\.protect\.table = private unnamed_addr constant \[(\d+) x ptr\] \[([^\]]*)\]", ir
    )
    check(table is not None, "the table global is missing")
    assert table is not None
    table_size = int(table.group(1))
    check(table_size >= 4, "the table does not hold the four callees")
    check("@target" in table.group(2), "the table does not hold the target")
    check("@external_target" in table.group(2), "the table does not hold the external symbol")
    check("@sret_target" in table.group(2), "the table does not hold the sret callee")
    check(
        "@external_sret_target" in table.group(2),
        "the table does not hold the external sret callee",
    )

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

    # The mixer is **per build**: its multiplier and its xor constant are in
    # the globals, and its rotation is the one the decode emits. The test
    # reads all three out of the IR, so it does not have to know them: the
    # decode has to be the one the pass encrypted the descriptor with.
    constants = {}
    for name in ("mixmul", "mixxor"):
        found = re.search(rf"@cs2\.protect\.{name} = private unnamed_addr constant i64 (-?\d+)", ir)
        check(found is not None, f"the constant '{name}' is missing")
        assert found is not None
        constants[name] = int(found.group(1)) & MASK64
    rotation = rotation_of(caller)
    check(rotation is not None, "the decode does not rotate: there is no rotation in the IR")
    assert rotation is not None
    check(1 <= rotation <= 63, f"the rotation is {rotation}, and it is not in [1, 63]")

    mixer = Mixer(constants["mixmul"], constants["mixxor"], rotation)
    key = mix64(parts[0] ^ parts[1] ^ parts[2], mixer)

    entries = re.findall(r"ptr (@[A-Za-z0-9_.$]+)", table.group(2))
    decoded: set[str] = set()
    for value in values:
        index = (value & MASK64) ^ key
        check(index < len(entries), "the decoded index is outside the table")
        decoded.add(entries[index])
    check(
        decoded == {"@target", "@external_target", "@sret_target", "@external_sret_target"},
        f"the decoded indices point at {sorted(decoded)} and not at the four callees",
    )

    # The calls that must be skipped are untouched.
    check(
        "call i32 (i32, ...) @variadic_target(i32 %x, i32 %x)" in caller,
        "the variadic call was rewritten",
    )
    check(
        "musttail call i32 @musttail_target(i32 %x)" in caller,
        "the musttail call was rewritten",
    )

    # The decode is inline: the caller makes exactly the calls it made
    # before, and none of them is a call to a shared helper (a helper would
    # add a stack frame to every call site).
    calls = re.findall(r"\bcall\b", caller)
    check(len(calls) == 6, f"the caller makes {len(calls)} calls and not six")
    symbols = set(re.findall(r"@[A-Za-z0-9_.$]+\(", "\n".join(caller.splitlines()[1:])))
    check(
        symbols == {"@variadic_target(", "@musttail_target("} | {
            f"@{name}(" for name in thunks_of(ir)
        },
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
        len(volatile_loads) == 12,
        f"the two dispatches have {len(volatile_loads)} volatile loads and not twelve",
    )

    # The constants of the mixer live in globals and not in the instruction
    # stream. The X86 backend of this LLVM mis-materialises a 64-bit
    # immediate in this shape (it loses a bit of it), and a wrong constant
    # is a wrong call target -- not a crash. A constant in a global is
    # written by the object writer, so it reaches the image bit for bit.
    for name, value in (("mixmul", constants["mixmul"]), ("mixxor", constants["mixxor"])):
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

    check_thunks(ir)


def thunks_of(ir: str) -> list[str]:
    """The names of the thunks the pass emitted."""
    return re.findall(r"^define internal i64 @(cs2\.protect\.thunk\.\d+)\(", ir, re.MULTILINE)


def check_thunks(ir: str) -> None:
    """The assertions of the thunk.

    The thunk is the adapter of a struct-returning call: it takes the
    scratch, loads the arguments out of it and calls the callee **with the
    `sret` attribute**. The hidden pointer is the cell of the scratch that
    the `sret` attribute names -- the first one in this fixture, where the
    callees are free functions -- and the thunk passes it unchanged.
    """
    thunks = thunks_of(ir)
    check(len(thunks) == 2, f"the pass emitted {len(thunks)} thunks and not two")
    for thunk in thunks:
        body = body_of_thunk(ir, thunk)
        # The call of the thunk: the callee is a value (the dispatch), and
        # the call carries the `sret` attribute.
        call = re.search(r"call void %\d+\(ptr [^()]*sret\([^()]*\) (%\d+),", body)
        check(
            call is not None,
            f"the call of the thunk '{thunk}' does not carry the sret attribute",
        )
        # The hidden pointer: the cell of the scratch that the `sret`
        # attribute names. In this fixture it is the **first** one: the
        # cell is the scratch itself (offset zero), and the thunk's
        # parameter is `%0`.
        if call is not None:
            argument = re.escape(call.group(1))
            check(
                re.search(rf"{argument} = load ptr, ptr %0,", body) is not None,
                f"the thunk '{thunk}' does not take the hidden pointer out of the cell of the "
                "scratch that the sret attribute names",
            )
        # The thunk returns zero: the callee returns nothing.
        check(
            re.search(r"ret i64 0", body) is not None,
            f"the thunk '{thunk}' does not return zero",
        )
        # The decode is in the thunk: the callee is reached through the
        # caller's own table.
        check(
            "@cs2.protect.table" in body,
            f"the thunk '{thunk}' does not reach the callee through the table",
        )
    # The caller: the `sret` calls go through the thunks, and the scratch is
    # in the caller's frame.
    caller = body_of(ir, "caller")
    for thunk in thunks:
        check(
            f"call i64 @{thunk}(" in caller,
            f"the caller does not call the thunk '{thunk}'",
        )
    check(
        "alloca { ptr, i32 }" in caller,
        "the scratch of the thunk is not in the caller's frame",
    )


def check_layout(ir: str, same: str, other: str) -> None:
    """The assertions of the layout permutation.

    The pass permutes the module's function list with a permutation derived
    from the module's seed: the object writer emits the functions in that
    order, and the linker keeps the order of an object's sections, so the
    permutation is what moves the code in the image.
    """
    defines = defines_of(ir)
    check(len(defines) >= 2, "the fixture has fewer than two definitions")
    check(len(set(defines)) == len(defines), "the module defines a function twice")

    # The same seed gives the same order, and another seed gives another
    # one.
    check(defines_of(same) == defines, "the same seed gives another order")
    check(defines_of(other) != defines, "another seed gives the same order")

    # The order is a permutation of the original one: no function is lost
    # and none is duplicated. The fixture's order is the one of the file,
    # and the thunks the pass emitted are part of the module.
    original = ["target", "variadic_target", "musttail_target", "sret_target", "caller"]
    expected = sorted(original + thunks_of(ir))
    check(sorted(defines) == expected, "the permutation lost or duplicated a function")
    check(defines != original, "the function list is in the original order: it was not permuted")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
