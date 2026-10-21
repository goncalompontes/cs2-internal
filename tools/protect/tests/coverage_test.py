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

import hashlib
import shutil
import sys
import tempfile
from pathlib import Path

# The readers are shared with the checkers, and the checkers put the
# directory of the tool on the path.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_policy  # noqa: E402
from protect_checks import Failure, PeImage, read_coverage  # noqa: E402


class TestFailure(Exception):
    """A check that did not hold."""


def check(condition: bool, what: str) -> None:
    """Fails the test with a message that says what did not hold."""
    if not condition:
        raise TestFailure(what)


def digest_of(data: bytes) -> str:
    """The digest of a file, as the plugin spells it (upper case)."""
    return hashlib.sha256(data).hexdigest().upper()


def write_coverage(path: Path, policy_digest: str, source: Path, rewritten: int) -> None:
    """Writes a coverage file the way the plugin writes it."""
    body = (
        f"policy-sha256 {policy_digest}\n"
        f"module {source.name}\n"
        f"source-path {source.as_posix()}\n"
        f"source-sha256 {digest_of(source.read_bytes())}\n"
        "protect table=2 descriptor=3 multiplier=0x9e3779b97f4a7c15 "
        "xor=0xbf58476d1ce4e5b9 key=0x1 key=0x2 key=0x3\n"
        "entry matched cs2::aim::solve\n"
        f"fn ?solve rewritten={rewritten} skipped=0\n"
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
    write_coverage(coverage / "0x1.cov", policy_digest, tree / "payload" / "solver.cpp", 7)
    # The coverage of another worktree that shares the policy.
    write_coverage(coverage / "0x2.cov", policy_digest, other, 3)
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


def check_the_image_reader() -> None:
    """A truncated image is a reported failure, not a crash."""
    for data in (b"", b"MZ", b"MZ" + bytes(0x3C)):
        try:
            PeImage(data)
        except Failure:
            continue
        raise TestFailure(f"the reader accepted {data!r} as a PE image")


def main() -> int:
    try:
        check_the_image_reader()
        root = Path(tempfile.mkdtemp(prefix="cs2-protect-coverage-test-"))
        try:
            check_the_readers(root)
        finally:
            shutil.rmtree(root, ignore_errors=True)
    except TestFailure as error:
        print(f"coverage_test: {error}", file=sys.stderr)
        return 1

    print("coverage_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
