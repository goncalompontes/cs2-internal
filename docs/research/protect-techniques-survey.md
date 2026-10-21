# Anti-reverse-engineering techniques — a survey for the compile-time protection pass

> Research note, 2026-10-21. Written for the `cs2-internal` protection
> project (`docs/orchestration/plan-protect.md`, tasks T18-T22).
>
> **Scope.** This note surveys the *state of the art* for compile-time /
> LLVM-pass-based anti-reverse-engineering protection, and asks a narrower
> question than the literature usually does: **which of these techniques is
> worth building for a manually mapped, CRT-constrained, Windows x64 payload
> DLL that lives inside a game process running its own anti-cheat?**
>
> **It is not a feature list.** Nothing here describes a cheat feature; the
> subject is the protection of the code we already have.
>
> **Companions.** [`neverlose-protection.md`](neverlose-protection.md) and
> [`plaguecheat-protection.md`](plaguecheat-protection.md) describe *other
> people's* protection designs. This note does not repeat them: it says what
> the current literature and the current tooling can actually do, and what is
> worth copying into `tools/protect/`.
>
> **Confidence labels.** `verified` = read out of the source, the SDK headers
> or the built image on this machine; `documented` = stated by the upstream
> project and quoted as such (not independently reproduced here);
> `inferred` = my reading of the evidence; `unknown` = not established.

---

## 0. Executive summary

The literature is much larger than the useful part of it. Of the four serious
out-of-tree LLVM obfuscators surveyed here (`fuqiuluo/amice`,
`qusamulab/kagura`, `90th/llvm-obfus`, and the `obfuscator-llvm` lineage),
the great majority of their surface is **control-flow noise** — flattening,
bogus control flow, MBA, opaque predicates — that the projects' own
documentation admits is a size and compile-time cost rather than a security
boundary, and that a modern analyst removes with a solver or ignores. The
part of the literature that does pay is much narrower and is already partly
in this repository: **a per-build key that is not a compile-time constant**,
**encrypted data that is decoded at run time**, and **call-target
indirection that keeps the static call graph out of the file**. The
virtualization work planned as T20 is the most expensive item in the plan and
the one the 2024-2026 literature most directly attacks: a December 2025
write-up devirtualizes VMProtect 3 with an Intel Pin trace, byte-pattern
clustering of handler segments and Triton symbolic execution, in a single
afternoon's reading. That does not make T20 pointless — the *offline*
analyst still cannot read the algorithm without running the payload — but it
does make T20 the **lowest-payoff-per-effort** item left, and it argues for
scoping T20 down hard (one function, one ISA, one differential test) and
spending the remaining effort on **T21 (strings + a runtime-derived key)**,
which is the plan's own number-one ranked technique, is far cheaper, and has
a **concrete, already-available source of runtime entropy in this repository**:
the injector calls the payload's entry point as `DllMain(base,
DLL_PROCESS_ATTACH, 0)`, so the payload's own **mapped image base** arrives in
`hinst` — a value that is not in the file, varies per injection, costs no
import and no TLS, and is relocation-safe by construction. The third
recommendation is not in the plan at all: **close T19's cross-translation-unit
hole**, which today leaves 295 of 332 call sites of the selected functions
direct, including the ones that carry the aim call graph — a cheaper and more
honest payoff than another obfuscation pass. The fourth is cheap and
already half-built: **finish the per-build polymorphism** (the build seed
already exists) so the shipped DLL is not signaturable, because the DLL
exists as a file on disk and the game's own loader runs a signature check.

**Ranked conclusion:**

1. **T21 (strings + runtime-derived key) is the highest-value remaining
   task**, ahead of T20. The payload's strings are plaintext in the shipped
   DLL today, and a key derived from the mapped image base (mixed with the
   build seed) makes the dumped file useless on its own.
2. **T20 (virtualization) is worth doing, but only small** — one function,
   integers and pointers, one proven differential test — and it must not be
   allowed to absorb the remaining budget.
3. **Close T19's cross-TU hole before adding more transforms** (proposed
   T23): make the claim T19 already makes actually true, instead of adding a
   transform that leaves the interesting graph direct.
4. **Finish the per-build polymorphism and treat the shipped DLL as a
   signature** (proposed T24 or a T21 by-product): the file on disk is the
   artefact an anti-cheat hashes, and today every build of it is identical
   except for a table permutation.
5. **T22 (integrity) is anti-tamper, not anti-RE**, and the expensive part is
   the post-link binder, not the runtime check. It is worth doing *after*
   T21, and it is worth designing jointly with T21 because a runtime-derived
   key that is *verified* needs exactly the same post-link binding
   machinery.

---

## 1. The ranked table

`payoff (offline)` — what it buys an analyst who has the DLL and opens it in
IDA. `payoff (debugger)` — what it buys against someone attached to the live
process. `cost` — build time and run-time cost. `freestanding` — does it fit
"no CRT, no TLS, no new imports, manually mapped"? `status` — where it
stands in this project.

| # | technique | payoff (offline) | payoff (debugger) | cost | risk of breaking the payload | freestanding? | status |
|---|---|---|---|---|---|---|---|
| 1 | **Runtime-derived key** (image base / injector nonce mixed with the build seed) | **high** — the dumped file no longer decrypts itself | none (the key is in RAM while running) | low build, ~zero run | low if derived from `hinst`; medium if it gates *everything* | **yes** — no import, no TLS, no absolute address in the image | **next task T21** (highest value) |
| 2 | **String / constant encryption** (per-string key, run-time decode) | **high** — the strings are the readable part of the DLL | none (plaintext is in RAM while used) | low build, <1% run (`kagura` measured) | medium — a decode bug is a wrong string, not a crash | **yes** | **next task T21** |
| 3 | **Call-target indirection across TUs** (close T19's hole) | **medium-high** — the static call graph of the *interesting* code leaves the file | none | low build, small run | medium — cross-module tables are a new seam | **yes** | **proposed T23** (not in the plan) |
| 4 | **Per-build polymorphism / signature defeat** (seeded layout, keys, opcodes, strings) | **medium** — no fixed byte pattern to match | none | low build, zero run | low | **yes** — the seed already exists | partially done (T19) |
| 5 | **Virtualization** (IR -> bytecode -> interpreter) | **medium** — a dispatcher plus an encrypted blob instead of the algorithm | none (a trace devirtualizes it) | **very high** build, 5-50x run | **high** — a miscompile is a wrong aim | **yes**, if the interpreter is emitted as IR | **next task T20** |
| 6 | **Post-link self-checksum** (sample 16-32 bytes of loaded code, bind after link) | low (it is a tamper check, not a hiding technique) | very low — software breakpoints are caught, hardware ones are not | medium build (the binder is post-link work) | medium — a bad binding bricks the payload | **yes** | **cheap add-on T22** |
| 7 | **Opcode permutation / per-build VM shape** (inside T20) | low-medium (helps only if the blob is decoded) | none | low (inside T20) | low | yes | part of **T20** |
| 8 | **In-place XOR with a rolling key, decrypt-use-reencrypt** (a string variant) | medium | none | low | medium — re-encrypting in place is easy to get wrong | yes | variant of **T21** |
| 9 | **Encrypted lookup table for the VM dispatcher** (`kagura-elt` shape) | low | none | low | low | yes | optional, inside **T20** |
| 10 | **Import hiding / hashed API resolution** | **low** — the import table names *capabilities*, not algorithms | none | medium | **high** — the injector resolves by name; a mistake bricks the injection | possible (PEB walk) but pointless here | **rejected** for this payload |
| 11 | **Anti-debug (PEB `BeingDebugged`, `NtGlobalFlag`, `IsDebuggerPresent`)** | none | none — one `jz` is one `patch` | trivial | **high** — a false positive kills the game | yes | **rejected** |
| 12 | **Anti-dump (block `MiniDumpWriteDump`, `PEB->ProcessDump` flags)** | none | none | trivial | **high** — the game's own anti-cheat dumps the process | yes | **rejected** |
| 13 | **Anti-VM / hypervisor checks** | none | none | trivial | high (Proton/Wine **is** a hypervisor-ish environment) | yes | **rejected** |
| 14 | **CFG flattening, bogus control flow, MBA, opaque predicates** | low — noise for a modern decompiler; it raises the *volume*, not the *cost* | none | **high** build + code size | medium (miscompiles, size) | yes | **rejected** (the plan already says so) |
| 15 | **Anti-disassembly tricks** (junk bytes, overlapping instructions) | low | none | medium | medium — the LLVM backend fights you | no | **rejected** |
| 16 | **Code/data separation, opaque handles, pointer encoding** | low-medium in principle; **high cost, high risk** here | none | high | high — the payload passes real pointers everywhere | partially | **rejected for now** |
| 17 | **Anti-tamper on the mapped image** (verify own sections after mapping) | none (it is a robustness measure) | none | low | medium | yes | **rejected as protection; keep as robustness** |
| 18 | **Watermark / leak marking** (per-user build marking) | none against an analyst; **high against redistribution** | none | medium | low | yes | **out of scope** (a product decision, not a pass) |

---

## 2. The state of the art: the four serious projects

The plan already names three of them (`90th/llvm-obfus`, `fuqiuluo/amice`,
`qusamulab/kagura`). This section says what each one actually does, how it is
built, what is broken on Windows, and — the only question that matters here —
**whether its idea is worth re-implementing in `tools/protect/`**.

### 2.1 `90th/llvm-obfus`

* `documented` — <https://github.com/90th/llvm-obfus>. "out-of-tree LLVM 21+
  pass plugin for policy-driven IR obfuscation." C++23, CMake, GPL-3.0.
* **Build shape**: an out-of-tree pass plugin loaded with `-fpass-plugin`, plus
  a set of wrapper binaries (`obf-clang`, `obf-clang++`, `obf-bc`,
  `obf-rustc`, `obf-tinygo`) and a **separate static runtime library**
  (`libobf_runtime.a`) that the wrappers link in. `documented` (README).
* **Platform support**: "Linux and Windows x86-64, with LLVM 21 or later"
  (`documented`, README). This is the only one of the four that claims
  Windows x86-64 support *and* a modern LLVM. Our SDK is 22.1.8, so the
  version window includes us. `unknown` — I did not build it.
* **Protection levels**: `none` / `light` / `strong` / `vm` / `strong_vm`,
  with profiles (`fast`, `standard`, `guarded`, `fortress`, `lab`) that set
  transform limits. `documented`.
* **What is actually in it** (`documented`, `docs/protection.md`):
  * *Virtualization* with **encoded dispatch**, per-instruction bytecode
    integrity checks, **caller tokens** (a plugin-generated VM refuses to run
    for an unregistered caller), and **physical anchors**.
  * *MBA*, instruction substitution, CFG flattening, outlining, indirect
    dispatch, opaque predicates, bogus control flow, block splitting.
  * *String and constant encoding* with optional authenticated decoding.
  * *Self-checksum* with **post-link binding**.
  * *Call proxy chains* (private call routes through a stateful context).
* **The parts worth stealing (ideas, not code — GPL-3.0)**:
  1. **The physical-anchor idea** (§3.4): a *reversible, seeded byte
     permutation plus a per-byte XOR mask* applied to the canonical
     ciphertext, decoded in place, with **no runtime decode buffer** and the
     same payload length. This is a strictly better shape than "decrypt the
     whole blob into a scratch buffer" for a manually mapped image, because
     the blob stays `private constant` and nothing writable is needed.
  2. **The self-checksum model** (§7.1): sample 16-32 bytes of *loaded*
     machine code, XOR the runtime checksum into a protected integer
     computation, and **bind the expected value after the final link**; an
     unbound site traps. This is exactly the "post-link binder" T22 needs, and
     the project already documents the security limits precisely (§7.4).
  3. **The explicit security contract** (`SECURITY.md`) — a list of what the
     mechanism does *not* do, written by the implementers. That document is
     the most useful thing in the repository, and this note's §7 is largely
     an echo of it.
  4. **The "authenticated" string decoding model**: a keyed BLAKE2s
     tag over descriptor metadata and ciphertext, with a trap on failure.
     The limits are stated: "not a MAC", "no key secrecy".
* **What is broken for us**: the design assumes a **CRT and a runtime
  library**. Its string decoder calls `rt_core_sd3`; its waiting model uses
  Windows `Sleep(1)` (`documented`, `docs/protection.md` §"Waiting and
  availability"), which is **an import the payload may not add**. Its
  `Windows DLL lifecycle` section explicitly assumes "C++ globals keep the
  normal `.CRT$XCU` initialization" — which the pass must not create here.
* **Verdict**: **read it, do not port it.** Its *self-checksum* and
  *physical-anchor* designs are the two most directly transferable ideas in
  the whole survey, and its `SECURITY.md` is the model for how this project
  should document its own limits.

### 2.2 `fuqiuluo/amice`

* `documented` — <https://github.com/fuqiuluo/amice>. "Rust LLVM obfuscation
  framework with instruction-level VMP/code virtualization", Apache-2.0,
  beta (`v0.1.6-beta.1`).
* **Build shape**: a Rust pass plugin (`llvm-plugin-rs`) loaded with
  `-fpass-plugin`; configuration travels through **environment variables or
  `amice.toml`** (`AMICE_STRING_ENCRYPTION`, `AMICE_VM_VIRTUALIZE`, ...).
  `documented` (`docs/EnvConfig_en_US.md`).
  > That configuration channel is **the same shape this repository already
  > uses** (`CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`, `CS2_PROTECT_VERBOSE`),
  > because it is the only channel that works for a statically linked plugin
  > on clang-cl. `verified` (see `tools/protect/README.md` §4).
* **Platform support**: "Prebuilt plugins cover LLVM/Clang 18-23 on
  Linux/macOS, plus Android NDK... **No prebuilt Windows package yet**;
  build from source instead." `documented` (README). So: no Windows binary
  to crib from.
* **What is in it** (`documented`, `docs/EnvConfig_en_US.md`):
  * *String encryption*: `xor` / `simd_xor`; `lazy` (on first use, then
    cached) or `global` (at startup) decoding; **stack or heap** allocation
    of the decoded buffer. Wide strings supported.
  * *Control flow*: flatten (basic/dominator), bogus control flow, indirect
    call/branch, split basic block, lower switch, **VM flatten**, function
    wrapper, clone function, alias access (pointer-chain), delay offset
    loading, parameter aggregation, BB outlining, block shuffle.
  * *MBA*, and an *instruction-level VMP* ("lifts functions into VM bytecode
    with a generated interpreting runtime"), driven by a **profile package**
    (`AMICE_VM_PROFILE_PATH`).
* **The detail that matters most for T20**: amice's VM **runs at the
  optimizer's end** and the docs say, in as many words, what a VM-flatten
  pass cannot do: "IR successor sets remain visible, and single-successor
  indirect branches may become direct branches during optimization. Volatile
  accesses resist constant folding; **they do not guarantee resistance to
  extraction, switch recognition, or recovery.** Initialization and runtime
  decoding remain observable." `documented`.
  > This is an upstream author stating the same thing this survey concludes:
  > **a compile-time VM is not a wall.** It is a *cost*.
* **What is broken for us**: the VMP runtime is a generated *runtime library*
  (`amice-simple-vmp` profile) — a linked artifact with a C ABI, not IR
  emitted into the module. The plan's opposite decision (emit the
  interpreter **as LLVM IR**, so it is freestanding) is the right one for a
  manually mapped payload, and it is a decision amice does not have to make
  because amice targets normal executables and shared libraries.
* **Verdict**: **use as a design reference for the pass ordering and the
  config channel; do not port the runtime.** The one genuinely reusable
  *idea* is the string-decoding **strategy ladder** (§5.2).

### 2.3 `qusamulab/kagura`

* `documented` — <https://github.com/qusamulab/kagura>. "LLVM-based code
  obfuscation and anti-tamper toolkit", MIT, with a **Zenodo DOI and a
  paper** (the most rigorously packaged project of the four).
* **Build shape**: new pass manager, loaded as a pass plugin; **on Windows
  the passes link statically into `kagura-opt`** because "the MSVC-targeted
  LLVM ships with `LLVM_ENABLE_PLUGINS=OFF`" and "`-fpass-plugin` is
  unavailable" there. `documented` (README, "Known issues").
  > This is the same *official-installer* limitation `plan-protect.md` §4
  > records ("LLVM's own `clang/test/CodeGen/pass-plugins.c` is
  > `UNSUPPORTED: target={{.*windows.*}}`"). But it is **not the same
  > limitation as ours**: the *release tarball* SDK at
  > `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc` **does** load
  > plugins under clang-cl with `/clang:-fpass-plugin=<dll>`, which this
  > repository has proven empirically. `verified` (handoff §5, and the
  > working build in `tools/protect/build`).
* **Pass list** (`documented`, `docs/passes/*`):

  | area | passes |
  |---|---|
  | control flow | `fla` (flatten), `bcf` (bogus CF), `ibr` (indirect branch), `ci` (call indirection -> runtime-resolved thunk table), `lt` (loop transform), `fsplit` (function split), `bbs` (block split), `bbr` (block reordering), `dci` (dead code insertion), `elt` (encrypted lookup table), `vtp` (vtable/RTTI name obfuscation) |
  | data | `str` (XOR string encryption, lazy), `str-aes` (AES-128-CTR), `string-split` (fragment long literals), `wstr` (wide strings), `co` (constant obfuscation via MBA), `sub` (MBA substitution), `genc` (global encryption), `mvo` (memory value obfuscation: XOR-encrypt `alloca`ed integers at every store/load), `pe` (pointer encryption), `cse-break` |
  | anti-analysis | `anti-debug`, `tamper` (FNV-1a function checksums at startup), `pac` (XOR-tagged function pointers), `sv` (symbol visibility), `honey` (decoy globals / fake stubs), `bbcheck`, `telemetry` |
  | platform | ObjC/JNI, and the **VM** (`lib/Transforms/VM/`) |

* **The two things worth taking from kagura**:
  1. **The measured cost table** (`docs/passes/performance/`), `documented`,
     on "a representative mobile game module (~200 functions, Cortex-A55)":
     `str` **+5-15% size, <1% runtime**; `str-aes` +8-20%, <1%; `fla` +40-120%
     per function, 5-15% on tight loops; `bcf` +20-50% blocks, <2%;
     `sub` +10-30% per function, 3-8%; `mvo` +15-40% per function, 5-12%;
     **`vm` -30 to +200% size, 10-50x slowdown**; `anti-debug` <1%,
     negligible; `bbcheck` +10-20%, 2-5%.
     > The `vm` row is the closest published corroboration of the plan's
     > "5-50x" figure, from an independent project on a different target.
  2. **The honesty note on `-kagura-bbcheck`** (`docs/passes/anti-analysis/`),
     `documented`: the shipped `kagura_bb_check` is "a weak, always-passing
     stub" because the data the pass hands over "cannot be verified at run
     time" — the expected value is FNV-1a over **LLVM IR opcodes**, and "IR
     opcodes do not exist in the emitted binary". The docs say a fake check
     "would be strictly worse than the stub: it would look like protection
     while providing none and adding false positives."
     > That is the single most useful sentence in this survey for **this**
     > project, whose entire discipline is "a build that looks protected and
     > is not is the failure mode this project exists to avoid". It is also
     > the direct answer to T22's design: a *basic-block* checksum computed
     > pre-codegen is worthless; a checksum over **loaded machine code**,
     > bound **after the link**, is worth something. See §7.
* **What is broken for us**: most of the anti-analysis passes call into
  `libkagura_runtime.a` — a linked C runtime. `anti-debug` calls
  `IsDebuggerPresent` and `NtQueryInformationProcess`; `tamper` hashes the
  loaded text section at startup. Both are **imports** (or at least a
  runtime library) and neither is available to a manually mapped payload that
  may not add imports. `documented`.
* **Verdict**: **borrow the cost table and the honesty discipline.** The
  `mvo`/`pe` idea (XOR-encrypt `alloca`ed values) is the one data-obfuscation
  idea that survives contact with a freestanding constraint — but it is a
  *runtime* cost (5-12%) on every load/store, which is a bad trade for a
  payload whose whole problem is being on the per-frame path.

### 2.4 `obfuscator-llvm/obfuscator` and its forks

* `verified` — <https://github.com/obfuscator-llvm/obfuscator>: 4393 stars,
  1462 forks, `license: null`, **last push 2023-10-20**, 97 open issues.
* **Shape**: it is **not a pass plugin**. It is a **fork of LLVM** with the
  obfuscation passes compiled into the compiler (`-mllvm -fla`, `-bcf`,
  `-sub`, `-str`, `-vmp`, ...). The whole ecosystem of "OLLVM 16", "OLLVM 17",
  `obfuscator-llvm` Windows builds, `wwh1004/ollvm-16` and so on is a
  *replacement compiler*, not a plugin. `documented` by their build
  instructions.
* **Consequence for us**: unusable. The whole premise of this repository is
  that the payload is built with the **stock clang-cl 22.1.8** and a
  *plugin*; replacing the compiler is not an option (it would fork the
  toolchain, break the reproducibility of the build, and double the
  maintenance).
* **But**: the *ideas* it popularised (flattening, bogus control flow, MBA,
  string encryption, `vmp`) are the ideas the other three projects
  reimplemented as plugins. Its "VMP" pass is the ancestor of everything in
  §3.
* **Verdict**: **rejected as a dependency; it is the vocabulary, not a
  component.**

### 2.5 O-MVLL (the `obfuscator-llvm` successor)

* `documented` — <https://github.com/open-obfuscator/o-mvll>,
  <https://obfuscator.re/omvll/>. Python-driven, new pass manager, loaded
  with `-fpass-plugin`.
* **The disqualifying fact**: "O-MVLL currently supports **AArch64** and
  **AArch32** architectures." `documented` (the introduction page). It is a
  mobile (Android/iOS) tool. `unknown` — I did not check whether an x86-64
  branch exists.
* Its pass list is worth noting as a *menu of what people consider worth
  doing*: anti-hooking, arithmetic obfuscation, basic-block duplication,
  control-flow breaking, control-flow flattening, function outline, indirect
  branch, indirect call, opaque constants, opaque fields access, strings
  encoding. Nine of the eleven are in the "noise" bucket of this survey.
* **Verdict**: **rejected** (wrong architecture), but read its
  `strings-encoding` and `opaque-constants` pages if a specific technique
  needs a reference implementation.

### 2.6 LLVM's own plumbing: what is actually available in 22.1.8

This section is `verified` against the SDK headers on this machine
(`F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc\include`).

| thing | status in LLVM 22.1.8 | note |
|---|---|---|
| `llvm/Plugins/PassPlugin.h`, `LLVM_PLUGIN_API_VERSION == 2` | **present** | already used by `tools/protect/src/plugin.cpp` |
| `PassBuilder::registerPipelineStartEPCallback` | **present** | the selection phase |
| `PassBuilder::registerOptimizerLastEPCallback` | **present** | the transform phase |
| `PassBuilder::registerPipelineParsingCallback` | **present** | the `cs2-protect` named pipeline |
| `PassBuilder::registerScalarOptimizerLateEPCallback` | **present** | a candidate for a *string* pass that wants late IR |
| `llvm/Transforms/Utils/CodeLayout.h` | **present**, but it only declares `llvm::codelayout::computeExtTspLayout`, `calcExtTspScore` and `computeCacheDirectedLayout` | these are **profile-driven function-layout** algorithms, not a randomiser. `verified` (header read) |
| `llvm/Transforms/IPO/IROutliner.h` (`IROutlinerPass`) | **present** | extracts repeated code regions; needs `CodeExtractor`-style machinery |
| `llvm/Transforms/Utils/CodeExtractor.h` (`CodeExtractorPass`) | **present** | block outlining |
| `llvm/Transforms/IPO/BlockExtractor.h` (`BlockExtractorPass`) | **present** | block outlining, newer interface |
| `llvm/Transforms/Utils/Cloning.h` | **present** | `CloneBasicBlock`, `CloneFunctionInto` — what a "decoy/split code" pass would use |
| `llvm/Transforms/Utils/ModuleOutline.h` | **ABSENT** | `verified`: no file matching `*Outline*` outside `CGData/` and `CodeGen/MachineOutliner.h`, and no header mentions `ModuleOutline`. The plan's mention of it does not apply to this SDK. `unknown` upstream — I did not check which upstream release added it |
| `llvm/Transforms/Utils/CodeMoverUtils.h` | **present** | block reordering primitives |
| `llvm/Transforms/Utils/SplitModule.h`, `SplitModuleByCategory.h` | **present** | module splitting (a "code/data separation" primitive) |
| `llvm/IR/Instructions.h` `CallInst`, `IndirectBrInst`, `BlockAddress` | **present** | what a threaded-dispatch VM would need |
| `llvm/Transforms/Utils/LowerSwitch.h` | **present** | the "comparison tree instead of a jump table" escape hatch |
| `llvm/Transforms/Utils/Mem2Reg.h`, `SROA` | present | the usual |
| `opt.exe` in the SDK | **present** (`bin/opt.exe`) | the T20 IR test needs it |

**The honest conclusion of this section**: LLVM 22.1.8 offers everything the
remaining tasks need, and offers *nothing extra* for the obfuscation tricks
the plan already calls noise. The only two genuinely useful upstream
components are `IROutliner`/`CodeExtractor` (if we ever want real outlining
rather than a hand-rolled one) and `LowerSwitch` (if the VM dispatcher has
to avoid a jump table).

---

## 3. VM-based protection (T20)

### 3.1 How the bytecode is encoded

Across the four projects the shape converges:

* a **fixed-width instruction record** (a header of opcode + flags +
  immediates + operand descriptors + result slot + edge count, then the
  operands). `90th/llvm-obfus` documents exactly this header and checks
  *every* decoded chunk of it — including padding — against the expected
  value. `documented` (`SECURITY.md`, "VM bytecode integrity contract").
* an **opcode set** that is **permuted per build**. `documented`
  (`llvm-obfus` README: "encoded dispatch"; `kagura`'s VM; `amice`'s
  "instruction-level VMP").
* **relative offsets only** inside the bytecode — a virtual instruction
  names its successors by *index*, not by address. This is exactly the
  relocation-safety rule the payload imposes, and it is not a coincidence:
  the projects that support manual mapping have the same constraint.
* a **slot/stack model**: `llvm-obfus` and the plan both use a *slot array*
  (one slot per SSA value) rather than an operand stack, which is easier to
  lower from LLVM IR but costs memory traffic.

For this repository the encoding is already specified
(`docs/orchestration/spec-t20-protect-virtualization.md` §2.3): one slot per
SSA value, PHIs as edge assignments, a parallel copy for the PHI moves,
`nsw`/`nuw`/`exact` carried, `freeze` for repeated `undef`. That spec is
**sound** and, as far as this survey can tell, **stricter than the reference
implementations** (it refuses more). I have nothing to add to it except the
observations in §3.3-§3.6.

### 3.2 Opcode permutation

**What it is**: the mapping from the bytecode's opcode number to the
handler is permuted per build, so the bytecode of two builds of the same
source is different, and the dispatcher's jump table (if any) is different.

**What it buys**: it defeats a **signature**. A scanner that knows "a
VMProtect-style VM has a 1-byte opcode and a 256-entry handler table" cannot
key on the bytes. It also means a dumped build cannot be matched against
another build.

**What it does not buy**: against an analyst who *runs* the payload, the
permutation is a 30-second exercise: the dispatcher is a switch, the
permutation is a bijection, and recovering it is one pass over the handler
table. `90th/llvm-obfus` is explicit that its per-build keys "remain in the
binary, and decoded data can be read at runtime" (`documented`, README
"Security boundaries").

**For this repo**: the seed already exists (`Config::seed`, `module_seed()`,
`permute()` in `tools/protect/src/transform.cpp` — `verified`). T20 should
reuse it for the opcode permutation and the handler table, and the *policy
seed* should be required to differ between released builds (see §9.1).

### 3.3 Handler shapes and the dispatcher: `switch`, comparison tree, or threaded dispatch?

This is where the 2024-2026 literature says something the T20 spec does not.

**The classic shape** is the *decode-dispatch loop*: fetch an opcode, decode
it, jump to the handler, execute, loop. It is recognisable: a `switch`-like
dispatch, a handler table, a virtual instruction pointer. `documented` —
Salwan's `VMProtect-devirtualization` and every write-up since assumes it.

**The modern commercial shape is *threaded dispatch***: "each virtual
instruction handler is assigned its own unique decode-dispatch routine...
after a virtual instruction handler performs the operation corresponding to
that handler, it executes the handler-specific decode-dispatch routine to
compute the address of the next handler to be dispatched." `documented` —
VMPredator, arXiv:2605.30902 (29 May 2026), §II-A. The paper's point is that
threaded dispatch "hides the typical while-loop switch-case pattern of
conventional virtual machines, thereby neutralizing existing analysis
methods".

**What this means for T20, honestly:**

* The T20 spec's §6 danger list item 10 says: "A `switch` dispatcher compiles
  to a jump table whose shape is the signature every deobfuscator keys on...
  Consider a comparison tree instead of a `switch` if the jump table shows up
  in the disassembly." `verified` (local file).
* **This is over-investment.** The dispatcher's *shape* only matters against
  an analyst who is **running** the code — because the shape is only visible
  once the code runs. Against the *offline* analyst (the one the plan
  targets), the dispatcher is one recognisable function in a 2.4 MB DLL, and
  the analyst's real problem is that the *bytecode* is encrypted. Changing
  `switch` to a comparison tree costs the analyst nothing they were not
  already going to pay, and costs us a jump table vs. a compare chain in
  code size and in clarity.
* **The correct prioritisation inside T20** is therefore: (1) the blob is
  encrypted, (2) the opcode permutation is per-build, (3) the dispatcher
  shape is whatever is simplest to emit as IR. If a comparison tree is free
  (it is not free — it is more IR and more code), take it; if it is not,
  take the `switch`.
* **A threaded dispatcher is not worth building.** In IR it means every
  handler ends in an `indirectbr` over a `blockaddress` table. That is a
  large amount of machinery, it inhibits a lot of LLVM's own optimisation, and
  it buys a property that only matters to the adversary who is already
  winning.

### 3.4 Decrypt-on-fetch vs per-block decryption vs in-place anchors

Three shapes, in increasing order of "no writable memory":

| shape | what it costs | what it buys |
|---|---|---|
| **decrypt the whole blob into a scratch buffer at entry** | a writable buffer (an `alloca` of the blob size) and a re-entrancy problem (the same function called twice re-enters the decoder) | the simplest code |
| **decrypt on fetch** (the T20 spec's choice) | the interpreter re-encrypts or re-derives per fetch; a re-entrancy problem if the buffer is shared | the blob is never fully plaintext at once |
| **in-place / "physical anchor"** (`llvm-obfus`): a reversible, seeded byte permutation + a per-byte XOR mask over the canonical ciphertext, decoded byte-by-byte in place, **no runtime decode buffer**, same payload length | per-byte decode cost in the hot loop | the blob is never contiguous plaintext, and the image needs no writable data at all |

`documented` — `llvm-obfus` `SECURITY.md`, "Physical anchor diversification":
"Each execution-bearing anchor stores a reversible, seeded permutation of the
canonical ciphertext, with a per-byte XOR mask. The decoder computes
physical offsets at emission time and restores each ciphertext byte before
decoding or state diffusion. **The transform does not allocate a runtime
decode buffer** or change the serialized instruction contract. Each anchor
has the same payload length."

> **This is the single most directly transferable design in the survey**, and
> it is better than the T20 spec's decrypt-on-fetch for a *manually mapped*
> payload: it needs **no writable memory at all** for the blob, it keeps the
> blob `private constant` (so no relocations, no `.data`, no CRT
> initialisation), and it is re-entrant by construction.
>
> The cost is per-byte work in the fetch path, and the fact that the
> permutation is per-byte and per-build (so it *is* a signature-defeating
> transform, and it *is* recoverable by anyone who reads the decoder).

**Recommendation for T20**: keep "decrypt on fetch" as the *concept*, but
implement it as an in-place byte transform (permutation + mask) rather than
as "copy the blob to a buffer and decrypt there". The spec's §2.4 says
"decrypted on fetch so the blob stays read-only and the interpreter is
re-entrant" — the in-place form satisfies that goal *more* strictly.

### 3.5 The anti-analysis value — honest

* **Against the offline analyst**: real but bounded. They get a dispatcher
  loop, an encrypted blob, and no algorithm. To learn the algorithm they must
  run the payload — which means injecting into CS2, which means the game and
  the anti-cheat and a live process. For *most* analysts of a cheat, that is
  a much bigger investment than "open it in IDA". **This is the actual
  value of T20, and it is a value against the analyst who only wanted to
  read the algorithm.**
* **Against the dynamic analyst**: nothing. They single-step the interpreter
  and read the bytecode out of memory.
* **The cost is the largest in the plan**: 5-50x run time (the plan's number,
  corroborated by kagura's measured 10-50x for its `vm` pass — `documented`),
  a multi-KB frame per virtualized frame (so recursion multiplies it), an
  ABI that must be preserved by construction, and a miscompile risk that is
  not a crash but a *wrong answer*.
* **The plan already says all of this** (spec §7). Nothing in this survey
  contradicts it; the survey only adds the observation that the *modern
  literature has industrialised the attack*, which lowers the expected
  residual value of the technique.

### 3.6 The known attacks — what a 2024-2026 analyst actually does

This is the part the plan does not have, and it should be in the record.

**The canonical attack (documented in detail, December 2025).** The
`hackyboiz` write-up "[Research] VMProtect Devirtualization: Part 2" walks
through the DevirtualizeMe challenge (VMProtect 3.0.9, Virtualization mode)
end to end:

1. **Find the VM entry** statically: follow the call graph to the virtualized
   function. IDA decompilation fails there ("call analysis failed").
2. **Collect a full instruction trace with Intel Pin** — "regardless of what
   kind of obfuscation is applied, Pin can intercept and log every single
   instruction that actually executes. It will not miss even one instruction."
   One VM cycle is ~10,000 trace lines.
3. **Find the handlers by frequency**: the dispatcher and the most-used
   handler are the hottest addresses in the trace (`uniq -c` over the trace).
   The dispatcher is identified by appearing *N* times; the hottest handler by
   appearing *N-1* times.
4. **Split the trace into per-handler segments**, by anchoring on two fixed
   instruction pointers, and **cluster the segments by byte pattern** — the
   segments with identical instruction sequences are the same handler.
   107 executions of one handler produced 70 distinct patterns; one
   representative per pattern is enough.
5. **Symbolically execute one handler** (Triton) with the recorded input
   state and the recorded bytecode operand, and read off the state
   transformation: "ESI increases by exactly 5 [bytes] per execution", "EBX
   is updated to a completely new value", "two different encrypted bytecode
   values converge to the same result, strongly suggesting a decryption
   function".
6. **Do that for every handler**, map opcode values to handlers by
   instrumenting the dispatcher's *fetch* site, and rebuild the function as
   native x86 (Keystone assembles the reconstructed snippets; the binary is
   patched so `vir_Entry` jumps straight to the reconstruction).
   `documented` — <https://hackyboiz.github.io/2025/12/11/banda/VMPpart2/en/>.

**The academic version (May 2026).** VMPredator
(arXiv:2605.30902) automates the same idea and generalises it beyond the
decode-dispatch loop: it defines three **semantic anchors** — *VM entry, VM
exit, handler set* — and extracts them from a trace, then applies symbolic
execution (angr/Claripy) to each handler block, keeping only the expressions
that compute values stored in the VM's virtual context. It reports 83
handlers detected for a 3-instruction program, and an ~85% reduction in
information versus the obfuscated trace, with 194 s average analysis time for
a 20-instruction program. It explicitly handles the **threaded** variant,
where the older decode-dispatch tools time out. `documented`.

**The older, still-relevant attack.** `JonathanSalwan/VMProtect-devirtualization`
(2021) does the same thing with Triton and a purpose-built VM model, and is
still the tool the 2025 write-up builds on. `documented`.

**What this means for us — three conclusions, in order of importance:**

1. **The "interpreter is the target" problem is not solved by making the
   interpreter pretty.** The 2025 write-up is explicit that it gave up on
   "walking through the handlers and dispatcher in the debugger" and
   switched to a DBI trace; VMProtect's "thousands of junk instructions and
   aggressive control-flow flattening" made the dispatcher and handler paths
   "extremely tangled" and did not stop it. Junk and flattening buy the
   analyst *time*, not a wall — and they cost us code size and compile time
   on every build.
2. **The only thing that actually resists the trace attack is the
   *encryption of the bytecode*, because the trace is of the *interpreter*,
   not of the bytecode** — and the interpreter is a fixed, known shape. The
   encrypted blob resists it *only* for as long as the analyst has not
   decided to dump the decrypted blob from memory. Which they will.
3. **The residual value of T20 is therefore entirely "against the offline
   analyst"**, and that value is real but finite.

**A note on the "symbolic execution of the interpreter" attack.** The
VMPredator paper's framing is the one to keep in mind: a VM is best modelled
as "a collection of state-transition functions" (VIP, VSP, VStack, Scratch,
VFlags). An analyst does not need to understand the *whole* interpreter
symbolically; they need to understand *one handler at a time*, and each
handler is a handful of instructions. This is why the *per-handler* cost
matters: a VM with 40 handlers costs the analyst 40 small symbolic
executions, not one impossible one. **A smaller ISA is not more secure — it
is less work for the analyst.** The spec's "integers and pointers only" is
right for *risk* reasons (miscompiles) but is not a security decision.

### 3.7 The "interpreter is the target" problem — what to actually do

The spec's §6 item 8 says: "The interpreter is the target. Once the VM
exists, the interpreter is the only native code left, so it is what an analyst
attacks. Give it the per-build polymorphism (opcode permutation, handler
shapes) - and remember that the *other* passes (call encryption, string
encryption) should be applied to it too, in a later task."

That is the right list. Concretely, in priority order:

1. **The interpreter must itself be subject to the call-encryption pass
   (T19).** The interpreter's calls (to the native callees of the virtualized
   function) are exactly the "static call graph" that T19 exists to remove.
   The T19 transform works on *selected functions*; the interpreter is a
   pass-generated function, so it needs to be either (a) emitted already
   obfuscated, or (b) marked for the call-encryption pass. Option (b) is
   cleaner: the virtualizer can tag the interpreter it emits with the same
   selection metadata the T19 transform reads, so the *next* pass in the
   pipeline encrypts its calls for free. `verified` that the plumbing exists:
   `record_selection()` / `read_selection()` in `tools/protect/src/transform.hpp`
   are a module-metadata hand-off between the two phases.
2. **The interpreter's data (the bytecode blob, the frame layout) must be
   subject to the string/constant pass (T21).** The bytecode blob is the most
   valuable data in the image.
3. **The interpreter's *shape* should be per-build** (opcode permutation, and
   optionally a per-build choice among a few handler encodings), but **not**
   through junk code. See §10.
4. **The interpreter's own code should be small.** A 40-handler interpreter
   is 40 small symbolic-execution targets. A 12-handler interpreter is 12.
   Prefer a *small* ISA with a *loop* over a big ISA with straight-line
   handlers.

### 3.8 Concrete implementation sketch for this repository

*(This section restates the spec where the spec is right, and adds only
what the survey adds.)*

**Files.** New: `tools/protect/src/vm/isa.def` (the single source of truth
for the opcode numbers — the spec's §2 demands one `.def` file; use
`X-macro` style so the emitter and the interpreter generator both include
it), `tools/protect/src/vm/lower.hpp/.cpp` (IR -> micro-instructions),
`tools/protect/src/vm/encode.hpp/.cpp` (micro-instructions -> bytecode +
the anchor permutation), `tools/protect/src/vm/interp.hpp/.cpp` (the
interpreter, emitted as IR), `tools/protect/src/vm/transform.hpp/.cpp` (the
rewrite). The existing `tools/protect/src/transform.hpp/.cpp` keeps the
T19 call encryption; the T20 virtualizer becomes a sibling selected by
`ProtectionLevel::virtualized` (`tools/protect/src/config.hpp` — the enum
already has the value).

**Extension point.** Same as T19: `registerOptimizerLastEPCallback`
(`tools/protect/src/plugin.cpp:269`). The T20 transform *replaces* the T19
transform for the functions whose applied level is `virtualized`; the T19
transform keeps the `call`-level functions. The spec's §2.6 already asks
for this decision and it has to be made explicitly: **a function is
virtualized or call-encrypted, never both**, because the virtualized body is
gone.

**LLVM APIs the implementation needs** (all present in 22.1.8 — `verified`):

* `llvm::BasicBlock::Create`, `llvm::IRBuilder<>` (`llvm/IR/IRBuilder.h`) to
  build the interpreter's blocks.
* `llvm::SwitchInst::addCase` for the dispatcher, or
  `llvm::IRBuilder::CreateICmp` chains for a comparison tree.
* `llvm::ConstantDataArray::getString` / `llvm::ConstantDataSequential` for
  the blob; `llvm::GlobalVariable` with `setLinkage(PrivateLinkage)` +
  `setConstant(true)` so it lands in `.rdata` as a constant with **no
  relocations** (the blob holds indices and offsets only).
* `llvm::CallInst::CreateCall` with the callee's `AttributeList` preserved
  (`llvm::AttributeList`) for the native-call micro-instruction, so the ABI
  is preserved by construction.
* `llvm::FreezeInst` for repeated `undef`.
* `llvm::MemIntrinsic` / `llvm::Intrinsic::memcpy` — **but** the spec's §3
  rule stands: a `memcpy` with a constant size <= ~64 bytes is inlined by
  the X86 backend; anything larger must be an explicit load/store loop. The
  `llvm-obfus` "physical anchor" shape (§3.4) removes the need for a copy at
  all.
* `llvm::verifyModule` / `llvm::verifyFunction` after the rewrite — the
  spec's §6 item 9 ("the optimiser is not looking at your output") is exactly
  the reason to call the verifier explicitly.

**Environment needed.** Nothing beyond what T19 already needs: the build
seed (`CS2_PROTECT_SEED` / the policy's `seed`) for the opcode permutation
and the anchor permutation, and the policy level.

**Failure modes, ranked.**

1. **A miscompile that does not crash** (the worst: a wrong aim).
2. **A half-lowered function** (the spec's §6 item 1 — refuse, never guess).
3. **Stack blow-up** in a recursive virtualized function.
4. **A CRT call in the interpreter** (the spec's §3) — the *interpreter* must
   not call `memcpy`/`memset`; with the anchor shape it calls nothing.
5. **The optimiser folding the dispatch** — the T19 lesson (the whole
   dispatch is a compile-time constant unless something blocks the fold). The
   T20 equivalent: the opcode decode must read the blob with **volatile**
   loads, exactly as T19's mixer does (`tools/protect/README.md` §10.3).
   This is the single most likely way for T20 to ship a "protected" image
   that is byte-identical to the unprotected one, and the payload check must
   assert it (the T19 check already counts dispatches in the image and
   asserts that each one decodes to a callee of the image — the T20 analogue
   is "the original instruction bytes are gone" + "the blob is not the
   plaintext opcode sequence" + "every opcode in the blob is handled by the
   interpreter").

### 3.9 A note on the interpreter's ABI and the `.pdata`

The spec's §2.5 says "The function's type never changes, so the x64 ABI is
preserved by construction". That is right. But the *interpreter* is a new
function, and it will have its own unwind information. The payload's image
must keep a valid `.pdata`/`.xdata` (the mapper registers it — see §12 for
the correction about how fatal that is). An emitted interpreter with a
`switch` and no calls needs only a leaf-style unwind record, which the
backend generates. **No action needed, but the payload check must keep
asserting the `.pdata` is well formed** (it already does —
`tools/protect/check_payload.py`).

---

## 4. Runtime-derived keys (T21's subject)

This is the plan's number-one ranked technique
(`plan-protect.md` §1: "Runtime-derived keys for strings/constants — the
only thing that makes a dumped image useless on its own"). The survey's
contribution is: **where the runtime secret comes from, and how strong each
source is.**

### 4.1 The requirement

A key is "runtime-derived" if:

* it is **not a compile-time constant** in the image (otherwise the analyst
  greps for it);
* it is **not recoverable from the image alone** (otherwise the analyst
  computes it);
* it is **not baked as an absolute address** (the mapper relocates; a
  relocatable value inside a cipher is corrupted — the T19 rule, already
  learned the hard way).

### 4.2 The candidate sources, ranked

| source | entropy | recoverable offline? | needs an import? | notes |
|---|---|---|---|---|
| **the payload's own mapped image base** (`hinst` in `DllMain`) | 12-20 bits of address entropy (the injector picks the allocation address) | **no** | **no** | `verified`: the injector's entry stub calls `entry(base, DLL_PROCESS_ATTACH, 0)` (`injector/src/main.cpp:594-601`), so the payload's `DllMain` receives the **actual** mapped base. `payload/src/payload_main.cpp:764` currently names the parameter `HINSTANCE` (unnamed) and throws it away. |
| **an injector-supplied nonce** written into a reserved global slot | 64 bits | **no** | no (the injector writes it, like it writes the IAT and `_tls_index`) | needs a small injector change; the strongest of the four, because the secret is per-injection and never on disk |
| **the build seed mixed with the above** | +64 bits of build entropy | no | no | the seed already exists (`Config::seed`); it makes the key *build-specific* even if the base is not |
| hardware (CPUID vendor/brand, TSC, `rdrand`) | 64+ bits, but **public**: the same on every machine | no | no (intrinsics) | good as a *salt*, not as a secret; `rdrand` must be feature-tested or it is a crash |
| time (`GetTickCount64`, `QueryPerformanceCounter` — both already imported) | low | no | no (already imported) | makes the key different every run, which breaks reproducibility of the *behaviour* and makes a mismatch permanent |
| **a hash of the payload's own code** | 64 bits | **partly** — the analyst can hash the same bytes | no | only meaningful *after* a post-link binding step; see §7 |

### 4.3 The "key is in memory once running" weakness, and what actually mitigates it

`documented` — `90th/llvm-obfus` `SECURITY.md` is the best statement of this
in the literature:

* "The checks are not a MAC. They do not guarantee key secrecy or prevent
  every bypass."
* "The entropy anchor supports opaque arithmetic, **not secret key storage**
  or authentication-key binding. Its five accessor variants read the anchor
  through different arithmetic and memory paths. **Runtime initialization
  mixes the current anchor with hardware or timestamp input. This input does
  not bind authentication keys.**"
* "The binary embeds the 32-byte build keys in internal globals. The runtime
  reconstructs derived keys from those embedded keys."

That last line is the crux: **a key that is *derived* rather than *stored* is
not a secret from anyone who can read the image and the decoder.** The
mitigations that actually exist are all about *raising the cost of the dump*,
not about making the key secret:

1. **Never materialise the key.** Derive it and use it immediately
   (a `xor`/`rol` chain whose result is consumed by the next step), so there
   is no 64-bit constant in memory to scan for. This is what `llvm-obfus`
   calls the "entropy anchor" and it is the right shape.
2. **Never materialise the plaintext.** The `physical anchor` shape (§3.4)
   and the "micro-slot" string strategy (§5.2) both decode *per byte* or
   *per use*, so the full plaintext is never contiguous in memory. This is
   the mitigation that actually reduces what a memory dump shows.
3. **Re-encrypt after use** (decrypt-use-reencrypt) so the plaintext's
   *lifetime* is short. This does not stop a dump taken at the wrong moment,
   but it does mean a dump taken at a random moment usually does not contain
   the string.
4. **Do not cache the plaintext.** `kagura`'s `str` caches the decoded string
   ("decrypt on first access, zero after; hot strings cached") — that is the
   *worst* choice for a memory dump, and it is the default there because
   that project targets a phone, not a memory-forensics adversary. For us,
   the opposite trade is right: **pay the decode every time** (the cost is
   <1% per kagura's measurement) and keep no plaintext at rest.

> **The honest summary of §4**: a runtime-derived key does not make the key
> secret. It makes the **image** useless on its own, which is exactly the
> property the plan wants ("the only thing that makes a dumped image useless
> on its own"). The adversary who has a **live process** gets the key, and
> the plan already says that "nothing is a wall against a debugger". The
> mitigation that matters is not the key but the **plaintext lifetime**.

### 4.4 Concrete sketch for this repository

**The cheapest, highest-value design, which needs no injector change:**

1. In `payload/src/payload_main.cpp`, name the `DllMain` parameter
   (`HINSTANCE instance`) and, at `DLL_PROCESS_ATTACH`, publish the instance
   handle into a **module global** that the pass reserves.
   - This is **not** a TLS variable and **not** a dynamic initialiser: it is
     a `private` global with a *constant* initialiser (zero) that is written
     once at attach. The image's `.data` is already there (`verified`: the
     built payload has a `.data` section).
   - **It is not an absolute address baked into the image**: the value is
     *read* at run time from the parameter, and only ever used as an opaque
     value (mixed into the key). No relocation is affected.
2. The pass emits, for every protected string/blob, a decode sequence that
   mixes: the build seed (from a `private constant` global, read with a
   volatile load, as T19 already does), the published instance handle (a
   volatile load of the reserved global), and a per-string/per-blob constant.
   The result is the key; it is consumed immediately and never stored.
3. The **failure mode is catastrophic and must be handled**: if the key is
   wrong, every protected string decodes to garbage. That is a *wrong
   behaviour*, not a crash, and it will not be noticed by a smoke test. The
   mitigation is the same as T19's: **the payload check must decode the
   image's blob with the image's own key and assert it produces the expected
   bytes** (`tools/protect/check_payload.py` already has this shape for the
   call descriptors — §11 of the README). With a *runtime* key, the check
   has to run the image's decoder, which means the check must know the
   instance handle — so the check must simulate it. This is the single
   biggest engineering wrinkle in T21 and it must be designed in from the
   start, not discovered at the end.

**The stronger design (needs a small injector change):** the injector
generates a random 64-bit nonce at injection time and writes it into a
reserved global slot in the mapped image (exactly like it writes the IAT and
`_tls_index` — `verified`, `injector/src/main.cpp:808` and `:483`). The
payload's key is `mix(build_seed, nonce, instance_base)`. Then:

* the key is **not on disk at all** — the file is useless even to an analyst
  who runs it *outside* the game;
* the key differs per injection, so a dump of one session does not decrypt
  another;
* the injector change is ~10 lines and is in the *host* project, which the
  protection work otherwise does not touch.

**Recommendation**: implement the *base-derived* key first (no injector
change, no cross-project coordination, and it already satisfies "not a
compile-time constant" and "not recoverable from the image"), and design
the nonce slot as a **compatible extension** (the pass reserves a global
that is zero when the injector does not fill it; the key derivation mixes
it in either way). Then the nonce is a one-line injector change later.

### 4.5 Failure modes

1. **The wrong key produces wrong data, not a crash.** The check must
   decode, not just count.
2. **The instance handle is not stable across the payload's life?** It is:
   the injector maps the image once and calls the entry point; nothing
   remaps it. But `cs2::loader::self_unmap` exists — the payload unmaps
   itself at some point (it is explicitly *denied* by the current policy,
   `tools/protect/policy.local.json`). If the payload unmaps and re-maps
   itself, the base changes and every cached key is stale. `verified` that
   `self_unmap` exists and is denied by the policy; `unknown` whether a
   re-map path exists.
3. **The key material is in RAM.** Accepted (§4.3).

---

## 5. String and constant encryption (T21)

### 5.1 What is actually in the payload's strings

The payload is a game cheat; its strings are *operational* strings, and they
are the readable part of the DLL: convar names, event names, JSON keys,
config keys, console commands, file names, and format strings. `verified`
that the payload has a `control/json.hpp`, `control/event_names.hpp`,
`control/console.hpp`, `control/rage_config.hpp` and a `features/config_store`
— i.e. a substantial string surface, mostly in `payload/control/` and
`payload/features/`. `unknown` — I did not count the bytes.

This is a *good* target for string encryption, because these strings are
exactly what an analyst greps for: a convar name or a JSON key identifies a
feature immediately, and a feature list is what the analyst wants first.

### 5.2 The techniques that work in a freestanding, manually mapped DLL

Ordered by "how little plaintext exists at any instant":

| strategy | how | plaintext lifetime | needs a writable buffer? | needs a CRT? | source |
|---|---|---|---|---|---|
| **micro-slot** | each byte of the string is stored in its own `private constant` global, XORed with a per-byte mask; each use XORs the byte back into an SSA value | **never contiguous** — no buffer at all | no | no | `documented` (`llvm-obfus` "Micro-slots": "transient SSA bytes, not a transform-created contiguous plaintext buffer") |
| **local stack decode** | decode into a small `alloca` in the using function | the duration of one use | yes (a stack slot) | no (write the loop) | `documented` (`llvm-obfus`: "at most eight stored bytes and at most two proven uses") |
| **decrypt-use-reencrypt** | decrypt in place, use, re-encrypt | the duration of one use | yes (in place) | no | the classic |
| **lazy decode into a cached global** | decode once into a global, keep it | **forever** | yes (a global) | no | `documented` (`amice` `lazy`; `kagura` `str`) |
| **constructor decode** | decode all strings at load time into globals | forever, from the start | yes | **yes** (`.CRT$XCU`) | `documented` (`amice` `global`; `kagura` `str`) |
| **string splitting** | split a long literal into several globals, recombine at run time | the duration of one use | yes | no | `documented` (`kagura` `string-split`) |
| **AES-128-CTR** | real crypto | as above | yes | **yes** (a runtime) | `documented` (`kagura` `str-aes`; "requires runtime") |

**The two that fit this payload are the first two** (micro-slot and local
stack decode), for one decisive reason: **the payload may not have a
constructor** (§7 of the T20 spec: "no global with a non-constant
initialiser"; and the payload must not gain a `.CRT$XCU` — the T20 spec
§3 says to *check* for it). A constructor-decode strategy is therefore out
by construction, and a cached-global strategy is out by policy (it puts
plaintext at rest in the image's memory forever — §4.3 point 4).

**The micro-slot form is the one to prefer**, and the reason is not
theoretical: it is the only form where the plaintext **never exists as a
contiguous buffer at all**, so a memory dump of the process does not contain
the string as a recognisable byte sequence. Its cost is a load + xor per
byte, and a slightly larger `.rdata`.

**The "no CRT" rule, concretely.** The spec's §3 is right and the reason
should be stated precisely: the payload's own build is (as built on this
machine) **dynamically linked against the MSVC runtime** — see §12 — so
"the payload has no CRT" is not currently true. But the *pass* must not add
CRT dependencies, because (a) the image's import table is a checked
contract (`check_payload.py` asserts the imports are unchanged) and (b) a
`memcpy` call in a manually mapped image is a call to a function in a DLL
that may not be loaded in the target. The rule stands; the *reason* needs
correcting.

### 5.3 Keeping the plaintext out of the image and out of the dump

* **Out of the image**: the encrypted bytes are in `.rdata`; the plaintext is
  not. The key material is *also* not in the image if it is runtime-derived
  (§4). The combination is what matters: encrypted bytes + a compile-time
  key = the analyst just runs the decoder; encrypted bytes + a runtime key =
  the analyst must run the *program* (§4.4).
* **Out of the crash dump**: the payload has a crash handler
  (`payload/diag/src/crash_handler.cpp`) — so the project *writes its own
  dumps*. Whatever it dumps is a disclosure surface. `verified` that the
  file exists; `unknown` what it writes. If it writes the payload's memory
  to a file, the micro-slot strategy is what keeps the strings out of it.
* **Out of a *third party's* dump**: the game's anti-cheat dumps the
  process. We cannot stop that. The micro-slot strategy is the only thing
  that helps, and it helps only for the strings (not for the algorithm, not
  for the decrypted bytecode if the VM decodes a whole instruction at a time
  — which is another argument for the anchor shape of §3.4).

### 5.4 The trade-off, stated plainly

> A decrypted buffer in RAM is visible to a memory dump. Nothing changes
> that. What changes is *how much* is visible and *for how long*:
> micro-slots make the plaintext non-contiguous; stack decode makes it
> short-lived; re-encryption makes it short-lived *and* returns the image's
> memory to its encrypted state.

That is the whole of §5's promise. It is worth having, because the *offline*
analyst gets nothing from it and the *dump* analyst gets less — but it is
not a wall, and the document should say so in those words.

### 5.5 Concrete implementation sketch for this repository

**Where it lives.** A new pass, in the **late** phase alongside the existing
transform, because string encryption wants optimised IR (the literal is
usually folded into the use). `tools/protect/src/plugin.cpp:269` already
registers a `ModulePassManager` at `registerOptimizerLastEPCallback`; the
string pass is added there, after (or before) the call-encryption transform.
Two ordering constraints:

* the string pass must run **before** the VM transform if the VM's blob is
  to be encrypted by the string machinery — but the VM blob is a
  `private constant` array of bytes, not a C string, so the VM's blob should
  use the **constant-encoding** path instead (a keyed pool, `documented` in
  `llvm-obfus` as `keyed_pool`), not the string path;
* the pass must not re-encode the *generated* data (the T19 table, the T19
  descriptor, the VM blob) — `llvm-obfus` has a whole "data provenance"
  mechanism for this ("This prevents another string-encoding pass from
  re-encoding generated binary data", `documented`). In this repository the
  equivalent is easy: the generated globals have known names
  (`cs2.protect.table`, `cs2.protect.desc`, `cs2.protect.key0..2`,
  `cs2.protect.mixmul`, `cs2.protect.mixxor` — `verified`,
  `tools/protect/README.md` §10.2) and the string pass can skip any global
  whose name starts with `cs2.protect.`.

**The LLVM APIs.**

* Find the literals: iterate `module.global_variables()`, and select those
  whose initialiser is a `ConstantDataSequential` of `i8` that is
  NUL-terminated and printable (`llvm::ConstantDataSequential::isString()`,
  or `llvm::isCString`/`llvm::StringRef` over the bytes). `verified` that
  the SDK provides these.
* `llvm::GlobalVariable::setInitializer()` with a new
  `ConstantDataArray` holding the encrypted bytes; keep
  `setLinkage(PrivateLinkage)` and `setConstant(true)` so it stays in
  `.rdata` with no relocations.
* Replace each *use* of the global with the decode sequence. The simplest
  freestanding form is **micro-slot**: replace the global with N `i8`
  constants, each XORed with a distinct mask, and rebuild the string at the
  use site. But rebuilding a string at the use site means materialising it
  somewhere — and the use site usually wants a `const char*`.
  **This is the hard part of string encryption in a freestanding image, and
  it is where the "no heap, no CRT, no TLS" constraint bites**:
  * a `const char*` that outlives the function needs a **static buffer**;
  * a static buffer needs an initialiser (a constructor — forbidden) or a
    lazy write (a global written on first use — allowed, but it is a
    *data race* if two threads use it concurrently, and the payload is
    multi-threaded);
  * a *thread-safe* lazy write needs an atomic (allowed: `llvm::atomicrmw` /
    `cmpxchg` on a global — no CRT, no TLS) or a spin lock (a global flag —
    also fine, but it burns a core while contended).
  * **The alternative that avoids all of it: encrypt only the strings whose
    uses are *not* pointer-shaped.** A string compared with `strcmp` or
    `memcmp`, or hashed, or matched with `std::string_view` construction, is
    a *value*, and can be decoded into a stack buffer and used there. A
    string whose address is *stored* (a config key that is kept, a name
    registered with the engine) cannot be.
  * **The pragmatic scope for T21, then**: the first target is the
    **compare-only** strings — the ones the payload compares against
    (`std::string_view` comparisons, `strcmp`, convar lookups by name) —
    which is also where the feature-identifying strings live. Strings that
    are *stored* (registered names) are excluded and reported.
* The decode itself: an inline `xor` chain over a stack `alloca`, written
  as a **loop or a straight-line unrolled sequence of loads/stores** — not a
  `memcpy`/`memset` call (§3 of the T20 spec's freestanding rules, and
  §12 below for the correction about why).

**The report.** The T19 pass already writes a per-function report and a
coverage file (`tools/protect/src/report.hpp/.cpp`). The string pass should
report, per string: the symbol, the byte count, the strategy
(`micro-slot` / `stack` / `skipped`), and the reason a string was skipped
(a stored address, a non-constant initialiser, a `wchar_t` literal, a
string that is already encrypted). A skipped string is a **reported
outcome**, not a silent one.

**The check.** `tools/protect/check_payload.py` must gain: "the plaintext of
the protected strings does not appear in the image" (a byte search for the
plaintext — the simplest possible check, and a very strong one) and "each
protected string decodes, with the image's own key, to the expected bytes".
The second needs the runtime key, which is the wrinkle of §4.4.

### 5.6 Failure modes

1. **A wrong key produces a wrong string** — a wrong convar name, a wrong
   JSON key — which fails *silently* (the feature stops working). This is
   the same class of failure as a miscompiled VM and deserves the same
   discipline: the check must decode and compare, not count.
2. **A lifetime bug**: a decoded string whose buffer is freed or whose
   static storage is overwritten. The "stack decode" strategy confines this
   to one function; the "static buffer" strategy makes it a real risk.
3. **A concurrency bug**: a lazily-filled static buffer written from two
   threads. The payload is multi-threaded (`payload/src/payload_main.cpp`
   starts a thread; the render path is on another). `verified` that the
   payload starts threads.
4. **The optimiser folding the decode**: the same T19 failure mode — the
   decode must read the encrypted bytes with **volatile** loads, or the
   optimiser constant-folds the plaintext back into the image. `verified`
   that this is a real, observed failure in this repository
   (`tools/protect/README.md` §10.3).

---

## 6. Import hiding and API resolution

### 6.1 What the payload's import table says today

`verified` — `llvm-readobj --coff-imports` on the built
`build-payload-protect/payload.dll`:

| module | what it tells an analyst |
|---|---|
| `KERNEL32.dll` (44 symbols) | "this manipulates processes and memory": `ReadProcessMemory`, `WriteProcessMemory`, `VirtualAlloc`, `VirtualProtect`, `CreateRemoteThread`, `GetThreadContext`, `SuspendThread`, `ResumeThread`, `CreateToolhelp32Snapshot`, `Thread32First/Next` — i.e. **it is an injector that reads another process** |
| `USER32.dll` (22 symbols) | "there is a window": `RegisterClassExW`, `CreateWindowExW`, `GetAsyncKeyState`, `GetRawInputData`, `GetForegroundWindow` — i.e. **an overlay and a hotkey handler** |
| `MSVCP140.dll`, `VCRUNTIME140.dll`, 8 × `api-ms-win-crt-*` | "this is a C++ program built with the MSVC runtime" |
| `D3DCOMPILER_47.dll` (1 symbol, `D3DCompile`) | "this compiles a shader at run time" |

Read together, the import table says: *a C++ overlay that reads another
process's memory and compiles a shader at run time*. It does **not** say
"aimbot", "wall-bang", "anti-aim" or "chams" — those are in the code, not
in the imports. The import table is a **capability fingerprint**, and it is
the part of the image most likely to be matched by a signature.

### 6.2 What is safe here, and what is not

The constraint from the plan is: "imports are resolved by name => the pass
must not add imports of modules the target process does not have". `verified`
— and it is stronger than that: the injector resolves **by name** and
**fails the whole injection** if it cannot:

* `verified` — `injector/src/main.cpp:787-806`: for each imported module, if
  the module is not in the target and `--load-missing` was not given, the
  injector prints "import module %s not found in target" and **returns 1**;
  if a symbol cannot be resolved it prints "unresolved import" and returns
  1. The image is written **after** the imports are resolved, so a failure
  means nothing is injected at all.
* The injector *can* load a missing module into the target (a remote
  `LoadLibraryW`, `verified`, `injector/src/main.cpp:493-519`), but only
  with `--load-missing`, and doing so **adds a DLL to the game process** —
  which is itself a visible, unusual event in a game process.

**Therefore, for this payload:**

* **Adding an import is effectively forbidden** — not because the plan says
  so, but because a new import either breaks the injection (module not in the
  target) or *changes the game process* (a new DLL loaded into CS2, which is
  a detection signal in itself). Any anti-debug technique that needs
  `IsDebuggerPresent` or `NtQueryInformationProcess` is therefore **not
  merely useless — it is dangerous**. This is a stronger argument than the
  plan's.
* **Removing an import from the table and resolving it at run time** requires
  a resolution mechanism. The payload *already imports* `GetModuleHandleA`,
  `GetModuleHandleW` and `GetProcAddress` (`verified`, from the import dump
  above) — so a runtime resolution mechanism exists **without adding an
  import**. The name of the function would still be in the image, unless it
  is hashed — and hashing requires walking the export directory of a loaded
  module, which requires the module base, which is available from
  `GetModuleHandleA` (already imported) or from a PEB walk (no import at
  all, `gs:[0x60]` → `Ldr` → `InMemoryOrderModuleList`).
* **Ordinal imports are not usable.** The injector *does* support ordinal
  resolution (`verified`, `injector/src/main.cpp:368-370` and the PE parser
  at `payload/loader/src/pe.cpp:148-149`), but no linker emits them, and
  the ordinals of `kernel32`/`user32` are not stable across Windows
  updates. `unknown` — I did not check whether `lld-link` can emit an
  ordinal import from an object file; the standard `.def`-file mechanism is
  for *exports*, not imports.

### 6.3 The verdict

**Rejected for this payload**, and the reason is sharper than "it is
obfuscation, not security":

1. The payoff is small. The import table names *capabilities*, and the
   capability set of a game cheat is already obvious from the DLL's size, its
   section names, and the fact that it is a DLL at all. Hiding
   `ReadProcessMemory` does not hide "reads another process's memory".
2. The risk is not small. Every technique that removes an import either
   (a) adds a resolution path that can fail at run time (a wrong hash, a
   forwarded export, an API-set redirection) — a failure that is a crash in
   the middle of a game session, or (b) changes the injector, which is the
   component that has to work.
3. The **signature** problem it would help with is better solved by §9.1
   (per-build polymorphism): a per-build randomised import *order* and
   per-build *unused-import pruning* would break a byte signature just as
   effectively, with no run-time risk. (Pruning unused imports is a link-time
   optimisation the linker already does; the residual is the *set*, which a
   per-build build cannot change — but a per-build **reordering** is free and
   does break a byte-level signature.)

**One exception worth considering**: if the payload's *own* DLL is dropped on
disk and an anti-cheat hashes the file, then the import table is a large,
stable, easily-signatured region. §9.1's answer (per-build polymorphism)
covers that too, and covers the code as well. Import hiding would only
cover the import table.

---

## 7. Anti-tamper and integrity (T22)

### 7.1 The self-checksum model — the most transferable idea in the survey

`documented` — `90th/llvm-obfus` `SECURITY.md`, "Self-checksum security
contract":

* "When execution reaches a BOUND site, runtime code hashes **16 to 32 loaded
  machine-code bytes**."
* "The transformed code **XORs the runtime checksum with the checksum bound
  after final linking**."
* "The transformed code **injects a checksum-derived value into a protected
  integer computation**." (i.e. the difference is mixed into a real result,
  so a mismatch changes the program's *behaviour*, not just a flag)
* "When execution reaches a protected site with a required **UNBOUND** record,
  the runtime guard **traps**."
* "A single-byte change in the sample changes the full 64-bit v1 checksum."
* "**A software breakpoint changes the checksum** when it replaces a sampled
  byte with `0xCC`."
* "The v1 hash is **not cryptographic**."

**Why this shape and not another** — and this is the part worth internalising:

* The check is over **loaded machine code**, i.e. the bytes *as they are in
  the process*. It therefore detects **software breakpoints** (`0xCC`) and
  **inline patches**, which is the most common analyst action.
* It is **not** a MAC and does not authenticate anything.
* It is **not** whole-binary integrity: only the sampled ranges are covered.
* It is **not** a defence against an attacker who modifies both the code and
  the stored expected value.
* "A debugger attaches or sets **hardware** breakpoints without modifying
  sampled code bytes" is an **explicit non-goal**.

The design is honest about its limits in a way that this project should copy
verbatim into its own README.

### 7.2 The post-link binder — the actual cost

The "bound after final linking" part is the hard part, and it is a **separate
tool**, not a pass:

* The **pass** (compile time) picks the sample ranges and emits `UNBOUND`
  records — a small table of `(address, length, expected)`.
* The **binder** (post-link) reads the linked image, computes the checksum
  over each range, and **writes the expected values back into the image**.
* The **runtime** (in the image) computes the checksum over the same bytes
  and compares.

`documented` — `llvm-obfus` has a dedicated `docs/self-checksum.md` with a
"record format, relocation rules, and binder behavior", and the binder has
to cope with: "The binder accepts sample ranges that overlap loader-applied
relocations or fixups" (a defect if it does), "ambiguous, overlapping, or
non-file-backed section mappings", and "The PE binder accepts a nonzero PE
Security directory instead of rejecting it" (a defect if it does).

**For this repository the binder is a Python script**, in the same family as
`tools/protect/check_payload.py` and `tools/protect/compare_builds.py` —
which is the right shape, because those already parse the image
(`tools/protect/protect_checks.py` is "the image analysis ... shared by the
three consumers"). The binder would:

1. read the linked `payload.dll` with the same reader;
2. find the sample ranges (the pass emits them as a **named section** or as
   a well-known global, so the binder can find them without guessing);
3. compute the checksum over the *file* bytes of the range;
4. **write the expected value into a reserved slot** in the image.

Point 4 is the hard part: **the image is signed/relocated by the mapper**,
so the binder must write into a location that (a) the mapper will not
relocate, and (b) the runtime can find. A `private` global of 8 bytes per
sample, at a known symbol, is the natural answer — the binder patches the
*file* at the symbol's file offset. The symbol has to be exported or
otherwise findable by the binder: `llvm-readobj --coff-exports` and
`llvm-nm` are available in the SDK (`verified`).

**The relocation trap**: the mapper applies DIR64 fixups, so the *runtime*
bytes of a sampled range can differ from the *file* bytes if the range
contains a relocated pointer. The binder must therefore only sample ranges
that contain **no relocations** — code, or data with no absolute pointers.
`documented` that `llvm-obfus` treats "sample ranges that overlap
loader-applied relocations or fixups" as a binder defect; the same applies
here, and the check must enforce it.

### 7.3 Section / import / entry-point hashing, without new imports

* **The mapped image's base**: the payload knows its own base (§4.4) — no
  import needed. Its `SizeOfImage` is *not* known to the payload (the PE
  header is mapped, so the payload *can* read its own `IMAGE_DOS_HEADER` /
  `IMAGE_NT_HEADERS` at `hinst`! The mapper copies the headers —
  `verified`: `payload/loader/src/mapping.cpp` maps "headers and sections",
  and the injector writes the whole image including the headers). So the
  payload can find its own `SizeOfImage`, its section table, its entry point
  and its import directory **from its own base, with no import at all**.
  > This is a genuinely useful observation: the payload is a *fully mapped
  > PE image* in the target, so the entire PE header is available at run
  > time as data.
* **The import directory**: hashing the import names is a check that the
  image's imports were not tampered with. Since the injector *rewrites the
  IAT* with the target's real addresses at injection time
  (`verified`, `injector/src/main.cpp:808`), **the IAT in the target does not
  match the IAT in the file**. A check over the IAT must therefore run over
  the **ILT / name table** (which is not rewritten), not the IAT. The
  ILT is at `IMAGE_DIRECTORY_ENTRY_IMPORT` → the descriptors → the
  `OriginalFirstThunk` array. That is a subtle, real trap.
* **The entry point**: `AddressOfEntryPoint` is in the header, and the
  injector calls it (`verified`, `injector/src/main.cpp:598-601`). Checking
  that the entry point's first bytes match an expected hash is a cheap
  anti-tamper check.
* **A self-checksum of `.text`**: `documented` — `kagura`'s `tamper` pass
  "hashes the loaded text section and does work today". A whole-section
  hash is stronger than a 16-32 byte sample but (a) it costs a pass over
  ~1.7 MB of text at every start (`verified`: the built payload's `.text`
  `RawDataSize` is 1,681,408 bytes), which is a noticeable hitch at inject
  time, and (b) it is *self-referential* if the hash value is stored in the
  hashed section. It must be stored outside it.

### 7.4 The lesson from kagura's `bbcheck`

`documented` — `qusamulab/kagura` documents that its `-kagura-bbcheck` pass
"detects nothing as shipped", because the expected value is computed over
**LLVM IR opcodes**, which "do not exist in the emitted binary", and because
the `block_id` "is not a unique key". Its conclusion:

> "Faking a check here — hashing an arbitrary memory range and calling it
> verification — would be strictly worse than the stub: it would look like
> protection while providing none and adding false positives."

**This is the design rule for T22**: the check must be over bytes that
**exist in the final image**, and the expected value must be bound **after
the link**. A pre-codegen hash is a fake check, and this project's whole
discipline ("a build that looks protected and is not is the failure mode
this project exists to avoid") says: **do not ship a fake check**.

### 7.5 Concrete sketch for this repository

**Files.** New: `tools/protect/src/integrity.hpp/.cpp` (the pass that marks
the sample sites and emits the `UNBOUND` records), `tools/protect/bind_checksum.py`
(the post-link binder), and additions to `tools/protect/check_payload.py`
(the assertions).

**The pass** (in the late phase, `registerOptimizerLastEPCallback`): pick a
small number of *code* ranges in the selected functions — the first 32 bytes
of the function, say — and emit, for each:

* a **site**: a volatile load of the 32 bytes (as 4 × `i64`), a rolling
  hash, an `xor` with the expected value (a `private constant` global, one
  per site), and the result **injected into a protected integer computation**
  — e.g. the function's return value is `xor`ed with `(expected xor actual)`,
  so a mismatch changes the answer. `documented` shape from `llvm-obfus`.
* a **record**: `(site_id, address, length)` in a `private` global array that
  the binder reads.

**The binder** (`bind_checksum.py`, run by the Meson build after the link,
like `check_payload.py`):

1. find the record array in the linked image (by symbol);
2. for each record, read the 32 bytes **at the given offset**, check that the
   range contains no relocation (compare against the `.reloc` entries), and
   compute the hash;
3. write the hash into the corresponding expected-value slot (by symbol);
4. **fail the build** if a range is not file-backed, overlaps a relocation, or
   is ambiguous.

**The runtime check** is inside the payload: a function emitted by the pass
(or a small hand-written helper in `payload/diag/`), called from the
protected function. It needs **no import** (it reads the image's own bytes
through a pointer the pass provides) and no CRT.

**The check** (`check_payload.py`): after the binder has run, re-read the
image and assert that every expected value equals the hash of its range.
This is the same "the image's own key decodes to the right thing" shape as
the T19 check, and it is what makes the binder trustworthy.

**The `UNBOUND` trap**: `llvm-obfus` traps when it reaches an unbound site.
Weaker and safer: **the build fails** if any site is left unbound, and the
runtime treats a mismatch as a *wrong result* (not a trap), because a trap
in a game process is a crash. `documented` that "Guaranteed program
divergence for every checksum mismatch" is a non-goal of `llvm-obfus` (narrow
sites can truncate the XOR to zero) — the same applies here, so the site
width matters.

### 7.6 Failure modes

1. **The binder and the image disagree** (e.g. the build relinks after the
   binder runs) → the payload misbehaves. The Meson dependency order must
   make the binder run after the link and the check after the binder.
2. **A sampled range contains a relocation** → the runtime bytes differ from
   the file bytes → a permanent mismatch. The binder must refuse such
   ranges (and the pass should prefer code ranges).
3. **The check costs time at every call of the protected function.** A
   32-byte hash per call on the per-frame path is a real cost. Sample
   **once per function** (at entry), not per call — or sample a *rarely
   called* function.
4. **The image's own bytes change between the file and the target** — the
   mapper applies relocations, and the injector rewrites the IAT. A range
   that includes the IAT is not comparable. Same trap as §7.3.
5. **Anti-tamper is not anti-RE.** This is the plan's own ranking and it is
   right: a checker does not hide anything. Its value is that a *tampered*
   payload fails loudly instead of misbehaving — which the Neverlose note
   also concluded for our loader (`neverlose-protection.md` §8.2:
   "This is a **robustness** measure, not an anti-analysis measure").

---

## 8. Anti-debug, anti-dump, anti-VM — an honest evaluation

### 8.1 The situation

The payload runs inside `cs2.exe`, a process that (a) has its own
anti-cheat, and (b) is the target of a commercial product that the operator
is trying not to be detected by. Two consequences:

* **A debug flag check is a coin flip with a catastrophic downside.** If the
  check misfires, the payload does not load and the user sees a broken game
  session. The checks are also trivially defeated (`PEB.BeingDebugged` is one
  byte; `NtGlobalFlag` is one dword) — `documented` in the anti-debug
  literature (`https://anti-debug.checkpoint.com/techniques/debug-flags.html`).
  And a debugger that sets a *hardware* breakpoint, or that simply patches
  the one branch, defeats every one of them.
* **The game is already protected.** VAC/whatever the game runs is a far
  more capable adversary than a self-check, and it is *already* doing the
  things a self-check would do.

### 8.2 Anti-debug: what exists, and why it is rejected here

The available techniques (all `documented` from the anti-debug literature and
the `kagura` pass list):

| technique | cost | why it is rejected here |
|---|---|---|
| `PEB.BeingDebugged` (`gs:[0x60] + 2`) | one load + one branch | needs **no import** (so it is *possible* here), but it is one byte, it is defeated by a patch, and it breaks under *any* debugger — including a legitimate one the user attaches to debug the payload itself |
| `PEB.NtGlobalFlag` (`gs:[0x60] + 0xBC`) | one load + one mask | same |
| `IsDebuggerPresent` | an import | **forbidden** (new import) |
| `CheckRemoteDebuggerPresent` | an import | **forbidden** |
| `NtQueryInformationProcess` (`ProcessDebugPort`) | an import (ntdll) | **forbidden** |
| `ProcessDebugObjectHandle` via `NtQuerySystemInformation` | an import | **forbidden** |
| debugger-artefact scans (int 3, `DR0`-`DR7`, `ProcessDebugFlags` in the heap flags) | a loop | same as above, plus false positives |
| `kagura-anti-debug` (ptrace, Frida port, `/proc/maps`, hook, breakpoint, emulator checks; on Windows `IsDebuggerPresent`, `NtQueryInformationProcess`, PEB heap flags) | a runtime library | the runtime library is not available to a manually mapped payload, and the checks are the ones above |
| timing checks (`rdtsc` deltas) | a few cycles | measures the wrong thing; a VM or a busy machine breaks it; and the analyst controls the machine |

**Verdict: rejected.** The one technique that needs no import (`BeingDebugged`)
is the one that is easiest to patch and the one that has the worst failure
mode. The honest summary is: *against a dynamic adversary, nothing you can
do in the payload is a wall* — which the plan already says. The only
mechanism in this family with a real payoff is the **self-checksum** of §7,
because it detects the *modification* (a `0xCC` byte) rather than the
*presence* of a debugger, and it is honest about its limits.

**One exception, and it is not an anti-debug technique:** the payload's
`diag/src/crash_handler.cpp` (a VEH or a `SetUnhandledExceptionFilter`
handler — `verified`: `SetUnhandledExceptionFilter` is in the import table)
is a *robustness* feature. Its interaction with a debugger is a **correctness**
concern, not a security one: if the payload installs a VEH, a debugger's
first-chance exception handling changes what the payload sees. `unknown` —
I did not read the crash handler. It is worth a note, because "the payload
and the analyst's debugger fight over the exception filter" is a real
failure mode that a *protection* feature can cause.

### 8.3 Anti-dump: rejected

The available techniques are: setting `PEB->ProcessDebugFlags` /
`ProcessDumpFlags` to block `MiniDumpWriteDump`; hooking `NtSetInformationThread`
(`ThreadHideFromDebugger`); and `NtProtectVirtualMemory`-style tricks.
`documented` — the `MiniDumpWriteDump` API and the `MINIDUMP_TYPE` flags are
documented at
<https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump>.

**Why it is rejected here:**

* Most of them need **ntdll imports** that the payload does not have and
  may not add.
* `ProcessDumpFlags` blocks *well-behaved* dumpers, i.e. the crash reporter
  and the support tool — and it is trivially cleared by the dumper.
* The adversary we care about (a dynamic analyst) can read the process
  memory directly with `ReadProcessMemory` — which the payload *itself*
  imports, so it is certainly possible for them.
* And critically: **the game's own anti-cheat dumps the process.** Making
  the process harder to dump interferes with the thing we are trying not to
  be noticed by.

**The mitigation that does work is the one from §4.3/§5.4: do not keep the
plaintext.** Not because the dump is blocked, but because there is less to
find in it.

### 8.4 Anti-VM: rejected

`documented` — the Neverlose module fingerprints the machine (SMBIOS
serials, WMI, CPUID) and gates on the Windows build number
(`neverlose-protection.md` §§2, 5). Those techniques are for *licensing* and
*machine binding*, not for anti-analysis, and the Neverlose note's own
conclusion was that we should not copy the HWID collection.

Concretely for this payload:

* The payload is a **Windows DLL** and the target is CS2 under
  **Proton/Wine** on Linux (`verified`: the injector README describes
  running inside "the same Proton/Wine prefix as the target") and on native
  Windows. A "hypervisor present" check is **always true** in the Proton
  case. Any check of that shape is either always-true or always-false here.
* The Neverlose note's own §9 lists the opaque-predicate fingerprinting as
  something we should **not** copy.

---

## 9. Things with a real payoff that the current plan misses

### 9.1 The shipped DLL is a file on disk, and every build of it is identical

**This is the biggest gap.** `verified`:

* The injector takes a **path**: `injector.exe <process> <payload.dll>`, and
  reads the file. The DLL is a file on disk, at a known-ish path, before and
  after the injection.
* The injector's own README: "The target blocks `LoadLibrary` on unsigned
  modules and **runs a signature check**". So the target *does* have a
  signature mechanism, and a file on disk is the easiest thing to sign.
* The build seed exists (`Config::seed`, `CS2_PROTECT_SEED`, the policy's
  `seed`) and is used for the T19 table permutation and the key parts
  (`verified`, `tools/protect/src/transform.cpp:84-104`, `:394-398`).
  **But the seed is optional**: `module_seed()` falls back to
  `derive_seed(module_id, "cs2.protect")` when neither the environment nor
  the policy provides one — i.e. **the default build is deterministic**, and
  two builds of the same source produce the same image.

**What per-build polymorphism buys:** a signature over the *file* stops
matching. A signature over the *function* (a byte pattern at an offset)
stops matching. A YARA rule over the import table stops matching. The
techniques that achieve it are cheap:

1. **Require a seed for a release build** (or derive it from a build
   timestamp), and make the *policy* carry it. Today the seed is optional.
2. **Randomise more than the table order**: the string keys, the VM opcode
   permutation, the anchor permutation, the mixer's constants, the order of
   the generated globals. All of these are already-seeded *in principle* by
   the existing `module_seed()`.
3. **Randomise the *layout*, not just the content**: the order of functions
   in the image, the order of the sections' contents. LLVM has no
   "randomise function order" pass; `llvm/Transforms/Utils/CodeLayout.h`
   offers only *profile-driven* layout algorithms (`verified`, header read),
   and there is no profile here. The cheap version is to **permute the
   *order of the generated globals*** and to let the linker lay the rest out
   as it will — the linker already lays out functions in a deterministic but
   *source-dependent* order, so a change in the source order changes the
   image. That is a one-line change in the payload's build (link the TUs in
   a seed-derived order) — but it is fragile.
4. **Per-build *string* encryption keys** — this falls out of T21 for free
   if the key is derived from the build seed.

**The honest limit**: a signature that is written *after* seeing the binary
(the usual case for a determined anti-cheat) does not care about
polymorphism. Polymorphism defeats **pre-existing** signatures and
**generic** heuristics. Against an adversary who dumps your binary and then
writes the rule, only *behavioural* detection works. So this is worth doing
because it is cheap, not because it is decisive.

**One more concrete artefact**: the payload writes a boot marker file
("[cs2-payload] booted (pid %lu, build=%s)") — `verified`,
`payload/src/payload_main.cpp:730`. A file with a fixed name in the game
directory is a **behavioural** IOC, and unlike the DLL's bytes it cannot be
randomised away by the protection pass. It is out of scope for the
protection work, but it is worth naming in the record: **the protection pass
is not the weakest link in the detection surface.**

### 9.2 Close T19's cross-TU hole

`verified` — `tools/protect/README.md` §10.6: the T19 rewrite applies only
to calls whose callee is **defined in the same module**. In the current
payload, of the selected functions' 332 call sites, **37 became indirect
dispatches and 295 stayed direct** — and the 37 are "six destructors and
one constructor" in `solver.cpp`, while "the calls that carry the aim call
graph are the **cross-module** ones, and they stay direct calls in the
image".

**This is the honest state of the protection:** the static call graph of the
interesting code is still readable; what left the binary is the cleanup code.

**Two ways to close it**, both cheap:

* **Hoist the shared aim code into one translation unit.** The aim code
  lives in `payload/aim/src/*.cpp` (13 files) and is called from
  `payload/control/`. Moving the shared part into one TU (a unity build of
  the aim library) makes the calls same-module and therefore protectable by
  the *existing* transform. This is a **build change**, not a pass change —
  and it is far cheaper than any new transform. It also changes the
  optimisation picture (one TU means the inliner can see everything, which
  is *good* for the pass: the selected functions are already marked
  `noinline`).
* **A per-module table** (what the README calls "a seam the format of the
  descriptor has to keep open"): each module publishes a table of its
  exported-to-other-modules functions, and the caller indexes into the
  *callee's* table. This is a **new transform** with a real design problem
  (how does the caller name the callee's table? the answer is a
  cross-module symbol reference, which is a relocation — and the T19 rule is
  that the *descriptor* holds no relocatable value, but the *table pointer*
  can be a relocatable value, exactly as `cs2.protect.table` is today).
  This is doable but it is a real piece of work.

**Recommendation**: the unity-build route first (a day, no new code), then
the per-module table only if the unity build is not acceptable.

### 9.3 The interpreter must be protected by the other passes

Already discussed in §3.7. It is in the spec (§6 item 8) but it is worth
stating that the mechanism exists: the T19 transform reads the selection
from the module metadata (`read_selection()`), so the virtualizer can add
the interpreter to that record and the *same* transform will encrypt the
interpreter's calls. One line of plumbing, a large payoff in the image.

### 9.4 Code/data separation and opaque handles

`documented` — `kagura`'s `mvo` (XOR-encrypt `alloca`ed integers at every
store/load) and `pe` (the same for pointers), with the stated goal "to
defeat memory-dump analysis"; `amice`'s `alias_access` (pointer-chain
access); `llvm-obfus`'s `constant_encoding` `keyed_pool` mode (constants
recovered through "keyed, integrity-checked runtime pools").

**Why these are rejected here, concretely:**

* The payload is **full of real pointers**: the aim code walks the game's
  entity lists, the overlay holds DXGI/D3D11 device pointers, the hooks hold
  vtable pointers. Encrypting a pointer at every store and load costs a
  `xor` per access on the per-frame path — `kagura` measures `mvo` at
  **5-12% runtime** and `pe` as its pair — and it must be *thread-safe*
  (the same pointer read from two threads must decrypt to the same value,
  which is fine for a plain `xor` with a per-slot mask, but the mask storage
  is then a second thing to keep secret).
* `alias_access` (a pointer *chain* to reach a field) is a classic
  anti-analysis technique with a real cost: it defeats the optimiser's
  ability to see through the access, it multiplies the loads, and it is a
  *bug farm*. The payoff is against an analyst who wants to read the struct
  layout; our struct layouts are not secret (they are the game's, and the
  game's are public).
* **Verdict**: rejected for the payload's own data; **accepted** for the
  *generated* data (the VM bytecode, the T19 descriptor), where the
  "constant pool" shape is exactly right.

### 9.5 Watermarking and leak marking

Not a pass feature but a **build/deployment** feature: a per-user or
per-session marker embedded in the image (a constant derived from a
per-customer seed) makes a leaked build traceable. `documented` — this is
what `kagura`'s `honey` (decoy globals) and `telemetry` passes gesture at,
and what commercial protectors call "watermarking".

**Rejected as out of scope**, but recorded because it is the *one* technique
in the survey that addresses a threat the plan does not model at all: **the
plan assumes the adversary wants to reverse the payload; it does not model
the adversary who wants to *redistribute* it.** A per-user seed costs nothing
(the seed mechanism exists) and turns "we leaked a build" into "we leaked
*your* build".

### 9.6 Reproducible builds vs per-build polymorphism — a tension worth naming

Per-build polymorphism and reproducible builds are in direct opposition. If
the operator wants to *verify* that the shipped DLL is the one they built,
the build must be reproducible; if the build is per-build randomised, the
artefact cannot be reproduced from the source without the seed.

**The resolution**: the seed must be an **input to the build**, recorded
next to the artefact. Then "reproducible" means "reproducible given the
seed", which is the right property for a project that ships one artefact
per customer. `verified` — the seed already exists in the configuration
(`Config::seed`) and in the policy (`Policy::seed`); what is missing is the
requirement that a *release* build has one, and the recording of it next to
the artefact (the policy's `policy_digest` is already computed —
`verified`, `tools/protect/src/config.hpp:61` — and the coverage file
already records the seed-derived material).

---

## 10. Things that are explicitly **not** worth doing here

This section is as valuable as the rest: the plan already says most
obfuscation is noise, and the survey confirms it with measurements and with
the upstream projects' own admissions.

1. **CFG flattening.** `kagura` measures `fla` at **+40-120% code size per
   function** and **5-15% runtime on tight loops** (`documented`). Its
   effect on an analyst is to make the *graph* bigger, not the *semantics*
   harder; a decompiler recovers the semantics anyway. The plan calls it
   noise; the measurement agrees. **Rejected.**
2. **Bogus control flow.** `kagura`: `bcf` is **+20-50% blocks, <2%
   runtime**, and the dead code is "never executed" — i.e. it is *literally*
   dead weight in the image. **Rejected.**
3. **MBA (mixed boolean-arithmetic).** It rewrites `add`/`xor`/`and` into
   algebraically equivalent but unreadable forms. It is defeated by a
   symbolic-execution simplifier, it is defeated by *any* modern decompiler's
   pattern matcher, and `amice`'s own docs note that "Later scalar
   optimization can simplify MBA identities and opaque zeros" (`documented`).
   It also **destroys the readability of the code for the next maintainer**,
   which is a real cost in a repository that values the diff. **Rejected.**
4. **Opaque predicates.** A branch whose condition is a constant that the
   analyst must prove. Solvers prove them; the Neverlose note found
   **3,100** such sites in one image and the deobfuscation was
   *unambiguous* — they were all `je`/`jz` and the skipped gap was the
   decoy (`verified` locally in `neverlose-protection.md` §4). **Rejected.**
5. **Instruction substitution / junk instructions / anti-disassembly.**
   The 2025 VMProtect write-up is the argument: VMProtect fills the code
   with "thousands of junk instructions and aggressive control-flow
   flattening", and the analyst's response was not to be defeated but to
   switch to a DBI trace and *ignore the junk*. `documented`. The cost is
   real (code size, compile time, and the LLVM backend fighting the
   transform). **Rejected.**
6. **Anti-debug / anti-dump / anti-VM.** §8. **Rejected**, with the extra
   argument that the *game's* anti-cheat already does this and that the
   checks either need forbidden imports or have catastrophic false positives.
7. **Import hiding / API hashing.** §6. **Rejected**: the payoff is a
   capability fingerprint, not the algorithm; the risk is a run-time failure
   in the middle of a game session.
8. **Obfuscating the RTTI / vtable names** (`kagura-vtp`). The payload has
   **no RTTI** to speak of (it is not built with RTTI-heavy STL in the
   protected paths), and the game's vtables are public anyway. **Rejected.**
9. **Hiding the symbols** (`kagura-sv`, `StripSymbols`). The payload is a
   DLL whose *exports* must stay identical (`check_payload.py` asserts it).
   Hiding the *internal* symbol names would help a little against a
   symbol-driven analysis, and the cost is low — but the pass is not the
   right place (the linker is: `/debug:none`, no PDB, and strip the
   `.rdata` symbol table). **Not a pass feature; a link flag.**
10. **A "hardened VM"** (a VM with integrity checks on the bytecode, caller
    tokens, per-instruction checks — the `llvm-obfus` `strong_vm` level).
    `documented` that even that project's own contract says the checks "do
    not provide whole-binary integrity" and "do not guarantee key secrecy".
    It is a lot of work for a check against a tampering adversary who can
    patch the check. **Rejected for now**; if T20 lands and the blob is
    worth protecting, the *physical anchor* (§3.4) is the cheap 80% of it.
11. **MBA-based "constant encryption"** (`kagura-co`) as a way to hide
    constants. A constant reconstructed by an MBA expression is a constant
    that any decompiler folds back. The *keyed pool* form (a constant
    recovered from a runtime pool) is the real one, and it is a small
    addition to the string machinery. **Rejected in the MBA form; use the
    pool form.**

---

## 11. A concrete proposal for the next 2-3 tasks after T20

The plan's sequence is T20 (virtualization) → T21 (strings) → T22
(integrity). This section proposes the shape of T21 and T22, and adds a
**T23 that the plan does not have** and a **T24** that is a by-product
rather than a task. The estimates are **my estimates**, not measurements —
the repository has no benchmark for any of this (the T19 README says so
explicitly: "this repository does not measure it"), and a number that cannot
be reproduced from the repository is worse than no number.

### 11.1 T21 — `protect-strings` (strings + a runtime-derived key)

**Scope.**

* A new late-phase pass that encrypts the payload's **compare-only** string
  literals (the ones whose uses are comparisons, not stored addresses) and
  recovers them into a **stack buffer** at the use site, using a
  **micro-slot** representation where the size is small enough.
* A **runtime-derived key**: `mix(build_seed, mapped_image_base, per_string
  constant)`, where the mapped image base is published by `DllMain`'s
  `hinst` into a reserved global. The key is consumed immediately and never
  stored.
* The report: per string, the symbol, the byte count, the strategy, and the
  reason for any skip.

**Explicitly out of scope for T21**: strings whose address is stored
(config keys that are kept, names registered with the engine), wide strings,
`std::string` construction (which allocates), the VM's bytecode blob (that
is a *constant*, not a string), and any change to the import table.

**Acceptance criteria.**

1. The protected strings' **plaintext does not appear in the linked image**
   (a byte search over the whole image; the check is in
   `check_payload.py`).
2. The build's **import table is unchanged** and there is **no new
   `.CRT$XCU`** — the same assertions the T20 spec already asks for (§3).
3. The payload **boots** and the protected features work: the check must be
   behavioural, not structural. A host-side **differential test** (the
   T19 `diff_test.py` shape) that builds a fixture twice — once with the
   pass, once without — and compares the *output*, extended with a fixture
   that exercises several protected strings.
4. The **key is not in the image**: the check asserts that the key material
   the pass used is *not* present as a constant in the image (it is derived
   at run time from the base).
5. The report states, per string: bytes, strategy, and the skips.

**The tests that must exist.**

* A **fixture** that uses a protected string in a comparison, in a hash, and
  in a `std::string_view` construction, and prints a result that depends on
  all three. The differential test compares the two builds' output and exit
  code. **The test must be proven able to fail** (break the decoder on
  purpose, watch it fail, revert) — the T20 spec's §10.2 requirement, and
  the right precedent.
* An **image test**: the plaintext is not in the image.
* A **negative test**: a string that is *stored* (its address escapes) is
  **skipped and reported**, not encrypted. This is the test that proves the
  pass does not produce a dangling pointer.
* A **concurrency test**: two threads decrypting the same string
  simultaneously, if the "static buffer" strategy is used at all. (With the
  micro-slot/stack strategy, this test is vacuous — which is an argument for
  that strategy.)

**Effort / risk.** **Medium** effort (a pass, a policy level, a report, a
fixture, a check), **medium** risk — the risk is concentrated in the
lifetime and the concurrency, not in the cryptography. The *wrinkle* is the
check: the image check must decode with the image's own key, and the key
depends on the runtime base, so the check must simulate the base. Design
that in from the start.

### 11.2 T22 — `protect-integrity` (self-checksum + post-link binder)

**Scope.**

* A late-phase pass that marks a **small** number of sample sites in the
  protected functions (the first 32 bytes of the function, in the range that
  contains no relocations) and emits the `UNBOUND` records.
* A **post-link binder** (`tools/protect/bind_checksum.py`) that computes
  the checksums over the linked image and writes the expected values into
  the reserved slots.
* A **runtime check** that mixes the difference into a protected integer
  computation.

**Explicitly out of scope**: whole-binary hashing (too slow at every
start), anything that traps in the target (a trap is a crash), any check
that needs an import.

**Acceptance criteria.**

1. The **binder runs as part of the build** and the build **fails** if any
   site is left unbound, if any sampled range overlaps a relocation, or if a
   range is not file-backed.
2. After the binder, `check_payload.py` re-reads the image and asserts that
   every expected value equals the hash of its range.
3. The image's **imports are unchanged** and the `.pdata` is still well
   formed.
4. The report states the number of sites, the number of bytes sampled, and
   the address of each site.
5. A **negative test**: patch one byte of a sampled range in a copy of the
   image and assert that the check *fails* (the difference is non-zero). The
   test must be able to fail.

**The tests that must exist.**

* A **binder test**: build a fixture, run the binder, corrupt a byte, and
  assert the recorded value no longer matches.
* A **relocation test**: a fixture whose sampled range contains a pointer,
  and the binder **refuses** it (an error, not a warning).
* A **build-order test**: the check that the binder runs *after* the link
  and *before* `check_payload.py` (the Meson dependency).

**Effort / risk.** **Medium** effort (the binder is the work; the runtime
check is small), **medium** risk — the risk is entirely in the *binding*:
a mismatch is a permanently wrong payload, and the failure is behavioural
rather than a crash.

### 11.3 T23 (new, not in the plan) — `protect-callgraph` (close T19's hole)

**Scope.** Make the claim T19 already makes *true*: the static call graph of
the protected code should not be readable in the image.

* **Phase 1 (cheap, no new code):** hoist the shared aim code into one
  translation unit (a unity build of `payload/aim/src/`), so the calls that
  carry the aim graph become same-module and are therefore covered by the
  *existing* T19 transform. Re-measure the "37 of 332" number.
* **Phase 2 (only if phase 1 is not enough):** a per-module exported table.
  Each module publishes a table of the functions it exports to other
  modules; a caller encodes the *index* into the callee's table, and the
  callee's table pointer is a normal relocatable symbol reference (the
  descriptor still holds only integers — the T19 rule is preserved).

**Acceptance criteria.**

1. The image's **readable cross-module call graph of the protected
   functions is gone** — measured, not asserted by eye: the report states
   the number of protected call sites before and after, and the check
   counts the encrypted dispatches in the image.
2. The **imports are unchanged**, the `.pdata` is unchanged, the `.reloc`
   still has its DIR64 entries.
3. The **host suite stays green** (`meson test -C build`).
4. The **differential test** still passes (the aim maths is unchanged).

**The tests that must exist.**

* The existing `ir_test.py` shape, extended: a cross-module fixture (two
  `.ll` files) that asserts the call became an indirect dispatch through
  the *callee's* table.
* The image check: the number of encrypted dispatches in the image equals
  the number the pass reports (this is the T19 check, extended to the
  cross-module case).

**Effort / risk.** **Low** effort for phase 1 (a build change), **medium**
for phase 2 (a new table format and a new encoding). Risk: **low** for
phase 1 (the existing transform is already tested), **medium** for phase 2
(a new cross-module seam, and a wrong index is a wrong callee, which the
existing "every dispatch decodes to a callee of the image" check would
catch — the check is already the right shape).

**Why it is worth more than another obfuscation pass**: it makes an
*existing* protection real. The T19 README's own §10.6 says the rewrite
"applies to the calls of a callee that is defined in the same module" and
that the result is that "the static call graph of the interesting code is
therefore still readable". That sentence is the most important line in the
whole protection story, and it is fixed by a build change.

### 11.4 T24 (a by-product, not a task) — make the build non-signaturable

**Scope.** Require a seed for a release build; record it next to the
artefact; use it for the T19 table, the T21 string keys, the T20 opcode
permutation and the anchor permutation; and add a build check that a
release build **without** a seed fails.

**Acceptance criteria.**

1. Two builds with different seeds produce images that differ in the
   protected regions (the check compares the *sizes and the hashes of the
   generated globals*, not the whole file, which the linker reorders).
2. The seed is recorded next to the artefact (in the coverage directory,
   next to the `policy_digest` that already exists).
3. A build with no seed and no policy seed **fails** in a "release"
   configuration. (Today the seed falls back to a deterministic derivation —
   `verified`, `tools/protect/src/transform.cpp:84-92`.)

**Effort / risk.** **Low** effort, **low** risk. The payoff is that a
pre-existing signature stops matching, and that a leaked build is
traceable to a seed.

---

## 12. Facts that contradict the current plan

These are all `verified` on this machine (read out of the source and the
built image), and they are worth correcting in
`docs/orchestration/plan-protect.md` §4 and in the T20 spec §1.

1. **"The mapper registers the x64 unwind table and treats failure as
   fatal."** — **Not true of this repository.** The injector calls
   `RtlAddFunctionTable` and, on failure, prints *"injector: warning: could
   not register .pdata"* and **continues** (`injector/src/main.cpp:846-851`).
   The injector's README lists the `.pdata` registration under
   *"Remaining: ... not needed for the current payload"*.
   **The conclusion (keep a valid `.pdata`/`.xdata`) is still right** — an
   image whose unwind table is broken will crash the game on the first
   throw or on any OS-level unwind through the payload — **but the reason is
   robustness, not a fatal mapper check.** The difference matters: a fatal
   check would forbid *any* change to the unwind table, whereas the real
   requirement is only that the table stays *coherent*.

2. **"The mapper resolves TLS by an ntdll pattern scan and fails the whole
   injection if it is not found."** — **Not true of this repository, and it
   looks like it was imported from the Neverlose note.** This repository's
   injector resolves `TlsAlloc` from **kernel32 by name** via
   `resolve_export` (`injector/src/main.cpp:449`), allocates a TLS slot in
   the target, and points the image's `_tls_index` at it
   (`injector/src/main.cpp:483`). There is **no ntdll pattern scan** in
   `injector/src/main.cpp` (the only `ntdll` resolutions are
   `NtQueryInformationProcess` and `RtlAddFunctionTable`, both by name), and
   a missing TLS does **not** fail the injection — `setup_tls` just returns
   a not-ok slot. The pattern-scan description is the **Neverlose** loader's
   behaviour (`neverlose-protection.md` §1.2).
   **The conclusion (no thread-local state) is still right, for a different
   reason**: this injector allocates **one** TLS block for the whole image
   and sets it only for the entry thread (`injector/src/main.cpp:467-487`),
   so a `thread_local` in the payload is already fragile. And the payload
   *already has* three `thread_local` variables
   (`payload/control/src/input.cpp:1268`, `payload/features/src/chams.cpp:122`,
   `payload/features/src/thirdperson.cpp:39`) and a `.tls` section of
   4,073 bytes (`verified` with `llvm-readobj --sections`). The pass must
   not add more.

3. **"The payload is static-CRT, has no global dynamic initialisers and no
   exceptions."** — **Not true of the builds on this machine.** Every
   `build-payload*` directory was configured with `b_vscrt = from_buildtype`
   (`verified`: `build-payload/meson-info/intro-buildoptions.json`), i.e.
   **without** the cross file `toolchain/windows-msvc.ini`, which *is* the
   file that sets `b_vscrt = 'mt'`. The built
   `build-payload-protect/payload.dll` therefore imports **MSVCP140.dll,
   VCRUNTIME140.dll and eight `api-ms-win-crt-*` modules**, including
   `_CxxThrowException`, `__C_specific_handler`, `__CxxFrameHandler3`,
   `terminate`, `_crt_atexit` and `_execute_onexit_table` (`verified` with
   `llvm-readobj --coff-imports`).
   * The payload's **own** code has no `try`/`catch` (`verified`: a search
     over `payload/**/src/*.cpp` finds none), so the T20 spec's "refuse any
     EH pad" rule is still right — but for the reason "the interpreter
     cannot model unwinding", not "the payload has no exceptions".
   * The **consequence that matters**: the injector must find
     `MSVCP140.dll` and `VCRUNTIME140.dll` **in the target**, and it does
     not (`verified`: `injector/src/main.cpp:791-798` — "import module %s
     not found in target" and `return 1`). It only works with
     `--load-missing`, which **loads a DLL into the game process** — a
     visible event in a game process, and one that fails on a machine
     without the VC++ redistributable.
   * **Recommendation**: re-configure the protected build with
     `--cross-file toolchain/windows-msvc.ini` so the payload is genuinely
     static-CRT. That removes two import modules from the game process, it
     removes the `--load-missing` requirement, it removes the CRT's own
     global-destructor machinery from a manually mapped image (which has no
     loader to run them), and it makes the plan's "no CRT" constraint
     **true** rather than aspirational. It is a build change, not a pass
     change, and it should be done **before** T20, because it changes the
     import table that the protected build's check compares.

4. **"The T20 spec's §6 item 10: consider a comparison tree instead of a
   `switch`."** — This over-invests in the wrong direction (§3.3). The
   dispatcher's shape only matters to an analyst who is *running* the code,
   and that analyst wins anyway. The blob's encryption and the per-build
   opcode permutation are what matter. A `switch` is fine.

5. **"The T20 spec's §2.3: integers and pointers only"** — this is a
   *risk* decision (miscompiles), not a security one, and it should be
   labelled as such: **a smaller ISA is less work for the analyst too**
   (§3.6).

6. **The plan's ranking puts virtualisation third and runtime-derived keys
   first; the survey agrees with the ranking but disagrees with the
   *sequencing*.** The plan sequences T20 before T21 ("T18-T20 are the
   core. T21/T22 are cheap add-ons once the plumbing exists"). The survey
   says: **T21 has a higher payoff per unit of effort than T20**, and the
   *reason T21 depends on the plumbing* (a policy level, a report, a
   coverage file, a check) is already built by T18/T19. T21 does not need
   T20. If the sequence is ever re-planned, T21-before-T20 is defensible.

---

## 13. Sources

### 13.1 Local documents and code (all read on this machine, 2026-10-21)

* `docs/handoff-protect.md` — the T18/T19 state.
* `docs/orchestration/plan-protect.md` — the master plan, the threat model,
  the task sequence, the constraints (§4).
* `docs/orchestration/spec-t20-protect-virtualization.md` — the T20 spec.
* `docs/research/neverlose-protection.md` — the Neverlose protection layer.
* `docs/research/plaguecheat-protection.md` — the Plaguecheat reviver.
* `docs/research/README.md` — the research index.
* `tools/protect/README.md` — the plugin's own documentation, in particular
  §§4 (the configuration channel), 10.2 (the globals), 10.3 (the volatile
  barrier), 10.4 (the skip rules), 10.6 (what the rewrite is worth), 11
  (the payload-level checks), 12 (the verified facts).
* `tools/protect/src/plugin.cpp` — the extension-point wiring.
* `tools/protect/src/config.hpp`, `selection.hpp`, `transform.hpp` — the
  configuration, the selection and the transform interfaces.
* `tools/protect/src/transform.cpp` — `module_seed()`, `permute()`,
  `key_parts()`, the volatile decode.
* `injector/src/main.cpp` — the import resolution, the TLS setup, the
  `.pdata` registration, the entry stub.
* `injector/README.md` — "the target ... runs a signature check"; the TLS
  handling; the `.pdata` registration listed as *remaining*.
* `payload/meson.build`, `payload/src/payload_main.cpp`,
  `payload/loader/src/pe.cpp`, `payload/loader/src/mapping.cpp`,
  `toolchain/windows-msvc.ini` — the build and the loader.
* The built images: `build-payload/payload.dll`,
  `build-payload-protect/payload.dll` (imports and sections, via
  `llvm-readobj` from the SDK at
  `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin`).
* The SDK headers at
  `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc\include` — the
  presence and absence table in §2.6.

### 13.2 Upstream projects (read as documentation; **not built, not run**)

* `90th/llvm-obfus` — <https://github.com/90th/llvm-obfus>
  * `README.md` (levels, platforms, "Security boundaries": *"Obfuscation
    does not prevent debugging, tracing, or memory inspection. Keys remain
    in the binary, and decoded data can be read at runtime."*)
  * `docs/protection.md` (the VM, the bytecode integrity contract, the
    physical anchors, the strings, the constants, the seeds and keys, the
    self-checksum, the call proxy chains, the safe pipeline)
  * `SECURITY.md` (the VM bytecode integrity contract, the self-checksum
    security contract, the explicit non-goals)
  * *read via the raw URLs on 2026-10-21; the licence is GPL-3.0, so this
    note quotes and describes, and copies nothing.*
* `fuqiuluo/amice` — <https://github.com/fuqiuluo/amice>
  * `README.md` / `README_en_US.md` (the feature list, the platform
    support, the beta status, the no-Windows-prebuild note)
  * `docs/EnvConfig_en_US.md` (the pass matrix, the string-encryption
    options, the VM options, the VM-flatten discussion of what the
    technique does and does not achieve)
  * *Apache-2.0.*
* `qusamulab/kagura` — <https://github.com/qusamulab/kagura>
  * `README.md` (the pass table, the threat table, the Windows
    `-fpass-plugin` note, the LLVM 22 `-mllvm` note)
  * `docs/passes/` — `control-flow/`, `data/`, `anti-analysis/`,
    `performance/`
  * *MIT.*
* `obfuscator-llvm/obfuscator` — <https://github.com/obfuscator-llvm/obfuscator>
  * the repository metadata only (last push 2023-10-20, `license: null`).
  * **not** used as a source of code.
* `open-obfuscator/o-mvll` — <https://github.com/open-obfuscator/o-mvll>,
  <https://obfuscator.re/omvll/> (the AArch64/AArch32-only statement; the
  pass menu).

### 13.3 Analysis literature (read; **not reproduced**)

* banda / hackyboiz, "[Research] VMProtect Devirtualization: Part 2",
  2025-12-11 — <https://hackyboiz.github.io/2025/12/11/banda/VMPpart2/en/>
  — the Pin-trace / clustering / Triton workflow, end to end.
* banda / hackyboiz, "[Research] VMProtect Devirtualization: Part 1",
  2025-09-11 — <https://hackyboiz.github.io/2025/09/11/banda/LLVM_based_VMP/ko/>
  — the LLVM-based lifting approach (noted from Part 2's reference list;
  **not read in full**).
* Wanju Kim, Seoksu Lee, Eun-Sun Cho, *A Core-Structure-Based Automated
  Deobfuscation Tool for Commercial Virtualization Obfuscation Analysis*
  (VMPredator), arXiv:2605.30902, 29 May 2026 —
  <https://arxiv.org/html/2605.30902v1> — the semantic-anchor model, the
  threaded-dispatch discussion, the handler detection by "stores into the
  virtual context", the measured 85% information reduction and the 194 s
  analysis time.
* `JonathanSalwan/VMProtect-devirtualization` —
  <https://github.com/JonathanSalwan/VMProtect-devirtualization> — the
  tool the 2025 write-up builds on (referenced, not re-read).
* Rolles, *Unpacking Virtualization Obfuscators*, 3rd USENIX Workshop on
  Offensive Technologies, 2009 — the original decode-dispatch-loop analysis
  (referenced via VMPredator and the 2025 write-up).
* Zheng et al., *Deobfuscation of Virtualization-obfuscated Code through
  Symbolic Execution and Compilation Optimization*, ICICS 2017 —
  <https://cis.temple.edu/~qzeng/papers/deobfuscation-icics2017.pdf> — the
  "analyse each handler separately" argument and the measured trace blow-up
  (VMProtect 1.81: 485 → 107,998 trace lines; VMProtect 2.13: 485 →
  1,028,805).
* Blazytko, *Writing Disassemblers for VM-based Obfuscators*, 2021 —
  <https://synthesis.to/2021/10/21/vm_based_obfuscation.html> — the
  "the VM is a collection of state-transition functions" framing.
* Tim Strazzere / others on anti-debug — the technique inventory at
  <https://anti-debug.checkpoint.com/techniques/debug-flags.html> (the
  `PEB.BeingDebugged` and `PEB.NtGlobalFlag` checks) — cited only to say
  what the checks are, and that they are one byte and one dword.
* `MiniDumpWriteDump` — <https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump>
  — cited only to say what the anti-dump mechanism is.
* MITRE ATT&CK T1027.007, *Obfuscated Files or Information: Dynamic API
  Resolution* — <https://attack.mitre.org/techniques/T1027/007/> — the
  framing of API hashing as a known technique (and, by omission, its
  purpose).

### 13.4 Things I could **not** verify

* **I did not build, run or link any of the surveyed projects.** Every
  statement about `llvm-obfus`, `amice`, `kagura` and O-MVLL is *what their
  documentation says*, quoted as such. Where I say "measured", I mean
  *their* measurement, on *their* target, not ours.
* **I did not build the plugin or the payload** (out of scope for this
  task, and it takes minutes).
* **The behaviour of the payload under the protection has never been
  observed in the game** — the handoff says so (§8) and nothing here changes
  that.
* **O-MVLL's x86-64 status**: the documentation says AArch64/AArch32; I did
  not check for an x86-64 branch.
* **`ModuleOutline` upstream**: it is **absent** from the 22.1.8 SDK headers
  (verified locally). I did not check which upstream release introduced it.
* **Whether `lld-link` can emit an ordinal import** from an object file —
  not checked.
* **What the payload's crash handler does** (`payload/diag/src/crash_handler.cpp`)
  — not read; §8.2 mentions it only as a thing to check.
* **The number of string bytes in the payload** — not counted; §5.1 is
  qualitative.
* **The measured cost of any of this in the payload** — there is no
  benchmark in the repository, and the effort estimates in §11 are estimates.
