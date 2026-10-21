#!/usr/bin/env python3
"""Stage-wise clang-tidy auto-fix sweep for the cs2-internal tree.

WHY STAGES
    Applying every check at once produces one enormous, hard-to-review diff and
    lets mutually antagonistic checks fight (e.g. modernize-use-auto against an
    explicit-type style). This tool applies one *group* of checks at a time so
    each change is reviewable, buildable and revertable on its own.

WHAT IT COVERS
    Only checks clang-tidy can auto-fix. A few of the "add const" actions you see
    in the CLion UI have no clang-tidy fix-it and must be applied in the IDE
    instead (`Alt+Enter`, then press the right arrow to pick a scope):
      * readability-make-member-function-const  -> "Make member function 'const'"
      * "Bulk make local var const" / "Bulk make parameter const" /
        "Bulk make ptr or ref parameter const"
    Everything else in GROUPS below is applied by editing files directly.

USAGE
    python scripts/clang-tidy-fix.py --list-groups
    python scripts/clang-tidy-fix.py --group modernize-safe                 # dry run
    python scripts/clang-tidy-fix.py --group modernize-safe --apply         # write
    python scripts/clang-tidy-fix.py --apply --format --commit              # defaults, tidy, commit
    python scripts/clang-tidy-fix.py --group trailing-return-type --apply
    python scripts/clang-tidy-fix.py --check                                # the gate

    Review with `git diff`, then rebuild before committing:
        meson compile -C build && meson test -C build

CHECK MODE
    `--check` runs the selected groups **without writing anything** and fails
    when a selected group reports a finding. Without `--group`/`--checks` the
    selected groups are the *gated* ones (GATED_GROUPS below, `--list-gate`),
    so the command is the regression gate: a change that introduces a finding
    under a gated group fails it.

EXIT STATUS
    0  no translation units failed to analyse (and, in check mode, no gated
       group reported a finding)
    1  at least one translation unit failed (its output is shown), or a gated
       group reported a finding in check mode
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Meson build dirs, in preference order. When a source file appears in more than
# one database the first build dir that claims it wins, so each file is fixed
# exactly once.
BUILD_DIR_CANDIDATES = ("build", "build-payload", "build-injector", "build-overlay")

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c", ".hpp", ".hh", ".h", ".hxx", ".inl")

# Vendored / generated / toolchain trees that must never be auto-fixed.
EXCLUDE_PARTS = ("third_party", "external", "toolchain", "subprojects", "meson-private")
EXCLUDE_PREFIXES = (("payload", "sdk"),)  # generated SDK headers
# Build trees hold generated sources; skip them.
EXCLUDE_ROOT_NAMES = ("build", "_install")

# check groups -> clang-tidy checks. Every check listed here is auto-fixable
# (verified against the bundled clang-tidy); checks without a fix-it are omitted
# on purpose (see the module docstring).
GROUPS: dict[str, list[str]] = {
    "modernize-safe": [
        "modernize-use-nullptr",
        "modernize-use-override",
        "modernize-use-bool-literals",
        "modernize-use-equals-default",
        "modernize-use-equals-delete",
        "modernize-use-default-member-init",
        "modernize-redundant-void-arg",
        "modernize-use-using",
        "modernize-deprecated-headers",
        "modernize-loop-convert",
        "modernize-return-braced-init-list",
        "modernize-replace-auto-ptr",
    ],
    "modernize-types": [
        "modernize-use-auto",
        "modernize-use-emplace",
        "modernize-use-transparent-functors",
        "modernize-use-starts-ends-with",
        "modernize-make-unique",
        "modernize-make-shared",
    ],
    "trailing-return-type": ["modernize-use-trailing-return-type"],
    "nodiscard": ["modernize-use-nodiscard"],
    "const": ["misc-const-correctness", "readability-qualified-auto"],
    "performance": [
        "performance-unnecessary-value-param",
        "performance-unnecessary-copy-initialization",
        "performance-for-range-copy",
    ],
    "readability": [
        "readability-container-size-empty",
        # readability-convert-member-functions-to-static is EXCLUDED: its fix
        # emits `static [[nodiscard]] auto f(...)`, and an attribute list may
        # not appear there - the attribute must precede the decl-specifier-seq
        # (`[[nodiscard]] static auto f(...)`). The result does not compile.
        # See docs/style-guide.md §7.
        "readability-else-after-return",
        "readability-inconsistent-declaration-parameter-name",
        "readability-redundant-string-cstr",
        "readability-simplify-boolean-expr",
        "readability-static-accessed-through-instance",
        "readability-use-anyofallof",
    ],
    # --- wider stylistic / modernisation groups ---------------------------
    # NOTE: modernize-use-std-print is deliberately excluded: this payload is
    # mapped into the game process and the code avoids CRT/iostream machinery and
    # static initializers (see model_publisher.cpp / payload_main.cpp), which
    # <print> would reintroduce. Run it explicitly with --checks if wanted.
    # modernize-avoid-c-arrays / -variadic-functions have no fix-it and are
    # omitted to keep the counts meaningful.
    # NOTE: modernize-use-integer-sign-comparison is deliberately excluded. Its
    # fix rewrites `x != static_cast<std::size_t>(-1)` (a "no value" sentinel)
    # into `std::cmp_not_equal(x, -1)`, which is *always true* for unsigned x, so
    # the sentinel is never recognised. That silently corrupted the inline-hook
    # displacement handling (hooks-inline crashed with 0xC0000005). Run it via
    # --checks only if you review every hunk.
    "modernize-extra": [
        "modernize-use-scoped-lock",
        # modernize-use-ranges is EXCLUDED: its fix rewrites
        # `erase(std::remove_if(b, e, p), e)` into
        # `erase(std::ranges::remove_if(b, p))`, but ranges::remove_if returns a
        # subrange, not an iterator, so the result does not compile. The intent
        # is right and the correct form is `std::erase_if(b, p)` (C++20) - apply
        # it by hand where it is wanted. See docs/style-guide.md §7.
        "modernize-use-designated-initializers",
        "modernize-macro-to-enum",
        "modernize-use-constraints",
    ],
    "readability-extra": [
        "readability-braces-around-statements",
        "readability-isolate-declaration",
        "readability-implicit-bool-conversion",
        "readability-const-return-type",
        "readability-non-const-parameter",
        "readability-avoid-const-params-in-decls",
        "readability-container-contains",
        "readability-container-data-pointer",
        "readability-delete-null-pointer",
        "readability-duplicate-include",
        "readability-redundant-casting",
        "readability-redundant-declaration",
        "readability-redundant-member-init",
        "readability-redundant-preprocessor",
        "readability-redundant-smartptr-get",
        "readability-redundant-string-init",
        "readability-simplify-subscript-expr",
        "readability-static-definition-in-anonymous-namespace",
        "readability-uppercase-literal-suffix",
        "readability-use-std-min-max",
        "readability-misleading-indentation",
        "readability-avoid-nested-conditional-operator",
    ],
    "misc-extra": [
        "misc-unused-using-decls",
        "misc-unused-alias-decls",
        "misc-uniqueptr-reset-release",
        "misc-throw-by-value-catch-by-reference",
        "misc-redundant-expression",
        "misc-unconventional-assign-operator",
        "misc-definitions-in-headers",
    ],
}

GROUP_DOCS: dict[str, str] = {
    "modernize-safe": "mechanical, semantics-preserving modernisations (nullptr, override, =default, ...)",
    "modernize-types": "type-inference/emplace rewrites (use-auto, make-unique, emplace, ...)",
    "trailing-return-type": "rewrite signatures to trailing return types (stylistic, opt-in)",
    "nodiscard": "add [[nodiscard]] where return values are routinely ignored",
    "const": "const-correctness (misc-const-correctness, readability-qualified-auto)",
    "performance": "avoid copies (value params, copy-init, range-for)",
    "readability": "curated cosmetic cleanups",
    "modernize-extra": "further modernisations (scoped-lock, ranges, designated-init, std::print, ...)",
    "readability-extra": "broader style rewrites (braces, bool conversion, redundant-*, ...)",
    "misc-extra": "misc cleanups (unused using/alias decls, uniqueptr reset/release, ...)",
}

# Applied when neither --group nor --checks is given: the conservative,
# semantics-preserving modernisations.
DEFAULT_GROUPS = ("modernize-safe", "modernize-types")

# The groups the tree is expected to be **clean** under. `--check` selects
# them by default, and a finding in one of them fails the check.
#
# WHY ONLY THESE
#     The tree is not clang-tidy-clean historically (docs/style-guide.md §7),
#     and the other groups cannot be gated as they stand:
#       * `const` has three findings left, all pre-existing;
#       * `modernize-types` wants `auto` exactly where the style guide wants
#         the type spelled out ("auto where the type is evident; spell the
#         type out when it carries meaning"), so gating it would fight the
#         house style;
#       * `modernize-safe` wants `return {x};` in places where the explicit
#         constructor reads better;
#       * `readability`/`readability-extra`/`trailing-return-type`/
#         `modernize-extra` still have findings in the files the concurrent
#         control-channel work owns, and in a vendored header;
#       * `misc-extra`'s three findings are false positives (a redundant
#         comparison that is not redundant).
#     Widening the gate is a deliberate act: clear the group's findings, then
#     add it here. `performance` and `nodiscard` are the two that catch the
#     mistakes that matter -- a copy in a hot path, and a dropped result.
GATED_GROUPS: tuple[str, ...] = ("performance", "nodiscard")

DEFAULT_HEADER_FILTER = r".*(payload|overlay|injector|tools)/.*"
DEFAULT_EXCLUDE_HEADER_FILTER = r"(^|/)(third_party|external|toolchain|subprojects)/"
NO_HEADER_FILTER = r"^$"  # matches nothing: diagnostics restricted to main files

DIAG_RE = re.compile(r"\[([a-zA-Z0-9][a-zA-Z0-9._-]*)\]\s*$")

# The compiler's own diagnostics are not findings of a check group. A
# translation unit that does not compile is reported by the sweep's "failed on
# N translation unit(s)" path (clang-tidy prints "Error while processing"), and
# counting it as a finding of the group would make the gate say "performance
# reported a finding" and send the reader hunting a style problem that does
# not exist. (Compiler *warnings* need no rule: clang-tidy filters them out
# with the check selection, so they never reach the counter.)
NON_FINDING_CHECKS = frozenset({"clang-diagnostic-error"})


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT)).replace(os.sep, "/")
    except ValueError:
        return str(path)


def die(message: str) -> "NoReturn":  # type: ignore[name-defined]
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


# --- discovery ------------------------------------------------------------

def discover_build_dirs(args: argparse.Namespace) -> list[Path]:
    if args.build:
        dirs = [(REPO_ROOT / b).resolve() for b in args.build]
    else:
        dirs = [REPO_ROOT / b for b in BUILD_DIR_CANDIDATES]
    usable = [d for d in dirs if (d / "compile_commands.json").is_file()]
    if not usable:
        die(
            "no Meson build dir with compile_commands.json found "
            f"(looked for: {', '.join(args.build or BUILD_DIR_CANDIDATES)}). "
            "Run `meson setup build .` first."
        )
    missing = [d for d in dirs if d not in usable]
    for d in missing:
        print(f"note: skipping {rel(d)} (no compile_commands.json)", file=sys.stderr)
    return usable


def is_excluded(path: Path) -> bool:
    try:
        parts = path.relative_to(REPO_ROOT).parts
    except ValueError:
        return True
    if parts and (parts[0] in EXCLUDE_ROOT_NAMES or parts[0].startswith("build-")):
        return True
    if any(part in EXCLUDE_PARTS for part in parts):
        return True
    return any(parts[: len(pre)] == pre for pre in EXCLUDE_PREFIXES)


def load_sources(build_dir: Path) -> list[Path]:
    data = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    out: list[Path] = []
    for entry in data:
        raw = Path(entry["file"])
        if not raw.is_absolute():
            raw = Path(entry.get("directory", build_dir)) / raw
        try:
            resolved = raw.resolve()
        except OSError:
            continue
        if resolved.suffix.lower() in SOURCE_SUFFIXES and resolved.is_file():
            out.append(resolved)
    return out


def build_plan(args: argparse.Namespace) -> list[tuple[Path, Path]]:
    """Return (source, build_dir) pairs, one entry per source file."""
    plan: list[tuple[Path, Path]] = []
    seen: set[Path] = set()
    for build_dir in discover_build_dirs(args):
        for source in load_sources(build_dir):
            if source in seen or is_excluded(source):
                continue
            seen.add(source)
            plan.append((source, build_dir))
    if args.filter:
        plan = [(s, b) for s, b in plan if any(f in rel(s) for f in args.filter)]
    plan.sort(key=lambda item: rel(item[0]))
    return plan


def tracked_sources() -> list[Path]:
    """All git-tracked C/C++ files, so header-only fixes are detected too."""
    proc = subprocess.run(
        ["git", "ls-files", "-z"], cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
    )
    if proc.returncode != 0:
        return []
    out = []
    for name in proc.stdout.decode("utf-8", "replace").split("\0"):
        if name and Path(name).suffix.lower() in SOURCE_SUFFIXES:
            path = REPO_ROOT / name
            if path.is_file() and not is_excluded(path):
                out.append(path)
    return out


def snapshot(paths: list[Path]) -> dict[Path, str]:
    state: dict[Path, str] = {}
    for path in paths:
        try:
            state[path] = hashlib.sha1(path.read_bytes()).hexdigest()
        except OSError:
            continue
    return state


# --- running clang-tidy ---------------------------------------------------

def parse_findings(output: str) -> Counter[str]:
    counts: Counter[str] = Counter()
    for line in output.splitlines():
        if ": warning:" not in line and ": error:" not in line:
            continue
        match = DIAG_RE.search(line.rstrip())
        if match and match.group(1) not in NON_FINDING_CHECKS:
            counts[match.group(1)] += 1
    return counts


def run_one(
    clang_tidy: str,
    build_dir: Path,
    source: Path,
    checks: str,
    header_filter: str,
    exclude_header_filter: str,
    apply: bool,
) -> tuple[Path, bool, Counter[str], str]:
    cmd = [clang_tidy, "-p", str(build_dir), str(source), f"--checks={checks}"]
    if header_filter:
        cmd.append(f"--header-filter={header_filter}")
    if exclude_header_filter:
        cmd.append(f"--exclude-header-filter={exclude_header_filter}")
    if apply:
        # Plain -fix only. Never -fix-errors: that rewrites code clang-tidy
        # could not even parse and is exactly how "auto-fixes" break builds.
        cmd.append("-fix")

    proc = subprocess.run(
        cmd, cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace"
    )
    output = proc.stdout or ""
    ok = "Error while processing" not in output and proc.returncode >= 0
    return source, ok, parse_findings(output), output


def run_group(
    clang_tidy: str,
    plan: list[tuple[Path, Path]],
    checks_csv: str,
    jobs: int,
    header_filter: str,
    exclude_header_filter: str,
    apply: bool,
    verbose: bool,
    group: str = "",
    reported: set[str] | None = None,
) -> tuple[list[str], Counter[str]]:
    failures: list[str] = []
    findings: Counter[str] = Counter()
    if reported is None:
        reported = set()

    def work(item: tuple[Path, Path]) -> tuple[Path, bool, Counter[str], str]:
        source, build_dir = item
        return run_one(clang_tidy, build_dir, source, checks_csv, header_filter, exclude_header_filter, apply)

    if jobs > 1:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            results = list(pool.map(work, plan))
    else:
        results = [work(item) for item in plan]

    for source, ok, counts, output in results:
        findings.update(counts)
        if not ok:
            failures.append(rel(source))
            # A translation unit that fails in two groups is one translation
            # unit: its output is shown once, and the summary counts it once.
            if rel(source) not in reported:
                reported.add(rel(source))
                print(f"\n--- clang-tidy failed on {rel(source)} (group: {group}) ---",
                      file=sys.stderr)
                print(output, file=sys.stderr)
        elif verbose and counts:
            print(f"  {rel(source)}: {sum(counts.values())} finding(s)")

    return failures, findings


def gated_findings(selected: list[str], per_group: dict[str, Counter[str]]) -> list[str]:
    """The selected groups that reported a finding.

    This is the decision the check mode turns into an exit code, and it is
    kept separate from the sweep itself so it can be tested without running
    clang-tidy (see scripts/scenario/tests/test_clang_tidy_fix.py).
    """
    return [name for name in selected if sum(per_group.get(name, Counter()).values()) > 0]


def distinct_failures(failures: list[str]) -> list[str]:
    """The translation units that failed, each of them once.

    The failures are collected per group, so a translation unit that fails
    in two groups is listed twice -- but it is still *one* translation
    unit, and the count the gate prints must not count it twice.
    """
    seen: set[str] = set()
    unique: list[str] = []
    for source in failures:
        if source not in seen:
            seen.add(source)
            unique.append(source)
    return unique


# --- reporting ------------------------------------------------------------

def report(group: str, checks: list[str], scan: int, findings: Counter[str], changed: list[Path]) -> None:
    total = sum(findings.values())
    print(f"\n== {group} ==")
    print(f"   checks: {', '.join(checks)}")
    print(f"   scanned {scan} translation unit(s); {total} finding(s); {len(changed)} file(s) changed")
    for check, count in findings.most_common():
        print(f"     {count:5d}  {check}")
    for path in changed:
        print(f"     changed  {rel(path)}")


def stage(changed: list[Path], message: str) -> None:
    paths = [rel(p) for p in changed]
    subprocess.run(["git", "add", "--", *paths], cwd=REPO_ROOT, check=True)
    subprocess.run(["git", "commit", "-m", message], cwd=REPO_ROOT, check=True)


# --- cli ------------------------------------------------------------------

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="clang-tidy-fix.py",
        description="Apply clang-tidy fixes across the project, one check group at a time.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Groups:\n" + "\n".join(
            f"  {name:22} {GROUP_DOCS.get(name, '')}"
            + ("   [default]" if name in DEFAULT_GROUPS else "")
            for name in GROUPS
        ),
    )
    parser.add_argument("--group", action="append", metavar="NAME",
                        help="check group to run (repeatable); default: " + ", ".join(DEFAULT_GROUPS))
    parser.add_argument("--checks", metavar="CSV",
                        help="explicit comma-separated checks, bypassing groups (advanced)")
    parser.add_argument("--build", action="append", metavar="DIR",
                        help="build dir(s) holding compile_commands.json (repeatable); default: all found")
    parser.add_argument("--filter", action="append", metavar="SUBSTR",
                        help="only touch files whose repo-relative path contains SUBSTR (repeatable)")
    parser.add_argument("--apply", action="store_true",
                        help="write the fixes (default is a dry run that only reports)")
    parser.add_argument("--dry-run", action="store_true", help="explicit alias for the default")
    parser.add_argument("--format", action="store_true",
                        help="run clang-format -i on changed files after each group")
    parser.add_argument("--commit", action="store_true",
                        help="git-commit each group's changes (implies --apply)")
    parser.add_argument("--no-headers", action="store_true",
                        help="do not apply fixes in headers; restrict to translation units")
    parser.add_argument("--header-filter", default=DEFAULT_HEADER_FILTER, help="clang-tidy --header-filter")
    parser.add_argument("--exclude-header-filter", default=DEFAULT_EXCLUDE_HEADER_FILTER,
                        help="clang-tidy --exclude-header-filter")
    parser.add_argument("-j", "--jobs", type=int, metavar="N",
                        help="parallel clang-tidy processes (default 1 when applying)")
    parser.add_argument("-v", "--verbose", action="store_true", help="show per-file finding counts")
    parser.add_argument("--list-groups", action="store_true", help="list groups and exit")
    parser.add_argument("--list-files", action="store_true", help="list the translation units to scan and exit")
    parser.add_argument("--check", action="store_true",
                        help="check mode: write nothing, and fail when a selected group reports a finding "
                             "(the selected groups are the gated ones unless --group/--checks is given)")
    parser.add_argument("--gate", action="append", metavar="NAME",
                        help="replace the gated groups (repeatable); see --list-gate")
    parser.add_argument("--list-gate", action="store_true", help="list the gated groups and exit")
    return parser.parse_args(argv)


def resolve_groups(args: argparse.Namespace) -> list[tuple[str, list[str]]]:
    if args.checks and args.group:
        die("--checks and --group are mutually exclusive")
    if args.checks:
        return [("custom", [c.strip() for c in args.checks.split(",") if c.strip()])]
    names = args.group or list(DEFAULT_GROUPS)
    groups: list[tuple[str, list[str]]] = []
    for name in names:
        if name not in GROUPS:
            die(f"unknown group '{name}'; see --list-groups")
        groups.append((name, GROUPS[name]))
    return groups


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if args.list_groups:
        for name, checks in GROUPS.items():
            marker = " [default]" if name in DEFAULT_GROUPS else ""
            print(f"{name}{marker}\n    {GROUP_DOCS.get(name, '')}\n    {', '.join(checks)}")
        return 0

    # The gated groups: the ones a check run must be clean under. `--gate`
    # replaces the set, so a wider (or narrower) gate is a deliberate act.
    gate = list(args.gate) if args.gate else list(GATED_GROUPS)
    for name in gate:
        if name not in GROUPS:
            die(f"unknown group '{name}'; see --list-groups")

    if args.list_gate:
        print("the gated groups (a finding in one of them fails --check):")
        for name in gate:
            marker = " [gated]" if name in GATED_GROUPS else ""
            print(f"  {name}{marker}\n    {GROUP_DOCS.get(name, '')}")
        return 0

    # In check mode the gated groups are the selection, unless the caller
    # picked the groups explicitly.
    if args.check and not args.group and not args.checks:
        args.group = list(gate)

    groups = resolve_groups(args)
    apply = args.apply or args.commit
    if args.check:
        # A check never writes: it reports, it does not fix.
        apply = False
    plan = build_plan(args)

    if args.list_files:
        for source, build_dir in plan:
            print(f"{rel(source)}   [{rel(build_dir)}]")
        print(f"\n{len(plan)} translation unit(s)")
        return 0

    clang_tidy = shutil.which("clang-tidy")
    if not clang_tidy:
        die("clang-tidy not found on PATH")
    clang_format = shutil.which("clang-format")

    jobs = args.jobs if args.jobs else (1 if apply else (os.cpu_count() or 1))
    if apply and jobs > 1:
        print("warning: parallel --apply can corrupt shared headers; use -j 1 if that happens",
              file=sys.stderr)

    header_filter = NO_HEADER_FILTER if args.no_headers else args.header_filter

    print(f"repo    {REPO_ROOT}")
    print(f"mode    {'CHECK' if args.check else ('APPLY' if apply else 'dry run')}   jobs={jobs}")
    print(f"plan    {len(plan)} translation unit(s)")
    print(f"groups  {', '.join(name for name, _ in groups)}")

    watch = sorted({p for p, _ in plan} | set(tracked_sources()))
    failed: list[str] = []
    reported: set[str] = set()
    per_group: dict[str, Counter[str]] = {}

    for name, checks in groups:
        # clang-tidy *merges* --checks with the Checks list in .clang-tidy rather
        # than replacing it, so reset to nothing first. Without the leading -*,
        # every "group" would silently run the whole .clang-tidy set.
        checks_csv = "-*," + ",".join(checks)
        before = snapshot(watch)
        group_failures, findings = run_group(clang_tidy, plan, checks_csv, jobs, header_filter,
                                             args.exclude_header_filter, apply, args.verbose,
                                             name, reported)
        failed.extend(group_failures)
        per_group[name] = findings
        after = snapshot(watch)
        changed = sorted(p for p in after if before.get(p) != after[p])

        if apply and changed and args.format:
            if not clang_format:
                die("--format requested but clang-format is not on PATH")
            subprocess.run([clang_format, "-i", *[str(p) for p in changed]], cwd=REPO_ROOT, check=True)

        report(name, checks, len(plan), findings, changed)

        if apply and args.commit and changed:
            stage(changed, f"chore(clang-tidy): apply {name} fixes")

    if failed:
        # A translation unit that failed in two groups is still one
        # translation unit: the count must not count it twice.
        distinct = distinct_failures(failed)
        print(f"\nfailed on {len(distinct)} translation unit(s):", file=sys.stderr)
        for name in distinct:
            print(f"  {name}", file=sys.stderr)
        return 1

    if args.check:
        dirty = gated_findings([name for name, _ in groups], per_group)
        if dirty:
            print(f"\ncheck FAILED: {', '.join(dirty)} reported a finding", file=sys.stderr)
            return 1
        print("\ncheck OK: the selected groups report no finding")
        return 0

    if not apply:
        print("\nThis was a dry run. Re-run with --apply to write the fixes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
