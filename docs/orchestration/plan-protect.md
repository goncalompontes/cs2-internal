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

1. **Getting the data out of the image** — not "encrypting it in place". A
   value that is **baked into the image cannot be protected by a
   runtime-derived key**: the value that encrypted it must be computable at
   build time, so a per-injection key makes the decode produce garbage
   (T21 found this the hard way — see the correction in §1.2). What actually
   works is the data **not being in the file**: delivered by the injector, or
   read from the host process at run time.
2. **Call-target indirection** — the static call graph is not in the file.
   T19 does it for same-module callees; **T23a** does it for the 26 cross-TU
   edges that carry the aim graph.
3. **Virtualization** — the original machine code is not in the file. Expensive
   (5-50x slowdown, stack blow-up, ABI risk), so it is for a *small* set of
   high-value functions only. **T20a measured the set: it is empty** (§1.3).
4. **Post-link integrity checks** — anti-tamper, not anti-reversing.

The plan is sequenced so the cheap, high-value items land first.

### 1.1 The survey (2026-10-21)

The full survey is
[`docs/research/protect-techniques-survey.md`](../research/protect-techniques-survey.md).
It ranks the techniques against **this** payload and marks every claim
`verified` / `documented` / `inferred`. Its conclusions that changed this
plan:

- **T21 (strings + a runtime-derived key) is the highest-value remaining
  task**, ahead of the virtualization. The plan's own number-one technique,
  and the survey found the entropy source already in the repository: the
  injector calls the payload's entry as `DllMain(base, DLL_PROCESS_ATTACH, 0)`
  (`injector/src/main.cpp`), so the payload's **own mapped image base** arrives
  in `hinst` — a value that is not in the file, costs no import and no TLS,
  and is relocation-safe. The payload currently discards it
  (`payload/src/payload_main.cpp`). **(Corrected — see §1.2.)**
- **T20 (the virtualization) is the lowest-payoff-per-effort item left.** The
  2024-2026 literature has industrialised the attack: a trace plus symbolic
  execution devirtualizes a commercial VM in an afternoon. It still buys
  against an **offline** analyst, so it is worth doing — but scoped hard
  (one function, integers and pointers, one proven differential test), and it
  must not absorb the remaining budget. **(Measured — see §1.3.)**
- **T19's cross-translation-unit hole is the cheapest real payoff** and was
  not in this plan: 295 of the 332 call sites of the selected functions stay
  direct, and they are the ones that carry the aim call graph. Proposed as
  **T23**. **(Measured — the hole is 26 edges, not 295: §1.4.)**
- **The shipped DLL is a signature.** The file on disk is the artefact an
  anti-cheat hashes, and today every build is identical except for a table
  permutation: the build seed exists but is optional. Proposed as **T24**.
- **T22 (integrity) is anti-tamper, not anti-reversing**, and it is worth
  designing jointly with T21: a runtime-derived key that is *verified* needs
  the same post-link binding machinery.

### 1.2 The correction: a baked ciphertext cannot have a runtime key

**T21's implementer found this, and it is worth more than the task
itself.** The plan ranked "runtime-derived keys" first with the claim that
they are "the only thing that makes a dumped image useless on its own". That
claim is **false for data that is baked into the image**.

The ciphertext is written into the image at **build** time. The value that
produced it must therefore be computable at **build** time. A per-injection
value — the image base, a nonce — cannot key it: mixing it into the key
makes the decode produce **garbage**, not plaintext.

So the honest options are:

| option | what it gives |
|---|---|
| **encrypt in place with a build key** (what T21 does) | the strings are not plaintext and not greppable; an analyst who reads the decoder (which is in the image) recovers them. **Weak but real.** |
| **the data is not in the file** — the injector delivers it | the shipped DLL has no strings at all. **Strong**, and it costs a cross-project interface. |
| **the data is read from the host** — the game's own resources | the shipped DLL has no strings at all. **Strong**, and it costs a feature (the event table becomes a run-time read). |

The **image base** is still worth publishing: it is a runtime input to the
decode, it keeps the optimiser from folding the decoder away, and it is the
slot a future injector-delivered mask would use.

**And the second measurement (from T21's review): the payload's own data
model pins the feature keys.** `overlay/include/cs2/overlay/settings_table.hpp`
holds `inline constexpr std::array<SettingInfo, 87> SETTING_TABLE = {{ {"box",…},
{"health_bar",…}, …}}` — a **shared header** between the payload and the
overlay, and a **global initializer**. So the strings the payload's feature
list is made of **cannot leave the payload's image** by any in-place scheme:
the address is baked into the table.

Measured on a protected build: of the literals the pass reports as
"protected", **a third to a half are still plaintext in the shipped DLL** —
the feature-key table is one global initializer, and a literal that **any**
module refuses survives the link (`linkonce_odr`), so the *module* that
protects it does not decide whether it leaves the image. With the repo
policy: 77 protected per module, **54 really absent**, 23 still in it (13 as a
whole string, 10 as the **prefix** of another string). With a different
policy the reviewer measured 25 absent of 54 — the same shape.

So the honest statement of T21's value is: **the payload's own feature-key
list is not protectable in place.** What T21 delivers is the machinery and
about half the literals; what the payload needs is a **data-model change** —
the names arriving at run time (from the injector, or hashed and looked up in
a table the payload builds at run time).

The lesson generalises, and it is the same one the cross-TU task hit: **a
per-module pass cannot decide anything about a symbol the linker may keep.**
Both tasks now make the *image-level* check the authority, and the per-module
claim says "in this module".

> **Corrected later the same day (the orchestrator).** The plan first called
> a follow-up — "the strings leave the image" — the highest value string
> task. **That was wrong, and the reason is the payload's product.** The
> feature-key names are **displayed**: the in-process menu draws them
> (`payload/features/src/menu.cpp`) and so does the overlay's fallback menu.
> A name that is displayed **must exist in a shipped image**, so it cannot
> be kept out of the payload's image — the best that can be done is to hold
> it in **one** artifact instead of two.
>
> So the strings line is **done**, and its conclusion is:
>
> - the feature-key list is the payload's **data**, not its code. Hiding it
>   is a **product decision** (what the menu shows), not a pass decision.
> - what *is* worth protecting in the string domain is the **internal** keys
>   (the JSON keys, the convar names, the console commands) — they are never
>   displayed and could be compared by hash. That is a small task.
> - the **event/field tables** (364 literals) are Valve's own names and are
>   public; the payload could read them from the game's resource files at
>   run time instead of shipping a generated copy
>   (`scripts/extract-gameevents.py` is the build-time counterpart). Low
>   value, small cost — a candidate, not a priority.



### 1.3 The measurement: the VM cannot protect the aim maths

**T20a's gate says none of the six protected functions is virtualizable.**
Four are refused `eh_pad` (they have an EH edge) and two are refused
`unsupported_type` (they return a `float`). The aim maths is a **float
pipeline with exception edges**, and this machine handles integers and
pointers only.

Over the whole `payload/aim` module the gate admits 1878 of 2490 functions —
but what it admits is the small integer helpers (`RecordRing::newest`, the
`std::span` accessors), not anything an analyst would care about.

**Consequence: T20b and T20c are deferred.** The interpreter is the
expensive half of the VM (the IR surgery, the thunks, the per-build opcode
polymorphism) and it would protect **nothing** in the payload. T20a is merged
anyway: it changes no image, it is the precondition for any future VM work,
and it is the artefact that says *the VM cannot help here*. That is a
valuable negative result.

### 1.4 The measurement: the cross-TU hole is 26 edges, not 295

Of the 332 call sites of the six protected functions, 37 are rewritten
(same-TU only) and 295 are skipped — but 179 of those are **intrinsics** and
59 are **CRT calls**. The hole that matters is **26 cross-TU call sites to 24
distinct payload functions**: the aim graph. Of those, 14 are ordinary calls
and 12 are `sret` edges.

The **unity build** does not work (it does not compile today: six
redefinition errors) and the **LTO** route would make the transform not run
at all (the pre-link phase sets the module flag, so the link phase skips it).
**The virtualization does not close it either** — the VM's per-call-site
thunks call the real callees directly, so the edges stay direct.

So the plan is **T23a**: a cross-TU table in the caller's own module, with the
callee as a **relocatable pointer to the external symbol**, and the payload's
own `cross_module` list naming the callees (at the IR level an import and an
internal declaration are the same thing, so the pass cannot tell them apart
by looking at the module — the operator declares, the payload check proves).



## 2. The task sequence

| Task | Name | What it delivers | Depends on |
|---|---|---|---|
| **T18** | `protect-foundation` | the plugin builds, loads under clang-cl, selects functions (annotation + policy file), reports; Meson wiring; no code change yet | - |
| **T19** | `protect-call-encryption` | direct calls to selected targets become indirect dispatches through an encrypted descriptor; relocation-safe | T18 |
| **T20a** | `protect-vm-front-end` | the ISA, the fail-closed eligibility gate, the IR→bytecode lowering, the encoder (encrypted, per-build opcode permutation); **read-only** — the image does not change | T19 |
| **T20b** | `protect-vm-interpreter` | the interpreter emitted as IR, the per-call-site thunks, the rewrite of the function's body, the `virtualized` level made real, the differential test | T20a |
| **T20c** | `protect-vm-payload` | the virtualization applied to the payload's real aim helpers, with the slowdown measured | T20b |
| **T21** | `protect-strings` | string/constant encryption with a **runtime-derived** key (the mapped image base) | T18 |
| **T21b** | `protect-strings-out` | ~~the strings leave the image entirely~~ — **dropped**, see §1.2: the feature-key names are *displayed*, so they must exist in a shipped image. What remains is small: the internal keys by hash, the event table read from the game | T21 || **T22** | `protect-integrity` | post-link binder + runtime self-check | T19, T21 |
| **T23** | `protect-cross-tu` | the cross-TU call graph of the protected functions leaves the image (today 295 of 332 call sites stay direct) | T19 |
| **T24** | `protect-polymorphism` | the shipped DLL is not signaturable: the build seed is mandatory and drives the layout, the keys and the tables | T19 |

T18-T19 are done. **T20a is done; T20b and T20c are deferred** — the gate
measured the eligible set and it is **empty** for the payload's real code
(§1.3), so the interpreter would protect nothing.

**T21 is done** and the string line is closed (§1.2). **T23a** and **T24**
are in flight; **T23b** (the `sret` edges) and **T22** (the integrity check)
are specced. The **order changed** in 2026-10-21 after the survey (§1.1):
**T21 is now the highest-value task**, and T20 is deliberately scoped down.
T21 and T23 are independent of the VM and can be interleaved with it.

**T21b is the task that would deliver what T21 could not**: the strings are
encrypted in place, so the image still decrypts itself (§1.2). Getting them
**out of the image** is what makes a dumped file useless — but the feature
names are *displayed*, so they must exist in a shipped image. **Dropped**, and
the reasoning is in §1.2.


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

The payload is **manually mapped** into `cs2.exe` by the injector
(`injector/src/main.cpp`):

- the mapper **applies base relocations** (DIR64 / HIGHLOW) => an encrypted
  blob must never contain a relocatable pointer, and a cipher must never bake
  in an absolute address. Encode an **index** or a **relative offset**.
- the mapper registers the x64 unwind table and **only warns** when it cannot
  (`injector/src/main.cpp`) => the protected DLL should keep a correct
  `.pdata`/`.xdata` pair (robustness, not a fatal gate).
- the injector allocates **one** TLS block for the whole image and sets it on
  the **entry thread only** => the pass must not introduce TLS variables: a
  `thread_local` read from another thread would read the wrong block. (The
  payload already carries three `thread_local` variables and a 4 KB `.tls`
  section.)
- imports are resolved by name => the pass must not add imports of modules the
  target process does not have.
- **nothing in the image is ever initialised**: the map is done by the
  injector, the CRT startup never runs, so a global with a dynamic
  initialiser would be **silently zero** in the mapped image. **Any runtime
  code the pass injects must be freestanding**: no `new`, no `std::vector`, no
  global constructors, no unwind tables it cannot justify. This is why the VM
  interpreter is **emitted as LLVM IR** rather than linked in as C++.

  > **Corrected (2026-10-22, by the T22 investigation).** The **mechanism**
  > was wrong: the built image's entry point **is** `_DllMainCRTStartup` and
  > the injector calls it (`injector/src/main.cpp` uses the PE's
  > `entry_point_rva`), so **the CRT startup does run**. What does *not* run
  > is the payload's **own** C++ global construction: the image references
  > neither `__xc_a` nor `__xc_z`, so the CRT never walks the user module's
  > `.CRT$XCU`, and the payload's globals stay **zero-initialised**. The
  > **conclusion** is unchanged — a pass must not add a global with a dynamic
  > initialiser, and the injected runtime code must be freestanding — but the
  > reason is that the payload has no such globals today, not that the CRT is
  > absent. Adding one would make the CRT walk it, which changes the image's
  > initialisation semantics and is not wanted.

> **Corrected on 2026-10-21** (the survey found these, and the built image
> confirms them): the payload is built **`/MD`** (dynamic CRT — the image
> imports `MSVCP140.dll`, `VCRUNTIME140.dll` and eight `api-ms-win-crt-*`
> modules), **not** static CRT; the TLS is resolved by the injector **by
> name**, not by an ntdll pattern scan; and the unwind table registration is a
> **warning**, not a fatal error. The *conclusions* (no TLS, a coherent
> `.pdata`, freestanding injected code) stand — but the reasons were wrong,
> and the injector has to `--load-missing` the two CRT DLLs into the game
> process. The documented cross file (`toolchain/windows-msvc.ini`, which sets
> `b_vscrt = mt`) is **not used** by any of the builds on this machine:
> all four `build-payload*` directories are configured `b_vscrt =
> from_buildtype`.

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
