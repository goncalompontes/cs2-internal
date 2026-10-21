# cs2-internal — the protection pass: handover

> Written at the end of the T18/T19 session (2026-10-21). The next session
> starts here. Read this, then `docs/orchestration/plan-protect.md` for the
> map, then `docs/orchestration/spec-t20-protect-virtualization.md` for the
> next task.

## 1. What exists

An **LLVM pass plugin** (`tools/protect/`, built by CMake) that the payload
build loads with clang-cl. It selects functions to protect and rewrites the
calls inside them into indirect dispatches through an encrypted descriptor, so
the static call graph of those calls is no longer readable in the binary.

| Task | State | Where |
|---|---|---|
| **T18** — the foundation: the plugin, the env-var policy, the selection, the report, the Meson wiring | **merged** | `docs/orchestration/spec-t18-protect-foundation.md` |
| **T19** — the call encryption | **merged** | `docs/orchestration/spec-t19-protect-call-encryption.md` |
| **T20** — the virtualization (IR -> bytecode -> a freestanding interpreter) | **spec written, not started** | `docs/orchestration/spec-t20-protect-virtualization.md` |

The merge commits are `03504fd` (T19) and `a3403ee` (the T19 fix round).

## 2. How to use it

```powershell
cd C:\Users\gpmpo\cs2-internal

# the plugin (the LLVM dev SDK must be present - see section 4)
$plugin = python scripts\build-protect-plugin.py      # prints only the DLL path

# a protected payload
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"
```

`CS2_PROTECT_CONFIG` must be set for **both** the configure and the compile
step: the plugin reads it in the compiler process. The build fails at the
configure step if it is missing (a protected build that protects nothing is
indistinguishable from an unprotected one, so the wiring refuses it).

`tools/protect/policy.local.json` is **gitignored** and is this machine's
choice of what to protect. It currently selects six `cs2::aim::*` functions and
denies `cs2::loader::self_unmap`.

The full documentation is `tools/protect/README.md` - read it before changing
the pass.

## 3. The checks

The protected build **checks itself**. `meson compile` on a protected build
runs `check_policy.py` and `check_payload.py`, and fails if either fails:

```
check_payload: the exports are the same
check_payload: the imports are the same
check_payload: the base relocation table has 13025 DIR64 entries
check_payload: the unwind table has 3300 entries
check_payload: the protected image has 37 encrypted dispatches, the unprotected one 0
check_payload: 37 dispatches decode to a callee of the image
```

The last line is the one that matters: the image's **own** descriptor, decoded
with the image's **own** key, resolves at every dispatch site to a real callee
inside the image. It is what catches a wrong call target - a failure mode that
does **not** crash, so nothing else would notice it.

To compare against an unprotected image, pass one:
`-Dprotect_reference=<path to an unprotected payload.dll>`.

The plugin's own tests: `ctest --test-dir tools/protect/build` (4 tests).

## 4. The environment

- **LLVM 22.1.8** at `C:\Program Files\LLVM` is a **clangd-only** install: no
  headers, no `LLVMConfig.cmake`, no `opt.exe`. The dev SDK is the release
  **tarball**, extracted at
  `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc`. `scripts/build-protect-plugin.py`
  looks for it at `$CS2_LLVM_SDK` and then at that default path, and **fails
  loudly** with the download URL and the SHA-256 if it is missing.
  Re-download:
  `https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/clang%2Bllvm-22.1.8-x86_64-pc-windows-msvc.tar.xz`
  (sha256 `d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234`).
- `cmake` and `ctest` are **not** on PATH: they are in
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
- The payload's compiler is **clang-cl 22.1.8**, which is what `vcvars64` +
  `set CC=clang-cl` resolves to. The plugin must be built with the same one
  (the ABI and the CRT must match).
- The **host** build must use **gcc** on this machine: `hooks-inline` fails
  under MSVC (a pre-existing `noinline` attribute with no MSVC branch).
  Unrelated to the protection work, but it will bite if you switch compilers.

## 5. The facts that cost a day to find

Do not re-derive these. They are all in `tools/protect/README.md` section 11.

- The plugin header is `llvm/Plugins/PassPlugin.h` (moved in LLVM 22) and
  `LLVM_PLUGIN_API_VERSION` is **2**.
- `add_llvm_pass_plugin()` is a **no-op on Windows** (CMake's `MODULE` library
  type is unsupported by the MSVC toolchain). The plugin is a `SHARED` library.
- The SDK's exported CMake targets hardcode a **dead VS 2022 DIA SDK path**;
  the imported target's `INTERFACE_LINK_LIBRARIES` has to be rewritten.
- `llvmGetPassPluginInfo` needs an explicit `__declspec(dllexport)`.
- **clang-cl ignores `-fpass-plugin=`**; the working spelling is
  **`/clang:-fpass-plugin=<dll>`**.
- A plugin's `cl::opt` is **not reachable via `-mllvm`** (a statically linked
  plugin has its own `cl` registry) => the configuration channel is
  **environment variables**.
- The release LLVM is `-fno-rtti` + static CRT; the plugin must match.
- **LLVM 22.1.8's X86 backend mis-materialises a 64-bit immediate** in some IR
  shapes (bit 32 flipped). The mixer constants therefore live in **globals**,
  read with volatile loads, and never in the instruction stream. See below.

## 6. The two bugs worth remembering

**The optimiser folded the whole dispatch away.** Because the key was a
compile-time constant, the optimiser could fold the index, the table load and
the indirect call back into a **direct** call. The "protected" image was
byte-identical to the unprotected one and the protection was **void**. The
fix: the mixer constants live in module globals (`cs2.protect.mixmul`,
`cs2.protect.mixxor`), read with volatile loads, so no 64-bit immediate has to
survive codegen. A global is written by the object writer, so the value that
reaches the image is by construction the value the pass encrypted with.

The image only worked before that fix by a **three-fold coincidence**: the
backend's error landed in bit 32, the mixer rotated by exactly 31, and the
table stride was exactly 8 (`bit63 * 8 = 0 mod 2^64`). Change any one and the
dispatch calls a garbage address - in a manually mapped payload, with no
exception tables, that kills the game.

**The `inbounds` promise was broken.** The GEP into the table used
`inbounds` with an out-of-range index, which is poison by the letter of the
LangRef. The `inbounds` is gone: a wrong index is now a wrong address rather
than poison (a load from an out-of-range pointer is still UB, but not poison).

## 7. What the protection is worth - honestly

The rewrite applies to **same-module callees only**. In the current payload
that is **37 of 332** call sites of the selected functions, and they are
predominantly **destructors and constructors**. The cross-TU calls that carry
the real aim call graph stay direct in the image.

So the static call graph of the *interesting* code is still readable; what
leaves the binary is the cleanup code. Making the stronger claim true needs a
follow-up: hoist the shared aim code into one TU, or route cross-TU calls
through a per-module table.

Against a reverser **with a debugger**, none of this is a wall - it is a tax on
their time. Against an **offline** analyst (a dump and IDA), the runtime
anchoring is what pays.

## 8. Not verified

**The game.** Session 0 has no GPU, so the payload has **never been injected
with the protection active**. Everything above is static analysis of the built
image plus a host-level differential test. To exercise it for real:
`schtasks /run /tn CS2Launch`, then inject with
`build-injector\injector.exe cs2.exe build-payload-protect\payload.dll --load-missing`
(**inject once** - a second injection into a running game double-hooks it).

Also unverified: the Linux -> Windows cross build (the protected build now
needs the SDK's `llvm-objdump` at configure time; `$CS2_LLVM_SDK` works
everywhere, but the default path in `find_objdump.py` is Windows-specific), the
LTO pre-link behaviour of the two module flags (the payload is not an LTO
build), and the mapper's handling of the new `.rdata` fixups at run time (the
DIR64 entries and the table entries were checked at the image level only).

## 9. The next task

**T20 — the virtualization.** The spec is
`docs/orchestration/spec-t20-protect-virtualization.md`; the map is
`docs/orchestration/plan-protect.md`. Summary: a selected function's body is
compiled to a custom bytecode and executed by an interpreter **emitted as LLVM
IR** (so it needs no CRT, no global constructors and no unwind tables - the
payload is manually mapped). Scope it to **integers and pointers only**, with a
fixed slot budget and **fail-closed** eligibility: a function the pass cannot
lower completely is left completely untouched and reported, never half-lowered.

It is the expensive one: 5-50x at run time, a multi-week build in its full
form, and a miscompile risk. Get one function right and provably first.

Run it through the same loop: `orchestrate.plan` -> an implementer sub-agent
-> an **adversarial** reviewer sub-agent that reads the same spec -> a fix
round -> `orchestrate.merge`.
