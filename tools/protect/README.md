# The protection pass — `Cs2Protect.dll`

An LLVM **pass plugin**: the payload build loads it with `-fpass-plugin=`,
and it protects the functions a policy names.

* the **selection** phase (the pipeline start) picks the functions, marks them
  `noinline` so that they survive to the late point, and reports them;
* the **transform** phase (the optimizer last) rewrites the calls of a
  selected function into **indirect dispatches through an encrypted
  descriptor**, so that the static call graph is no longer readable from the
  binary — **the calls of a callee that is defined in the same module
  only** (§10.6 says how much that is worth in the current payload: 37 of
  the 332 call sites of the selected functions, and they are predominantly
  destructor calls; the cross-translation-unit calls stay direct).

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
    vm.hpp/.cpp         the front end of the virtualizer: the ISA, the
                        eligibility gate, the lowering and the encoder
    isa.def             the instruction set of the virtual machine: the one
                        source of truth for the opcode numbers
  tests/
    protect_tests.cpp   the plugin's own tests (CTest)
    ir_test.py          the shape of the rewritten IR
    diff_test.py        the differential test: the same fixture, with and without
    coverage_test.py    the readers of the coverage: a malformed file is reported,
                        the coverage of another tree is not counted
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
reasons) and the IR of the analysis (the verdict of every function of a
fixture, and the proof that the analysis does not touch the IR).

There are **five** tests:

```
  1/5 Test #1: protect-tests ....................   Passed
  2/5 Test #2: protect-ir-test ..................   Passed
  3/5 Test #3: protect-diff-test ................   Passed
  4/5 Test #4: protect-coverage-test ............   Passed
  5/5 Test #5: protect-vm-ir-test ...............   Passed
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
| `CS2_PROTECT_SEED` | hexadecimal seed. Absent ⇒ derived per function from the module id and the symbol |
| `CS2_PROTECT_VERBOSE` | `1` ⇒ print the skipped functions and why |
| `CS2_PROTECT_COVERAGE` | where the per-module coverage goes (default: under the temp directory) |
| `CS2_PROTECT_SOURCE_ROOT` | the tree that is being built. **The checkers read it** (the plugin does not): the coverage of another tree is not counted (§9) |

The seed precedence is `CS2_PROTECT_SEED` → the policy's `seed` → the
derived per-function seed (FNV-1a over the module id and the symbol, so a
build stays reproducible).

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
* `deny` wins over every selection source.
* An unknown key is an error, not a warning: a typo in a policy file must
  not be ignored silently.
* The seed is **64 bits**. A longer seed (the example above carries a
  128-bit value) keeps its low 64 bits, and the plugin says so:
  `[cs2-protect] the policy's seed: the seed is 64 bits; the high bits of
  '0xA042…' are dropped`.

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
[cs2-protect] solver.cpp: transform ?solve@aim@cs2@@YA?AUSolveResult@12@… (cs2::aim::solve) rewritten=7 skipped=88 indirect_parameter=9 external_callee=77 invoke=2
[cs2-protect] solver.cpp: transform table=7 descriptor=11
```

* `rewritten` — the calls that became indirect dispatches.
* `skipped` — the calls that were left alone, **per reason**:
  `variadic_callee`, `indirect_parameter`, `musttail`, `external_callee`,
  `optnone_callee`, `invoke`, `indirect_callee`.
* `table` and `descriptor` — the size of the two globals the module gained.

A selected function that did not survive to the late point is reported as
`vanished`, and a level that is not implemented is reported as
`level=virtualized not-implemented`.

With `CS2_PROTECT_VERBOSE=1` the pass also prints every function it
skipped and why (`not_annotated`, `denied`, `optnone`, `always_inline`,
`declaration`, `intrinsic`), every skipped **call** of the transform (with
its callee), the state of every policy entry (`matched`, `blocked`,
`unimplemented`, `vanished`, `unmatched`), and a summary line per module.

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
@cs2.protect.mixmul = private unnamed_addr constant i64 <the multiplier>
@cs2.protect.mixxor = private unnamed_addr constant i64 <the xor constant>
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
| `external_callee` | the callee has no code in this module: it is an import, an intrinsic or an `available_externally` function (the last one would resolve to address zero in the table) |
| `intrinsic_callee` | the callee is a compiler intrinsic: the backend expands it, there is nothing to call |
| `invoke` | the pass does not model the EH edges yet |
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
module** (the specification's §2.4). The calls that cross a translation
unit stay **direct**: the table of a module holds only the callees of that
module.

In the current payload, with the policy of `tools/protect/policy.local.json`:

| selected function | rewritten | skipped |
|---|---|---|
| `cs2::aim::solve` | 7 | 88 (77 cross-module, 9 with an `sret` parameter, 2 `invoke`) |
| `cs2::aim::occlude_polygon` | 21 | 146 |
| `cs2::aim::solve_backtrack` | 4 | 15 |
| `cs2::aim::project_capsule` | 3 | 45 |
| `cs2::aim::DamageField::hit_probability` | 2 | 1 |
| `cs2::aim::penetration_survival` | 0 | 0 |
| **total** | **37** | **295** |

So **37 of the 332 call sites** of the selected functions became indirect
dispatches. The table of `solver.cpp` holds seven entries, and they are six
destructors and one constructor: the calls that carry the aim call graph are
the **cross-module** ones, and they stay direct calls in the image. The
static call graph of the interesting code is therefore still readable; what
leaves the binary is the cleanup code.

A follow-up is the only way to make the claim true: hoist the shared aim
code into one translation unit, or rewrite the cross-module calls through a
per-module table (which needs the table of *another* module — a seam the
format of the descriptor has to keep open).

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
  hold exactly the values the pass encrypted the descriptor with;
* the number of the **encrypted dispatches** is the number of the calls the
  pass reports having rewritten (read from the coverage files), and it is
  zero in the unprotected image;
* every dispatch **decodes to a callee of the image**: the descriptor,
  decoded with the key the pass used, names an entry of the table of the
  image, and that entry is the code of the image.

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

Two of the opcodes are **not** in the specification's table, and both are
there because the table cannot express what the lowering has to express:

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
  entry per call site, and the bytecode holds the index.

A `switch` is lowered to a chain of `Cmp` + `BrIf`. It is not refused.

### 14.2 The encoding

Every instruction is a fixed **24 bytes**:

```
u8 opcode | u8 width | u8 flags | u8 pad | u16 dst | u16 src1 | u16 src2 | u16 src3 | u32 aux | u64 imm
```

* `opcode` is the permuted opcode (§14.5);
* `width` is the width of the operation, in bytes (1, 2, 4 or 8);
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
u32 reserved
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
[slot_count, +arg_slots)         the argument stack     (64 cells)
[slot_count + arg_slots, +call)  the call scratch       (64 cells)
```

**384 cells, 3072 bytes.** The specification's §6 table says 2560 bytes,
and its formula lists the three regions; 2560 is the size of the *two*
regions (256 value cells and 64 argument cells) and leaves no room for
the call scratch that §3.2 names. The front end therefore uses the
three regions the layout describes, and the report says 3072.

* the **value slots** hold the values: one slot per SSA value, plus the
  slots of the constants, of the globals and of the temporaries the
  lowering needs;
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

The checks run in the order of the specification's table, and the first
one that holds is the reason that is reported. The two budget checks are
the exception: the argument count is raised in the call scan and the slot
count when the allocator runs out, so a function that exceeds **both** is
reported as `too_many_arguments`.

| reason | refused because |
|---|---|
| `eh_pad` | the function has an EH pad, an `invoke`, a `landingpad` or a `resume`: the interpreter cannot model unwinding |
| `variadic_function` | the function itself is variadic |
| `variadic_callee` | it calls a variadic callee: the indirect call cannot reproduce the variadic ABI |
| `indirect_call` | it makes an indirect call: there is no relocatable address for the pool |
| `indirect_parameter` | a callee has a `byval` / `sret` / `inalloca` / `preallocated` parameter: Windows x64 rejects these in an indirect signature |
| `musttail` | the tail-call contract does not survive the rewrite |
| `volatile_access` | a volatile access: the ordering guarantees are not modelled |
| `atomic_access` | an atomic access: the ordering guarantees are not modelled |
| `dynamic_memcpy` | a `memcpy` / `memset` with a **non-constant** length: it would lower to a CRT call |
| `unsupported_type` | a float, a vector, an aggregate or an integer of more than 64 bits: this task handles integers of at most 64 bits and pointers only |
| `address_taken` | the address of the function is taken (`&F`): the rewrite changes what the address means |
| `too_many_slots` | the function needs more slots than the budget |
| `too_many_arguments` | a call needs more arguments than the budget |
| `recursion` | the function calls itself: the frame would multiply by the recursion depth |
| `virtualized_callee` | the function calls a function that is itself selected at the `virtualized` level: the interpreter would have to call into another interpreter |
| `unsupported_instruction` | the catch-all: an IR instruction the lowering does not model. The lowering has a default branch that refuses. |
| `vanished` | the function did not survive to this phase: there is nothing to virtualize. The transform reports the same thing the same way. |

The rules of the transform (§10.4) are **reused**, not written twice:
the variadic callee, the indirect parameter, the `musttail` call and the
indirect call are the same rules, and the same reasons. The gate calls
the transform's `skip_reason` (`src/transform.hpp`) and maps its reasons
onto the virtual machine's, so the two phases cannot drift apart. The
**order** is the transform's order — the variadic callee, the indirect
parameter, the `musttail` call and the indirect call — and it is the
one documented exception to the order of the table above: the table
lists the indirect call before the indirect parameter, and the two
phases have to agree.

What the lowering models: the integer and pointer arithmetic, the
comparisons, the conversions, the loads and the stores, the `getelementptr`
(both the constant and the variable indices), the branches, the `switch`,
the `select` (a branch), the `phi` (a parallel copy, §14.3), the
`freeze`, the `memcpy` and the `memset` with a constant length, the calls
and the `ret`. What it refuses: the floats, the aggregates, the
`alloca` (the frame is a constant and has no room for the locals), the
`memmove` (it may copy overlapping ranges), the atomic and the volatile
accesses, and everything else.

### 14.8 What the analysis says about the payload

The analysis runs for **every** selected function, whatever its level,
and it is in the report (§7) and in the coverage (§9). The coverage
gains a `vm` line per function:

```
vm ?newest@RecordRing@aim@cs2@@QEBAPEBURecord@23@XZ verdict=virtualizable slots=7 frame=3072 bytecode=392 instructions=15 calls=2
vm ?solve@aim@cs2@@YA?AUSolveResult@12@… verdict=refused reason=eh_pad detail=the_function_has_an_EH_edge slots=0 frame=3072 bytecode=0 instructions=0 calls=0
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

The gate is not empty, though. Over the whole `payload/aim` module
(2490 functions, the standard library's templates included), it admits
1878 of them and refuses 612:

| reason | refused |
|---|---|
| `unsupported_instruction` | 264 |
| `unsupported_type` | 122 |
| `eh_pad` | 115 |
| `indirect_parameter` | 71 |
| `dynamic_memcpy` | 23 |
| `address_taken` | 8 |
| `recursion` | 5 |
| `indirect_call` | 2 |
| `atomic_access` | 1 |
| `variadic_function` | 1 |

and the admitted ones are the small integer helpers:
`cs2::aim::RecordRing::newest` (7 slots, 392 bytes of bytecode, 15
instructions, 2 calls), `cs2::aim::RecordRing::oldest` (the same),
`cs2::aim::Record::~Record` (4 slots, 176 bytes, 6 instructions, 1
call) and the `std::span` / `std::array` accessors of the aim types.

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
