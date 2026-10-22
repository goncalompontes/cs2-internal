#!/usr/bin/env python3
"""The test of the coverage readers.

The coverage is the evidence the payload check counts, and it is written by
the plugin into a directory that is shared by everything that uses the same
policy. Two things can go wrong, and neither of them may crash a reader:

  * a file that is **malformed** -- a half-written file, a file of an older
    plugin, a garbage file in the directory. It has to be *reported* and
    ignored, not counted and not a traceback;
  * a file that belongs to **another tree** -- another worktree that shares
    the policy writes its coverage into the same directory. Counting it
    would count every rewrite twice, and the payload check would then
    report that the image has half the dispatches the pass wrote: a false
    "the compiler folded the dispatch away" on a perfectly good image.

The test drives the shared module (`protect_checks.py`) and the policy
checker, so the two readers of the coverage cannot drift apart.

Usage: coverage_test.py
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import shutil
import struct
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

# The readers are shared with the checkers, and the checkers put the
# directory of the tool on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_policy  # noqa: E402
import check_payload  # noqa: E402
from check_payload import check_cross_module_entries, check_direct_callees  # noqa: E402
from protect_checks import (  # noqa: E402
    Coverage,
    DispatchSite,
    Failure,
    PeImage,
    mix64,
    read_coverage,
)


class TestFailure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise TestFailure(what)


def digest_of(data: bytes) -> str:
    """The digest of a file, as the plugin spells it (upper case)."""
    return hashlib.sha256(data).hexdigest().upper()


def write_coverage(path: Path, policy_digest: str, source: Path, rewritten: int,
                   vm: str | None = None, layout: str | None = None,
                   cross: str | None = None, direct: str | None = None,
                   cross_rewritten: int = 0) -> None:
    """Writes a coverage file the way the plugin writes it.

    The `vm` line is the verdict of the virtualizer's analysis. It is
    optional: a coverage file of an older build has none, and a reader that
    does not know the section still reads the file. The `layout` line is the
    permutation of the module's function list, and it is optional in the same
    way.

    The `cross` and the `direct` lines are the cross-module routing: the
    state of every `cross_module` entry and the cross-TU callees that
    stayed direct. They are optional in the same way.
    """
    body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {source.name}\n"
        f"source-path {source.as_posix()}\n"
        f"source-sha256 {digest_of(source.read_bytes())}\n"
        "protect table=2 descriptor=3 multiplier=0x9e3779b97f4a7c15 "
        "xor=0xbf58476d1ce4e5b9 rotate=31 key=0x1 key=0x2 key=0x3\n"
        "entry matched cs2::aim::solve\n"
        f"fn ?solve rewritten={rewritten} skipped=0 cross={cross_rewritten}\n"
    )
    if vm is not None:
        body += vm
    if layout is not None:
        body += layout
    if cross is not None:
        body += cross
    if direct is not None:
        body += direct
    path.write_text(body + f"content-sha256 {digest_of(body.encode('utf-8'))}\n",
                    encoding="utf-8")


def write_string_coverage(path: Path, policy_digest: str, source: Path,
                          field: str = "sha256") -> None:
    """Writes a coverage file with the section of the string protection.

    The digests of the literals are salted with the seed of the build, and
    the coverage names the salt and carries its proof -- the salt itself is
    not in the file.

    The field is named by the caller: the current one is `digest=` (the
    digest is salted), and a coverage of an older build names it
    `sha256=`. Both are read.
    """
    body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {source.name}\n"
        f"source-path {source.as_posix()}\n"
        f"source-sha256 {digest_of(source.read_bytes())}\n"
        f"str-salt build-seed {field}=" + "A" * 64 + "\n"
        f"str protected len=7 {field}=" + "B" * 64 + " uses=1 functions=1\n"
        f"str refused len=11 {field}=" + "C" * 64 + " uses=0 functions=0 "
        "reason=escapes_to_initializer\n"
    )
    path.write_text(body + f"content-sha256 {digest_of(body.encode('utf-8'))}\n",
                    encoding="utf-8")


def build_tree(root: Path) -> tuple[Path, Path, Path, Path]:
    """The tree, the other tree, the policy and the coverage directory."""
    tree = root / "tree"
    (tree / "payload").mkdir(parents=True)
    source = tree / "payload" / "solver.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")

    other = root / "other-tree" / "payload" / "solver.cpp"
    other.parent.mkdir(parents=True)
    other.write_text("int main() { return 0; }\n", encoding="utf-8")

    policy = root / "policy.json"
    policy.write_text('{"protect": ["cs2::aim::solve"]}\n', encoding="utf-8")

    coverage = root / "coverage"
    coverage.mkdir()
    return tree, other, policy, coverage


def check_the_readers(root: Path) -> None:
    """The assertions of the test."""
    tree, other, policy, coverage = build_tree(root)
    policy_digest = digest_of(policy.read_bytes())

    # The coverage of this build.
    write_coverage(coverage / "0x1.cov", policy_digest, tree / "payload" / "solver.cpp", 7,
                   vm="vm ?solve verdict=virtualizable slots=42 frame=3072 bytecode=268 "
                      "instructions=57 calls=17\n",
                   layout="layout functions=42 moved=40 seed=0x0123456789abcdef\n",
                   cross="cross matched cs2::aim::DamageField::add "
                         "?add@DamageField@aim@cs2@@QEAA...\n"
                         "cross blocked:indirect_parameter cs2::aim::project_capsule "
                         "?project_capsule@aim@cs2@@YA?AUPolygon@12@XZ\n"
                         "cross unmatched cs2::aim::never_called\n",
                   direct="direct ?atan2f external_callee\n"
                          "direct ?project_capsule@aim@cs2@@YA?AUPolygon@12@XZ indirect_parameter\n",
                   cross_rewritten=3)
    # The coverage of another worktree that shares the policy. It has no
    # `vm` line, no `layout` line and no rotation in its `protect` line:
    # it is the coverage of an older build, and a reader has to read it.
    old_body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {other.name}\n"
        f"source-path {other.as_posix()}\n"
        f"source-sha256 {digest_of(other.read_bytes())}\n"
        "protect table=2 descriptor=3 multiplier=0x9e3779b97f4a7c15 "
        "xor=0xbf58476d1ce4e5b9 key=0x1 key=0x2 key=0x3\n"
        "entry matched cs2::aim::solve\n"
        "fn ?solve rewritten=3 skipped=0\n"
    )
    (coverage / "0x2.cov").write_text(
        old_body + f"content-sha256 {digest_of(old_body.encode('utf-8'))}\n", encoding="utf-8")
    # A file whose count is not a number.
    (coverage / "0x3.cov").write_text(
        f"policy-sha256 {policy_digest}\nsource-path "
        f"{(tree / 'payload' / 'solver.cpp').as_posix()}\nfn ?solve rewritten=abc\n",
        encoding="utf-8")
    # A file whose table size is not a number.
    (coverage / "0x4.cov").write_text(
        f"policy-sha256 {policy_digest}\nsource-path "
        f"{(tree / 'payload' / 'solver.cpp').as_posix()}\nprotect table=x\n",
        encoding="utf-8")
    # A broken file of another tree: it is not this build's business.
    (coverage / "0x6.cov").write_text(
        f"policy-sha256 {policy_digest}\nsource-path {other.as_posix()}\n"
        "fn ?solve rewritten=abc\n", encoding="utf-8")
    # A file that is not a coverage file at all.
    (coverage / "0x5.cov").write_bytes(b"MZ")

    read = read_coverage(coverage, policy, tree)

    # Only the coverage of this build is counted: the file of the other tree
    # would add its three rewrites to the seven of this one, and the payload
    # check would then report that the image has half the dispatches the
    # pass wrote.
    check(len(read.files) == 1, f"the reader counted {len(read.files)} coverage files, not 1")
    check(read.files[0].rewritten == 7, "the reader counted the wrong number of rewrites")
    check(
        any(name == "0x2.cov" for name, _, _ in read.skipped),
        "the coverage of the other tree was not skipped",
    )

    # The malformed files of this build are reported, not counted and not a
    # crash. The broken file of the other tree is only skipped.
    malformed = {name for name, _ in read.malformed}
    check("0x3.cov" in malformed, "the file with a count that is not a number was not reported")
    check("0x4.cov" in malformed, "the file with a table size that is not a number was not "
                                  "reported")
    check(
        "0x6.cov" not in malformed,
        "the broken file of the other tree was reported as this build's",
    )
    check(
        all(entry.path.name != "0x3.cov" for entry in read.files),
        "a malformed file was counted",
    )

    # The verdict of the virtualizer's analysis is in the coverage, and it
    # is read.
    verdict = read.files[0].vm.get("?solve")
    check(verdict is not None, "the reader did not read the verdict of the virtualizer")
    assert verdict is not None
    check(verdict.get("verdict") == "virtualizable", "the verdict is not the one that was written")
    check(verdict.get("slots") == "42", "the slot count is not the one that was written")
    check(verdict.get("bytecode") == "268", "the bytecode size is not the one that was written")

    # The cross-module routing is in the coverage, and it is read: the state
    # of every `cross_module` entry, the symbols they named, and the
    # cross-TU callees that stayed direct.
    states = read.files[0].cross_states()
    check(
        states.get("cs2::aim::DamageField::add") == "matched",
        f"the reader did not read the state of the cross_module entry: {states}",
    )
    check(
        states.get("cs2::aim::project_capsule") == "blocked:indirect_parameter",
        f"the reader did not read the blocked state: {states}",
    )
    check(
        states.get("cs2::aim::never_called") == "unmatched",
        f"the reader did not read the unmatched state: {states}",
    )
    symbols = read.files[0].cross_symbols()
    check(
        ("cs2::aim::DamageField::add", "?add@DamageField@aim@cs2@@QEAA...") in symbols,
        f"the reader did not read the symbol of the match: {symbols}",
    )
    direct = read.files[0].direct_callees()
    check(direct.get("?atan2f") == "external_callee",
          f"the reader did not read the direct callee: {direct}")
    check(direct.get("?project_capsule@aim@cs2@@YA?AUPolygon@12@XZ") == "indirect_parameter",
          f"the reader did not read the reason of the direct callee: {direct}")
    check(read.files[0].cross_rewritten == 3, "the reader did not read the cross rewrites")

    # A coverage file of an older build has no `vm` line: the reader still
    # reads it, and it is not malformed.
    old = Coverage(coverage / "0x2.cov")
    check(not old.malformed, f"a coverage file without a vm section is malformed: {old.malformed}")
    check(old.intact, "a coverage file without a vm section is not intact")
    check(old.vm == {}, "a coverage file without a vm section has verdicts")

    # The mixer of the build: the multiplier, the xor constant and the
    # rotation are per build, and the coverage names them.
    mixer = read.files[0].mixer()
    check(mixer.multiplier == 0x9E3779B97F4A7C15, "the reader did not read the multiplier")
    check(mixer.xor_constant == 0xBF58476D1CE4E5B9, "the reader did not read the xor constant")
    check(mixer.rotate == 31, "the reader did not read the rotation")

    # The layout: the permutation of the module's function list. The section
    # is new, and a coverage file of an older build has none.
    check(read.files[0].layout_functions == 42, "the reader did not read the number of the "
                                                  "definitions")
    check(read.files[0].layout_moved == 40, "the reader did not read the number of the moved "
                                            "definitions")
    check(old.layout_functions == 0, "a coverage file of an older build has a layout")
    check(old.mix_rotate == 0, "a coverage file of an older build has a rotation")

    # The same holds for the cross-module sections: a coverage file of an
    # older build has no `cross` line, no `direct` line and no `cross=` key,
    # and the reader still reads it.
    check(old.cross_states() == {}, "a coverage file without a cross section has states")
    check(old.cross_symbols() == [], "a coverage file without a cross section has symbols")
    check(old.direct_callees() == {}, "a coverage file without a direct section has callees")
    check(old.cross_rewritten == 0, "a coverage file without a cross section has rewrites")

    # The policy checker reads the same directory, and its pruning must not
    # choke on the malformed files either.
    removed = check_policy.prune_stale_coverage(coverage, policy)
    check(removed > 0, "the pruning removed nothing: the malformed files are still there")
    check(
        not (coverage / "0x3.cov").exists(),
        "the malformed file was not pruned",
    )
    check(
        (coverage / "0x1.cov").exists(),
        "the pruning removed the coverage of this build",
    )

    # The section of the string protection: the salt of the digests and the
    # digests themselves. A coverage of an older build has no salt line,
    # and its digests are unsalted -- both are read. The field that names
    # the digest is read under both names as well: the current one is
    # `digest=` (the digest is salted), the older one is `sha256=`.
    strings = root / "strings"
    strings.mkdir()
    write_string_coverage(strings / "0x8.cov", policy_digest, tree / "payload" / "solver.cpp",
                          field="digest")
    read = read_coverage(strings, policy, tree)
    check(len(read.files) == 1, "the reader did not read the coverage of the strings")
    if read.files:
        entry = read.files[0]
        check(entry.str_salt == "build-seed", "the reader did not read the name of the salt")
        check(entry.str_salt_proof == "A" * 64, "the reader did not read the proof of the salt")
        check(len(entry.strings) == 2, "the reader did not read the two literals")
        if len(entry.strings) == 2:
            check(entry.strings[0].state == "protected", "the reader did not read the verdict")
            check(entry.strings[0].length == 7, "the reader did not read the length")
            check(entry.strings[0].digest == "B" * 64, "the reader did not read the digest")
            check(entry.strings[1].reason == "escapes_to_initializer",
                  "the reader did not read the refusal reason")
    older = strings / "0x9.cov"
    body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {(tree / 'payload' / 'solver.cpp').name}\n"
        f"source-path {(tree / 'payload' / 'solver.cpp').as_posix()}\n"
        f"source-sha256 {digest_of((tree / 'payload' / 'solver.cpp').read_bytes())}\n"
        "str protected len=7 sha256=" + "D" * 64 + " uses=1 functions=1\n"
    )
    older.write_text(body + f"content-sha256 {digest_of(body.encode('utf-8'))}\n",
                     encoding="utf-8")
    read = read_coverage(strings, policy, tree)
    older_entry = [entry for entry in read.files if entry.path.name == "0x9.cov"]
    check(len(older_entry) == 1, "the reader did not read the coverage of an older build")
    if older_entry:
        check(older_entry[0].str_salt == "", "an older coverage claims a salt")
        check(len(older_entry[0].strings) == 1, "the reader did not read the older literal")
        if older_entry[0].strings:
            check(older_entry[0].strings[0].digest == "D" * 64,
                  "the reader did not read the digest of the older coverage")


def check_the_image_reader() -> None:
    """A truncated image is a reported failure, not a crash."""
    for data in (b"", b"MZ", b"MZ" + bytes(0x3C)):
        try:
            PeImage(data)
        except Failure:
            continue
        raise TestFailure(f"the reader accepted {data!r} as a PE image")


#: The image base of the synthetic image the table test builds.
IMAGE_BASE = 0x180000000

#: The RVA of the `.text` section of the synthetic image.
TEXT_RVA = 0x1000

#: The RVA of the `.rdata` section of the synthetic image.
RDATA_RVA = 0x2000


def synthetic_image(rdata: bytes) -> bytes:
    """A PE image with a `.text` section and a `.rdata` section.

    The image is the smallest one the reader of the check accepts: the
    tests of the table lookup need a `.text` (the code addresses of the
    table have to be in it) and a `.rdata` (the table and its descriptor).
    """
    text = bytes(0x100)
    headers = bytearray(0x200)
    headers[0:2] = b"MZ"
    struct.pack_into("<I", headers, 0x3C, 0x80)
    headers[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<H", headers, coff, 0x8664)        # the machine
    struct.pack_into("<H", headers, coff + 2, 2)         # the number of sections
    struct.pack_into("<H", headers, coff + 16, 0xF0)     # the size of the optional header
    optional = coff + 20
    struct.pack_into("<H", headers, optional, 0x20B)     # PE32+
    struct.pack_into("<Q", headers, optional + 24, IMAGE_BASE)
    struct.pack_into("<I", headers, optional + 108, 16)  # the number of the directories
    table = optional + 0xF0
    sections = ((".text", TEXT_RVA, len(text), len(headers)),
                (".rdata", RDATA_RVA, len(rdata), len(headers) + len(text)))
    for index, (name, rva, size, offset) in enumerate(sections):
        header = table + index * 40
        headers[header:header + len(name)] = name.encode("ascii")
        struct.pack_into("<IIII", headers, header + 8, size, rva, size, offset)
    body = bytearray(headers)
    body += text
    body += rdata
    return bytes(body)


def check_the_table_lookup(root: Path) -> None:
    """The table of a module is found, and it is the module's **own** table.

    The check decodes the descriptor of a dispatch in the table of the
    module, and it finds the table by its **content**: a run of code
    addresses in the read-only data whose descriptor follows it.

    Two things have to hold:

    * the run may be **longer** than the table. The linker may put two
      tables of the same size next to each other, or a global that holds a
      code address right after the table. A scan that only accepts a run of
      exactly `table_size` addresses does not find the table then, and the
      check fails with "the dispatch does not decode to a callee of the
      image" -- a baffling message for a pure layout change;
    * the table the check reads is the **module's own** table. The content
      of a run of code addresses does not say which module it belongs to,
      so the link between a dispatch and its table comes from the register
      the dispatch calls through (the `leaq` that loaded it). The content
      scan is the **fallback**, for the sites where that tracking failed.
    """
    # The coverage entry of the module: the table size, the descriptor size
    # and the key the call site uses.
    tree = root / "table-tree"
    (tree / "payload").mkdir(parents=True)
    source = tree / "payload" / "solver.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")
    policy = root / "table-policy.json"
    policy.write_text('{"protect": ["cs2::aim::solve"]}\n', encoding="utf-8")
    policy_digest = digest_of(policy.read_bytes())
    coverage = root / "table-coverage"
    coverage.mkdir()
    write_coverage(coverage / "0x1.cov", policy_digest, source, 7)
    entry = Coverage(coverage / "0x1.cov")
    check(entry.intact, "the coverage of the table test is not intact")

    # The key the call site mixes: the xor of the three parts of the key.
    mixed = mix64(0x1 ^ 0x2 ^ 0x3)

    def code(offset: int) -> int:
        """A code address of the `.text` section."""
        return IMAGE_BASE + TEXT_RVA + offset

    #: The table of the module, and the table of another module.
    table = [code(0x10), code(0x20)]
    other = [code(0x30), code(0x40)]
    #: The descriptor of the module: the encrypted indices of its call sites.
    descriptor = [mixed ^ 0, mixed ^ 1, mixed ^ 0]

    def rdata_bytes(runs: list[list[int]]) -> bytes:
        """The read-only data of the given runs of 64-bit values."""
        out = bytearray()
        for run in runs:
            for value in run:
                out += struct.pack("<Q", value)
        return bytes(out)

    # The plain layout: the run is exactly the table, and the descriptor
    # follows it.
    plain = PeImage(synthetic_image(rdata_bytes([table, descriptor])))
    found = check_payload.tables_of(plain, entry, RDATA_RVA + 8 * len(table))
    check(found == [RDATA_RVA],
          f"the table of the plain layout was not found: {found}")

    # Two tables of the same size next to each other: the run is twice as
    # long as the table. The scan has to find the module's own table -- the
    # second one -- and not give up.
    adjacent = PeImage(synthetic_image(rdata_bytes([other, table, descriptor])))
    slot = RDATA_RVA + 8 * (len(other) + len(table))
    found = check_payload.tables_of(adjacent, entry, slot)
    check(RDATA_RVA + 8 * len(other) in found,
          f"the table of the adjacent layout was not found: {found}")

    # And the dispatch reads **its own** table, not the one that happens to
    # be next to it: the tracked table is the module's.
    site = DispatchSite(0, RDATA_RVA + 8 * len(other), [slot])
    resolved = check_payload.resolve_site(adjacent, site, entry)
    check(resolved == table[0],
          f"the dispatch resolved to {resolved}, not the table of the module ({table[0]})")

    # A global that holds a code address right after the table: the run is
    # one address longer than the table.
    trailing = PeImage(synthetic_image(rdata_bytes([table + [code(0x50)], descriptor])))
    found = check_payload.tables_of(trailing, entry, RDATA_RVA + 8 * (len(table) + 1))
    check(RDATA_RVA in found,
          f"the table before the code-address global was not found: {found}")


def run_check_policy(policy: Path, coverage: Path) -> int:
    """Runs the policy checker and returns its exit code."""
    with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
        return check_policy.main(["--policy", str(policy), "--coverage", str(coverage)])


def expect_failure(action: Callable[..., object], *args: object) -> None:
    """The action has to fail: the rule it holds is the one under test."""
    try:
        action(*args)
    except Failure:
        return
    raise TestFailure(f"{action.__name__} did not fail")


def expect_success(action: Callable[..., object], *args: object) -> None:
    """The action must not fail."""
    try:
        action(*args)
    except Failure as error:
        raise TestFailure(f"{action.__name__} failed: {error}")


def check_the_payload_rules(root: Path) -> None:
    """The two payload-level rules of the cross-module routing.

    They are the rules the acceptance of the task rests on, and they are
    pure functions of the coverage and of the payload's own symbol set:

    * a `cross_module` entry that names a symbol the payload does not
      define is **stale** -- the function is gone, and the entry protects
      nothing;
    * a protected function may not call the payload's own code **directly**.
      The `sret` edges are the only exception: this task does not narrow
      the `sret` rule.

    The test drives them with the coverage a plugin would write, so that a
    rule that rots (a rule that is neutered, or one that stops reading the
    coverage) fails the suite instead of passing silently.
    """
    tree = root / "rules-tree"
    (tree / "payload").mkdir(parents=True)
    source = tree / "payload" / "solver.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")
    # A second module: the coverage of one module is the evidence of that
    # module, and the rules have to decide per (module, symbol).
    other = tree / "payload" / "field.cpp"
    other.write_text("int main() { return 0; }\n", encoding="utf-8")

    policy = root / "rules-policy.json"
    policy.write_text(
        '{"protect": ["cs2::aim::solve"], "cross_module": ["cs2::aim::own"]}\n',
        encoding="utf-8",
    )
    policy_digest = digest_of(policy.read_bytes())

    coverage = root / "rules-coverage"
    coverage.mkdir()

    #: The symbols the payload's own libraries define.
    payload = {"?own@aim@cs2@@YAXXZ", "?sret@aim@cs2@@YAXXZ"}

    def with_cross(name: str, symbol: str) -> list[Coverage]:
        """The coverage of one module, with a `cross_module` match."""
        target = coverage / name
        write_coverage(target, policy_digest, source, 7,
                       cross=f"cross matched cs2::aim::own {symbol}\n")
        return [Coverage(target)]

    def with_direct(name: str, symbol: str, reason: str,
                    module: Path = source) -> list[Coverage]:
        """The coverage of one module, with a callee that stayed direct."""
        target = coverage / name
        write_coverage(target, policy_digest, module, 7,
                       direct=f"direct {symbol} {reason}\n")
        return [Coverage(target)]

    # A `cross_module` entry that names a symbol the payload does not
    # define is stale: the build has to fail.
    expect_failure(check_cross_module_entries,
                   with_cross("0x1.cov", "?gone@aim@cs2@@YAXXZ"), payload)
    # The entry that names a symbol the payload does define passes.
    expect_success(check_cross_module_entries,
                   with_cross("0x2.cov", "?own@aim@cs2@@YAXXZ"), payload)

    # A protected function that calls the payload's own code directly is a
    # failure -- the callee is the payload's own code.
    expect_failure(check_direct_callees,
                   with_direct("0x3.cov", "?own@aim@cs2@@YAXXZ", "external_callee"), payload)
    # The `sret` edges are excepted: the rule is not narrowed by this task.
    expect_success(check_direct_callees,
                   with_direct("0x4.cov", "?sret@aim@cs2@@YAXXZ", "indirect_parameter"), payload)
    # A callee the payload does not own is an import or a CRT symbol: it is
    # left alone, and the check says how many it left alone.
    excepted, left_alone = check_direct_callees(
        with_direct("0x5.cov", "?atan2f", "external_callee"), payload)
    check(excepted == 0, f"the check excepted {excepted} callees, not 0")
    check(left_alone == 1, f"the check left {left_alone} callees alone, not 1")

    # The advice names the reason: a callee that the routing cannot route
    # is not fixed by adding it to the `cross_module` list.
    try:
        check_direct_callees(
            with_direct("0x6.cov", "?own@aim@cs2@@YAXXZ", "variadic_callee"), payload)
    except Failure as error:
        message = str(error)
        check("variadic_callee" in message,
              f"the advice does not name the reason of the refusal: {message}")
        check("cannot bypass" in message,
              f"the advice does not say that the routing cannot bypass the reason: {message}")
    else:
        raise TestFailure("a variadic callee did not fail the check")

    # The exception is decided per (module, symbol): a symbol that one
    # module excepts and another module refuses is a failure, whatever the
    # order the coverage files are read in.
    both = (with_direct("0x7.cov", "?own@aim@cs2@@YAXXZ", "indirect_parameter")
            + with_direct("0x8.cov", "?own@aim@cs2@@YAXXZ", "external_callee", other))
    expect_failure(check_direct_callees, both, payload)
    expect_failure(check_direct_callees, list(reversed(both)), payload)


def check_the_cross_module_rule(root: Path) -> None:
    """A `cross_module` entry that did not route every call fails the build.

    The rule is the one the `protect` entries follow: an entry that protects
    nothing is a build failure. The priority is the opposite: a
    `cross_module` entry has to be routed *everywhere* it is called.
    """
    tree = root / "cross-tree"
    (tree / "payload").mkdir(parents=True)
    source = tree / "payload" / "solver.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")

    policy = root / "cross-policy.json"
    policy.write_text(
        '{"protect": ["cs2::aim::solve"], "cross_module": ["cs2::aim::DamageField::add"]}\n',
        encoding="utf-8",
    )
    policy_digest = digest_of(policy.read_bytes())

    coverage = root / "cross-coverage"
    coverage.mkdir()
    target = coverage / "0x1.cov"

    # The entry was seen, but the call to it could not be routed: the build
    # has to fail, and the reason has to be named.
    write_coverage(target, policy_digest, source, 7,
                   cross="cross blocked:indirect_parameter cs2::aim::DamageField::add "
                         "?add@DamageField@aim@cs2@@QEAA...\n")
    status = run_check_policy(policy, coverage)
    check(status == 1, f"a blocked cross_module entry did not fail the build (status {status})")

    # The entry matched nothing at all: the build has to fail too.
    write_coverage(target, policy_digest, source, 7,
                   cross="cross unmatched cs2::aim::DamageField::add\n")
    status = run_check_policy(policy, coverage)
    check(status == 1, f"an unmatched cross_module entry did not fail the build (status {status})")

    # The entry routed every call: the build passes.
    write_coverage(target, policy_digest, source, 7,
                   cross="cross matched cs2::aim::DamageField::add "
                         "?add@DamageField@aim@cs2@@QEAA...\n")
    status = run_check_policy(policy, coverage)
    check(status == 0, f"a routed cross_module entry failed the build (status {status})")


def main() -> int:
    try:
        check_the_image_reader()
        root = Path(tempfile.mkdtemp(prefix="cs2-protect-coverage-test-"))
        try:
            check_the_readers(root)
            check_the_cross_module_rule(root)
            check_the_payload_rules(root)
            check_the_table_lookup(root)
        finally:
            shutil.rmtree(root, ignore_errors=True)
    except TestFailure as error:
        print(f"coverage_test: {error}", file=sys.stderr)
        return 1

    print("coverage_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
