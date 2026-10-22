# The protection pass — `Cs2Protect.dll`

An LLVM **pass plugin**: the payload build loads it with `-fpass-plugin=`,
and it protects the functions a policy names.

* the **selection** phase (the pipeline start) picks the functions, marks them
  `noinline` so that they survive to the late point, and reports them;
* the **transform** phase (the optimizer last) rewrites the calls of a
  selected function into **indirect dispatches through an encrypted
  descriptor**, so that the static call graph is no longer readable from the
  binary — for the callees that are **defined in the same module**
  (§10.6 says how much that is worth in the current payload: 37 of
  the 332 call sites of the selected functions, and they are predominantly
  destructor calls) and for the **cross-translation-unit** callees the
  policy's `cross_module` list names (§10.7: 14 more, for 51 in all).

The `virtualized` level (moving a body into an interpreter) is **reserved
for T20**: a policy that asks for it fails the build, because a build that
looks protected and is not is the failure mode this project exists to avoid.

```
tools/protect/
  CMakeLists.txt        the plugin build (CMake, because it needs LLVMConfig.cmake)
  README.md             this file
  check_policy.py       the whole-build check: every policy entry matched something
  check_payload.py      the check of the image the transform produced (§11)
  protect_checks.py     the image analysis and the coverage readers, shared by the
                        three consumers (the payload check, the IR test, the
                        differential test)
  compare_builds.py     the acceptance helper: compares two built payloads
  find_objdump.py       the llvm-objdump of the LLVM SDK (the payload check needs it)
  verify_plugin.py      the guard: the plugin did not change under the build
  policy.example.json   an example policy
  src/
    plugin.cpp          llvmGetPassPluginInfo + the PassBuilder wiring
    config.hpp/.cpp     the environment configuration and the policy file
    selection.hpp/.cpp  which functions are protected
    report.hpp/.cpp     what the pass reports, and the coverage file
    transform.hpp/.cpp  the call encryption
    cipher.hpp/.cpp     the cipher the transform and the virtualizer share
    strings.hpp/.cpp    the string encryption: the literals of the policy
    vm.hpp              the front end of the virtualizer: the ISA, the frame,
                        the budgets and the refusal reasons
    vm_internal.hpp     what the three front-end files share: the type and
                        the width model
    vm_gate.cpp         the eligibility gate
    vm_lower.cpp        the lowering: the IR of a function to the bytecode
    vm_encode.cpp       the encoder: the container, the encryption and the
                        per-build opcode permutation
    isa.def             the instruction set of the virtual machine: the one
                        source of truth for the opcode numbers
  tests/
    protect_tests.cpp   the plugin's own tests (CTest)
    vm_values_test.cpp  the values the bytecode computes: the container is
                        decrypted and executed in an interpreter written from
                        the contract
    ir_test.py          the shape of the rewritten IR
    diff_test.py        the differential test: the same fixture, with and without
    coverage_test.py    the readers of the coverage: a malformed file is reported,
                        the coverage of another tree is not counted, and the two
                        payload-level rules of the cross-module routing hold
    str_ir_test.py      the string protection: the literal is gone from the IR
    str_diff_test.py    the differential test of the string protection
    image_scan_test.py  the image scan of the protected strings
    vm_ir_test.py       the front end of the virtualizer: the verdict of every
                        function, and the proof that the analysis is read-only
```

---

## 1. The LLVM SDK

The plugin needs LLVM's headers and libraries. `C:\Program Files\LLVM` is a
**clangd-only** install on this machine: no `include/llvm`, no
`LLVMConfig.cmake`, no `opt.exe`, no `FileCheck.exe`. The dev SDK is the
*release tarball*:

| | |
|---|---|
| URL | <https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/clang%2Bllvm-22.1.8-x86_64-pc-windows-msvc.tar.xz> |
| SHA-256 | `d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234` |
| unpacked at | `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc` |

The build script takes the SDK from `$CS2_LLVM_SDK`, else from the default
path above, and **fails loudly** (with the URL and the digest) when it is
missing.

## 2. Building the plugin

```powershell
python scripts/build-protect-plugin.py
```

The script is idempotent and prints the path of the built DLL on stdout —
**and nothing else**: the CMake and ninja output goes to
`tools/protect/build/build-protect-plugin.log`, whose path is printed on
stderr. So the README's recipe below works verbatim:

```
C:\...\cs2-internal\tools\protect\build\Cs2Protect.dll
```

It configures `tools/protect` with CMake + Ninja, using **clang-cl** — the
same compiler that will load the plugin. The ABI and the CRT have to match,
so the plugin is built with `/MT` (the static CRT) and `/GR-` (no RTTI),
just like the release LLVM itself (`llvm-config --cxxflags` →
`-std:c++17 ... /EHs-c- /GR-`).

The export is the contract with the driver:

```powershell
llvm-readobj --coff-exports tools\protect\build\Cs2Protect.dll
#   Export {
#     Ordinal: 1
#     Name: llvmGetPassPluginInfo
#     RVA: 0x90120
#   }
```

### The plugin's own tests

The plugin is a build-time tool, so its tests live with it and run under
CTest:

```powershell
ctest --test-dir tools/protect/build --output-on-failure
#   1/1 Test #1: protect-tests ....................   Passed
```

(`ctest` is not on `PATH` on this machine: it is next to the CMake of
Visual Studio, `…\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe`.)

They cover the policy reader (a malformed policy is a *reported* error,
never a crash), the qualified name, the selection (an entry that named a
function that cannot be protected is *blocked*, not matched), the
coverage file (two modules with different identifiers write two files),
the front end of the virtualizer (the eligibility, the lowering, the
parallel copy of a PHI, the flags of the arithmetic and the refusal
reasons), the **values** the bytecode computes (the container is
decrypted with the keystream of the contract and executed in an
interpreter written from the contract) and the IR of the analysis (the
verdict of every function of a fixture, and the proof that the analysis
does not touch the IR).

There are **six** tests:

```
  1/6 Test #1: protect-tests ....................   Passed
  2/6 Test #2: protect-ir-test ..................   Passed
  3/6 Test #3: protect-diff-test ................   Passed
  4/6 Test #4: protect-coverage-test ............   Passed
  5/6 Test #5: protect-vm-values-test ...........   Passed
  6/6 Test #6: protect-vm-ir-test ...............   Passed
```

## 3. Enabling it in the payload build

The payload is a **separate Meson project**, so the options live in
`payload/meson_options.txt`:

```powershell
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$plugin='C:/absolute/path/to/tools/protect/build/Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload-protect"
```

* `protect` — off by default. When it is off, **not a single flag is added**
  and the build is byte-identical to today's build.
* `protect_plugin` — the absolute path of `Cs2Protect.dll`. The path must be
  absolute and must not contain a space or a comma: it becomes **one**
  compiler argument.
* `protect_reference` — the absolute path of the **unprotected** `payload.dll`.
  It is optional: with it, the payload check also compares the two images
  (the exports, the imports, and the "the unprotected image has no
  dispatch"); without it, the checks that need only the protected image
  still run.

A protected build runs **two** checks of its own (§11): the policy check
(every entry of the policy matched something) and the payload check (the
image the transform produced is the image the pass meant to write). Both
**fail the build** when they fail. The payload check needs the disassembler
of the LLVM SDK (`tools/protect/find_objdump.py` finds it), so the
configure step fails when it is missing.

The flag that reaches the compiler is

```
/clang:-fpass-plugin=C:/absolute/path/to/Cs2Protect.dll
```

and not `-fpass-plugin=…`: **clang-cl ignores the plain spelling** ("unknown
argument ignored in clang-cl"). The `/clang:` prefix routes the option to
the clang driver, which is where the pass plugin is loaded.

**`protect=true` needs a policy.** The configure step fails when
`CS2_PROTECT_CONFIG` is unset or names a file that does not exist:

```
ERROR: -Dprotect=true needs a policy file: set CS2_PROTECT_CONFIG=<path to a policy JSON file>
(see tools/protect/policy.example.json). A protected build without a policy protects nothing,
and that is indistinguishable from an unprotected one.
```

The *plugin* stays a no-op without a policy (the spec sanctions that); it is
the *wiring* that knows `protect` is on, so the wiring is where a silent
no-op has to be caught.

The environment variable has to be set for the **configure** step *and* for
the **compile** step: the plugin reads it in the compiler process.

## 4. The configuration channel: environment variables

A statically linked pass plugin has **its own `cl` registry**, so an option
of the plugin is *not* reachable through `-mllvm`:

```
clang-cl … -mllvm -cs2-protect-enabled=false
#   error: unknown command line argument '-cs2-protect-enabled'
```

The driver parses `-mllvm` against *its own* registry and rejects the name
before the plugin ever sees it. The plugin is therefore configured through
the **environment**, which the compiler process hands down unchanged:

| Variable | Meaning |
|---|---|
| `CS2_PROTECT_CONFIG` | path of the JSON policy file. Absent ⇒ no policy |
| `CS2_PROTECT_SEED` | hexadecimal seed. **Mandatory**: without it (and without the policy's `seed`) the plugin refuses to run |
| `CS2_PROTECT_VERBOSE` | `1` ⇒ print the skipped functions and why |
| `CS2_PROTECT_COVERAGE` | where the per-module coverage goes (default: under the temp directory) |
| `CS2_PROTECT_SOURCE_ROOT` | the tree that is being built. **The checkers read it** (the plugin does not): the coverage of another tree is not counted (§9) |

The seed precedence is `CS2_PROTECT_SEED` → the policy's `seed`. There is
**no third source**: the seed is mandatory, and the plugin refuses to run
without one.

```
[cs2-protect] the protection needs a build seed: set CS2_PROTECT_SEED to a 64-bit
hexadecimal value, or put a "seed" in the policy file. Without one the plugin falls back to a
constant of the payload (an FNV-1a over the module id), and every build without a seed ships the
same file.
LLVM ERROR: cs2-protect: the configuration is invalid
```

The fallback is not "a default", it is a **constant of the payload**: the
module id hashed with FNV-1a, the same in every build and in every version.
Two builds without a seed are byte for byte the same file, and a release
that ships it ships a signature. The gate is in `load_config()`
(`src/config.cpp`), and the payload's wiring refuses to *configure* without
one as well (`payload/meson.build`, the way it refuses to configure without
a policy): a build that looks protected and is not is the failure mode this
project exists to avoid.

The seed is what the release process sets, and **two releases must not use
the same one**: the seed fully determines the image (§16), so two builds
with the same seed are the same file. The plugin cannot check it (it has no
memory of the previous build), so the rule is procedural and the evidence is
the coverage file, which records the seed.

`CS2_PROTECT_COVERAGE` names the coverage directory outright — the escape
hatch for a CI build. The plugin uses it as it is; without it, the coverage
goes to `<temp>/cs2-protect-coverage/<sha256 of the policy path>` (the same
path `check_policy.py` computes). A value with a trailing space (cmd.exe
leaves one) is trimmed on both sides.

## 5. The policy file

```json
{
  "protect": [
    "cs2::aim::solve",
    { "symbol": "cs2::game::StateReader::*", "module": "state_reader.cpp", "level": "virtualized" }
  ],
  "cross_module": [
    "cs2::aim::DamageField::add",
    "cs2::aim::OccluderCell::lower_bound_distance"
  ],
  "deny": ["cs2::loader::self_unmap"],
  "seed": "0xA04252B187478C00A40BC6E8B04F1C9A"
}
```

* A `protect` entry is a **glob** (`*` and `?`) matched against the mangled
  symbol **and** against the demangled qualified name, so both
  `?solve@aim@cs2@@YA?…` and `cs2::aim::solve` select the same function.
* The optional `module` scope names the source file the entry belongs to.
* The optional `level` is `call` (T19 encrypts the call sites) or
  `virtualized` (T20 moves the body into an interpreter). The default is
  `call`. **`virtualized` is not implemented yet**: an entry that asks for
  it fails the build (§10.5).
* A **`cross_module` entry** is a glob too, matched the same way, and it
  names the **cross-TU callees** whose calls may be routed through the
  caller's own table (§10.7). The list is a list of plain globs: the entry
  names a *declaration* (a function with no body in the module that calls
  it), so a module scope would name the wrong thing.
* `deny` wins over every selection source.
* An unknown key is an error, not a warning: a typo in a policy file must
  not be ignored silently.
* The seed is **64 bits**. A longer seed (the example above carries a
  128-bit value) keeps its low 64 bits, and the plugin says so:
  `[cs2-protect] the policy's seed: the seed is 64 bits; the high bits of
  '0xA042…' are dropped`.
* The seed is **mandatory**: the policy's `seed` or `CS2_PROTECT_SEED`, one
  of the two (§4). The seed drives the protection's constants, its keys and
  the layout of the image (§16).

### How the file is read

The reader is the **SDK's** (`llvm::json::parse`, `llvm/Support/JSON.h`):
`LLVMSupport` is linked anyway, and a hand-rolled parser is a parser to
maintain. Two checks sit on top of it:

* **The nesting depth is bounded (64 levels).** The SDK's parser recurses,
  so a policy file with 50 000 nested `[` would overflow the compiler's
  stack and kill the *compiler* with an access violation and no message at
  all. The depth is measured before the text reaches the parser, so the
  result is a reported error:

  ```
  [cs2-protect] CS2_PROTECT_CONFIG: policy.json: the policy file is nested too deeply (50000 levels, the limit is 64)
  ```

* **A duplicated key is an error.** The SDK's parser keeps the *first* of
  two equal keys, Python's `json` keeps the *last*: without a check the
  plugin and `check_policy.py` would disagree about what the policy says.
  Both reject it, with the same message:

  ```
  check_policy: policy.json: the policy file has a duplicate key 'protect'
  ```

A UTF-8 byte order mark (some Windows editors write one) is accepted and
skipped.

### A `protect` entry that matches nothing is an error

A pass sees **one translation unit at a time**, so it cannot know whether an
entry matched *somewhere* in the build. Two mechanisms close that gap:

* an entry **scoped to a module** that matches nothing *in that module*
  fails the compile right away (`LLVM ERROR: cs2-protect: a policy entry
  matched no function`);
* the build as a whole checks the coverage after the payload was linked
  (`tools/protect/check_policy.py`, wired into `payload/meson.build`): an
  entry that matched in no module at all fails the build.

### A `protect` entry that matches a function that *cannot* be protected

An entry that names a function the pass cannot protect — an `alwaysinline`
function, a declaration, a compiler intrinsic, a `deny`-listed function —
protects **nothing**. That is not a match, and the build says so:

```
[cs2-protect] solver.cpp: policy cs2::aim::solve blocked reason=always_inline
LLVM ERROR: cs2-protect: a policy entry matched a function that cannot be protected
```

For an entry **scoped to a module** the compile fails right away; for an
entry without a scope the whole-build check fails it:

```
check_policy: policy.json: 1 policy entry matched a function that cannot be protected:
  cs2::aim::solve (always_inline)
```

### A `cross_module` entry that is not routed is an error

A `cross_module` entry names a **declaration** of the payload's own code:
a function that is defined in another translation unit and called from a
protected function. The entry that named nothing, or whose call could not
be routed, protects nothing — the same rule the `protect` entries follow:

```
check_policy: policy.json: 1 cross_module entry named a declaration whose call could not be routed:
  cs2::aim::DamageField::add (variadic_callee)
check_policy: policy.json: 1 cross_module entry matched no declaration:
  cs2::aim::DamageField::add
```

The **priority** of the states is the opposite of the `protect` entries':
a `cross_module` entry has to be routed *everywhere* it is called, so a
call that could not be routed in one module outranks a routing in another
one. The aggregation over the modules is therefore "the worst state
wins", with the worst being `blocked` > `matched` > `unmatched`.

### The `strings` section: the literals that are protected

The `protect` entries name **functions**. A string is not a function, so the
strings get their own section:

```json
{
  "strings":       [ "box", "health_bar", "cs2_*" ],
  "strings_deny":  [ "CS2_*" ],
  "strings_min_length": 4
}
```

* a `strings` entry is a **glob** (`*` and `?`) matched against the
  **content** of the literal (an exact string is a glob with no wildcard);
* a literal is selected when it matches one of the `strings` globs and
  **none** of the `strings_deny` globs;
* a literal shorter than `strings_min_length` is refused (`too_short`); the
  default is **four** -- a three-byte string carries too little to be worth
  a decode, and the operator can lower it;
* **nothing is protected unless the section says so**: the default is
  "protect no string", because a surprise is worse than a gap.

The strings are not a `ProtectionLevel`: they are not functions, they do not
go through the selection phase's function machinery, and they get their own
selection step in the transform phase. What the pass does with them is
described in **§15**.

## 6. The other selection source: the annotation

```cpp
__attribute__((annotate("cs2.protect"))) void cs2::aim::solve(…) { … }
```

The annotation lands in `llvm.global.annotations` and is keyed by the
**mangled** symbol name. It also keeps the annotated function alive (no DCE,
no inlining) — which is what makes it usable as a selection source.

## 7. The report

The selection phase prints one line per selected function to stderr:

```
[cs2-protect] solver.cpp: protect ?solve@aim@cs2@@YA?AUSolveResult@12@… (cs2::aim::solve) source=policy level=call seed=0xa40bc6e8b04f1c9a noinline=added
```

* `source` — `annotation` or `policy`.
* `level` — `call` or `virtualized`.
* `seed` — the effective seed of the function.
* `noinline=added` — the pass added the `noinline` attribute, so that the
  function survives to the late point. The marking **changes the generated
  code**, so the report has to name it: the difference between the protected
  and the unprotected build is attributable to the selected functions (and to
  their callers, which no longer inline them) and to the new globals.
* `level-not-implemented` — the entry asks for a level that this task does
  not implement (T20). The build fails for it.

The transform phase prints what it did:

```
[cs2-protect] solver.cpp: transform ?solve@aim@cs2@@YA?AUSolveResult@12@… (cs2::aim::solve) rewritten=14 skipped=81 indirect_parameter=9 external_callee=26 intrinsic_callee=44 invoke=2 cross_module=7
[cs2-protect] solver.cpp: transform table=14 descriptor=18 mixmul=0x1f8b3c5d7e9a2b4c mixxor=0x8e3d5f7a9b1c3d5e rotate=27
[cs2-protect] solver.cpp: layout functions=69 moved=66 seed=0xf2ba62bd9b1ff889
```

* `rewritten` — the calls that became indirect dispatches.
* `skipped` — the calls that were left alone, **per reason**:
  `variadic_callee`, `indirect_parameter`, `musttail`, `external_callee`,
  `optnone_callee`, `invoke`, `indirect_callee`.
* `cross_module` — the rewrites that were **cross-TU**: the calls the
  policy's `cross_module` list routed through the module's own table.
* `table` and `descriptor` — the size of the two globals the module gained.
* `mixmul`, `mixxor` and `rotate` — the **mixer of the build**: the
  multiplier, the xor constant and the rotation the module's calls decode
  with. They are per build (§10.2), and the report names them because the
  payload check compares them with the image.
* `layout functions=… moved=… seed=…` — the **permutation of the module's
  function list** (§16): how many definitions the module has, how many of
  them moved, and the seed the permutation came from.

A selected function that did not survive to the late point is reported as
`vanished`, and a level that is not implemented is reported as
`level=virtualized not-implemented`.

The string phase prints what it did with the literals the policy selected:
the ones it protected **in this module** and the ones it refused, and why
(§14.6). It names the content only with `CS2_PROTECT_VERBOSE=1`.

With `CS2_PROTECT_VERBOSE=1` the pass also prints every function it
skipped and why (`not_annotated`, `denied`, `optnone`, `always_inline`,
`declaration`, `intrinsic`), every skipped **call** of the transform (with
its callee), the state of every policy entry (`matched`, `blocked`,
`unimplemented`, `vanished`, `unmatched`), the state of every
`cross_module` entry, the cross-TU callees that stayed direct, and a
summary line per module.

The analysis of the virtualizer has a line of its own (§14):

```
[cs2-protect] solver.cpp: vm ?solve@aim@cs2@@YA?AUSolveResult@12@… (cs2::aim::solve) refused reason=eh_pad detail=the function has an EH edge
[cs2-protect] solver.cpp: vm ?newest@RecordRing@aim@cs2@@QEBAPEBURecord@23@XZ (cs2::aim::RecordRing::newest) virtualizable slots=7 frame=3072 bytecode=392 instructions=15 calls=2
```

* `virtualizable` — the function could be virtualized, and the numbers say
  what it would cost: the slots, the frame, the bytecode, the instructions
  and the calls.
* `refused reason=… detail=…` — the function cannot be virtualized, and the
  reason says why. The gate fails **closed**: a function that fails one
  check is left completely untouched.

## 8. What the pass does **not** do

* It does not **virtualize** anything. The `virtualized` level is T20; a
  policy that asks for it fails the build (see §10). What it does is
  **analyse**: it says whether a function *could* be virtualized, and what
  it would cost (§14).
* It does not touch the calls of a function that is not selected, and it
  does not touch a call it cannot rewrite (§10).
* It runs from `-O1` up. At `-O0` the driver uses its O0 pipeline, which
  does not invoke the pipeline-start extension point, so the pass does not
  run at all.
* It does not change the build when `protect` is off.

## 9. The coverage directory

The coverage is one file per **(policy, module)**:

* the **policy digest** is part of the file name, so the coverage of one
  policy is never mistaken for the coverage of another;
* the **module's identity inside the source tree** is part of it, so two
  sources never write the same file.

The identity is the path of the source file *relative to the root the policy
file and the source file share* — `payload/aim/src/solver.cpp` for the
payload, when the policy lives in `tools/protect/`. It does **not** depend
on the build directory, so a rebuild overwrites the file instead of leaving
a stale copy behind.

When the policy lives **outside** the tree — the local reality, where the
policy of one worktree is used to build the sources of another — the root
the two share is the parent of both, and the identity is
`cs2-wt/<worktree>/payload/aim/src/solver.cpp`: it names the worktree. Two
worktrees that share the policy then write **two files for one module into
the same coverage directory** (the directory is keyed by the policy), and
counting both would count every rewrite twice: the payload check would
report that the image has half the dispatches the pass wrote — a false
*"the compiler folded the dispatch away"* on a perfectly good image.

The readers therefore only count the coverage files whose source belongs to
**the tree that is being built**: `--source-root` (the build passes it;
`CS2_PROTECT_SOURCE_ROOT` is the fallback for a manual run). The coverage
of another worktree is reported and **not** counted. Without the source
root the readers count every file of the policy, which is what the check
did before — and which is the double count above.

The file carries the **digest of the source file** and a **digest of its own
content**, so that a reader can tell a coverage file of *this* build from
one of another build: a file whose source has changed since the compile, or
a file that was edited by hand, is **skipped** (and reported) instead of
being counted.

The content digest is an **unkeyed** SHA-256: it catches an *accidental*
corruption — a truncated write, a file that was edited by mistake — and
nothing else. A file that was edited **on purpose** can be re-digested, so
the digest is not a defence against a hostile editor: the coverage is
build evidence, not a security boundary. (The *source* digest is what
binds the coverage to the source: it is what tells a file of another state
of the source from a file of this one.)

The directory name is the **upper case** hexadecimal of the SHA-256 of the
policy path: the plugin spells it `llvm::toHex` (upper case by default)
and `check_policy.py` spells it `hexdigest().upper()`, so the two name the
same directory on a file system whose case matters.

The path is **normalised** before it is hashed: the separators are turned
into forward slashes, so that the plugin and the checkers compute the same
directory whatever the spelling of `CS2_PROTECT_CONFIG` is (`C:/a/b.json`
and `C:\a\b.json` name the same directory). Without that, a path spelled
with forward slashes -- the spelling Meson uses -- put the plugin's
coverage in one directory and the checker's lookup in another, and a fresh
protected build failed with *"no coverage file of this policy describes
this build"*.

The directory is keyed by the policy path (or `CS2_PROTECT_COVERAGE`), so an
edited policy leaves the files of the previous version behind. The checker
**prunes** them after it has read them — the files of the *current* policy
are left alone, so a rebuild that compiles nothing still finds its
evidence; a file of the current policy that does not describe the current
build (an older plugin wrote it, or it was edited by hand) is pruned too,
because it is not evidence about anything. The pruning runs after the
build, so it cannot race with a compile that is writing its coverage.

The state of an entry is one of:

| state | meaning |
|---|---|
| `matched` | the entry named a function that is protected |
| `unmatched` | the entry named no function in this module |
| `blocked:<reason>` | the entry named a function that cannot be protected |
| `unimplemented:<level>` | the entry named a function whose level is not implemented yet |
| `vanished` | the entry named a selected function that did not survive to the transform phase |

The worst state wins, so that a failure is never masked by a match in
another module.

The `protect` line names what the call site decodes with: the size of the
table, the size of the descriptor, the **mixer of the build** (the
multiplier, the xor constant and the rotation) and the three parts of the
key. The `layout` line names the **permutation of the module's function
list**: the number of the definitions, the number of them that moved, and
the seed it came from. Both lines are **new**: a coverage file of an older
build has no `layout` line and no `rotate=` field, and a reader that does
not know them still reads the file (`tools/protect/protect_checks.py`).

The `cross_module` entries have their **own** vocabulary and their own
priority, in their own lines:

| line | meaning |
|---|---|
| `cross matched <pattern> <symbol>` | the entry named a declaration and every call to it was routed |
| `cross blocked:<reason> <pattern> <symbol>` | a call to the declaration it named could not be routed |
| `cross unmatched <pattern>` | the entry named no declaration in this module |
| `direct <symbol> <reason>` | the cross-TU callee that stayed direct, and why |

The `direct` lines name the **distinct** cross-TU callees that a protected
function still calls directly: the payload check compares them with the
symbols the payload's own libraries define (§11).

The `direct` lines are **not** only about the payload's own code: they
name every declaration a protected function calls directly, and that
includes the CRT's (`atan2f`, `operator delete`, `_Query_perf_counter`).
Those are imports, the payload does not own them, and the check counts them
as "left alone" (§11).

The two extra fields — the symbol of a `cross` line and the reason of a
`direct` line — are **not** in the parent specification's example of the
format (`cross <state> <pattern>` and `direct <symbol>`). The readers
accept both: the two fields are optional, so a coverage file written in
either spelling is read.

The `fn` line carries one more key, `cross=`: the number of the cross-TU
rewrites of that function.

```
fn ?solve@aim@cs2@@… rewritten=14 skipped=81 cross=7
```

The readers of the coverage pick the keys they know and ignore the
others, and the new lines are **new**: a coverage file of an older build
has none of them, and a reader that does not know them still reads the
file.

A source that is compiled into **two targets of the same build** would write
the same file. The coverage of a module does not depend on the target (the
policy's `module` scope names the *source* file), so the content is the
same; the plugin writes the whole file in one go, so two such compiles
cannot interleave their writes.

## 10. The transform: the call encryption

### 10.1 The two phases

The transform needs fully optimised IR, and the selection has to happen
before the optimiser runs, so the pass has **two** extension points:

| phase | extension point | module flag | job |
|---|---|---|---|
| selection | `registerPipelineStartEPCallback` | `cs2.protect.selected` | select the functions, mark them `noinline`, record the selection |
| transform | `registerOptimizerLastEPCallback` | `cs2.protect.transformed` | analyse the selected functions for the virtualizer (§14), rewrite the calls |

The flags are **per phase**: one flag would make the late phase bail out,
because the early phase would have set it. They are module flags with the
`Warning` behaviour, so an LTO merge of two modules that both carry them is
quiet, and the pre-link phase does not transform a module twice.

The transform has to run at the optimizer last: the optimiser's
`SimplifyIndirectCalls` would rewrite the encrypted indirect call straight
back into a direct one, and the inliner would have eaten the selected
function before a late point could rewrite it.

The two phases hand over through the module's metadata
(`!cs2.protect.functions` and `!cs2.protect.entries`): the early phase
records the selection, the late phase reads it back. That record is also how
the late phase notices a selected function that **did not survive** the
optimiser (it is reported, and the whole-build check fails for it).

The pass is also reachable **by name**, so that `opt` can run it over a
fixture:

```
opt.exe -load-pass-plugin=Cs2Protect.dll -passes=cs2-protect -S fixture.ll
```

and the analysis of the virtualizer alone, under the name
`cs2-protect-vm` (§14.9):

```
opt.exe -load-pass-plugin=Cs2Protect.dll -passes=cs2-protect-vm -S fixture.ll
```

### 10.2 The table, the descriptor and the key

For every module that has at least one rewritten call, the pass adds two
globals:

```
@cs2.protect.table   = private unnamed_addr constant [N x ptr] [ptr @callee_0, …]
@cs2.protect.desc    = private unnamed_addr constant [M x i64] [i64 <cipher>, …]
@cs2.protect.key0   = private unnamed_addr constant i64 <part>
@cs2.protect.key1   = private unnamed_addr constant i64 <part>
@cs2.protect.key2   = private unnamed_addr constant i64 <part>
@cs2.protect.mixmul = private unnamed_addr constant i64 <the multiplier of the build>
@cs2.protect.mixxor = private unnamed_addr constant i64 <the xor constant of the build>
```

* the **table** holds the real pointers. The linker emits the DIR64 fixups
  for them and the mapper applies them, so the entries are right at run time.
  The order is **permuted per build** (a build-seeded permutation), so the
  index in the descriptor is not a predictable table position and the table
  does not have the same shape in two builds.
* the **descriptor** holds the *encrypted* index of every rewritten call
  site: `index ^ key`. It holds plain integers only — a relocatable value
  inside the cipher would be corrupted by the fixups of the mapper.
* the **key** is split into three constants in three globals, and the
  call site mixes them at run time (the mixer is inline IR, a few
  `xor`/`mul`/`rol` on `i64`; there is no call to a shared helper, because
  that would add a stack frame to every call site);
* the **constants of the mixer** live in globals as well, and **not** in the
  instruction stream. That is not a style choice: the X86 backend of this
  LLVM (22.1.8) **mis-materialises a 64-bit immediate** in the shape the
  transform emits — `xor i64 %mul, 0xBF58476D1CE4E5B9` becomes
  `movabsq $0xBF58476C1CE4E5B9`, one bit of the constant lost. The dispatch
  still reached the right callee, but only because the lost bit happened to
  be the sign bit of the index and the table's stride is 8 bytes: three
  coincidences. A constant in a global is written by the object writer, so
  it reaches the image bit for bit, and the payload check asserts that the
  image holds the constants the pass encrypted the descriptor with (§11).

### The mixer is per build

The multiplier, the xor constant and the **rotation** of the mixer are
**derived from the module's seed**, the way the parts of the key already
are (`src/cipher.hpp`: `mixer_for(seed)`). They were fixed constants until
T24, and they were the protection's **only fixed bytes**: sixteen bytes at
the same four addresses in every build and in every version — a signature an
analyst reads in one instruction.

The three values are not treated alike, and the difference is deliberate:

| value | where it lives | why |
|---|---|---|
| the multiplier | a global, read with a **volatile** load | a 64-bit immediate in the instruction stream can be mis-materialised (§10.2) |
| the xor constant | a global, read with a **volatile** load | the same |
| the **rotation** | the instruction stream (`rolq $imm8`) | it is the one value that has to be in the code, and it is what makes the `.text` of two builds differ |

The rotation is in **[1, 63]**: a rotation of 0 would make the `lshr` by 64
of the decode undefined behaviour, and a rotation of 64 is not a rotation.
The decode emits `shl` by the rotation and `lshr` by 64 minus the rotation,
and the backend folds the pair into a single `rolq $imm8`.

**What it does not buy.** It hides nothing: the analyst reads the rotation
out of the instruction stream in one instruction, and the mixer out of the
globals. It buys **the absence of a signature**, which is what the task is
for.

**The coupling this creates.** `mix64()` is shared by the call encryption
and by the virtualizer's encoder, and the mixer is a **per-build** function
now, so its rotation and its constants travel with the module's seed. The
encoder derives them from the same seed (§14.5).

**The seam for the interpreter (T20b, not written).** The interpreter has to
decode with the mixer the pass encrypted with, and the values have to be
**in the image**, in a form a runtime component can read. Today they are not,
and the decision is where they go:

* The **rotation** is an *immediate* in the instruction stream. Nothing in
  the image holds it as a value a runtime component can read.
* The **multiplier** and the **xor constant** are in the module's globals
  (`cs2.protect.mixmul`, `cs2.protect.mixxor`), and those globals have
  `PrivateLinkage`: they are local to the object, they are not in the
  image's symbol table, they are not exported, and there are **four
  different mixers in one image** (one per protected module).
* The **coverage** names the mixer of every module, but the coverage is a
  file on the build machine. It is not in the image, and a runtime
  component cannot read it.

**The decision: the pass emits the interpreter into the same module.** The
interpreter is not a library that is linked in and looks the mixer up: it
is emitted by the pass, into the module whose bytecode it decodes. It can
therefore reference that module's private globals directly, and it carries
the rotation as **its own immediate** — the same value the call site's
decode uses. The seam is therefore: *the pass emits the interpreter, and the
interpreter is emitted with the mixer values it must use*. A runtime
component that is not in the module does not have to find the mixer: it is
not a runtime lookup, it is a value the emitter puts there.

**The container's header cannot carry the mixer.** The obvious place for a
per-module mixer would be the header of the VM container, and it is the one
place that cannot hold it: the container is encrypted **as a whole, the
header included** (`src/vm_encode.cpp`: "the whole container, the header
included, so that an analyst reads nothing but noise"). A mixer stored in
the header would be a chicken-and-egg: the interpreter needs the mixer to
*read* the header. The header must not be expected to carry the mixer, and
T20b must not be planned around it.

The call site becomes:

```
  %i = <the inline decode>                       ; the index
  %p = getelementptr ptr, ptr @cs2.protect.table, i64 %i
  %f = load ptr, ptr %p
  %r = call <the original function type> %f(<the original arguments>)
```

The GEP is **not** `inbounds`: the index is a value the image computes at
run time, and the pass has no right to promise that it is inside the table.
The promise would make an out-of-range index **poison**, and the poison
would spread into the call; without it, an out-of-range index is a load
from a wrong address — still undefined behaviour by the letter of the
reference, but a *wrong address* and not a poison value. The index cannot
be out of range with a correct descriptor, and the payload check verifies
that for the built image; the IR itself does not assert it.

The **function type is unchanged**: the call still has the callee's
parameter types, its return type, its calling convention and its
`AttributeList`. The ABI is preserved by construction — the call becomes an
*indirect* call with the same signature.

**What the key buys, and what it does not.** A scanner that greps for a
64-bit constant does not find the key: it has to reconstruct it from three
constants and a mixer. It does **not** buy security against an analyst
who recognises the mixer — that one recovers the key in minutes. This is
obfuscation, not cryptography.

### 10.3 The volatile barrier (and why it is load-bearing)

The key is a **compile-time** key, so the whole dispatch is a compile-time
constant: the index is a constant, the load from the table folds to the
callee's address, and the indirect call becomes a **direct** one again.

That is not theoretical. Without a barrier, the optimiser folds the
dispatch away in most of the payload's modules: the protected image then
behaves exactly like the unprotected one, and the protection is void — the
exact failure mode this project exists to avoid.

So the decode reads the key, the constants of the mixer and the
descriptor with **volatile** loads — six of them. A volatile load is the
cheapest barrier that keeps the image read-only (a writable table would be
patched at run time), and it is also what keeps the mixer constants in the
data of the image instead of the instruction stream (§10.2). The
`ir_test.py` test asserts that the six volatile loads are still there, and
`check_payload.py` asserts that the number of the encrypted dispatches in
the image is the number of the calls the pass reports having rewritten.

**What it costs.** A protected call site decodes with eight instructions and
six memory reads (the three key constants, the two mixer constants and the
descriptor), against the one instruction of the direct call it replaces. The
table's address is hoisted out of the loop, the constants are not: they are
volatile. The *shape* (eight instructions, six reads) is what the IR test
asserts, and it is what the cost consists of. The *time* it costs depends on
the load of the machine and on the call the dispatch guards, and **this
repository does not measure it**: there is no benchmark in it, and a number
that cannot be reproduced from the repository is worse than no number.

### 10.4 The skip rules

A call is **not** rewritten when any of these holds, and every skip is
reported (with its reason):

| skip | why |
|---|---|
| `variadic_callee` | the indirect call cannot reproduce the variadic ABI |
| `indirect_parameter` | the callee has a `byval` / `sret` / `inalloca` / `preallocated` parameter: Windows x64 rejects these in an indirect signature |
| `musttail` | a tail call cannot become an indirect call without breaking the contract |
| `external_callee` | the callee has no code in this module: it is an import, an intrinsic or an `available_externally` function (the last one would resolve to address zero in the table) — **unless the policy's `cross_module` list names it** (§10.7) |
| `intrinsic_callee` | the callee is a compiler intrinsic: the backend expands it, there is nothing to call |
| `invoke` | the pass does not model the EH edges yet, unless the policy's `cross_module` list names the callee (§10.7) |
| `optnone_callee` | rewriting it would defeat the `optnone` intent |
| `indirect_callee` | the call already goes through a pointer: there is nothing to hide |

A call is either **fully** rewritten or **fully** untouched. Never half.

### 10.5 The level gate

The policy carries a level per entry: `call` (this task) or `virtualized`
(T20). A `virtualized` entry names a function that this task does **not**
transform, so:

* the function is selected (the selection is the same), but the pass reports
  it as *not implemented*;
* the entry is **not** counted as protected, and the whole-build check
  **fails** for it (a module-scoped entry fails the compile right away).

`policy.example.json` therefore asks for the `call` level; the `virtualized`
level is reserved for T20.

The level that applies to a function is the **strongest** level any of the
entries that name it asks for: a function that one entry wants virtualized
is not transformed at the call level, so the call entry must not claim it
either. The state of an entry in the coverage follows the level that was
*applied* — an entry that named a function that was not transformed is not
reported as *matched*.

### 10.6 What the rewrite is worth (and what it is not)

The rewrite applies to the calls of a callee that is **defined in the same
module** (the specification's §2.4) and to the calls of a callee that is
defined in **another** module and that the policy's `cross_module` list
names (§10.7).

In the current payload, with the policy of `tools/protect/policy.local.json`:

| selected function | rewritten | of them cross-TU | skipped |
|---|---|---|---|
| `cs2::aim::solve` | 14 | 7 | 81 (9 with an `sret` parameter, 2 `invoke`, 26 cross-module, 44 intrinsics) |
| `cs2::aim::occlude_polygon` | 27 | 6 | 140 |
| `cs2::aim::solve_backtrack` | 4 | 0 | 15 |
| `cs2::aim::project_capsule` | 3 | 0 | 45 |
| `cs2::aim::DamageField::hit_probability` | 3 | 1 | 0 |
| `cs2::aim::penetration_survival` | 0 | 0 | 0 |
| **total** | **51** | **14** | **281** |

So **51 of the 332 call sites** of the selected functions became indirect
dispatches, **14** of them the cross-TU edges that carry the aim call
graph. The cross-TU edges that are **still direct** are the ones the
`sret` rule refuses (11 functions, 12 call sites): the rule is not narrowed
by this task, and the payload check names them (§11).

The edges that are still visible are listed in §10.8.

### 10.7 The cross-module routing

A call to a **declaration** — a function with no body in the module that
calls it — is normally refused with the reason `external_callee`: at the
IR level an import and an internal cross-TU declaration are the same
thing, and the pass cannot tell them apart by looking at the module.

The policy's **`cross_module`** list is what tells them apart. An entry is
a glob, matched exactly like a `protect` entry: against the mangled symbol
and against the demangled qualified name (`qualified_name()` in
`src/selection.cpp`). `cs2::aim::DamageField::*` covers every member of
the struct in one line.

The rule is:

* the entry is matched against a **declaration** in the module (a function
  with no body here). A same-module callee needs no entry: it is routed by
  the ordinary rule;
* a call to a declaration the list names goes into the **caller's own**
  table, and the call becomes an indirect dispatch through the encrypted
  descriptor, exactly like a same-module callee;
* the table entry is the **symbol itself** — `ptr @?project_capsule@…` —
  and not a copy of its address. The object writer emits an
  `IMAGE_REL_AMD64_ADDR64` relocation against the external symbol, the
  linker resolves it inside the DLL, and the mapper applies the DIR64
  fixup. The table therefore holds a **relocatable pointer**, and the
  image base is never baked into anything;
* the **descriptor** still holds plain integers: the index of the entry,
  `index ^ key`. Nothing about the cipher changes, and the permutation of
  the table applies to the cross-TU entries too;
* every other rule still applies. A cross-TU callee that is variadic, or
  that has a `byval` / `sret` / `inalloca` / `preallocated` parameter, is
  still refused — with the reason it always had.

**The EH edges.** The `invoke` rule does not hold for a declaration the
list names. The rule exists because the pass does not model the EH edges,
and the rewrite does not model them either: it replaces the **callee** and
leaves the landing pad and the unwind edge exactly as they are. In the
current tree, four of the fourteen cross-TU edges are EH edges
(`DamageField::add`, `DamageField::add_hitbox`, `DamageField::clip` and
`subtract_convex` may throw), and the measurement that counted them as
ordinary calls could not tell them apart: the rule order reports a call
to a declaration as `external_callee` whether it is an `invoke` or not. The
cross-TU graph cannot be closed while they stay direct.

**The list is the operator's data.** It is the operator's declaration of
"these cross-TU edges must not be readable", and the payload check is the
proof of the part of it that the pass can reach: the check compares the
cross-TU callees that stayed direct with the symbols the payload's own
libraries define (§11).

**What the declaration covers, and what it does not.** The routing rewrites
the calls **inside the protected functions**. The *same* symbols are still
called **directly** by the functions the pass does not protect, and those
edges are still in the image. The `cross_module` list is therefore a
declaration about the **edges of the protected functions**, not about the
symbols: `polygon_contains` is unreadable as a *call of a protected
function*, and it is a plain direct call in `DamageField::value_at`, which
the pass does not touch. The payload check counts the direct calls that
are left and prints the number (§11), so the operator sees what the
routing does not hide.

### 10.8 What is still visible

Stated plainly, because the claim of the protection must not be stronger
than what it is:

* **The inbound edges.** The callers of the protected functions — the
  control service and the trigger — call them **directly**. The transform
  only rewrites the calls *inside* a selected function.
* **The outbound edges of the *unprotected* functions.** The pass rewrites
  the calls of the selected functions only, so a cross-TU aim function that
  an unprotected function calls — `polygon_contains` from
  `DamageField::value_at`, for instance — is still a **direct call** in the
  image. The `cross_module` list is a declaration about the edges of the
  protected functions; it does not make the *symbols* unreadable, and the
  call graph of the whole payload still has these edges in it. The payload
  check counts them and prints the number (§11): in the current tree, **46**
  of them are left — 40 in `field.cpp`, one in `damage.cpp`, two in
  `geometry.cpp`, two in `montecarlo.cpp` and one in `occlusion.cpp`.
* **The `sret` edges.** Eleven cross-TU callees return a big aggregate, so
  the call carries an `sret` parameter and the indirect call cannot
  reproduce it. The rule is not narrowed by this task; the payload check
  names the eleven and does not fail for them.
* **The same-module EH edges.** The pass does not model the EH edges of
  the calls it does not route, and the `invoke` edges of the same-module
  calls stay direct.
* **The intrinsic calls.** `llvm.lifetime.*`, `memset`, `memcpy`, `trap`,
  `fmuladd`, `sqrt`, `smax`: an intrinsic has no body, the backend
  expands it, and it carries no call graph of the payload's code.
* **The CRT calls.** `operator new` / `operator delete`,
  `_Query_perf_counter`, `_Query_perf_frequency`, `atan2f`, `asinf`,
  `powf`: they are in the import table, or they are the CRT's own code.
* **The callee set.** The table is a flat list of addresses in `.rdata`.
  An analyst who finds the table reads the **set** of functions a module
  can reach. What this task hides is the **edge**: which call site calls
  which callee. The code of the callees is still there.
* **The run-time cost of the dispatch** is not measured by this repository
  (§10.3): there is no benchmark in it, and a number that cannot be
  reproduced from the repository is worse than no number.

## 11. The payload-level checks

`check_payload.py` checks the image that the transform produced:

```
python tools/protect/check_payload.py --protected build-payload-protect/payload.dll \
    --unprotected build-payload/payload.dll --objdump <llvm-objdump.exe> \
    --coverage <the coverage directory> --policy <the policy file> \
    --source-root <the tree that is being built>
```

* the **exports** and the **imports** are the same as the unprotected ones
  (only with `--unprotected`);
* the `.reloc` section is there, with DIR64 entries (the mapper applies
  them);
* the exception directory (`.pdata`) is there and well formed (the mapper
  registers the unwind table and treats its absence as fatal);
* the **constants of the mixer** are in the read-only data of the image, and
  hold exactly the values the pass encrypted the descriptor with. They are
  **per build**, so the check reads them from the coverage — the coverage is
  the contract ("the value the pass encrypted with");
* the **rotation** of the mixer in the image's instruction stream is the one
  of **its own module**: the check reads the rotate in front of every
  dispatch and compares it with the `rotate=` of the coverage of the module
  the dispatch belongs to (the modules of one build have different mixers,
  so "one of the rotations of the build" would be a one-in-four test). The
  rotation is the only per-build value of the protection that is in the
  code, and it is in [1, 63]. The X86 backend spells it in **three** ways
  -- `rolq $imm8` for a rotation of 2 to 62, and `rolq %rax` (a rotation of
  1) and `rorq %rax` (a rotation of 63), which have **no immediate at all**
  -- and the check has to read all three: a reader that knows only
  `rolq $imm8` cannot see two of the sixty-three legal rotations, and the
  build of such a seed fails its own check;
* the **layout** of every module with more than one definition moved: the
  coverage names how many functions the pass permuted, and a module that
  moved none is a module the pass did not permute;
* the number of the **encrypted dispatches** is the number of the calls the
  pass reports having rewritten (read from the coverage files), and it is
  zero in the unprotected image;
* every dispatch **decodes to a callee of the image**: the descriptor,
  decoded with the key the pass used, names an entry of the table of the
  image, and that entry is the code of the image;
* **no protected function calls the payload's own code directly**: the
  cross-TU callees that stayed direct (the coverage's `direct` lines) are
  compared with the symbols the payload's own libraries define;
* every `cross_module` entry of the policy names a symbol the payload
  still defines: an entry that names a symbol that is gone is a **stale**
  entry, and it protects nothing;
* the **direct calls the payload's own objects still make to the
  `cross_module` symbols** are counted: the routing rewrites the calls of
  the *protected* functions, and the calls of the functions it does not
  protect stay direct. The count is a **measurement**, not a rule -- the
  direct calls of the unprotected functions are the payload's business --
  and the operator has to see it: it is what the `cross_module` list does
  *not* hide (§10.8);
* the **plaintext of the protected strings** is looked for in the protected
  image (and in the unprotected one): the coverage carries the length and
  the salted digest of every protected literal, and the check hashes the
  candidates of the image and compares. The scan is at **every offset** of
  every NUL-terminated run of the image -- a literal that is in the image
  as a substring *is* in the image, and the MSVC ABI merges a literal with
  a longer one that ends with it -- and the check reports the number of
  literals it **verified** absent and the ones that are still there with
  their coverage hash (§15.7). The same scan runs on the unprotected
  image, so that the comparison cannot pass vacuously, and so that the
  verdict does not depend on the layout of the image.

  The scan also looks at the windows that **span** a NUL: a literal whose
  content holds an embedded NUL is not inside a run (the run is cut at the
  NUL), and a scan that only looks at the runs calls it absent -- the same
  mistake the prefix-only scan made, one level down. Only the NULs with a
  printable byte on both sides are considered, because the image holds half
  a million NUL bytes and the binary ones (padding, GUIDs) cannot split a
  string.

The scan is the **expensive** part of the check: it hashes every substring
of the image at the length of every wanted literal, so it costs about a
minute on the payload image (2.4 MB, 17 lengths). That is the price of
a predicate that cannot be fooled by the shape of the image, and the check
is part of every protected build.

The last check is the one the specification asks for ("the pass rewrites
calls, so a mistake shows up as a wrong call target, not as a crash. The
test must catch that"): a count does not catch a dispatch that reaches the
*wrong* function, and a wrong function does not crash.

What it proves is exactly what it says: the descriptor, decoded with the key
the pass used, names an entry of the table of the image, and that entry is
code of the image. It does **not** compare the entry with the callee of the
original call site (the plugin does not record it), and it does not check
that the arithmetic of the image is the one the check assumes: a backend
that reordered the arithmetic, or a table that held the wrong function,
would pass it. The chain is sound today (the pass encrypts the descriptor
with the key the check decodes it with, and the key is in the image), but
the check is a *plausibility* check on the callee, not a proof of it.

A dispatch is counted by its **own signature**: an indirect call through the
table of the transform, or the tail jump the backend emits when the call is
the last thing a function does (the decode is right before it, and a switch
table jump has no decode before it). The unprotected image has none.

The **table** of a module is the one the dispatch calls through: the
register the dispatch uses is tracked back to the `leaq` that loaded it,
and that address is the table of the module. The tracking is the
**primary** source, because it keeps the link between a dispatch and *its
own* table.

The **content** scan is the fallback, for the sites where the tracking
fails: the register allocation is the backend's business, and a linear
scan of the disassembly does not model the control flow (a table loaded in
a loop head is used by a dispatch that the scan reaches by another path,
and a tail jump copies the table's address into another register). The
scan looks for a run of `table_size` code addresses in the read-only data
whose **descriptor** follows it (the linker may leave a gap between the
two) and whose descriptor decodes with the key of the module. The run may
be **longer** than the table -- the linker may put two tables next to
each other, or a global that holds a code address right after the table --
so every start of the table inside the run is tried, and the descriptor
that follows it decides which one it is.

The check names the case the table entry is an **import's IAT slot**
instead of the function: the entry does not hold the callee, the dispatch
calls the slot, and a `cross_module` entry must not name an import.

### The table of a module, and the payload's own code

Two checks need to know what the payload's own code is, and they read it
the same way: the SDK's **`llvm-nm`** over the payload's own static
libraries — the ones in the build directory the check runs in. The
`llvm-nm` is in the same directory as the `llvm-objdump` the check is
given, so **no build wiring changes**.

* **The direct calls.** For every symbol in the coverage's `direct` lines
  the check asks: is this symbol **defined in one of the payload's own
  libraries**? A symbol the payload defines is the payload's own code, and
  the edge is still a direct call in the image: the build **fails**, with
  the name of the symbol and the reason it stayed direct. A symbol the
  payload does not define (the CRT's `operator new`, `atan2f`,
  `_Query_perf_counter`, …) is an import or a CRT symbol: it names no aim
  function, and it is left alone. The check prints the number of the calls
  it left alone, so the operator sees them.
* **The advice names the reason.** A callee that is not in the policy's
  `cross_module` list is refused with `external_callee`, and the list
  routes it: the advice says to add it. A callee that the list **already**
  names may still stay direct, because the routing cannot reproduce a
  variadic call, a tail call or a call to an `optnone` function
  (`variadic_callee`, `musttail`, `optnone_callee`): no `cross_module`
  entry will ever route it, and the advice says so instead of sending the
  operator in a circle.
* **The `sret` edges are excepted**, and the check **names** them: the
  rule that refuses them is not narrowed by this task, and the eleven
  cross-TU callees that return a big aggregate stay direct. The check says
  so instead of failing on them. The exception is decided per
  **(module, symbol)** pair and not per symbol: the coverage of one module
  is the evidence of that module, and a symbol that one module excepts and
  another module refuses for another reason is a failure, whatever the
  order the coverage files are read in.
* **The stale entries.** Every `cross_module` entry that matched a
  declaration names the mangled symbol it named, and the check asks
  whether the payload's own libraries define it. An entry that names a
  symbol the payload does **not** define is stale — the function is gone,
  or was never there — and the build fails.
* **The direct calls that are left.** The two rules above look at the
  calls of the **protected** functions, because that is what the coverage
  records. The *same* symbols are still called directly by the functions
  the pass does not protect, and those edges are still in the image. The
  check counts them too, at the **object** level: a linked image has no
  relocation left for an internal call, so the object file is the only
  place where the direct edges are visible, and a REL32 relocation in
  `.text` against a `cross_module` symbol is one of them. The count is a
  **measurement**, not a rule — the direct calls of the unprotected
  functions are the payload's business — and the check prints it.

When the libraries or the `llvm-nm` cannot be found, the check **says so**
and **fails**: a rule that cannot be evaluated must not be reported as
passed. The "no coverage, no image comparison" mode of the check (the
manual run without `--coverage`) skips the rules that need the evidence.

The libraries and the objects are looked for in the **current working
directory**, so the check has to be run from the build directory (the
build does that, and a manual run has to `cd` there). Run from anywhere
else, the check finds no library and **fails** with the message above: it
does not guess where the payload's own code is.

### The check is part of the build

The check is **not** a manual step: the protected build runs it, and the
build **fails** when it fails. Without it, a regression (the mixer
constants back in the instruction stream, or the backend mis-materialising a
64-bit constant again) would leave every build green while the shipped
image calls the wrong functions.

The build passes the image it has just linked, the coverage directory, the
policy and the tree that is being built (`--source-root`, §9). The
disassembler is the one of the LLVM SDK the plugin was built with — another
LLVM may print the instructions differently enough for the check to miss a
dispatch — and `tools/protect/find_objdump.py` finds it (`$CS2_LLVM_SDK`,
else `$CS2_PROTECT_OBJDUMP`, else the default SDK); the configure step
**fails** when it is missing, because a protected build that cannot check
its image must not pretend to.

The **unprotected image** is optional: without it, the checks that compare
the two images (the exports, the imports, and the "the unprotected image has
no dispatch") are skipped, and the rest of the checks still run. Pass it
with

```
meson setup build-payload-protect payload -Dprotect=true \
    -Dprotect_plugin=<the dll> -Dprotect_reference=<the unprotected payload.dll>
```

A coverage directory that holds **no** file of this build is a **failure**,
not a skip: the check would otherwise pass without having checked anything.

`compare_builds.py --attribute` attributes the difference between the two
images. A byte-for-byte comparison cannot work any more: the transform
makes the image bigger, so every function after the first one moves. The
attribution is therefore per function: the functions whose *size* changed
are the ones the transform touched (the selected ones, and their callers,
which no longer inline them), and the sections that grew are the ones that
hold the new globals.

`compare_builds.py --section <name>` compares **one section** of the two
images and prints its size in both, the number of the bytes that differ and
whether the two sections are identical. The exit code is 0 when they are
identical and 1 when they differ. The linker's per-link stamps are
normalised out, so the PDB path of the two builds is not a difference of
the section. It is the measurement of the acceptance criterion (§16): the
criterion is about the `.text` alone, and the whole-file comparison would be
satisfied by a handful of bytes in the read-only data.

```
python tools/protect/compare_builds.py --section .text build-a/payload.dll build-b/payload.dll
```

The attribution is **by size**, not by content: the helper compares the
sizes of the functions, so it cannot show that the unselected code is
unchanged byte for byte. The differences are therefore *attributable* to the
selected functions, their callers and the new globals — not *only* to the
selected functions and the new globals, because the `noinline` marking
changes the callers as well.

## 12. Verified facts (do not re-derive them)

| Fact | Why it matters |
|---|---|
| `add_llvm_pass_plugin()` is a no-op on Windows ("Loadable modules not supported on this platform") | the plugin has to be a `SHARED` library (a `.dll`) |
| The SDK's exported CMake targets hardcode the **build machine's** DIA SDK path (`…/Visual Studio/2022/Enterprise/DIA SDK/…`) | the `INTERFACE_LINK_LIBRARIES` of the imported LLVM targets has to be rewritten, or the link fails with "diaguids.lib missing" |
| LLVM 22 moved the plugin header to `llvm/Plugins/PassPlugin.h`; `LLVM_PLUGIN_API_VERSION` is 2 | the include and the struct layout |
| `llvmGetPassPluginInfo` has to be exported explicitly (`__declspec(dllexport)`) | the loader resolves it by name with `GetProcAddress`; the SDK header declares it without the attribute, which also warns (`-Wdll-attribute-on-redeclaration`) |
| clang-cl ignores `-fpass-plugin=` | the flag has to be `/clang:-fpass-plugin=…` |
| A plugin's `cl::opt` is not reachable via `-mllvm` | the configuration travels through the environment |
| The release LLVM is built without RTTI and with the static CRT | the plugin is compiled `/GR- /MT`; clang-cl rejects `-fno-rtti` and `-fms-runtime-lib=static` ("unknown argument ignored in clang-cl") |
| `registerPipelineStartEPCallback` runs during a normal `-O2` compile | a pass that is only registered through `registerPipelineParsingCallback` never runs unless the pipeline is requested by name, and the "protected" build is then byte-identical to the baseline |
| `registerOptimizerLastEPCallback` runs *before* the module inliner and the other late module passes | the transform has to run after the optimiser (the `SimplifyIndirectCalls` would undo it) and before the code generator |
| a compile-time key makes the whole dispatch a compile-time constant | the optimiser folds the index, the load from the table and the indirect call back into a direct call: without the volatile barrier the protection is void (see §10.3) |
| `__attribute__((annotate("x")))` lands in `llvm.global.annotations`, keyed by the mangled symbol | the annotation selection source |
| `-O0` adds `optnone` to every function, and a pass skips `optnone` functions | the `optnone` skip reason |
| The SDK's `llvm::json::parse` **recurses** | the policy file's nesting depth has to be bounded *before* the text reaches it |
| The SDK's `llvm::json::parse` keeps the **first** of two equal keys, Python's `json` keeps the **last** | a duplicated key is an error on both sides |
| The X86 backend of the SDK's LLVM 22.1.8 **mis-materialises a 64-bit immediate** in the shape the transform emits (`xor i64 %mul, C` becomes `movabsq $C'`, one bit lost) | the constants of the mixer live in globals, and the payload check asserts that the image holds them (§10.2, §11) |
| The backend turns the **last call of a function into a tail jump** (`jmpq *(%rX,%rY,8)`) | a dispatch is not always a `callq`: the check counts the tail jump as well, and tells it apart from a switch table jump by the decode in front of it (§11) |
| The backend folds the `shl`/`lshr` pair of the mixer into a single **`rolq $imm8`** | the rotation is the one per-build value of the mixer that is in the instruction stream, and the check reads it out of the disassembly (§11) |
| The **object writer** emits the functions in the **module's function list** order, and the **linker** keeps the order of an object's sections | a pass that reorders the module's function list moves the code in the image (§16) |
| **Renaming** the symbols per build moves **nothing**: the layout follows the definition order, not the name | the layout is changed by *reordering* the function list, not by renaming it (§16) |

## 13. Known limits of the build wiring

* **The protected build needs the `llvm-objdump` of the LLVM SDK** (§11): the
  payload check disassembles the image it verifies, and the disassembler has
  to be the one of the SDK the plugin was built with. The configure step
  **fails** when it is missing: a protected build that cannot check its image
  must not pretend to.
* **The comparison against the unprotected image needs a second build**
  (`-Dprotect_reference=<the unprotected payload.dll>`): the payload project
  builds one image, so the reference comes from a second build directory
  (§11). Without it the checks that need only the protected image still run.
* **The coverage directory is shared by everything that shares the policy**
  (§9): the coverage of another worktree is in it, and the readers skip it
  by comparing the source of every coverage file with the tree that is being
  built. A reader that is not told the tree counts every file of the policy
  — and counts every rewrite twice.
* **Meson 1.11 dropped `depend_files` and `build_depends` for build
  targets.** The payload therefore uses `link_depends` (the modern
  equivalent) for the plugin file, and the verification target
  (`tools/protect/verify_plugin.py`) is what guards against a stale plugin:
  Ninja cannot attach an arbitrary file to a compile edge, so a plugin that
  changed after the configure step would otherwise leave stale objects
  behind without any error. The verification target compares the plugin
  with the digest that the configure step recorded and **fails the build**
  when they differ.
* For the same reason, the digest of the plugin (and of the policy) is part
  of the compiler command line: after a change, the configure step has to be
  repeated (`meson setup --reconfigure …`) for the objects to be rebuilt.
  The digests are macros that the payload never reads, so the generated code
  does not change.
* **The link is not reproducible byte for byte**, and neither is the
  *image*: the PE header carries the link time, and the debug directory
  carries a CodeView GUID, an age and the path of the PDB that the linker
  generates per link. Two links of the *same* build therefore differ in
  those bytes. The honest criterion is:

  > the protected payload is **identical to the unprotected one apart from
  > the linker's per-link stamps** (the PE timestamp, the debug directory's
  > timestamp and size, the CodeView GUID and age, and the embedded PDB
  > path).

  `tools/protect/compare_builds.py --ignore-link-stamps` proves exactly
  that: it normalises those ranges (the PDB path is replaced by a filler of
  the same length, so the layout of the image does not move) and compares
  the rest byte for byte.

  The comparison is only meaningful when the two builds embed PDB paths of
  the **same length**: a path of a different length shifts the layout of
  the image, and the `.text` section is then genuinely different. The tool
  says so instead of pretending the two images are comparable.

## 14. The front end of the virtualizer

The `virtualized` level (T20) moves the body of a function into an
interpreter. This section documents the **front end** of that: the
instruction set, the frame, the budgets, the eligibility gate and the
encoder. The interpreter and the rewrite of the function are the next
task; they read this section (and `src/isa.def`) for the contract.

What the front end does today:

* it **decides** whether a selected function can be virtualized, and says
  why when it cannot (one reason per function);
* it **lowers** the optimised IR of the function to the bytecode of the
  virtual machine;
* it **encodes** that bytecode (encrypted, with a per-build opcode
  permutation) and reports its size.

What it does **not** do: it does not change the function. The analysis is
**read-only**: the module comes out of it byte for byte the way it went
in, and the `vm_ir_test.py` test proves it. The image of a protected build
is therefore the image of today: the same 37 encrypted dispatches, the
same import table, the same exports.

### 14.1 The instruction set

The opcodes live in **one** file, `src/isa.def`, written as an X-macro
list, and every side that needs a number includes it: the encoder of this
task and the dispatcher of the interpreter. A hand-written copy of the
table in two places is a table that drifts, and the drift is a silently
wrong bytecode.

```
CS2_VM_OPCODE(Const,  0, "dst = imm, extended to the width")
CS2_VM_OPCODE(Move,  1, "dst = src1, truncated to the width")
CS2_VM_OPCODE(Add,   2, "dst = src1 + src2, at the width")
...
CS2_VM_OPCODE(Pool, 30, "dst = the pool entry imm (a relocatable pointer)")
```

The number in the file is the **canonical** number: it is dense, it is
0-based, and it is what the interpreter's switch uses. The number that
appears in the bytecode is the **permuted** one (§14.5), so the canonical
number never appears in the image.

| opcode | operands | meaning |
|---|---|---|
| `Const` | dst, imm | dst = imm (extended to the width) |
| `Move` | dst, src1 | dst = src1 (truncated to the width) |
| `Add` `Sub` `Mul` `And` `Or` `Xor` | dst, src1, src2 | the arithmetic, at the width |
| `SDiv` `UDiv` `SRem` `URem` | dst, src1, src2 | the division, at the width |
| `Shl` `LShr` `AShr` | dst, src1, src2 | the shift amount is masked to `width * 8 - 1` (the **width is in bytes**, so the mask is 7, 15, 31 or 63) |
| `Neg` `Not` | dst, src1 | the negation and the complement, at the width |
| `Cmp` | dst, src1, src2, aux | dst = 1 if the comparison holds, else 0 |
| `Cast` | dst, src1, aux | zext / sext / trunc / bitcast |
| `Load` | dst, src1, aux | dst = *(iN*)src1 |
| `Store` | src1, src2, aux | *(iN*)src1 = src2 |
| `Gep` | dst, src1, imm | dst = src1 + imm (a **signed** byte offset) |
| `Br` | aux | pc = the instruction index in aux |
| `BrIf` | src1, aux | if src1 != 0, pc = the instruction index in aux |
| `Arg` | src1 | push src1 onto the argument stack |
| `Pop` | dst | dst = the top of the argument stack, popped |
| `Call` | dst, imm, aux | dst = the result, imm = the pool index, aux = the argument count. The arguments are the **top** `aux` cells of the argument stack, and they are in the order of the call: the **first argument of the call is the first one pushed** |
| `Ret` | src1 | return src1 |
| `MemCpy` | src1, src2, imm | copies a **constant** number of bytes |
| `MemSet` | src1, src2, imm | sets a **constant** number of bytes |
| `Pool` | dst, imm | dst = the pool entry imm (a relocatable pointer) |
| `Alloca` | dst, imm | dst = the address of the frame's local at the byte offset `imm` |

Three of the opcodes are **not** in the specification's table, and all
three are there because the table cannot express what the lowering has
to express:

* **`Pop`** — the parallel copy of a PHI pushes every source and pops
  every destination in the reverse order (§14.3), and the argument stack
  is the only stack the frame has. Without a pop, the copy would have to
  be a *sequential* copy, and a sequential copy breaks a swapping
  back-edge (the classic `a, b = b, a`).
* **`Pool`** — the bytecode may hold **indices and relative offsets
  only**, never an absolute address: the payload is manually mapped and
  the mapper applies the relocations. The address of a global is
  therefore not in the bytecode; the **pool** holds it, and the pool is a
  relocatable global (the linker emits the DIR64 fixups for its
  entries). The `Call` opcode uses the same pool for the callees: one
  entry per call site, and the bytecode holds the index. The pool holds
  the **data** pointers as well (the address of a global), and the
  opcode tells the two apart.
* **`Alloca`** — a local variable is a **byte offset into the frame
  arena** (T20 §2.3), and the frame is an arena. The opcode carries the
  offset, so the bytecode holds a relative offset and never an address;
  the interpreter adds the base of the frame to it, and the local is
  then real memory, like every other pointer of the virtual machine.

A `switch` is lowered to a chain of `Cmp` + `BrIf`. It is not refused.

**The width of each opcode** (§14.2) is part of the contract, and it is
never 0:

* the opcodes that produce a **value** carry the width of that value:
  the width of the IR type of the operation (`Const`, `Move`, the
  arithmetic, `Cmp`, `Cast`, `Load`, `Store`, `Gep`, `Pool`, `Alloca`),
  and the width of the return type of the callee (`Call`);
* the opcodes that produce **no** value carry the width of a cell of
  the frame (**8**): `Br`, `BrIf`, `Arg`, `Pop`, and the `Call` of a
  function that returns nothing (the uniform signature returns a
  64-bit value);
* `MemCpy` and `MemSet` are **byte-wise** and carry **1** (their length
  is in `imm`);
* the `Ret` carries the width of the **value it returns**, and 8 for a
  function that returns nothing.

### 14.2 The encoding

Every instruction is a fixed **24 bytes**:

```
u8 opcode | u8 width | u8 flags | u8 pad | u16 dst | u16 src1 | u16 src2 | u16 src3 | u32 aux | u64 imm
```

* `opcode` is the permuted opcode (§14.5);
* `width` is the width of the operation, in bytes (**1, 2, 4 or 8 — never
  0**). It is the width of the value the instruction **produces** (§14.1):
  an instruction that produces no value carries the width of a cell of
  the frame (**8**), except `MemCpy` and `MemSet`, which are byte-wise
  and carry **1**. The `Ret` carries the width of the returned value, and
  8 for a function that returns nothing. The `Pop` **does** produce a value
  (the top of the argument stack), but it yields a whole cell, so it
  carries **8** whatever the type of the value in it is. **A width of 0 is
  not a legal value**: an interpreter written against this contract
  truncates its result to the width, and a width of 0 would truncate it to
  nothing — it would return 0 from every function and load a null pointer
  for every global;
* `flags` carries `nsw` (bit 0), `nuw` (bit 1) and `exact` (bit 2). The
  flags are **carried**, never dropped: dropping `nsw` turns undefined
  behaviour into a defined wrap;
* `dst`, `src1`, `src2` and `src3` are **slot indices**; `0xFFFF` means
  "no slot" (the budget is 256 slots, so the value cannot be one);
* `aux` carries the sub-opcode: the kind of a comparison, the kind and
  the source width of a cast, the target of a branch, the size of a
  memory access;
* `imm` carries the immediate, and the index of the pool for `Call` and
  `Pool`.

The container has a **32 byte header** in front of the stream:

```
u32 magic          'C','S','2','V'
u32 version        1
u32 slot_count     the number of value slots
u32 arg_slots      the size of the argument-stack region
u32 call_slots     the size of the call-scratch region
u32 instruction_count
u32 flags          bit 0: the container is encrypted, bit 1: the opcodes are permuted
u32 local_cells    the number of cells the locals take
```

The header is the **contract** between the encoder and the interpreter:
the interpreter reads the layout of the frame out of it instead of
assuming it.

### 14.3 The frame

The frame is a flat array of 64-bit cells, and it is a **constant**: the
rewritten function (the next task) allocates exactly this, so the
interpreter's frame is always the same shape and the slot indices are
absolute.

```
[0, slot_count)                  the value slots        (256 cells at most)
[slot_count, +local_cells)       the locals            (the arena above the value slots)
[slot_count + local_cells, +arg_slots)         the argument stack     (64 cells)
[slot_count + local_cells + arg_slots, +call)  the call scratch       (64 cells)
```

**384 cells, 3072 bytes.** The specification's §6 table says 2560 bytes,
and its formula lists the three regions; 2560 is the size of the *two*
regions (256 value cells and 64 argument cells) and leaves no room for
the call scratch that §3.2 names. The front end therefore uses the
three regions the layout describes, and the report says 3072.

* the **value slots** hold the values: one slot per SSA value, plus the
  slots of the constants, of the globals and of the temporaries the
  lowering needs;
* the **locals** are the cells above the value slots: a local variable
  is a byte offset into the frame arena (T20 §2.3), and the arena is
  the frame. The value region holds 256 cells at most, so a function
  whose locals do not fit is **refused** (`local_variable`);
* the **argument stack** holds the values that were pushed for a call
  (`Arg`), and the values of a parallel copy (`Arg` and `Pop`). The
  pushes and the pops are balanced, so the stack is empty at every
  statement boundary, and the deepest stack is a call with the most
  arguments or a parallel copy with the most moves;
* the **call scratch** holds the arguments of the call in flight: the
  interpreter copies them out of the argument stack and hands them to
  the thunk of the call site (§14.6).

**The width model.** A slot holds a 64-bit value, and the IR's integers
are *signless*: the value of an `i32` is stored **zero-extended**, and a
signed operation re-interprets the low 32 bits as signed (which is what
`sext` does). Every operation truncates its result to the width of the
operation, so an `i32` addition is an `i32` addition: widening it to
`i64` would change the result on overflow.

An `i1` lives in a byte, and the value of an `i1` is its low bit, so the
lowering masks every operation whose result is an `i1` (a comparison
produces zero or one and needs no mask).

**The frame is 8-byte aligned.** The cells are 64 bits wide, and a local
is a byte offset into the frame (§14.1), so the base of the frame has to
be a multiple of eight: that is the alignment of a cell, and it is the
alignment every local may ask for. A local that needs more than eight
bytes of alignment is **refused** (`local_variable`), so the frame's
alignment is the only alignment the interpreter has to provide.

### 14.4 The budgets

| budget | value | on overflow |
|---|---|---|
| value slots per function | 256 | refused (`too_many_slots`) |
| call arguments | 64 | refused (`too_many_arguments`) |
| frame size | 384 cells = 3072 bytes | fixed |

A function that needs more is **refused**, not truncated: a truncated
function is a miscompile.

### 14.5 The encryption and the permutation

The bytecode is encrypted with a **per-build key**, derived from the
module seed the way T19 derives its key (`splitmix64` over the module
seed) and split across **three** constants, so that no single constant
in the image is the key. The cipher is a counter-mode stream: the word
at the offset *n* is the mixer of the key and of *n*, and the byte is
one of its eight. The interpreter decrypts **on fetch**, so the blob
stays read-only and the interpreter is re-entrant.

The whole container is encrypted, the header included: an analyst reads
nothing but noise. The header is what the interpreter reads *after* it
has decrypted it, so there is nothing to gain from leaving it in the
clear.

**The keystream, exactly.** The word at the byte offset `n` is

```
mix64(key + (n / 8) * M)
```

where `M` is the **multiplier of the build**, and the byte at the offset `n`
is its `(n % 8)`-th byte, little endian. The mixer is

```
mix64(v) = rotl((v * M) ^ X, R)
```

where `M`, `X` and `R` are the multiplier, the xor constant and the
rotation **of the build**: they come from the module's seed, the way the
parts of the key do (`src/cipher.hpp`: `mixer_for(seed)`; the rotation is
in [1, 63]). The encoder derives them from the module's seed, and the
**interpreter** has to read them out of the image rather than assume them:
the contract is "the value the pass encrypted with", and the coverage names
it.

The key is the xor of the **three** parts of the key, mixed: the
parts come from `splitmix64` over the module seed, a part that is zero
becomes one, and two equal parts are redrawn (they would cancel each
other in the xor). The interpreter decrypts **on fetch**, so the blob
stays read-only and the interpreter is re-entrant.

The **opcode permutation** is `p(x) = (x * k_a + k_b) mod N`, with `k_a`
odd and coprime with the number of the opcodes and `k_b` from the module
seed. The encoder emits `p(x)`; the interpreter maps the fetched byte
back with `p^-1`. The canonical number never appears in the image, and
the dispatch table of the interpreter does not have the same shape in
two builds.

**What the key buys, and what it does not.** This is a **build key**,
not a runtime key: it stops a scanner from grepping for the key, and it
makes the blob different in two builds. It does **not** buy anything
against an analyst who recognises the mixer (they recover the key in
minutes), and it does not survive a dump of the running process: the
bytecode is in memory, in the clear, while the interpreter runs. A
**runtime** key — one that is derived when the process starts and that
the image never holds — is a later task's job.

### 14.6 The calls

The interpreter cannot call a function with an arbitrary signature (the
IR has no way to build a call with a dynamic signature, and there is no
`libffi` in a freestanding payload). The interpreter therefore calls
through a **uniform signature**, and a **per-call-site thunk** adapts
the real callee to it:

```
the interpreter:   u64 thunk(u64* scratch)      // scratch[0..n-1] are the arguments
the thunk:         loads the arguments out of the scratch array with the
                   right types, calls the real callee, stores the result
```

The front end records, per call site, the **symbol of the callee** and
the **types of its arguments and of its return value**, so that the
thunk can be emitted. A callee that returns a struct is refused, and a
callee that returns nothing returns zero.

**The order of the arguments is pinned by the contract.** The lowering
pushes the arguments of the call onto the argument stack **in the order
of the call**: the first argument of the call is the first one pushed.
The interpreter copies the **top** `aux` cells of the argument stack into
the scratch **in the order they were pushed** — so `scratch[0]` is the
first argument of the call — and it pops them. The stack pops last in,
first out, so an interpreter that popped the arguments and wrote them
into the scratch in the order it popped them would hand the callee its
arguments **reversed**: the contract says which way it is, because the
two halves of the virtual machine are written by two different tasks.

A call that returns an `i1` is **masked** by the lowering: the x64 ABI
defines the low bit of the byte and leaves the other seven undefined, so
the value of the `i1` is its low bit and nothing else.

### 14.7 The eligibility gate

The gate fails **closed**: a function that cannot be lowered completely is
left completely untouched, and the reason is reported. There is no "best
effort" path — a half-lowered function computes garbage, and in a
manually mapped payload with no exception tables that kills the game.

The gate's checks run first, and the first one that holds is the reason
that is reported. The table below is the **order of the code**: the
gate's checks, and then the reasons that come from the lowering. The two
budget checks are the exception: the argument count is raised in the
call scan and the slot count when the allocator runs out, so a function
that exceeds **both** is reported as `too_many_arguments`.

The reasons that come from the **lowering** (`too_many_slots`,
`unsupported_instruction`, `unsupported_type` and `local_variable`) can
only be reported after **every** gate check has passed: the gate looks at
the function, and the lowering looks at its instructions.
`local_variable` is the last one of the table, because the locals are
placed after the whole body is lowered — the value slots above them are
only known at the end.

| reason | refused because |
|---|---|
| `eh_pad` | the function has an EH pad, an `invoke`, a `landingpad` or a `resume`: the interpreter cannot model unwinding |
| `variadic_function` | the function itself is variadic |
| `variadic_callee` | it calls a variadic callee: the indirect call cannot reproduce the variadic ABI |
| `indirect_parameter` | a callee has a `byval` / `sret` / `inalloca` / `preallocated` parameter: Windows x64 rejects these in an indirect signature |
| `musttail` | the tail-call contract does not survive the rewrite |
| `indirect_call` | it makes an indirect call: there is no relocatable address for the pool |
| `volatile_access` | a volatile access: the ordering guarantees are not modelled |
| `atomic_access` | an atomic access: the ordering guarantees are not modelled |
| `dynamic_memcpy` | a `memcpy` / `memset` with a **non-constant** length: it would lower to a CRT call |
| `unsupported_type` | a float, a vector, an aggregate or an integer of more than 64 bits: this task handles integers of at most 64 bits and pointers only |
| `address_taken` | the address of the function is taken (`&F`): the rewrite changes what the address means |
| `too_many_arguments` | a call needs more arguments than the budget |
| `recursion` | the function calls itself: the frame would multiply by the recursion depth |
| `virtualized_callee` | the function calls a function that is itself selected at the `virtualized` level: the interpreter would have to call into another interpreter |
| `too_many_slots` | the function needs more slots than the budget |
| `unsupported_instruction` | the catch-all: an IR instruction the lowering does not model. The lowering has a default branch that refuses. |
| `vanished` | the function did not survive to this phase: there is nothing to virtualize. The transform reports the same thing the same way. |
| `local_variable` | the function has a local variable that the frame cannot hold: the locals are the cells above the value slots, and a local that does not fit (or one that needs more than eight bytes of alignment, or one whose size is not constant) is refused |

The rules of the transform (§10.4) are **reused**, not written twice:
the variadic callee, the indirect parameter, the `musttail` call and the
indirect call are the same rules, and the same reasons. The gate calls
the transform's `skip_reason` (`src/transform.hpp`) and maps its reasons
onto the virtual machine's, so the two phases cannot drift apart. The
**order** is the transform's order — the variadic callee, the indirect
parameter, the `musttail` call and the indirect call — and the table
above lists them in that order too. (The specification's §4 table still
lists the indirect call before the indirect parameter: the table there
and the order of the code disagree, and the code is the one that is
right — the two phases have to agree.)

What the lowering models: the integer and pointer arithmetic, the
comparisons, the conversions, the loads and the stores, the `getelementptr`
(both the constant and the variable indices), the branches, the `switch`,
the `select` (a branch), the `phi` (a parallel copy, §14.3), the
`freeze`, the `alloca` (a byte offset into the frame arena, §14.3), the
`memcpy` and the `memset` with a constant length, the calls, the
`ret` and the `unreachable` (a `Ret` of zero: the original is undefined
behaviour, and what matters is that the interpreter **stops** — the ISA
has no "stop" opcode, and the `Ret` is what stops it). What it refuses:
the floats, the aggregates, a local that the frame cannot hold
(`local_variable`), the `memmove` (it may copy overlapping ranges), the
atomic and the volatile accesses, and everything else.

**The moves of a PHI belong to the edge.** A PHI becomes edge
assignments (§14.3): the predecessor performs the moves of the
successor's PHIs **before its branch, on the edge that leads to the
successor**. The taken edge of a conditional branch therefore gets a
trampoline, and every case of a `switch` gets one of its own: the moves
of an edge that is not taken are not performed. A copy that runs on the
wrong edge is observable — a back edge that swaps two values would swap
them even when the loop is left.

### 14.8 What the analysis says about the payload

The analysis runs for **every** selected function, whatever its level,
and it is in the report (§7) and in the coverage (§9). The coverage
gains a `vm` line per function:

```
vm ?newest@RecordRing@aim@cs2@@QEBAPEBURecord@23@XZ verdict=virtualizable slots=12 locals=0 frame=3072 bytecode=392 instructions=15 calls=0
vm ?solve@aim@cs2@@YA?AUSolveResult@12@… verdict=refused reason=eh_pad detail=the_function_has_an_EH_edge slots=0 locals=0 frame=3072 bytecode=0 instructions=0 calls=0
```

The section is **new**: a coverage file of an older build has no `vm`
line, and a reader that does not know the section still reads the file.

With the policy of `tools/protect/policy.local.json` (the six
`cs2::aim::*` functions), the verdict of every function is:

| function | verdict | why |
|---|---|---|
| `cs2::aim::solve` | refused | `eh_pad` (the function has an EH edge) |
| `cs2::aim::solve_backtrack` | refused | `eh_pad` |
| `cs2::aim::occlude_polygon` | refused | `eh_pad` |
| `cs2::aim::project_capsule` | refused | `eh_pad` |
| `cs2::aim::DamageField::hit_probability` | refused | `unsupported_type` (the return type is a `float`) |
| `cs2::aim::penetration_survival` | refused | `unsupported_type` (the return type is a `float`) |

**None of them is virtualizable, and that is the honest answer**: the
aim maths is a float pipeline, and the virtual machine of this task
handles integers and pointers only. The numbers the operator needs are
therefore the numbers of the *refusals*, and the first thing to do
before the next task is to widen the machine (the floats) or to narrow
the ambition (the integer helpers).

The gate is not empty, though. The numbers below are **measured**: they
come from a protected payload build with the policy
`{"protect": ["cs2::aim::*"]}`, and they are the `vm` lines of that
build's coverage. The analysis runs for every *selected* function, so
the population is the 278 functions the policy selects (the aim module's
own functions and the standard library's templates that they pull in).
**56 of them are virtualizable and 222 are refused**:

> **That wild policy is a measurement policy, not a build policy.** A
> protected build with it is **red**: `protect-policy-check` fails ("1
> policy entry named a selected function that did not survive to the
> transform phase") and `check_payload` fails ("539 encrypted dispatches,
> but the pass reported 566 rewrites: the compiler folded the dispatch
> away"). Both failures are the checks doing their job — the wild policy
> names functions the optimiser removes, and the call encryption's
> descriptors are folded away for the ones it cannot keep. The **numbers**
> below are what the analysis says; the **build** it was measured with is
> not a build anyone should ship. The shipped policy
> (`tools/protect/policy.local.json`) is the six functions of §14.8's
> first table, and it builds green.

| reason | refused |
|---|---|
| `unsupported_type` | 97 |
| `unsupported_instruction` | 46 |
| `eh_pad` | 45 |
| `indirect_parameter` | 12 |
| `dynamic_memcpy` | 9 |
| `indirect_call` | 6 |
| `local_variable` | 4 |
| `recursion` | 2 |
| `vanished` | 1 |

and the admitted ones are the small integer helpers:
`cs2::aim::RecordRing::newest` (12 slots, 392 bytes of bytecode, 15
instructions), `cs2::aim::RecordRing::oldest` (10 slots, 344 bytes, 13
instructions), `cs2::aim::RecordRing::trim` (32 slots, 2024 bytes, 83
instructions, 2 calls), `cs2::aim::UniformRandomStream::random_int` (66
slots, 2600 bytes, 107 instructions) and the destructors and the
accessors of the aim types.

**The figures that were here before** (2490 functions of the module, 1878
admitted and 612 refused) were measured **before** the fix round, over a
*different* population: every function of the module, not the selected
ones. They are not comparable with the table above, and they are not
reproduced here.

### 14.9 Where the analysis runs

The analysis runs in the **transform phase** (the optimizer last), and
it runs **before** the call encryption: the transform rewrites the calls
of a selected function into indirect dispatches, and an indirect call is
a refusal, so the analysis has to see the calls the source had.

It is also reachable by the name `cs2-protect-vm`, which runs the
analysis and **nothing else**:

```
opt.exe -load-pass-plugin=Cs2Protect.dll -passes=cs2-protect-vm -S fixture.ll
```

That pipeline is what the IR test uses to prove that the analysis is
read-only: the module comes out of it byte for byte the way it went in.

## 15. The transform: the string encryption

The payload's operational strings -- the feature keys, the JSON keys, the
convar names -- are plaintext in the shipped DLL: an analyst with the file
opens it in IDA, reads them, and learns the feature list without running
anything. This section is about the pass that takes them out of the file.

### 15.1 What is selected

The policy's `strings` section (§5) is a list of globs matched against the
**content** of a literal. Nothing is protected unless the policy says so,
and a literal that the policy does not select is **not even looked at**: the
gate never sees it, and the report never names it.

### 15.2 The gate: fail closed

A literal is protected only if it passes **every** check. A refusal leaves
the literal **exactly as it is** -- the gate does not half-protect -- and
the reason is reported:

| refusal | why |
|---|---|
| `escapes_to_initializer` | the literal's address ends up in a **global initializer** (a `constexpr` array of `string_view`): the pointer is baked into the image, and a pointer to a decoded buffer cannot exist before the program runs |
| `unsupported_shape` | the literal is not a `ConstantDataSequential` of `i8` |
| `unsupported_address_space` | the literal is in **another address space**: the blob and the buffer are created in the default one, and a literal in another one cannot be replaced by them (the module would come out broken) |
| `empty` | the literal is empty |
| `too_short` | it is shorter than `strings_min_length` |
| `optnone_function` | a user of it is in an `optnone` function |
| `virtualized_function` | a user of it is in a function selected at the `virtualized` level: the VM's blob format and the string blob format must not meet |
| `eh_use` | a user of it is in a function with an EH pad, or the use is in a landingpad: the rewritten IR must stay exception-free |
| `unsupported_use` | the use is not a plain pointer use (the address is compared, stored in a structure, or the user is not an instruction), or the user is a **constant expression** the pass cannot rewrite (a `ptrtoint`, or a GEP with a variable index) |

A use the pass cannot rewrite is a use that **fails** the gate: it is
never *skipped*. A skipped use would let the pass delete the literal
while the expression that referred to it is still there, and the X86
backend dies on the dangling reference ("Cannot select: ... PLEASE submit
a bug report"). The gate is fail closed in both directions: a literal is
protected only if **every** one of its uses can be rewritten.

**The reason is the first *rule* that holds, not the first failing use.**
The table above is an ordered list, and the reason a literal gets is the
first rule of it that holds -- a literal with two reasons (an `optnone`
function *and* an unrewritable use) has **one** verdict, whatever the
order of its use list is. The order of the uses is the order of the
module's use list, and it is not a rule.

**The constant expressions that cannot be rewritten are `ptrtoint` and a
GEP with a variable index.** The list used to be longer, and it was
wrong: an `inttoptr` needs a **constant** operand, so it can never name
a literal; a `select` with a constant condition is folded by the parser,
so it is always the instruction form; and a GEP of a GEP is folded into a
single GEP by the parser, so it never reaches the pass either. The
handling of a constant expression the pass does not understand is
unchanged: the literal is **refused**, not skipped.

**The `KNOWN_EVENTS` case.** `payload/control/include/cs2/control/event_names.hpp`
holds `inline constexpr std::array<std::string_view, 273> KNOWN_EVENTS` and
`KNOWN_EVENT_FIELDS` (656 fields). Those are **Valve's** names: they are
public knowledge, they identify the *game build* and not the payload, and
they **cannot** be protected by this design at all -- a `constexpr` array of
`string_view` bakes the pointers into a global initializer, so they are
refused with `escapes_to_initializer` and they stay in the image. That is
the honest answer, not a gap to be papered over: protecting them would need
a different data model in the payload (an index table plus a runtime-decoded
table), which contradicts the "no constructor" rule.

**The `linkonce_odr` assumption.** A string literal is `private` in the
Itanium ABI, but the MSVC ABI (the one the payload is built with) gives it
`linkonce_odr` linkage, so that the linker can fold the identical strings of
two translation units. The pass therefore accepts that linkage: it deletes
the literal and replaces it with a private blob. The consequence is an
**assumption, not a guarantee**:

> **every** translation unit that uses a selected string has to be built with
> the plugin **and with the same policy**.

A translation unit that is compiled without the plugin (or with a policy
that selects nothing) keeps its reference to the symbol the protected module
deleted, and the **link fails** with an unresolved external. That is a loud
failure -- not a wrong result -- but it is the *build* that fails, not the
pass, and the pass cannot detect it: a pass sees one translation unit at a
time. The same assumption is the reason why a literal that is protected in
one module and **refused** in another is still in the image (§15.2): the
refused module keeps its copy, and the linker keeps the COMDAT.

### 15.3 Where the plaintext lives: a stack buffer

The chosen form is a **decode into an `alloca` in the using function**,
once per function, before **every** use:

```
entry:
  %buf = alloca [N x i8], align 1
  call void @cs2.protect.strdecode(ptr @blob, i32 N, i32 %index, ptr %buf)
  ...
  ; every use of the old literal now points at %buf
```

The decode goes at the top of the block that **dominates** every use: the
nearest common dominator of the blocks of the uses (the entry block, when
the uses are in several blocks). It has to be *before* every use: a use
that reads the buffer before the decode filled it reads an **uninitialised
stack slot**, which is a *wrong string* and not a crash -- the failure mode
the specification calls out, and the one nothing else would notice. (The
first implementation chose the "first use" with `Instruction::comesBefore`,
which the LLVM header defines only *within one basic block*: for two uses
in two blocks it answers "false", and the decode landed *after* a use that
was already in the entry block. The differential test now covers that
shape -- one literal, two uses, one of them in the entry block -- and the
unit test checks the dominance of the decode with the dominator tree.)

The alternatives, and why they lose here:

| form | why it loses here |
|---|---|
| **micro-slot** (every byte in a global of its own, XORed back at every use) | the strongest anti-dump property -- the plaintext is never contiguous -- but the payload's uses are `std::string_view` comparisons, and a `string_view` is a `{pointer, length}` pair: it needs a **contiguous** buffer of the whole string. A micro-slot would have to reassemble the string at every use, which is a copy (and an allocation) on the path of every comparison |
| **cached global** (decode once, keep it) | the plaintext sits in the image's memory **forever**, and it needs an initialisation step, which needs a constructor, which is forbidden: nothing in a manually mapped image is ever initialised |
| **constructor decode** | forbidden outright |
| **decrypt-use-reencrypt** (in place) | the buffer must be writable; the image's `.rdata` is not. A writable copy would have to be a global, which brings back the previous row |

**The plaintext's lifetime.** The decode fills a **stack buffer** of the
using function: the plaintext lives in that frame from the first use to the
return. A **thread-stack dump** or a **crash dump** of the game shows it.
What this buys is time against an **offline** analyst; against a debugger it
is nothing. The value is that the **file on disk** no longer holds the
strings.

### 15.4 The key

```
key = mix(seed0, seed1, index)
```

step by step:

1. The **build seed**: the module seed (the build seed mixed with the module
   id, exactly as T19 derives its key) is split with `splitmix64` into two
   constants, `seed0` and `seed1`. They live in two `private constant`
   globals (`cs2.protect.strseed0`, `cs2.protect.strseed1`) and are read
   with **volatile** loads.
2. The **index**: the literal's index in the module's list of protected
   strings. It is an `i32` **argument** of the decode, so it is never a
   64-bit immediate.
3. The **mixer**: the same family as the call encryption's (`splitmix64` plus
   the multiply/xor/rotate mixer). The multiplier and the xor constant live
   in globals and are read with volatile loads: the X86 backend of this LLVM
   mis-materialises a 64-bit immediate in this shape (§12).
4. The **key stream**: one mixed value per byte, `stream(i) = mix64(key + i)`.
   The ciphertext is the plaintext XORed with the stream, so it is not the
   plaintext under any single build-time constant.

**The instance base, honestly.** The ciphertext is baked into the image, so
the key that decrypts it has to be computable at build time: a per-injection
value cannot be part of the key of something that is *already in the file*.
The decode therefore reads the base, mixes it into the runtime key and takes
it out again -- the base is part of the *derivation*, and the plaintext it
recovers is the plaintext. The alternative (a per-injection table of
ciphertext variants, the runtime picking one with the base) multiplies the
size of the image by the number of variants and buys nothing against an
analyst who has the image.

The **global** `cs2_protect_instance_base` is an `external global i64` that
the pass **declares** and the payload **defines**
(`payload/src/payload_main.cpp`). The payload publishes its own handle in
`DllMain`, *before* `boot()`: the handle is the payload's own mapped image
base -- a value that is not in the file and differs per injection. It costs
no import and no TLS, and it is not exported, so the export table does not
change. It is read with a **volatile** load.

**The key is in RAM while running.** The seeds are in the image, the
*global* of the instance base is in the image (its **value** is not: that is
the whole point), and the derived key is in a register. An analyst who dumps
the **running** process recovers the strings in seconds. This is
obfuscation, not cryptography: an analyst who recognises the mixer recovers
the key in minutes.

**The call descriptors still use a build-time key.** T19's descriptors (the
encrypted call sites) are keyed by the **build** seed, not by the instance
base; this task does not change them. The runtime-derived key of this task
is the key of the **strings** only.

### 15.5 The decode helper

The helper is emitted as IR, once per module, `internal` and `noinline`:

```
declare void @cs2.protect.strdecode(ptr %blob, i32 %len, i32 %index, ptr %out)
```

It reads the seeds, the instance base and the blob with **volatile** loads,
derives the key, and writes the plaintext into `%out` with a **loop**: no
CRT call, no `memcpy`, no `memset`, no new import.

The volatile loads are **load-bearing**, and not only for the same reason
as in T19: the whole decode is a compile-time constant once the seeds are
known, so a compiler that folds it constant-folds the **plaintext** into the
image -- which is exactly what the task exists to prevent. The blob is read
with a volatile load for the same reason, and the helper is `noinline` so
that the inliner cannot turn the blob into a constant either.

### 15.6 The report and the coverage

The **report** (stderr) names every literal the policy selected, and whether
it was protected or refused. It names the **content** only with
`CS2_PROTECT_VERBOSE=1`: a build log is a file, and a file holds the
strings. Without the verbosity the report carries the length and the
digest:

```
[cs2-protect] config_store.cpp: str: 77 literals protected in this module, 4 refused
[cs2-protect] config_store.cpp: str: (10 bytes, 1 uses, 1 function, digest 8A3F...): protected
[cs2-protect] event_names.hpp: str: (11 bytes, digest 51C0...): refused (escapes_to_initializer)
```

The **coverage** must not carry the content either: the coverage is a file,
and a file holds the strings. It records the **length** and a **digest of
the content**, the number of uses, the number of functions and the verdict:

```
str protected len=10 digest=… uses=1 functions=1
str refused len=11 digest=… uses=0 functions=0 reason=escapes_to_initializer
```

The field is named `digest=`, and not `sha256=`, because the digest is
**salted**: it is `sha256(seed ‖ content)`, not `sha256(content)`. A
reader (or a tool written against the older format) that assumes the
unsalted form computes a different value and silently finds nothing. A
coverage of an older build names the field `sha256=`, and the readers
take **both** names -- the salt line of the coverage says which form the
digests are in.

The digest is **salted with the seed of the build** (the environment's
`CS2_PROTECT_SEED`, else the policy's `seed`): an unsalted SHA-256 of a
four-to-twelve byte literal is a dictionary attack away from the plaintext,
so an unsalted digest in the coverage -- or in the build log -- would still
yield the strings. The salt is in **neither** the coverage nor the log; the
coverage names it and carries its proof, so that a reader with the wrong
seed knows it instead of finding no literal in the image and reporting a
protection that works:

```
str-salt build-seed digest=…
```

The section is **optional**: a coverage file of an older build has no `str`
line, and the readers ignore it. `tools/protect/check_payload.py` uses it to
prove that the plaintext of the protected strings is **absent** from the
protected image (and present in the unprotected one, when one is given): it
hashes the candidates of the image and compares with the digests. The
coverage of an older build carries **unsalted** digests, and the check tries
both forms, so an older coverage is still read.

**The build log of a protected build is not a secret.** It names no content
(unless `CS2_PROTECT_VERBOSE=1`), and the digests it carries are salted with
the seed of the build -- the same argument as the coverage's. The *seed* is
the secret: it is in the policy (or in the environment), and the policy is
not shipped. A build that protects nothing but leaves the strings in the
image is not fixed by any of this: the image check is what says so.

### 15.7 What the payload build measured

With the policy of `tools/protect/policy.local.json` (the config keys, the
JSON key families and the event-name families), the protected payload build
of this repository produced:

| | |
|---|---|
| selected literals | 484 |
| **protected in this module** | **77** (787 bytes of plaintext, 80 uses, 77 functions -- all of them in `config_store.cpp`) |
| refused `escapes_to_initializer` | 364 (the event names and the event fields) |
| refused `eh_use` | 35 |
| refused `too_short` | 8 |
| the image grew by | 5 632 bytes (0.23 %) |

per module:

| module | protected | refused |
|---|---|---|
| `config_store.cpp` | **77** | 4 `too_short` |
| `events.cpp` | 0 | 90 `escapes_to_initializer` |
| `state_reader.cpp`, `esp_world.cpp`, `input.cpp`, `payload_main.cpp` | 0 | 65 `escapes_to_initializer` each |
| `settings_access.cpp` | 0 | 14 `escapes_to_initializer` |
| `service.cpp` | 0 | 24 `eh_use`, 3 `too_short` |
| `snapshot_json.cpp` | 0 | 8 `eh_use` |
| `esp_world_class.cpp` | 0 | 1 `too_short` |
| `overlay.cpp` | 0 | 1 `eh_use` |
| `payload_main.cpp` | 0 | 2 `eh_use` |

**"Protected in this module" is not "absent from the image", and the
difference is bigger than it looks.** The gate is per module, but a string
literal is `linkonce_odr` (§15.2): it exists in **every** module that uses
it. If module A protects a literal and module B refuses it, B's copy
survives the link and **the plaintext is in the image**. And the MSVC ABI
merges a literal with a **longer** literal that ends with it
(`movement_thirdperson_fov` holds `thirdperson_fov` at offset 9), so the
bytes of a literal the pass deleted survive as the **tail** of the longer
one.

The image check therefore measures the image, and the scan it does is at
**every offset** of every NUL-terminated run of the image: a literal that
is in the image as a substring *is* in the image. The measurement of the
build above is:

| | |
|---|---|
| protected (the pass's claim, per module) | **77** |
| **really absent from the protected image** | **0** |
| still in the protected image | **77** |

**Not one of the 77 literals left the image.** The pass deleted the
literal's own symbol and the decode is correct, but the bytes of every one
of them are still in the shipped DLL. The check says so, and it prints the
number the scan produced:

```
check_payload: 0 of the 77 protected strings are absent from the protected image
check_payload: 77 of the 77 protected strings are still in the protected image
```

The 77 survivors have **two** causes, and they are different:

* **35** are refused in **another module** (`escapes_to_initializer` or
  `eh_use`), so that module keeps its copy and the linker keeps the
  COMDAT. This is the payload's own data model (§15.2).
* **42** are protected in **every** module that uses them, and their bytes
  survive as the **tail of a longer literal** of the image. This is the
  MSVC ABI's string merging, and the pass cannot see it: it sees one
  module at a time, and the longer literal may well be in another module.

An independent measurement (a byte scan of the image for the plaintext of
every one of the 77 literals, with no hashing at all) gives the same
answer, and it counts the **occurrences**. The numbers below are the
**debugoptimized** build (the project's default); a release build holds the
same literals in a different shape and the counts differ (734 of 768):

| | the protected image | the unprotected one |
|---|---|---|
| plaintext occurrences of the 77 literals | **858** | 952 |

The pass removed its own copies (94 occurrences), and every one of the 77
literals is still greppable:

```
thirdperson_fov  protected: 2   unprotected: 3
chams_mode       protected: 2   unprotected: 3
color_enemy      protected: 2   unprotected: 3
box_style        protected: 1   unprotected: 2
radar            protected: 5   unprotected: 6
health_bar       protected: 2   unprotected: 2
```

**What the check still fails on.** A literal that is in the image as a
**whole string** and that nobody refused is the pass's own copy: that is a
failure ("the protection did nothing"), and the check fails the build. A
literal that is only *inside* another string of the image is greppable,
but the pass cannot remove another string's bytes: it is reported, with
its coverage hash, and the build goes on. The same scan runs on the
**unprotected** image, so that the comparison cannot pass vacuously -- and
it runs at every offset there too, so that the verdict does not depend on
the layout of the image (a release build and a debug build hold the same
literals in a different shape).

**The strings leaving the image is a separate task — and it is not a task
anymore.** It is not a bug of the pass, and this round does not fix it:
making the bytes of a literal leave the image needs the *containing* literal
to be transformed as well (and the containing literal may be in another
module, and the pass is per module), and it needs the payload's data model to
stop pinning the feature keys into a global initializer.

What the follow-up actually is, and why it was dropped: the feature-key names
are **displayed** -- the in-process menu draws them and so does the overlay's
fallback menu -- so a name that is displayed **must exist in a shipped
image**. The best that can be done is to hold it in **one** artifact instead
of two, and that is a **product** decision, not a pass decision. The
reasoning, with the measurements, is in
[`docs/orchestration/plan-protect.md`](../orchestration/plan-protect.md) §1.2.

What is left in the string domain, and is small: the **internal** keys (the
JSON keys, the convar names, the console commands) are never displayed and
could be compared by hash; and the **event/field tables** (364 literals) are
Valve's own names and could be read from the game's resource files at run
time instead of shipped as a generated copy.

**The feature keys are pinned by the payload's own data model.**
`overlay/include/cs2/overlay/settings_table.hpp` holds

```cpp
inline constexpr std::array<SettingInfo, 87> SETTING_TABLE = {{
    {"box", "", SettingType::flag, 0, 1, 1},
    {"health_bar", "", SettingType::flag, 1, 1, 1},
    ...
```

and `payload/control/include/cs2/control/settings_access.hpp` includes it.
That is exactly the shape the specification describes in §3.5: a
`constexpr` array of `string_view` **bakes the pointers into a global
initializer**, and a pointer to a decoded buffer cannot exist before the
program runs. The feature keys -- the task's declared target -- are
therefore **refused** with `escapes_to_initializer` in the modules that
use the table, they stay in the image, and the strings an analyst greps
for (`health_bar`, `ammo_bar`, `only_enemies`, `show_dead`, `hitbox_mode`,
`weapon`, `name`, `flags`, `mode`, `enabled`, `distance`, `menu`, `radar`,
`movement`, ...) are all still in the shipped DLL.

**That is not a bug of the pass, and this round does not fix it.** The fix
is a change to the **payload's data model** (an index table plus a
runtime-decoded table instead of a `constexpr` array of `string_view`), and
that is a **separate task**: it contradicts the "no constructor" rule and it
touches the payload, which this task does not own. What this round does is
say it out loud, measure it, and make the check report the number that
really left the image.

The 364 `escapes_to_initializer` refusals are the event names and the event
fields: they are baked into the `constexpr` arrays of
`event_names.hpp`, so they cannot be protected by this design at all
(§15.2). The 35 `eh_use` refusals are the price of the rule: the payload is
built with `/EHsc`, so a function that owns a `std::string` has a cleanup
landing pad, and the rule refuses every literal one of them uses. A literal
that is protected in one module and refused in another is legitimately still
in the image, and `check_payload.py` says so -- with its coverage hash --
instead of failing the build. A literal that is protected in every module
and that is still in the image is the **tail merging** of the MSVC ABI, and
the check says that too.

## 16. The layout: the file on disk is not a signature

The seed drives the protection's constants, its keys and its tables -- but
until T24 it drove **nothing of the code**. The measurement is in
[`docs/orchestration/spec-t24-protect-polymorphism.md`](../orchestration/spec-t24-protect-polymorphism.md)
§1: two builds of the payload with two different seeds had a **byte
identical `.text`** (1,707,008 bytes, 69.8 % of the file) and differed in
441 bytes of `.rdata` -- the plugin's own globals. A scanner that hashed
the code would have matched every build, and the mixer's two constants sat
at the same four addresses in every build and in every version.

### What the pass does

At the end of the transform phase, the pass **permutes the module's
function list** with a permutation derived from the module's seed, and it
reports how many functions moved:

```
[cs2-protect] solver.cpp: layout functions=69 moved=66 seed=0xf2ba62bd9b1ff889
```

The permutation keeps **every name**, so the coverage, the report, the
policy matching and the payload check (which works on addresses, not names)
keep working untouched. That is the whole reason it is the chosen
mechanism.

**Why the permutation and not the renaming.** Both were measured:

* the **object writer** emits the functions in the module's function list
  order, and the **linker** keeps the order of an object's sections: a pass
  that reorders the list moves the code in the image;
* **renaming** the symbols per build moves **nothing**: the same bodies
  with the names permuted give a byte identical image. The layout follows
  the *definition* order, not the name.

The permutation is a Fisher-Yates walk over the module's definitions, with
the module's seed as its only input. A permutation that leaves the order
unchanged is a no-op, so the pass swaps the first two definitions when the
walk moved nothing: **a module with more than one definition always moves
at least one function**, and the payload check asserts it (§11).

### What it does not hide

The code is **identical**, only its position in the file changes. An analyst
reads the same functions. What the permutation buys is the absence of a
**fixed-offset** signature, nothing else.

**The reach is not bounded, and it is stated plainly.** The permutation is
*inside* a translation unit: it moves a function within its own unit's block
of code, so it moves the offsets of that unit's functions and the
displacements of every reference to them. It is **not size-neutral**, and
that is what the reach comes from: when a function moves, the encoded
displacements of the references to it change, and a short jump that used to
fit becomes a near jump. The permuted unit's block therefore changes
**size**, and **everything after it shifts**.

So the honest statement is: *the code the plugin does not compile is still
the same code, with the same internal order, but its **offsets move** with
everything else*. The MSVC STL's `vector_algorithms.obj` is 11.2 % of the
`.text` and the imgui is 43 % of it, and **neither keeps its place**: the
imgui's code moves by hundreds of kilobytes between two builds of two seeds
(the measurement is below). A signature that names the offset of an STL
function, or of an imgui function, does **not** survive this task — not
because the permutation reaches into the STL, but because everything after
the first permuted unit shifted with it.

What the permutation does **not** buy: a stable layout for the units the
plugin does not compile, and a *partial* immunity for the part of the image
that lies before every permuted unit. That is why the mixer's constants
matter (§10.2): the *protection's* bytes are gone whatever the layout does.

### The measurement

The acceptance criterion is a **measurement**, not a promise: two builds
with two seeds, compared per section.

```
python tools/protect/compare_builds.py --section .text \
    build-payload-t24a/payload.dll build-payload-t24b/payload.dll
```

The floor of the criterion is 0.5 % of the section (8,535 bytes of
1,707,008): a floor below it would be satisfied by the 37 bytes of the
rotation alone, which is not what the task is for.

**What was measured** (the payload of this tree, 2026-10-22, two builds
with the seeds `0x1111111111111111` and `0x2222222222222222`, the policy of
`tools/protect/policy.example.json`):

| what | measured |
|---|---|
| the `.text` of the two builds | **1,426,316 bytes differ — 83.53 % of the section** (the floor is 0.5 %) |
| the modules the pass permuted | 75, with 3,145 definitions in all |
| the definitions that moved | 3,078 (97.9 %); **no module with more than one definition moved none** |
| the mixer's contribution alone (the permutation disabled) | **37 bytes of `.text`** (the rotation's immediate, one per dispatch site) and 504 bytes of `.rdata` — 541 bytes of the image |
| two builds with the **same** seed | **0 bytes** differ apart from the linker's per-link stamps |

The last row is the rule of §4: the seed still fully determines the image,
and two releases must not share a seed.

