# The protection pass — `Cs2Protect.dll`

An LLVM **pass plugin**: the payload build loads it with
`-fpass-plugin=`, and it selects the functions that a later task (T19:
call encryption, T20: virtualization) will protect. This task is the
plumbing only — the pass **selects and reports**, it changes no code.

```
tools/protect/
  CMakeLists.txt        the plugin build (CMake, because it needs LLVMConfig.cmake)
  README.md             this file
  check_policy.py       the whole-build check: every policy entry matched something
  compare_builds.py     the acceptance helper: compares two built payloads
  verify_plugin.py      the guard: the plugin did not change under the build
  policy.example.json   an example policy
  src/
    plugin.cpp          llvmGetPassPluginInfo + the PassBuilder wiring
    config.hpp/.cpp     the environment configuration and the policy file
    selection.hpp/.cpp  which functions are protected
    report.hpp/.cpp     what the pass reports
  tests/
    protect_tests.cpp   the plugin's own tests (CTest)
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

They cover the policy reader (a malformed policy is a *reported* error,
never a crash), the qualified name, the selection (an entry that named a
function that cannot be protected is *blocked*, not matched) and the
coverage file (two modules with different identifiers write two files).

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
  `call`.
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

The pass prints one line per selected function to stderr:

```
[cs2-protect] solver.cpp: protect ?solve@aim@cs2@@YA?AUSolveResult@12@… (cs2::aim::solve) source=policy level=call seed=0xa40bc6e8b04f1c9a note=noinline-missing
```

* `source` — `annotation` or `policy`.
* `level` — `call` or `virtualized`.
* `seed` — the effective seed of the function.
* `note=noinline-missing` — the function is not `noinline`, so the transform
  has to add it (otherwise the body could be inlined away and the
  protection would be lost).

With `CS2_PROTECT_VERBOSE=1` the pass also prints every function it
skipped and why (`not_annotated`, `denied`, `optnone`, `always_inline`,
`declaration`, `intrinsic`), the state of every policy entry (`matched`,
`blocked`, `unmatched`), and a summary line per module.

## 8. What the pass does **not** do

* It changes **no code**. The pass returns `PreservedAnalyses::all()`, so
  the protected payload is the unprotected payload. The transforms are T19
  and T20.
* It runs from `-O1` up. At `-O0` the driver uses its O0 pipeline, which
  does not invoke the pipeline-start extension point, so the pass does not
  run at all.
* It does not change the build when `protect` is off.

## 9. The coverage directory

The coverage is one file per **(policy, module)**:

* the **policy digest** is part of the file name, so the coverage of one
  policy is never mistaken for the coverage of another;
* the **module identifier** is part of it, so two sources never write the
  same file.

The name does not depend on the build directory, so a rebuild in another
build directory **overwrites** the file instead of adding to the directory:
the directory holds one file per source, not one per build.

The directory is keyed by the policy path (or `CS2_PROTECT_COVERAGE`), so an
edited policy leaves the files of the previous version behind. The checker
**prunes** them after it has read them — the files of the *current* policy
are left alone, so a rebuild that compiles nothing still finds its
evidence. The pruning runs after the build, so it cannot race with a
compile that is writing its coverage.

A source that is compiled into **two targets of the same build** would write
the same file. The coverage of a module does not depend on the target (the
policy's `module` scope names the *source* file), so the content is the
same; the plugin writes the whole file in one go, so two such compiles
cannot interleave their writes.

## 10. For the next task

The rewriting pass (T19: call encryption, T20: virtualization) **must not**
run at the pipeline start:

* it has to run at `registerOptimizerLastEPCallback` — the optimiser's
  `SimplifyIndirectCalls` would rewrite an encrypted indirect call back
  into a direct one, and `IndirectBr`, `MergeICmps` and friends would
  happily undo the rest of the transform;
* an **early** point has to mark the selected functions `noinline`,
  otherwise the inliner removes the body before the late point can rewrite
  it (the report already says `note=noinline-missing` for a function that
  is not `noinline`);
* the module flag `cs2.protect.done` makes the pass safe to run twice: the
  pipeline-start extension point fires in the LTO pre-link phase as well,
  so a module can reach the pass twice, and a rewritten body must not be
  rewritten again.

## 11. Verified facts (do not re-derive them)

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
| `__attribute__((annotate("x")))` lands in `llvm.global.annotations`, keyed by the mangled symbol | the annotation selection source |
| `-O0` adds `optnone` to every function, and a pass skips `optnone` functions | the `optnone` skip reason |
| The SDK's `llvm::json::parse` **recurses** | the policy file's nesting depth has to be bounded *before* the text reaches it |
| The SDK's `llvm::json::parse` keeps the **first** of two equal keys, Python's `json` keeps the **last** | a duplicated key is an error on both sides |

## 12. Known limits of the build wiring

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
