# T18 — the protection pass: foundation (plugin, build wiring, selection)

**Goal:** stand up an LLVM pass plugin that the payload build loads, that selects
functions to protect, and that reports what it selected — **without changing the
generated code yet**. This is the plumbing task. The transforms (call
encryption, virtualization) are T19 and T20.

Everything below was verified empirically on this machine before the task was
written. Do not re-derive it; do not "simplify" it away.

---

## 1. Verified facts (do not contradict these)

| Fact | How it was verified |
|---|---|
| `C:\Program Files\LLVM` is a **clangd-only** 22.1.8 install: no `include/llvm`, no `LLVMConfig.cmake`, no `opt.exe`, no `FileCheck.exe`. | directory listing |
| The dev SDK is the **release tarball**, extracted at `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc` (headers, 145 `LLVM*.lib`, `opt.exe`, `llvm-config.exe`, `LLVMConfig.cmake`). | extracted + listed |
| The payload's compiler is **clang-cl 22.1.8** (`C:\Program Files\LLVM\bin\clang-cl.exe`), and it is what `vcvars64` + `set CC=clang-cl` resolves to. | `where clang-cl` under vcvars |
| **`add_llvm_pass_plugin()` is a no-op on Windows** — it prints *"Loadable modules not supported on this platform"* because CMake's `MODULE` library type is unsupported by the MSVC toolchain. The plugin must be a `SHARED` library (a `.dll`). | cmake configure output |
| The SDK's exported CMake targets hardcode the **build machine's** DIA SDK path (`.../Visual Studio/2022/Enterprise/DIA SDK/...`), which does not exist here. The `LLVMDebugInfoPDB` imported target's `INTERFACE_LINK_LIBRARIES` must be rewritten to the local DIA SDK, or the link fails with *"diaguids.lib, needed by 'Cs2Protect.dll', missing"*. | ninja error + `LLVMExports.cmake:537` |
| LLVM 22 moved the plugin header to **`llvm/Plugins/PassPlugin.h`** (not `llvm/Passes/PassPlugin.h`), and `LLVM_PLUGIN_API_VERSION` is **2**. | compile error + SDK header |
| `llvmGetPassPluginInfo` must be **exported explicitly** (`__declspec(dllexport)`); the SDK header declares it without it, which also produces a `-Wdll-attribute-on-redeclaration` warning that must be silenced. | `llvm-readobj --coff-exports` → `Ordinal: 1 / Name: llvmGetPassPluginInfo` |
| **clang-cl does not accept `-fpass-plugin=`** ("unknown argument ignored in clang-cl"). The working spelling is **`/clang:-fpass-plugin=<dll>`**, which routes the option to the clang driver. Verified through Meson: the pass printed during `meson compile`. | meson build output |
| A `cl::opt` **inside the plugin is NOT reachable via `-mllvm`** — a statically linked plugin has its own `cl` registry, so `-mllvm -cs2-protect-enabled=false` fails with *"Unknown command line argument"*, identically to a bogus option. ⇒ **the plugin's configuration channel is environment variables**, not command-line options. | compile test + control test |
| The release LLVM is built **without RTTI** and with the **static CRT** (`llvm-config --cxxflags` → `-std:c++17 ... /EHs-c- /GR-`). The plugin must be compiled `-fno-rtti -fms-runtime-lib=static` or it will not link / will not match the host's heap. | `llvm-config --cxxflags` |
| The host test suite is **42/42 green** at baseline and must stay green. | `meson test -C build` |
| `__attribute__((annotate("x")))` lands in `llvm.global.annotations`, is keyed by the **mangled** symbol name, and keeps the annotated function alive (no DCE, no inlining). | clang 22.1.8 `-emit-llvm` |
| `-O0` adds `optnone` to every function, and a pass skips `optnone` functions. | LLVM behaviour |

---

## 2. What to build

### 2.1 The plugin — `tools/protect/`

A CMake project (the plugin needs LLVM's own `LLVMConfig.cmake`; Meson has no
usable LLVM dependency — `dependency('llvm')` needs CMake on `PATH` anyway and
buys nothing). The plugin is a **build-time tool**, not a project artifact, so
ADR-0003 (Meson + Ninja) governs the project and the payload, not this glue.

```
tools/protect/
  CMakeLists.txt
  README.md
  src/
    plugin.cpp        # llvmGetPassPluginInfo + PassBuilder wiring
    config.hpp/.cpp   # environment-variable configuration
    selection.hpp/.cpp# which functions are protected
    report.hpp/.cpp   # what the pass reports
```

`CMakeLists.txt` must reproduce the verified recipe:

- `find_package(LLVM REQUIRED CONFIG)`; `include(AddLLVM)`.
- `add_library(Cs2Protect SHARED ...)` — **not** `add_llvm_pass_plugin`, which
  is a no-op on Windows.
- Rewrite the `INTERFACE_LINK_LIBRARIES` of every imported LLVM target that
  mentions the DIA SDK, substituting the local one
  (`$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64/diaguids.lib`). The generic fix is a
  loop over `get_property(... DIRECTORY PROPERTY IMPORTED_TARGETS)` plus
  `LLVMDebugInfoPDB`, which is where the bad path actually lives.
- `target_include_directories(... SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})`.
- Link the LLVM component libs. `llvm_map_components_to_libnames` is available
  but pulls in the broken DIA path; filtering the list is not enough on its
  own, the imported target's property has to be rewritten.
- `-fno-rtti -fms-runtime-lib=static`; C++20.
- Output name `Cs2Protect.dll`, no `lib` prefix.

`src/plugin.cpp` must:

- export `llvmGetPassPluginInfo` with `__declspec(dllexport)` and silence
  `-Wdll-attribute-on-redeclaration`;
- register the pass on an **extension point that runs during a normal
  `-O2` compile** — `registerPipelineStartEPCallback` is the verified working
  choice. A pass registered only via `registerPipelineParsingCallback` will
  **not** run unless the pipeline is requested by name, and the "protected"
  build will then be byte-identical to the baseline. This is the single easiest
  way to ship a no-op protection, so make the registration unconditional.

`src/config.hpp/.cpp` — configuration **by environment variable** (see §1):

| Variable | Meaning |
|---|---|
| `CS2_PROTECT_CONFIG` | path to a JSON policy file; absent ⇒ the pass is a no-op |
| `CS2_PROTECT_SEED` | hex seed; absent ⇒ derive one from the module id + function name (deterministic, so a build is reproducible) |
| `CS2_PROTECT_VERBOSE` | `1` ⇒ print the selection report to stderr |

`src/selection.hpp/.cpp` — two selection sources, both optional:

1. **Source annotation.** `__attribute__((annotate("cs2.protect")))` on a
   function definition. Read it from `llvm.global.annotations`. The key is the
   **mangled** name, so a C++ function must be matched by its mangled symbol —
   an `extern "C"` export, or an explicit mangled name in the policy file.
2. **Policy file.** A JSON file with, at minimum:
   ```json
   { "protect": ["cs2::aim::solve", "cs2::game::fnv1a_hash"],
     "deny":    ["cs2::self_unmap"],
     "seed":    "0xA04252B187478C00A40BC6E8B04F1C9A" }
   ```
   Entries are matched against the **mangled** symbol name and support the
   glob `*` and `?`. A `protect` entry that matches **nothing** is an error
   (a typo must not silently protect nothing) — report it and fail the
   compile.

`src/report.hpp/.cpp` — prints, to stderr, one line per selected function:
the mangled name, the selection source (annotation / policy), and the level.
With `CS2_PROTECT_VERBOSE=1` it also prints the functions that were *skipped*
and why (not annotated, denied, `optnone`, `noinline`-unsafe, …). The report is
the acceptance evidence for this task.

### 2.2 The build script — `scripts/build-protect-plugin.py`

A Python script (the repo's scripting language is Python; `scripts/` is full of
`.py`) that:

- locates the SDK: `$CS2_LLVM_SDK` if set, else the default
  `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc`; **fails loudly**
  with the download URL and the SHA-256 if it is missing;
- locates `cmake` (VS ships it at
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`);
- configures and builds `tools/protect` with `-G Ninja`,
  `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` (the **same**
  compiler that will load the plugin — the ABI and the CRT must match),
  `-DLLVM_DIR=<sdk>/lib/cmake/llvm`;
- prints the resulting DLL path on stdout so a caller can capture it.

The script must be idempotent and must not need the payload's build
environment.

### 2.3 The Meson wiring — `payload/meson_options.txt`, `payload/meson.build`

Two new options in `payload/meson_options.txt`:

```
option('protect',        type: 'boolean', value: false,
       description: 'Apply the LLVM protection pass to the payload.')
option('protect_plugin', type: 'string',  value: '',
       description: 'Path to Cs2Protect.dll (the LLVM pass plugin).')
```

In `payload/meson.build`, on the `shared_library('payload', ...)` target:

- when `protect` is true and `protect_plugin` is non-empty, add
  `cpp_args: ['/clang:-fpass-plugin=' + <abs path>]` — **the `/clang:` spelling
  is required**; plain `-fpass-plugin=` is silently ignored by clang-cl;
- the plugin path must be **absolute** and the flag must be one argument (no
  spaces, no commas);
- **the plugin DLL must be a build dependency of the payload target.**
  Ninja does not track a DLL mentioned only inside a compiler flag, so a
  changed plugin silently produces a stale object — a wrong-results failure
  mode with no error. Use `depend_files:` (or `build_depends:`) to make the
  payload depend on the plugin file;
- when `protect` is false, the target must be **byte-identical** to today's
  build. No flag may be added in that case.

Note the payload is a **separate Meson project** (`meson setup build-payload
payload`), so the options go in `payload/meson_options.txt`, not the root one.

### 2.4 Documentation

`tools/protect/README.md` — how to obtain the SDK, how to build the plugin, how
to enable it, what the environment variables mean, and the verified facts from
§1 that a future reader will otherwise re-derive (especially `/clang:` vs
`-fpass-plugin=`, and "the plugin's `cl::opt` is not reachable via `-mllvm`").

---

## 3. File ownership

You own exactly these paths. If you need anything else, **stop and report it**
instead of editing it.

- `tools/protect/**`
- `scripts/build-protect-plugin.py`
- `payload/meson_options.txt`
- `payload/meson.build`
- `docs/orchestration/spec-t18-protect-foundation.md` (the orchestrator writes this)

Do **not** touch: `payload/**` sources, `overlay/**`, `injector/**`, the root
`meson.build`, `AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, any
`docs/orchestration/spec-*.md` other than your own.

---

## 4. The verify loop

```powershell
# 1. The host build and the full test suite must stay green (42/42 at baseline).
cd <your worktree>
meson compile -C build
meson test -C build

# 2. The plugin builds.
python scripts/build-protect-plugin.py

# 3. The payload builds with the pass enabled and the pass reports its
#    selection. (Needs vcvars64 + clang-cl, as the existing verify loop does.)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload-protect payload --wipe -Dprotect=true -Dprotect_plugin=<abs path to Cs2Protect.dll>"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload-protect"

# 4. The protected payload is byte-identical to the unprotected one, because
#    this task changes no code.
```

Do **not** run `meson setup --wipe` on `build`, `build-payload`, `build-overlay`
or `build-injector` — other agents may be using them. Use a fresh directory
name for step 3.

---

## 5. Acceptance criteria

1. `python scripts/build-protect-plugin.py` prints the path to a
   `Cs2Protect.dll`, and `llvm-readobj --coff-exports <dll>` lists
   `llvmGetPassPluginInfo`.
2. `meson compile -C build` is clean and `meson test -C build` is **42/42**.
3. A payload build with `-Dprotect=true -Dprotect_plugin=<path>` prints the
   selection report (at least one line) to stderr during the compile, and the
   report names a function that is actually in the payload.
4. The same payload build with `-Dprotect=false` produces a **byte-identical**
   `payload.dll` to the unprotected build. (Prove it: hash both.)
5. A policy file with a `protect` entry that matches nothing **fails the
   compile** with a clear message. (Prove it: show the failure.)
6. `CS2_PROTECT_VERBOSE=1` prints the skip reasons.
7. Nothing outside §3 was touched.

---

## 6. Rules

- `docs/style-guide.md` is normative: C++23, `snake_case` functions,
  `PascalCase` types, trailing return types, `std::expected` for fallible
  calls, `[[nodiscard]]`, `noexcept` where it cannot throw, no new global
  mutable state, no `using namespace std;`, fully qualified `std::`.
  The plugin is a build-time tool, but it lives in this repo — hold it to the
  same standard.
- Errors are values, not exceptions. The plugin must never throw out of a
  pass; a malformed policy file is a **reported error**, not an exception.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`; never
  amend or rebase.
- Commit message style is `type(scope): summary` — e.g.
  `feat(protect): plugin skeleton, env-var config and annotation selection`.
- **Prove each new test can fail**: break it on purpose, watch it fail, revert.
- You cannot launch CS2 from the agent shell (session 0, no GPU). Say plainly
  what is unverified.

---

## 7. Report

What changed, per file. The design decisions (especially: why the plugin is
CMake and not Meson, why the configuration is environment variables and not
`-mllvm`, and why the flag is `/clang:-fpass-plugin=`). The exact commands you
ran and their output. How you proved each acceptance criterion, including the
byte-identical proof and the "no match ⇒ fail" proof. What you could not
verify.
