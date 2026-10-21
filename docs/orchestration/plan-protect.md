# cs2-internal — the LLVM protection pass: master plan

> Status: T18 in flight. This file is the map; each task has its own spec in
> `docs/orchestration/`.

## 0. What is being built

A **compile-time protection pass**: an LLVM pass plugin, loaded by clang-cl
during the payload build, that rewrites selected functions of the payload so
the shipped DLL is harder to reverse-engineer. VMProtect-shaped, but built the
way this repo can actually build it.

The plugin is a *build-time tool*. The project and the payload stay Meson +
Ninja (ADR-0003); the plugin is CMake, because it needs LLVM's own
`LLVMConfig.cmake`.

## 1. The threat model, stated honestly

Two different adversaries, and the ranking of techniques flips between them:

| Adversary | What actually helps |
|---|---|
| **Offline** (someone has the DLL, opens it in IDA) | runtime-derived keys (no constant key in the image), import hiding, anything that makes the *data* unreadable without running it |
| **Dynamic** (they can debug the live process) | nothing is a wall; the best you buy is time. A debugger shows the decrypted data and the real call graph. |

Techniques that are **mostly noise** against both: CFG flattening, opaque
predicates, MBA, anti-debug/anti-VM one-`jz` checks, randomized table order.
They cost build time and buy code size.

Techniques that pay, in order:

1. **Runtime-derived keys** for strings/constants — the only thing that makes a
   dumped image useless on its own.
2. **Call-target indirection** — the static call graph is not in the file.
3. **Virtualization** — the original machine code is not in the file. Expensive
   (5-50x slowdown, stack blow-up, ABI risk), so it is for a *small* set of
   high-value functions only.
4. **Post-link integrity checks** — anti-tamper, not anti-reversing.

The plan is sequenced so the cheap, high-value items land first.

## 2. The task sequence

| Task | Name | What it delivers | Depends on |
|---|---|---|---|
| **T18** | `protect-foundation` | the plugin builds, loads under clang-cl, selects functions (annotation + policy file), reports; Meson wiring; no code change yet | - |
| **T19** | `protect-call-encryption` | direct calls to selected targets become indirect dispatches through an encrypted descriptor; relocation-safe | T18 |
| **T20** | `protect-virtualization` | whole-function IR -> bytecode -> a freestanding interpreter emitted as IR | T19 |
| **T21** | `protect-strings` | string/constant encryption with a runtime-derived key | T18 |
| **T22** | `protect-integrity` | post-link binder + runtime self-check | T19, T21 |

T18-T20 are the core. T21/T22 are cheap add-ons once the plumbing exists.

## 3. The verified toolchain facts (all measured on this machine)

The single most valuable artefact of the research phase. A future agent that
re-derives these will burn a day.

| Fact | Consequence |
|---|---|
| `C:\Program Files\LLVM` (22.1.8) is **clangd-only** | the dev SDK is the release **tarball**, extracted at `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc` |
| `add_llvm_pass_plugin()` is a **no-op on Windows** | the plugin is a `SHARED` library, not a `MODULE` one |
| the SDK's exported CMake targets hardcode a **dead VS 2022 DIA SDK path** | rewrite the imported target's `INTERFACE_LINK_LIBRARIES` or the link fails |
| LLVM 22 moved the header to `llvm/Plugins/PassPlugin.h`, API version **2** | the old `llvm/Passes/PassPlugin.h` include does not compile |
| `llvmGetPassPluginInfo` needs an explicit `__declspec(dllexport)` | otherwise the plugin does not load |
| clang-cl **ignores** `-fpass-plugin=` | the working spelling is **`/clang:-fpass-plugin=<dll>`** |
| a plugin's `cl::opt` is **not reachable via `-mllvm`** | the plugin's configuration channel is **environment variables** |
| the release LLVM is `-fno-rtti` + static CRT | the plugin must be built `-fno-rtti -fms-runtime-lib=static` |
| the payload's compiler is clang-cl **22.1.8** | the plugin must be built with the same clang-cl (ABI + CRT) |
| `registerPipelineStartEPCallback` sees **raw** IR | the rewriting pass must use **`registerOptimizerLastEPCallback`** (optimized IR, before codegen) |
| a plain `PassInfoMixin` is an **optional** pass | it is skipped on `optnone` functions - the pass needs `isRequired() { return true; }` |
| `OptimizerLast` also fires in the **LTO pre-link** phase | guard against transforming a module twice (module flag) |
| clangd sets `ParseSyntaxOnly` and never calls `LoadRequestedPlugins` | the plugin **cannot** fire in the IDE; the `annotate` marker still shows in clangd's AST |
| ninja does **not** track a DLL named only inside a compiler flag | the payload target needs an explicit dependency on the plugin |
| the host suite is **42/42 green** | it must stay green |

## 4. Constraints the payload imposes on the pass

> **Upstream does not support this.** LLVM's own `clang/test/CodeGen/pass-plugins.c`
> is `UNSUPPORTED: target={{.*windows.*}}`, and `llvm/examples/Bye` skips Windows
> because "a shared library can't have undefined references". Everything in this
> plan is **engineering on top of an unsupported configuration**, verified
> empirically on this machine. Treat it accordingly: expect to re-verify after
> every LLVM upgrade, and keep the plugin behind an opt-in build option so a
> broken toolchain can never block the normal build.

The payload is **manually mapped** into `cs2.exe` by `payload/loader`:

- the mapper **applies base relocations** (DIR64 / HIGHLOW) => an encrypted
  blob must never contain a relocatable pointer, and a cipher must never bake
  in an absolute address. Encode an **index** or a **relative offset**.
- `RtlInsertInvertedFunctionTable` is **fatal** if it cannot be registered =>
  the protected DLL must keep a correct `.pdata`/`.xdata` pair.
- the mapper resolves TLS by an ntdll pattern scan and **fails the whole
  injection** if it is not found => the pass must not introduce TLS variables.
- imports are resolved by name => the pass must not add imports of modules the
  target process does not have.
- no CRT: the payload is static-CRT, has no global dynamic initialisers and no
  exceptions. **Any runtime code the pass injects must be freestanding**: no
  `new`, no `std::vector`, no global constructors, no unwind tables it cannot
  justify. This is why the VM interpreter is **emitted as LLVM IR** rather
  than linked in as C++.

## 5. The VM design (T20), in brief

Following the shape of `fuqiuluo/amice` (interpreter emitted as IR), the
discipline of `qusamulab/kagura` (one shared opcode table, all-or-nothing
eligibility, decrypt-on-fetch) and the per-build polymorphism of
`90th/llvm-obfus`:

- **Eligibility fails closed.** A function with an EH pad, varargs, an
  indirect call, a volatile/atomic access, a dynamic-length `memcpy`, or an
  unsupported type is **not** virtualized and the pass says so. A half-measured
  function must never be rewritten: the reference implementation shipped a
  bytecode that computed garbage and looped forever.
- **PHIs become edge assignments**, not instructions, and the copy is a
  parallel copy (push all, pop in reverse) so a swapping back-edge is correct.
- **The function's type never changes**, so the x64 ABI is preserved by
  construction; only the body is replaced.
- **Debug info is dropped** (the body is deleted).
- The bytecode is `private constant` (no dynamic initializer), encrypted with
  a per-build key and **decrypted on fetch** so the blob stays read-only and
  the interpreter is re-entrant.
- The **interpreter is the only native code left**, so it is what an analyst
  attacks - it gets the per-build polymorphism (opcode permutation, handler
  shapes) and is itself protected by the cheaper passes.

## 6. What to protect in this payload

Protect the **pure algorithmic helpers** - the aim model, the vector/ray math,
the hash, the config serialisation. Do **not** protect: the hooks, the
renderer, anything with `try`/`catch`, anything with virtual dispatch, anything
on the per-frame path, and the self-unmap routine.

Cost model: virtualization is 5-50x. A function called once per frame can
afford it; a function in the aim hot path cannot.

## 7. The orchestration loop

Each task goes: `orchestrate.plan` (worktree + branch + spec) -> an
implementer sub-agent -> `orchestrate.review` -> an **adversarial** reviewer
sub-agent that reads the same spec -> a fix round -> `orchestrate.merge` (which
runs the full verify loop). The worktree for T18 is
`C:\Users\gpmpo\cs2-wt\t18-protect-foundation`, branch
`feat/t18-protect-foundation`.
