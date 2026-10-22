# cs2-internal — the protection pass: handover

> Written at the end of the T20a/T21 session (2026-10-21, evening). The next
> session starts here. Read this, then
> `docs/orchestration/plan-protect.md` for the map and the ranking, then the
> spec of the task you pick up.

## 1. What exists now

An **LLVM pass plugin** (`tools/protect/`, built by CMake) that the payload
build loads with clang-cl. It selects functions, rewrites their calls into
indirect dispatches through an encrypted descriptor, encrypts the payload's
string literals, and analyses whether a function *could* be virtualized.

| task | state | what it delivered |
|---|---|---|
| **T18** — the foundation | **merged** | the plugin builds, loads under clang-cl, selects functions (annotation + policy file), reports; the Meson wiring |
| **T19** — the call encryption | **merged** | a direct call to a same-module callee becomes an indirect dispatch through an encrypted descriptor; relocation-safe |
| **T20a** — the virtualizer's front end | **merged** | the ISA (one `isa.def`), the fail-closed gate, the IR→bytecode lowering, the encoder (encrypted, per-build opcode permutation). **Read-only**: the image does not change |
| **T21** — the string encryption | **merged** | the string selection, the gate, the encryption, the decode emitted into the using function, the salted coverage, the image scan — **and the measurement that says the strings do not leave the image** (§3.2) |
| **T23a** — the cross-TU routing | **merged** | a call to a declaration the policy's `cross_module` list names goes through the caller's own table; the dispatch count **37 → 51**; the 14 cross-TU edges of the aim graph (4 of them EH edges) leave the image |
| **T24** — the per-build polymorphism | **in review** | the seed becomes mandatory, the mixer's constants and rotation become per-build, the pass permutes the module's function list: **83.53 %** of the `.text` differs between two seeded builds |
| **T23b** — the `sret` edges | **specced** | the last 12 cross-TU edges: T19's refusal of a struct-returning callee is a *policy*, not a limit of the target (`llc` accepts an indirect `sret` call) |
| **T22** — the integrity check | **specced** | a post-link binder and a run-time self-check: anti-tamper, not anti-reversal |

**T20b (the interpreter) and T20c (the VM in the payload) are deferred** — the
gate measured the eligible set and it is **empty** for the payload's real code
(§3.1).

## 2. Where the work is

The plan is `docs/orchestration/plan-protect.md`. The **task sequence** it
carries now: T20a ✓ → **T23a** → **T24** → T22 → T23b. T21 is done; the
string line is closed (§3.2).

Every task goes through the same loop: `orchestrate.plan` → an implementer
sub-agent → `orchestrate.verify` → an **adversarial** reviewer sub-agent that
reads the same spec → `orchestrate.fix` → `orchestrate.merge` (which runs the
verify loop on the merged tree before it commits). The mechanics are in
`docs/orchestration/README.md` and in the plugin's own repository
(`C:\Users\gpmpo\orchestration-plugin`).

**The reviews earned their keep.** Three of the five tasks went back for a
fix round, and the reviewers found real defects that every other check had
missed — a bytecode that computed the wrong values, an image check that
printed a number it had not measured, a gate that refused every `switch`. The
reviewer's own instrument was the decisive thing: it **wrote an interpreter
from the documented contract and executed the bytecode** against it, and it
**hashed every NUL-terminated run of the built image** to measure the leak.
Both are worth repeating: *the contract is the deliverable, so test the
contract, not the shape.*

## 3. The three measurements that reshaped the plan

### 3.1 The VM cannot protect the aim maths

T20a's gate says **none** of the six protected functions is virtualizable:
four are refused `eh_pad` (they have an EH edge) and two `unsupported_type`
(they return a `float`). The aim maths is a **float pipeline with exception
edges**, and the machine handles integers and pointers only. Over the whole
`payload/aim` module the gate admits 1878 of 2490 functions — but what it
admits is the small integer helpers, not anything an analyst would care about.

**So the interpreter (T20b) would protect nothing.** T20a is merged anyway: it
changes no image, it is the precondition for any future VM work, and it is the
artefact that says *the VM cannot help here*.

### 3.2 The strings do not leave the image

T21's mechanism works — the literal's own symbol is deleted and the decode is
correct — but the **bytes survive**:

* the **MSVC ABI merges a literal with a longer literal that ends with it**
  (`movement_thirdperson_fov` holds `thirdperson_fov` at offset 9), so
  deleting the literal's symbol does not delete its bytes;
* the payload's own data model **pins the feature keys** into a global
  initializer (`overlay/include/cs2/overlay/settings_table.hpp` holds
  `inline constexpr std::array<SettingInfo, 87> SETTING_TABLE`), and a
  literal that **any** module refuses survives the link (`linkonce_odr`).

Measured: **0 of 77** protected literals are absent from the shipped image;
858 plaintext occurrences survive against 952 in the unprotected one. The
cause split: **35** are refused in another module, **42** survive as the tail
of a longer literal.

**And the feature-key names are displayed** — the in-process menu draws them
and so does the overlay's fallback menu — so a name that is displayed **must
exist in a shipped image**. The feature list is the payload's **data**, not its
code: hiding it is a **product** decision, not a pass decision. That is why
the "strings leave the image" follow-up was **dropped** rather than scheduled.

### 3.3 The cross-TU hole is 26 edges, and the seed changes 441 bytes

Of the 332 call sites of the six protected functions, 37 are rewritten and 295
are skipped — but 179 of those are **intrinsics** and 59 are **CRT calls**.
The hole that matters is **26 cross-TU call sites to 24 payload functions**:
the aim graph. **T23a closes 14 of them** (4 of them EH edges, which the
review verified at run time: the exception is still caught through the
rewritten indirect `invoke`). **T23b** closes the 12 `sret` ones.

**And the edges that are not protected are not "hidden".** The same symbols
are still called **directly** from the *unprotected* functions - the reviewer
proved it at the object level (24 `REL32` relocations against
`?polygon_contains`, `?polygon_centroid` and `?yaw_scale` in
`src_field.cpp.obj`). The check now **measures and reports** that number
(46 on the merged tree) rather than claiming the edges are gone: the pass
rewrites the calls of the *selected* functions, and only those.

And the seed: two builds with different seeds differ in **441 bytes** of
`.rdata` and **not one byte** of the 1.7 MB `.text` — so a scanner that hashed
the code would match every build. Two builds with **no** seed are
byte-identical: the fallback is a constant of the payload. That is T24, and
it is in review: **83.53 %** of the `.text` now differs between two seeded
builds.

## 4. The environment

- **LLVM 22.1.8** at `C:\Program Files\LLVM` is a **clangd-only** install: no
  headers, no `LLVMConfig.cmake`, no `opt.exe`. The dev SDK is the release
  **tarball** at `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc`
  (`scripts/build-protect-plugin.py` finds it and **fails loudly** with the
  download URL and the SHA-256 if it is missing).
- `cmake` and `ctest` are **not** on PATH: they are in
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`.
- The payload's compiler is **clang-cl 22.1.8** (`vcvars64` + `set CC=clang-cl`).
  The plugin must be built with the same one (the ABI and the CRT must match).
- The **host** build must use **gcc** on this machine (`hooks-inline` fails
  under MSVC).

### The shared assets move under the build

`calibration/` and `external/Vulkan-Headers` are **junctions** to
`C:\Users\gpmpo\assets\cs2\`, and **`calibration/offsets.json` changes during
a session** (it was rewritten twice while the reviews were running: 29
offsets → 28). It **regenerates the SDK headers**, so the payload's base
relocation count moves with it (13,025 → 13,035 → 13,053 in one evening).

**What that means for you:** the relocation count is **not** a constant of the
tree, and two builds taken minutes apart can differ for that reason alone.
**Freeze the calibration** (check its SHA-256 before and after) when you
compare two images, and treat the relocation count as the tell that the input
moved. The T24 spec's §1 does exactly this and is the model.

## 5. The facts that cost a day to find

Do not re-derive these. They are all in `tools/protect/README.md` §12 and §14.

- The plugin header is `llvm/Plugins/PassPlugin.h` (moved in LLVM 22) and
  `LLVM_PLUGIN_API_VERSION` is **2**.
- `add_llvm_pass_plugin()` is a **no-op on Windows** (CMake's `MODULE` type
  is unsupported by the MSVC toolchain). The plugin is a `SHARED` library.
- The SDK's exported CMake targets hardcode a **dead VS 2022 DIA SDK path**;
  the imported target's `INTERFACE_LINK_LIBRARIES` has to be rewritten.
- `llvmGetPassPluginInfo` needs an explicit `__declspec(dllexport)`.
- **clang-cl ignores `-fpass-plugin=`**; the working spelling is
  **`/clang:-fpass-plugin=<dll>`**.
- A plugin's `cl::opt` is **not reachable via `-mllvm`** (a statically linked
  plugin has its own `cl` registry) → the configuration channel is the
  **environment**: `CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`,
  `CS2_PROTECT_VERBOSE`, `CS2_PROTECT_COVERAGE`.
- The release LLVM is `-fno-rtti` + static CRT; the plugin must match.
- **LLVM 22.1.8's X86 backend mis-materialises a 64-bit constant** in some IR
  shapes (bit 32 flipped). Every 64-bit constant the pass needs therefore
  lives in a **global** and is read with a **volatile load** — never in the
  instruction stream.
- `registerPipelineStartEPCallback` sees **raw** IR; the rewriting passes use
  **`registerOptimizerLastEPCallback`** (optimised IR, before codegen).
- A plain `PassInfoMixin` is an *optional* pass and is skipped on `optnone`
  functions — the pass needs `isRequired() { return true; }`.
- `OptimizerLast` also fires in the **LTO pre-link** phase → the per-phase
  module flags keep a module from being transformed twice.
- The **order of the passes inside the transform phase matters**: the
  virtualizer's analysis must run **before** the call encryption (the
  encryption rewrites the calls into indirect ones, and an indirect call is a
  refusal) — see `tools/protect/src/plugin.cpp`.

## 6. The commands

```powershell
cd C:\Users\gpmpo\cs2-internal

# the plugin
$plugin = python scripts\build-protect-plugin.py      # prints only the DLL's path
ctest --test-dir tools/protect\build --output-on-failure

# a protected payload
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"
```

`CS2_PROTECT_CONFIG` must be set for **both** the configure and the compile
step: the plugin reads it in the compiler process. Use a **fresh** build
directory name; do **not** `meson setup --wipe` on `build`, `build-payload`,
`build-overlay` or `build-injector`.

`tools/protect/policy.local.json` is **gitignored** — it is the operator's
file, and it carries the `seed`. `tools/protect/policy.example.json` is the
shipped one.

## 7. Not verified

- **The game.** The agent runs in session 0 with no GPU, so the payload has
  **never been injected** with the protection active. To exercise it for real:
  `schtasks /run /tn CS2Launch`, then
  `build-injector\injector.exe cs2.exe build-payload-protect\payload.dll --load-missing`
  (**inject once** — a second injection into a running game double-hooks it).
- The **bytecode has never been executed**: T20a checks the bytecode's
  *shape* and its *values* (the value test runs it in an interpreter written
  from the contract), but no interpreter runs it **in the payload**.
- The **run-time cost** of the extra dispatches (T23a) and of the virtualizer
  (T20c) is **not measured** by this repository.

## 8. The next task

Pick the first of these that is not in flight:

1. **T23a** — the cross-TU routing: 37 → 51 dispatches. *In review.*
2. **T24** — the per-build polymorphism: the seed becomes mandatory, the
   mixer's constants and rotation become per-build, the module's function list
   is permuted. The acceptance criterion is a **measurement**:
   `compare_builds.py --section .text` must report that two seeded builds
   differ. *In flight.*
3. **T23b** — the `sret` edges: T19's refusal of a struct-returning callee is
   a **policy, not a target limit** (`llc` accepts an indirect `sret` call on
   `x86_64-pc-windows-msvc`), so the rule can be narrowed. It changes the
   **call ABI**, so it needs its own differential test. The spec is
   `docs/orchestration/spec-t23-protect-cross-tu.md` §5.3.
4. **T22** — the integrity check (anti-tamper, not anti-reversing). The
   survey's §7 has the model and the cost: a post-link binder over a
   self-checksum. **Do not ship a pre-codegen check** — a fake check "looks
   like protection while providing none".

And when you take a task: **read the spec, then measure**. Every task in
this list was reshaped by a measurement that contradicted the plan, and every
one of those measurements is in the plan now (§1.1-§1.4) with the evidence.
