#!/usr/bin/env python3
"""The image analysis of the protection pass: the cipher and the dispatches.

Three consumers have to agree on both:

  * `tools/protect/check_payload.py` -- the payload-level check;
  * `tools/protect/tests/ir_test.py` -- the IR-level test;
  * `tools/protect/tests/diff_test.py` -- the differential test.

A copy of the cipher in three places is a cipher that can drift: the check
would decode the descriptor with a different mixer than the pass encrypted
it with, and it would report a wrong callee. The module is therefore
shared, and the tests put the directory of the tool on the path.

The cipher is the plugin's (`tools/protect/src/transform.cpp` is the other
half of it): the multiplier, the xor constant and the rotation.
"""
from __future__ import annotations

import hashlib
import os
import re
import struct
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

#: The multiplier of the mixer.
MIX_MULTIPLIER = 0x9E3779B97F4A7C15
#: The xor constant of the mixer.
MIX_XOR = 0xBF58476D1CE4E5B9
#: How far the mixer rotates its result.
MIX_ROTATE = 31
#: The mask of a 64-bit value.
MASK64 = (1 << 64) - 1


def mix64(value: int) -> int:
    """The mixer of the transform, in Python."""
    value = (value * MIX_MULTIPLIER) & MASK64
    value ^= MIX_XOR
    return ((value << MIX_ROTATE) | (value >> (64 - MIX_ROTATE))) & MASK64


def signed64(value: int) -> int:
    """The signed spelling of a 64-bit value (the IR prints negative ones)."""
    return value - (1 << 64) if value >= (1 << 63) else value


class Failure(Exception):
    """A check that did not hold."""


# --- The disassembly ----------------------------------------------------------

#: A RIP-relative memory operand: `movq 0x1234(%rip), %rax  # 0x180012340`.
#:
#: Every such operand is a read of a global of the image -- the table, the
#: descriptor, the key constants or the constants of the mixer. A `leaq` is
#: the one that puts the *address* of the global into a register: that is the
#: table of the dispatch.
RIP_MEMORY = re.compile(
    r"\b(?P<op>[a-z][a-z0-9]*)\s+0x[0-9a-f]+\(%rip\)(?:,\s*%(?P<register>r[a-z0-9]+))?"
    r"\s+#\s+0x(?P<address>[0-9a-f]+)"
)

#: An indirect call through a table: `callq *(%rsi,%rax,8)`.
DISPATCH_CALL = re.compile(r"\bcallq\s+\*\(%(?P<base>r[a-z0-9]+),%r[a-z0-9]+,8\)")

#: The other lowering of the same IR: the target is loaded into a register
#: first (`movq (%rsi,%rax,8), %rcx`) and the call goes through the register.
DISPATCH_LOAD = re.compile(
    r"\bmovq\s+\*\(%(?P<base>r[a-z0-9]+),%r[a-z0-9]+,8\),\s*%(?P<target>r[a-z0-9]+)"
)

#: An indirect call through a register.
INDIRECT_CALL = re.compile(r"\bcallq\s+\*%(?P<target>r[a-z0-9]+)")

#: A jump through a table: `jmpq *(%rsi,%rax,8)`.
DISPATCH_JUMP = re.compile(r"\bjmpq\s+\*\(%(?P<base>r[a-z0-9]+),%r[a-z0-9]+,8\)")

#: An instruction of the disassembly: `18011351a:\tcallq\t*(%rsi,%rax,8)`.
INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\s+(?P<text>.*)$")

#: How many instructions before a dispatch the check looks at.
#:
#: The decode reads the three key constants, the two mixer constants and the
#: descriptor right before the call: six reads in eleven instructions. The
#: window is generous, but it is not unbounded: a reference from far away
#: belongs to something else.
REFERENCE_WINDOW = 24


class DispatchSite:
    """One encrypted dispatch in the image."""

    def __init__(self, address: int, table: int | None, references: list[int]) -> None:
        self.address = address
        self.table = table
        self.references = references

    def __repr__(self) -> str:
        table = "?" if self.table is None else f"0x{self.table:x}"
        return f"<dispatch at 0x{self.address:x}, table {table}>"


def disassemble(objdump: Path, binary: Path) -> str:
    """The disassembly of a binary."""
    completed = subprocess.run([str(objdump), "-d", "--no-show-raw-insn", str(binary)],
                               capture_output=True, text=True)
    if completed.returncode != 0:
        raise Failure(f"objdump failed on '{binary}':\n{completed.stderr}")
    return completed.stdout


def reads_the_mixer(image: "PeImage | None", references: list[int]) -> bool:
    """Whether the references hold one of the constants of the mixer.

    The decode of a dispatch reads the constants of the mixer from the
    globals of the image. That is what tells a dispatch apart from another
    jump through a table: the backend turns the last call of a function into
    a tail jump, and that jump *is* a dispatch -- but a switch table jump is
    not, and it has no decode before it.
    """
    if image is None:
        return False
    for reference in references:
        try:
            value = image.u64_at_rva(reference)
        except Failure:
            continue
        if value == MIX_MULTIPLIER or value == MIX_XOR:
            return True
    return False


def find_dispatch_sites(asm: str, image_base: int, image: "PeImage | None" = None) \
    -> list[DispatchSite]:
    """The encrypted dispatches of a disassembly.

    A dispatch is the transform's own signature: an indirect call through the
    table of the transform. The table is the transform's own global, so the
    call's base register is tracked back to the `leaq` that loaded it.

    A *jump* through a table is a dispatch only when the decode precedes it:
    the backend turns the last call of a function into a tail jump, and that
    jump is a dispatch; a switch table jump is not one, and it has no decode
    before it.
    """
    sites: list[DispatchSite] = []
    tables: dict[str, int] = {}
    window: list[int] = []
    pending: tuple[str, str] | None = None

    for line in asm.splitlines():
        matched = INSTRUCTION.match(line)
        if matched is None:
            continue
        text = matched.group("text").split("#")[0].strip()
        if not text:
            continue
        operands = [operand.strip() for operand in text.split(",")]
        destination = operands[-1]
        register = destination[1:] if destination.startswith("%r") else ""

        rip = RIP_MEMORY.search(line)
        if rip is not None:
            # A read of a global of the image.
            reference = int(rip.group("address"), 16) - image_base
            window.append(reference)
            window = window[-REFERENCE_WINDOW:]
            if rip.group("op") == "leaq" and register:
                # A `leaq` puts the *address* of the global into the
                # register: that is the table of the dispatch.
                tables[register] = reference
                continue

        call = DISPATCH_CALL.search(line)
        jump = DISPATCH_JUMP.search(line)
        loaded = DISPATCH_LOAD.search(line)
        indirect = INDIRECT_CALL.search(line)
        if call is not None:
            base = call.group("base")
            sites.append(DispatchSite(int(matched.group(1), 16), tables.get(base), list(window)))
            window = []
            pending = None
        elif jump is not None and reads_the_mixer(image, window):
            # A tail call: the backend jumped to the callee instead of
            # calling it. The decode is right before it.
            base = jump.group("base")
            sites.append(DispatchSite(int(matched.group(1), 16), tables.get(base), list(window)))
            window = []
            pending = None
        elif loaded is not None:
            # The other lowering: the target is loaded into a register and the
            # call goes through it.
            pending = (loaded.group("base"), loaded.group("target"))
        elif indirect is not None and pending is not None and indirect.group("target") == pending[1]:
            base = pending[0]
            sites.append(DispatchSite(int(matched.group(1), 16), tables.get(base), list(window)))
            window = []
            pending = None

        # Any other write to the register invalidates the table it held.
        if register:
            tables.pop(register, None)

    return sites


def count_dispatches(objdump: Path, binary: Path) -> int:
    """The number of encrypted dispatches in a binary.

    A dispatch is an indirect call through the table of the transform: the
    direct call that the pass replaced is gone, so the count is the number
    of the call sites the pass rewrote. (The image base only matters for
    the table of a site, not for its number.)
    """
    image = PeImage(binary.read_bytes())
    return len(find_dispatch_sites(disassemble(objdump, binary), image.image_base, image))


def immediates(asm: str) -> set[int]:
    """The 64-bit immediates of a disassembly.

    A constant that the backend materialised as an instruction immediate is
    a constant the backend may have got wrong: the X86 backend of this LLVM
    loses a bit of a 64-bit immediate in the shape the transform emits. The
    mixer constants therefore live in globals, and this is how a test proves
    that they do.
    """
    found: set[int] = set()
    for line in asm.splitlines():
        for match in re.finditer(r"#\s*imm\s*=\s*0x(?P<value>[0-9a-f]+)", line):
            found.add(int(match.group("value"), 16))
    return found


if __name__ == "__main__":
    # A quick look at an image: how many dispatches does it have?
    if len(sys.argv) < 3:
        print(f"usage: {Path(sys.argv[0]).name} <objdump> <binary>", file=sys.stderr)
        raise SystemExit(2)
    tool = Path(sys.argv[1])
    image = Path(sys.argv[2])
    print(f"{len(find_dispatch_sites(disassemble(tool, image), 0))} dispatches")


# --- The PE image ------------------------------------------------------------


class Section:
    """One section of a PE image."""

    def __init__(self, name: str, virtual_address: int, virtual_size: int, raw_offset: int,
                 raw_size: int) -> None:
        self.name = name
        self.virtual_address = virtual_address
        self.virtual_size = virtual_size
        self.raw_offset = raw_offset
        self.raw_size = raw_size

    def contains_rva(self, rva: int) -> bool:
        """Whether an RVA falls in this section."""
        return self.virtual_address <= rva < self.virtual_address + max(self.virtual_size,
                                                                       self.raw_size)


class PeImage:
    """The parts of a PE image the checks need."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise Failure("the file is not a PE image")
        try:
            self._read_headers(data)
        except struct.error as error:
            # A truncated file (or a file that is not a PE at all) is a
            # *reported* failure, not a crash: a reader that dies with a
            # traceback tells the operator nothing.
            raise Failure(f"the file is not a PE image: {error}") from error

    def _read_headers(self, data: bytes) -> None:
        """Reads the headers of the image."""
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise Failure("the file is not a PE image")
        coff = pe_offset + 4
        self.machine = struct.unpack_from("<H", data, coff)[0]
        number_of_sections = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        optional = coff + 20
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic == 0x20B:
            self.image_base = struct.unpack_from("<Q", data, optional + 24)[0]
            directories = optional + 112
        else:
            self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
            directories = optional + 96
        number_of_directories = struct.unpack_from("<I", data, optional + 108 if magic == 0x20B
                                                  else optional + 92)[0]

        self.sections: list[Section] = []
        table = optional + optional_size
        for index in range(number_of_sections):
            header = table + index * 40
            name = data[header : header + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, header + 8
            )
            self.sections.append(Section(name, virtual_address, virtual_size, raw_offset,
                                         raw_size))

        self.directories: list[tuple[int, int]] = []
        for index in range(min(number_of_directories, 16)):
            self.directories.append(
                struct.unpack_from("<II", data, directories + index * 8)
            )

    def directory(self, index: int) -> tuple[int, int]:
        """The (rva, size) of a data directory."""
        if index >= len(self.directories):
            return (0, 0)
        return self.directories[index]

    def section_of(self, name: str) -> Section | None:
        """The section with a name."""
        for section in self.sections:
            if section.name == name:
                return section
        return None

    def bytes_at_rva(self, rva: int, size: int) -> bytes:
        """The bytes of an RVA range."""
        for section in self.sections:
            if section.contains_rva(rva):
                offset = section.raw_offset + (rva - section.virtual_address)
                return self.data[offset : offset + size]
        raise Failure(f"the RVA 0x{rva:x} is not in any section")

    def u64_at_rva(self, rva: int) -> int:
        """The 64-bit little-endian value at an RVA."""
        try:
            value: int = struct.unpack("<Q", self.bytes_at_rva(rva, 8))[0]
        except struct.error as error:
            raise Failure(f"the RVA 0x{rva:x} holds no 64-bit value: {error}") from error
        return value

    def is_code_address(self, value: int) -> bool:
        """Whether a value is the address of code in this image."""
        rva = value - self.image_base
        text = self.section_of(".text")
        return text is not None and text.contains_rva(rva)


# --- The coverage ------------------------------------------------------------


class Coverage:
    """One coverage file of a module."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.policy = ""
        self.module = ""
        self.source_path = ""
        self.source_sha256 = ""
        self.table_size = 0
        self.descriptor_size = 0
        self.key_parts: list[int] = []
        self.mix_multiplier = 0
        self.mix_xor = 0
        self.rewritten = 0
        self.intact = False
        self.malformed = ""
        self.reason = ""
        self.entries: dict[str, str] = {}
        #: The verdict of the virtualizer's analysis, per function. The
        #: section is new: a coverage file of an older build has none, and
        #: a reader that does not know it still reads the file.
        self.vm: dict[str, dict[str, str]] = {}
        #: The literals of the string protection: the length and the digest
        #: of the content, never the content itself.
        self.strings: list[StringLiteral] = []
        #: The salt the digests of the literals were computed with ("" when
        #: the coverage has no salt line: an older build, or a build without
        #: a seed). The salt itself is not in the coverage.
        self.str_salt: str = ""
        #: The proof that a salt is the salt of the build: a digest of a
        #: fixed text with the salt. It is in the coverage, the salt is not.
        self.str_salt_proof: str = ""

        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            self.malformed = f"the file cannot be read: {error}"
            self.reason = self.malformed
            return

        lines = text.splitlines()
        digest = ""
        try:
            for line in lines:
                key, _, value = line.partition(" ")
                if key == "policy-sha256":
                    self.policy = value.strip().upper()
                elif key == "module":
                    self.module = value.strip()
                elif key == "source-path":
                    self.source_path = value.strip()
                elif key == "source-sha256":
                    self.source_sha256 = value.strip().lower()
                elif key == "protect":
                    self._read_protect(value)
                elif key == "fn":
                    for part in value.split():
                        if part.startswith("rewritten="):
                            self.rewritten += int(part.split("=", 1)[1])
                elif key == "entry":
                    state, _, rest = value.partition(" ")
                    if state in ("blocked", "unimplemented"):
                        reason, _, pattern = rest.partition(" ")
                        self.entries[pattern.strip()] = f"{state}:{reason.strip()}"
                    else:
                        self.entries[rest.strip()] = state.strip()
                elif key == "vm":
                    self._read_vm(value)
                elif key == "str":
                    self._read_str(value)
                elif key == "str-salt":
                    self._read_str_salt(value)
                elif key == "content-sha256":
                    digest = value.strip().lower()
        except ValueError as error:
            # A number that is not a number, a line that does not have the
            # shape the plugin writes: the file is malformed. It is reported,
            # not counted -- and it never crashes the reader.
            self.malformed = f"the file is malformed: {error}"
            self.reason = self.malformed
            return
        self.intact = self._check_digest(lines, digest)

    def states(self) -> dict[str, str]:
        """The state of every policy entry of the module."""
        return dict(self.entries)

    def _read_protect(self, value: str) -> None:
        """The `protect` line: what the transform put into the module."""
        for part in value.split():
            name, _, number = part.partition("=")
            if name == "table":
                self.table_size = int(number)
            elif name == "descriptor":
                self.descriptor_size = int(number)
            elif name == "multiplier":
                self.mix_multiplier = int(number, 0)
            elif name == "xor":
                self.mix_xor = int(number, 0)
            elif name == "key":
                self.key_parts.append(int(number, 0))

    def _read_vm(self, value: str) -> None:
        """The `vm` line: the verdict of the virtualizer's analysis.

        The line is `vm <symbol> <key>=<value>...`. The symbol is a mangled
        name, so it has no space in it, and the values are numbers and
        words.
        """
        symbol, _, fields = value.partition(" ")
        if not symbol:
            return
        verdict: dict[str, str] = {}
        for part in fields.split():
            name, _, number = part.partition("=")
            verdict[name] = number
        self.vm[symbol.strip()] = verdict

    def _read_str(self, value: str) -> None:
        """A `str` line: one literal of the string protection.

        The line carries the length and the digest of the content, never the
        content: the coverage is a file, and a file holds the strings.

        The digest is **salted** with the seed of the build, and the field
        says so (`digest=`).  A coverage of an older build names it
        `sha256=`, and that name is still read: the digest it carries is the
        unsalted one. Which form a digest is in is decided by the coverage's
        `str-salt` line, not by the field's name.
        """
        state, _, rest = value.partition(" ")
        length = 0
        sha256 = ""
        uses = 0
        functions = 0
        reason = ""
        for part in rest.split():
            name, _, number = part.partition("=")
            if name == "len":
                length = int(number)
            elif name in ("digest", "sha256"):
                sha256 = number.strip().upper()
            elif name == "uses":
                uses = int(number)
            elif name == "functions":
                functions = int(number)
            elif name == "reason":
                reason = number.strip()
        self.strings.append(
            StringLiteral(state.strip(), length, sha256, uses, functions, reason)
        )

    def _read_str_salt(self, value: str) -> None:
        """The `str-salt` line: the salt the digests were computed with.

        The line names the salt and carries its proof; the salt itself is
        not in the coverage. A coverage of an older build has no such line,
        and its digests are unsalted.

        The proof is a **salted** digest as well, so the field says so
        (`digest=`); the older name (`sha256=`) is still read.
        """
        parts = value.split()
        self.str_salt = parts[0].strip() if parts else ""
        for part in parts[1:]:
            name, _, number = part.partition("=")
            if name in ("digest", "sha256"):
                self.str_salt_proof = number.strip().upper()

    def _check_digest(self, lines: list[str], digest: str) -> bool:
        """Whether the file still is the one the plugin wrote.

        The plugin ends the file with the digest of everything above it. A
        file that was edited, truncated or copied with a doctored count does
        not describe the build, and a reader that believes it would count a
        rewrite that never happened.
        """
        if not digest:
            self.reason = "the file carries no content digest"
            return False
        body = "\n".join(lines[:-1]) + "\n"
        actual = hashlib.sha256(body.encode("utf-8")).hexdigest()
        if actual != digest:
            self.reason = "the content digest does not match the content"
            return False
        return True

    def source_is_current(self) -> bool:
        """Whether the source file of the module is the one that was compiled.

        A coverage file of an earlier state of the source describes another
        build, and the check must not count its rewrites.
        """
        if not self.source_sha256:
            return True
        name = self.source_path or self.module
        source = source_of(self)
        if source is None:
            self.reason = "the coverage names no source file"
            return False
        if not source.is_file():
            self.reason = f"the source file '{name}' is gone"
            return False
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual != self.source_sha256:
            self.reason = f"the source file '{name}' changed since the build"
            return False
        return True


def source_of(coverage: Coverage) -> Path | None:
    """The source file a coverage file describes, or None when it names none.

    The path is normalised: the plugin writes it normalised, but a *relative*
    path (the one the compiler was given) is resolved against the working
    directory of the compile, and the result has to be comparable with the
    tree the check is looking at.
    """
    name = coverage.source_path or coverage.module
    if not name:
        return None
    source = Path(name)
    if not source.is_absolute():
        # The path is relative to the working directory of the compile.
        source = Path.cwd() / source
    return Path(os.path.normpath(source))


def belongs_to_tree(coverage: Coverage, root: Path) -> bool:
    """Whether a coverage file describes a source of the tree that is built.

    The coverage directory is keyed by the *policy*, so two worktrees that
    share a policy share the directory. Their coverage files are different
    (the identity of a module names the worktree when the policy lives
    outside the tree), and counting both would count every rewrite twice:
    the payload check would then report that the image has half the
    dispatches the pass wrote -- a false "the compiler folded the dispatch
    away" on a perfectly good image.

    The tree is therefore part of what a coverage file has to match: only
    the files whose source belongs to the tree being built are evidence
    about it.
    """
    source = source_of(coverage)
    if source is None:
        return False
    try:
        return source.is_relative_to(Path(os.path.normpath(root)))
    except ValueError:
        # Two paths on different drives (Windows) are never relative.
        return False


class StringLiteral(NamedTuple):
    """One literal of the string protection, as the coverage records it.

    The coverage carries the length and the digest of the content, never the
    content: the coverage is a file, and a file holds the strings.
    """

    #: "protected" or "refused".
    state: str
    #: The length of the content, without the trailing NUL.
    length: int
    #: The digest of the content, in upper case.
    #:
    #: The digest is **salted** with the seed of the build
    #: (`sha256(seed || content)`), and a coverage of an older build carries
    #: the unsalted form.  The field does not say which: the salt line of the
    #: coverage does.
    digest: str
    uses: int
    functions: int
    #: The refusal reason ("" when the literal was protected).
    reason: str


class CoverageRead(NamedTuple):
    """What the coverage directory holds."""

    #: The coverage files that describe this build.
    files: list[Coverage]
    #: The files that do not, with the reason: (name, reason, rewrites).
    skipped: list[tuple[str, str, int]]
    #: The files of this policy that are malformed: (name, reason).
    malformed: list[tuple[str, str]]


def policy_key(policy_path: str) -> str:
    """The spelling of the policy path the coverage directory is keyed by.

    The plugin and the checkers have to compute the **same** directory, and
    the path they are given is spelled in two ways: the environment carries
    it as the operator spelled it (``C:/tools/protect/policy.json``), and a
    reader that goes through a path object turns the forward slashes into
    backslashes on Windows.  The key is therefore the path with the
    separators normalised to forward slashes -- the same spelling on every
    platform, and the one the plugin computes.
    """
    return policy_path.replace("\\", "/")


def default_coverage_dir(policy_path: str) -> Path:
    """The directory the plugin writes the coverage to.

    The digest is the **upper case** hexadecimal of the SHA-256 of the policy
    path: the plugin spells it `llvm::toHex`, which is upper case by default,
    and the two have to name the same directory on a file system whose case
    matters.
    """
    digest = hashlib.sha256(policy_key(policy_path).encode()).hexdigest().upper()[:16]
    root = os.environ.get("TEMP") or os.environ.get("TMP") or "."
    return Path(root) / "cs2-protect-coverage" / digest


def read_coverage(coverage: Path, policy: Path, source_root: Path | None = None) -> CoverageRead:
    """The coverage files of a policy, and the ones that do not describe this build.

    A file that does not belong to the policy, that was edited by hand, that
    is malformed or that describes another state of the source is not
    evidence about this build: it is skipped, and the reason is returned
    with it.

    The source root is the tree that is being built. When it is given, the
    coverage of another tree (another worktree that shares the policy) is
    skipped as well: it would otherwise be counted twice.
    """
    files: list[Coverage] = []
    skipped: list[tuple[str, str, int]] = []
    malformed: list[tuple[str, str]] = []
    if not policy.is_file():
        return CoverageRead(files, skipped, malformed)
    digest = hashlib.sha256(policy.read_bytes()).hexdigest().upper()
    for path in sorted(coverage.glob("*.cov")):
        entry = Coverage(path)
        if entry.policy != digest:
            continue
        if entry.malformed:
            reason = entry.malformed
            skipped.append((path.name, reason, entry.rewritten))
            if source_root is not None and not belongs_to_tree(entry, source_root):
                # Another tree's broken file: it is not this build's business
                # (and it is pruned by the policy checker).
                continue
            malformed.append((path.name, reason))
            continue
        if not entry.intact or not entry.source_is_current():
            skipped.append((path.name, entry.reason, entry.rewritten))
            continue
        if source_root is not None and not belongs_to_tree(entry, source_root):
            skipped.append((path.name, "the coverage describes a source of another tree",
                            entry.rewritten))
            continue
        files.append(entry)
    return CoverageRead(files, skipped, malformed)
