# T23 — the cross-TU call graph: the aim graph leaves the image

**Goal:** the protection pass must stop hiding only the *cleanup* code. The
calls that carry the aim call graph — the ones that cross a translation unit —
must become encrypted dispatches too, and the build must **fail** when one of
them does not.

**This is a follow-up to T19** (`spec-t19-protect-call-encryption.md`) and it
**runs before T20** in the sense that it is cheap: it changes the pass, not
the payload's code generation. T20's virtualisation is a different (and much
more expensive) answer to the same question; §4.4 says why it is not the
answer to *this* one.

**Read `spec-t19-protect-call-encryption.md` first.** T19's §2.2 (the table,
the descriptor and the key), §2.4 (the rewrite), §2.5 (the skip rules), §2.6
(the report), §3 (the `noinline` marking), §4 (the danger list) and §5 (the
tests) apply verbatim. Where this document and T19 disagree, this document
wins — it is the narrower, later statement.

---

## 1. What this task delivers, and what it does not

| | |
|---|---|
| **Delivers** | (1) the pass routes a **cross-TU** callee through the module's table, as a relocatable pointer to the external symbol; (2) the mechanism that tells the pass which declarations may be routed (the policy's `cross_module` list) and the two build-level checks that keep the list honest; (3) the narrowing of the `sret` skip rule, with the evidence that the rule is a policy and not a limit of the target; (4) the report and the coverage fields that name what is covered and what is not; (5) the tests that prove all of it |
| **Does not deliver** | any change to the payload's sources, to the payload's build files, or to the payload's generated code beyond the dispatch rewrite itself; the virtualisation (T20/T20b); the hiding of the **inbound** edges (the callers of the protected functions — §5.4); the hiding of the `invoke` edges (§5.4); the hiding of the CRT imports (§5.4) |

**The payload is not touched.** Not one line of `payload/**` changes, and the
aim maths is compiled exactly as it is today. That is the point of the design
decision in §4.5, and it is what makes this task safe to land next to a
ragebot that is tuned for latency.

**What the operator gets:** a number that goes **up** (37 → at least 63
encrypted dispatches in the current tree, §3.3) and a build that **fails** when
a protected function makes a direct call into the payload's own code.

## 2. The rules this task must not break

Everything in T19 applies. In particular:

- The payload is **manually mapped**. The table holds **relocatable pointers**,
  the descriptor holds **plain integers**. A relocatable value inside the
  cipher would be corrupted by the mapper's fixups (T19 §4.2).
- The pass must not introduce **new imports**, and must not change the
  import or the export table of the image.
- The **ABI** of a rewritten call is unchanged: the same function type, the
  same parameter types, the same calling convention, the same `AttributeList`
  (T19 §2.4). This task touches that rule in exactly one place — the `sret`
  parameter (§5.3) — and the change is proven, not assumed.
- The **host test suite** (`meson test -C build`) must stay green. It does
  not use the pass, so any change there is a mistake.
- The pass runs at **`registerOptimizerLastEPCallback`** and must define
  **`isRequired() { return true; }`**.
- The **LTO pre-link** phase fires the same extension point: a module must not
  be transformed twice. T19's per-phase module flags (`cs2.protect.selected`,
  `cs2.protect.transformed`, `tools/protect/src/plugin.cpp:56-63`) are the
  model; this task does not change them.
- Errors are **values**, not exceptions: a pass must never throw out into the
  driver, which is built without exceptions.
- The pass must never **half-rewrite** a call: a call is either fully
  rewritten or fully untouched (T19 §2.5).

## 3. The hole, measured

All the numbers below were measured on this tree (2026-10-21) with
`tools/protect/policy.local.json` and the plugin of
`tools/protect/build/Cs2Protect.dll` (SHA-256 `a25be78d…`, the one the
protected build of `build-payload-protect` used). The measurement is the
plugin's own report, reproduced by compiling the twelve aim sources with the
plugin loaded:

```
clang-cl /I..\payload\aim\include /I..\payload\game\include /MD /EHsc /O2 \
          -std:c++latest /clang:-fpass-plugin=<Cs2Protect.dll> /c ../payload/aim/src/<x>.cpp
```

with `CS2_PROTECT_CONFIG` and `CS2_PROTECT_COVERAGE` set. The coverage files
of the protected build agree with it (the counts are the same: table=7
descriptor=11 for `solver.cpp`, table=13 descriptor=21 for `damage.cpp`,
table=2 descriptor=3 for `geometry.cpp`, table=2 descriptor=2 for
`field.cpp`).

### 3.1 The call sites of the six protected functions

| protected function | its TU | call sites | rewritten | skipped | the skips, per reason |
|---|---|---|---|---|---|
| `cs2::aim::solve` | `solver.cpp` | 95 | 7 | 88 | intrinsic 44, external 33, sret 9, invoke 2 |
| `cs2::aim::solve_backtrack` | `solver.cpp` | 19 | 4 | 15 | intrinsic 13, sret 1, invoke 1 |
| `cs2::aim::occlude_polygon` | `damage.cpp` | 167 | 21 | 146 | intrinsic 94, external 30, sret 3, invoke 19 |
| `cs2::aim::DamageField::hit_probability` | `field.cpp` | 3 | 2 | 1 | external 1 |
| `cs2::aim::project_capsule` | `geometry.cpp` | 48 | 3 | 45 | intrinsic 28, external 9, sret 4, invoke 4 |
| `cs2::aim::penetration_survival` | `damage.cpp` (inline in `field.hpp`) | 0 | 0 | 0 | — |
| **total** | | **332** | **37** | **295** | intrinsic 179, external 73, sret 17, invoke 26 |

The 37 rewritten call sites are the **cleanup code**: the table of
`solver.cpp` holds seven entries — `??1SolveInput`, `??0SolveInput`,
`??1Polygon`, `??1?$vector<UOccluderCell>`, `??1OccluderCell`,
`??1DamageField` (five destructors and a copy constructor) and
`?_Tidy@?$vector<UDamageFragment>` (a vector helper). That is what
`tools/protect/README.md` §10.6 and `docs/handoff-protect.md` §7 already say.

### 3.2 The cross-TU edges — the hole itself

Of the 90 skipped calls that are not intrinsics and not EH edges, **26 call
sites reach 24 distinct functions of the payload's own code**, and every one
of them stays a **direct call in the image**:

| # | callee | defined in | called from | why it stays direct |
|---|---|---|---|---|
| 1 | `cs2::aim::project_capsule` | `geometry.cpp` | `solve` | `indirect_parameter` (sret) |
| 2 | `cs2::aim::project_obb` | `geometry.cpp` | `solve` | `indirect_parameter` (sret) |
| 3 | `cs2::aim::occlude_polygon` | `damage.cpp` | `solve` | `indirect_parameter` (sret) |
| 4 | `cs2::aim::Basis::unproject` | `geometry.cpp` | `solve`, `occlude_polygon` | `indirect_parameter` (sret) |
| 5 | `cs2::aim::DamageField::add` | `field.cpp` | `solve` | `external_callee` |
| 6 | `cs2::aim::DamageField::add_hitbox` | `field.cpp` | `solve` | `external_callee` |
| 7 | `cs2::aim::DamageField::clip` | `field.cpp` | `solve` | `external_callee` |
| 8 | `cs2::aim::DamageField::value_at` | `field.cpp` | `solve` | `external_callee` |
| 9 | `cs2::aim::DamageField::base_value_at` | `field.cpp` | `solve` | `external_callee` |
| 10 | `cs2::aim::DamageField::value_at_pellets` | `field.cpp` | `solve` | `external_callee` |
| 11 | `cs2::aim::DamageField::hit_probability` | `field.cpp` | `solve` | `external_callee` |
| 12 | `cs2::aim::DamageField::argmax_hit_probability` | `field.cpp` | `solve` | `indirect_parameter` (sret) |
| 13 | `cs2::aim::DamageField::argmax_hitchance` | `field.cpp` | `solve` | `indirect_parameter` (sret) |
| 14 | `cs2::aim::DamageField::argmax_expected` | `field.cpp` | `solve` | `indirect_parameter` (sret) |
| 15 | `cs2::aim::DamageField::argmax_pellets` | `field.cpp` | `solve` | `indirect_parameter` (sret) |
| 16 | `cs2::aim::DamageField::argmax_damage` | `field.cpp` | `solve` | `indirect_parameter` (sret) |
| 17 | `cs2::aim::penetration_damage_at_target` | `penetration.cpp` | `occlude_polygon` ×2 | `external_callee` |
| 18 | `cs2::aim::OccluderCell::lower_bound_distance` | `occlusion.cpp` | `occlude_polygon` | `external_callee` |
| 19 | `cs2::aim::OccluderCell::ray_span` | `occlusion.cpp` | `occlude_polygon` | `indirect_parameter` (sret) |
| 20 | `cs2::aim::prepare_convex_clipper` | `occlusion.cpp` | `occlude_polygon` | `indirect_parameter` (sret) |
| 21 | `cs2::aim::subtract_convex` | `occlusion.cpp` | `occlude_polygon` | `external_callee` |
| 22 | `cs2::aim::polygon_contains` | `geometry.cpp` | `occlude_polygon` | `external_callee` |
| 23 | `cs2::aim::polygon_centroid` | `geometry.cpp` | `occlude_polygon` | `external_callee` |
| 24 | `cs2::aim::yaw_scale` | `geometry.cpp` | `hit_probability` | `external_callee` |

**26 call sites, 24 distinct functions, two reasons:**

- **14 call sites / 13 functions** are refused by the *module* rule:
  `callee->isDeclarationForLinker()` is true for a callee that has no code
  in this module, so the call is skipped with the reason `external_callee`
  (`tools/protect/src/transform.cpp:175-180`). The rule's own comment says
  "it is an import, and it is in the import table anyway" — which is **false
  for these 13**: they are the payload's own functions, defined in
  `geometry.cpp`, `field.cpp`, `occlusion.cpp` and `penetration.cpp`.
- **12 call sites / 11 functions** are refused by the *`sret`* rule: the
  callee returns a `Polygon`, a `std::vector<DamageFragment>`, a `Vec3`, a
  `Vec2`, a `std::optional<RaySpan>` or a `ConvexClipper` by value, so the
  call carries an `sret` parameter and the pass refuses the whole
  `byval`/`sret`/`inalloca`/`preallocated` family
  (`tools/protect/src/transform.cpp:155-162`).

The remaining 59 of the 73 `external_callee` skips are the CRT: `operator
new`/`operator delete` (32), `_Query_perf_counter` and `_Query_perf_frequency`
(20), `atan2f` (3), `asinf` (3), `powf` (1). Those are the "in the import
table anyway" case the rule was written for.

### 3.3 What the number becomes

| | today | after this task |
|---|---|---|
| encrypted dispatches in the image | **37** | **≥ 63** (37 + the 26 cross-TU sites) |
| …and with the `sret` rule narrowed | | **67** (the 4 remaining same-TU `sret` sites of `project_capsule` become rewritable too) |
| cross-TU edges of the protected functions that are direct | 26 | **0** |
| tables | 4 (one per module) | 4 (unchanged: the cross-TU entries go into the *caller's* table) |

The number 37 is a snapshot of the tree, exactly as T20a's acceptance
criterion 6 says. **The rule is that the number goes up**, and the rule that
proves it is not the number but the decode: `check_payload.py` already
decodes every descriptor of the image with the key the pass used, resolves the
index in the table of the image and checks that the entry is code of the
image (`tools/protect/check_payload.py:236-334`). That machinery has to keep
working for the **new** entries, and the new entries are exactly the ones
whose value is a relocation the linker resolved — the check must therefore
also prove that the resolved address is the *callee's* address and not the
address of the import slot (§7.3).

## 4. The four options, and the decision

### 4.1 Option A — a unity build of `cs2_aim`

Meson supports `unity: true` on a target; the twelve sources of
`payload/aim/meson.build:8-24` would become one translation unit, and the
cross-TU edges would become intra-module edges that the pass can rewrite.

**What breaks, concretely (measured):**

- **It does not compile.** A unity build of the twelve files today produces
  **six redefinition errors**: `k_epsilon` (`geometry.cpp:9`, `field.cpp:11`,
  `occlusion.cpp:9`), `k_pi` and `k_two_pi` (`geometry.cpp:63-64`,
  `heatmap.cpp:11-12`), `k_hitgroup_head` (`heatmap.cpp:29`,
  `reader.cpp:19`), and `unwrap_yaw` (a *function* defined in both
  `geometry.cpp:67` and `heatmap.cpp:16`). Fixing them means editing
  `payload/aim/**` — the task's blast radius grows from "the pass" to "the
  aim module's sources".
- **It changes the aim maths' code.** A unity build lets the compiler
  inline across the twelve files, and the project's FP code is already
  contracted into `llvm.fmuladd.f32` (the intrinsics are visible in the
  plugin's own report: 6 of them in `project_capsule`, 4 in `solve`). FMA
  contraction across an inlined return boundary changes the *rounding*, and
  the aim model's output is compared against exact values in
  `payload/aim/tests/`. The host suite is the safety net, but the suite
  compares with tolerances, not bit for bit.
- **It breaks the coverage's identity rule.** The coverage's module identity
  is the source path relative to the tree root, and the readers count only
  the files whose source is under the tree that is being built
  (`tools/protect/README.md` §9). A unity file lives in the **build
  directory**, so its identity would depend on the build directory's name —
  and two build directories of the same tree would then write two coverage
  files for one module, which is the double-count the README warns about
  ("the payload check would report that the image has half the dispatches the
  pass wrote").
- **It breaks the module-scoped policy entries.** `module_applies()`
  (`tools/protect/src/selection.cpp:335-353`) compares the module id with the
  entry's scope; a unity file is named `unity_0_…`, so the shipped example
  `{"symbol": "cs2::game::StateReader::*", "module": "state_reader.cpp"}`
  would match nothing.
- **It costs five times the compile time.** Measured with the SDK's
  `clang-cl -O2` on this machine: the twelve files compile in **≈ 6 s**
  (one shell, warm), the unity file in **≈ 30 s** (with the redefinition
  errors present, so the number is an upper bound).
- **And it does not close the hole.** The 12 `sret` edges are refused by a
  rule that does not care about translation units. A unity build takes the
  cross-TU edges from 26 to **12** — and the 12 that remain are the most
  interesting ones (11 functions, 12 call sites: `project_capsule`,
  `project_obb`, `occlude_polygon`, the five `argmax_*`, `unproject` (twice),
  `ray_span`, `prepare_convex_clipper`).

### 4.2 Option B — a cross-TU table in the transform (the pass routes the external callee)

The pass puts a **declaration** into the module's table as a relocatable
pointer to the external symbol, exactly like it puts a same-module callee
there today. The linker resolves the reference inside the DLL, the mapper
applies the DIR64 fixup, and the dispatch calls the function.

**What breaks, concretely:**

- **Nothing in the payload.** No source change, no build change, no change
  to the generated code beyond the dispatch itself.
- **The hard part is the classification** (§5.2): the pass sees one
  translation unit at a time, and at the IR level an *import* and an
  *internal cross-TU declaration* are the same thing — a `declare` with no
  body. The pass cannot tell them apart; §5.2 decides who does.
- **The `sret` edges still need §5.3.** The module rule and the `sret` rule
  are independent: option B closes the 14 `external_callee` edges, and the
  12 `sret` edges need the rule to be narrowed.
- **The check has to grow a little**: the existing "every dispatch decodes to
  a callee of the image" check is enough to catch a *wrong* entry, but the
  "the hole did not re-open" rule needs the payload's own symbol set
  (§7.3).

**The mechanism works at the object level.** A constant table in `.rdata`
that holds a pointer to an external symbol produces an
`IMAGE_REL_AMD64_ADDR64` relocation for that entry, and the linker turns it
into an `IMAGE_REL_BASED_DIR64` base relocation, which the mapper already
applies. Measured with the SDK's `clang-cl` on a two-line file
(`extern float other(float); extern "C" const void* const table[] = {&other,
&other};`):

```
Relocations [
  Section (5) .rdata {
    0x0 IMAGE_REL_AMD64_ADDR64 ?other@@YAMM@Z (18)
    0x8 IMAGE_REL_AMD64_ADDR64 ?other@@YAMM@Z (18)
  }
]
```

The import table is **not** touched: the symbol is defined in the DLL, so
the linker resolves it locally.

### 4.3 Option C — an LTO build of the payload

With LTO the whole payload is one module at the link phase, so every call is
intra-module and the hole disappears by construction.

**What breaks, concretely:**

- **The transform would not run at all.** The pass is registered at
  `registerOptimizerLastEPCallback` (`tools/protect/src/plugin.cpp:269-271`),
  and that extension point fires in the **LTO pre-link** phase as well. The
  pre-link phase sets the module flag `cs2.protect.transformed`, so the
  link phase — the one where the payload is a single module — would **skip
  the transform entirely**. The plugin would have to be loaded by the
  **link step** (lld's plugin mechanism) instead of by clang, and the
  per-phase flag discipline of T19 would have to be reworked around the
  pre-link/post-link distinction.
- **The payload links with `lld-link` today** (`build-payload-protect/build.ninja`:
  `rule cpp_LINKER / command = "lld-link" …`) but the *compile* step would
  have to produce bitcode, and the SDK's `opt`/`lld` plugin loading on
  Windows is not something this repository has ever exercised.
- **LTO changes the inlining of the whole payload**, and the payload's hot
  path is the ragebot (`docs/ragebot-perf.md`). The `noinline` marking of
  the selected functions survives, but every other inlining decision changes.
- **The coverage collapses to one file per link**, so the per-module
  identity (`README.md` §9) and the module-scoped policy entries break in
  the same way a unity build breaks them.
- **Honest verdict:** the risk is high and the payoff is uncertain. The
  transform would have to be re-homed into a link-time plugin, and the
  pre-link phase — where the pass *does* run — has exactly the same
  cross-module structure as today's per-TU compile.

### 4.4 Option D — leave the cross-TU graph alone and rely on the virtualisation

The virtualisation (T20/T20b) removes the **body** of the selected function:
the body becomes bytecode, and the interpreter runs it. That is a genuinely
different lever, and it is the only one that removes the *code* of the aim
maths from the image.

**It does not close this hole, for two reasons:**

1. **The thunks name their callees.** T20a's design (§3.5) is: the
   interpreter calls through a uniform signature, and a **per-call-site
   thunk** adapts the real callee to it. The thunk's body is "load the
   arguments out of the scratch array, **call the real callee**, store the
   result". So the thunk is a *direct* call to the cross-TU callee, and the
   edge is still a direct call in the image. The virtualisation hides the
   *order* of the calls (that is in the bytecode), not the *edges*.
   Closing the edges requires the thunks to be routed through the encrypted
   table, which is T20b's problem and is *this* task's mechanism.
2. **The inbound edges stay.** The virtualisation protects the *body* of
   `cs2::aim::solve`; the calls *to* `solve` are in
   `payload/control/src/service.cpp:2038`, `:2131`, `:2596` and
   `payload/control/src/trigger.cpp:286`, and they are not inside a selected
   function, so they stay direct (§5.4).

So D is a deferral of the *body*, not of the *edges* — and it costs 5-50x at
run time. The residual exposure of D, stated plainly: the callers stay
direct, the thunks name their callees, and the *set* of callees of every
protected function remains readable. This task does not fix those either
(§5.4), but it does remove the 26 edges, and it does so for the price of a
dispatch.

### 4.5 The decision

**Option B, plus the narrowing of the `sret` rule (§5.3).**

**The single strongest argument for it:** *it is the only option that closes
the hole without changing the code the payload's hot path is made of.* The
aim maths is the ragebot's inner loop; a unity build (A) or an LTO build (C)
changes what the compiler emits for it — FMA contraction across inlined
boundaries included — and the only safety net for that is a test suite that
compares with tolerances. Option B changes **one thing** in the generated
code: a direct call becomes an indirect dispatch through a table entry, which
is exactly the change T19 already makes and already proves (the differential
test, `tools/protect/tests/diff_test.py`).

The runner-up reasons:

- **It needs no new build wiring.** The classification comes from the
  policy, which the plugin already reads in both phases
  (`tools/protect/src/config.hpp:48-53`).
- **It composes with the existing structures.** The table, the descriptor,
  the key, the permutation (`tools/protect/src/transform.cpp:128-134`) and
  the coverage file are reused as they are; a cross-TU callee is just another
  table entry.
- **The build can keep the hole closed.** The two checks of §7 (the policy
  requirement, and the "no direct call into the payload's own code" rule)
  turn a silent weakening into a build failure — the failure mode this
  project exists to avoid.

**What has to be true for it to work:**

1. A cross-TU callee must be routable as a table entry: **verified** (§4.2).
2. The `sret` skip must be narrowable: **verified with the SDK's backend**
   (§5.3), and it must be proven by the differential test.
3. The pass must know which declarations are the payload's own: **decided**
   in §5.2 — the policy names them, and the build checks the list.
4. The check must be able to tell "the payload's own code" from "the CRT":
   **decided** in §7.3 — the payload's own static libraries, read with
   `llvm-nm` (the SDK's, next to the `llvm-objdump` the check is already
   given).

**The risk it carries:** the dispatch cost. Every protected call site pays
eight instructions and six volatile reads (T19 §10.3), so this task adds
**26 more dispatch sites** on the aim's path than today. The operator may
add the 32 allocator calls (`operator new`/`operator delete`) to the list as
well, which would be 32 more; that is the knob, and the operator's call.
This repository does not measure the cost and has no benchmark (§5.4), so
the number must not be invented.

## 5. The design decisions

### 5.1 The table entry of a cross-TU callee

**Decided:** the caller's own table holds the cross-TU callee, as a
relocatable pointer to the external symbol — the same kind of entry as a
same-module callee, only with a relocation instead of an absolute address.

- The table stays `private`, `constant` and in `.rdata`
  (`tools/protect/src/transform.cpp:419-423`); the mapper applies the DIR64
  fixups it already applies today.
- The **descriptor** still holds plain integers: the index of the entry
  (`index ^ key`). Nothing about the cipher changes.
- The **permutation** (`permute()`, `transform.cpp:128-134`) applies to the
  whole table, cross-TU entries included: the index in the descriptor is not
  a predictable table position, and the table does not have the same shape in
  two builds.
- The **call** keeps its function type, its parameters, its calling
  convention and its `AttributeList` (T19 §2.4). The callee operand becomes
  the loaded pointer; the call becomes indirect. The ABI is preserved by
  construction.

**Rejected: the table of *another* module.** `tools/protect/README.md` §10.6
mentions "route cross-TU calls through a per-module table (which needs the
table of *another* module — a seam the format of the descriptor has to keep
open)". That is the more general design, and it would let one module own the
table of a shared library of functions. It is rejected here for two reasons:
the caller's own table needs no change to the descriptor's format (the index
is an index, whatever table it is in), and a table entry that is a *pointer
to another table* would need a second indirection at the call site (a load
of a table-of-tables), which the eight-instruction decode does not have room
for. If a future task needs a shared table, it can be a *third* table kind
in the same module; nothing in this task forecloses it.

**Rejected: a per-module exported table in the payload** (a hand-written
`extern "C" void* cs2_aim_table[]` in the payload). It would need the
payload to change, and the pass would need to know the *index* of each
symbol in it — a second, hand-maintained mapping next to the policy's. The
policy's list is the only mapping this task needs.

### 5.2 How the pass learns that a declaration is the payload's own code

**The problem, stated exactly:** at the IR level an *import* and an
*internal cross-TU declaration* are the same thing — a `declare` with no
body. `callee->isDeclarationForLinker()` is true for both
(`transform.cpp:175`). The pass cannot tell `atan2f` (an import) from
`cs2::aim::project_capsule` (defined in `geometry.cpp`) by looking at the
module. Anything that claims otherwise is wrong.

**Decided: the policy names them.** A new top-level key of the policy file:

```json
{
  "protect": [ "cs2::aim::solve", … ],
  "cross_module": [
    "cs2::aim::project_capsule",
    "cs2::aim::project_obb",
    "cs2::aim::occlude_polygon",
    "cs2::aim::Basis::unproject",
    "cs2::aim::DamageField::*",
    "cs2::aim::yaw_scale",
    "cs2::aim::penetration_damage_at_target",
    "cs2::aim::OccluderCell::*",
    "cs2::aim::polygon_contains",
    "cs2::aim::polygon_centroid",
    "cs2::aim::prepare_convex_clipper",
    "cs2::aim::subtract_convex"
  ],
  "deny": [ "cs2::loader::self_unmap" ],
  "seed": "0xA04252B187478C00A40BC6E8B04F1C9A"
}
```

- A `cross_module` entry is a **glob**, matched exactly like a `protect`
  entry: against the mangled symbol and against the demangled qualified name
  (`tools/protect/src/selection.cpp:80-83`, `qualified_name()` at
  `selection.cpp:231`). `cs2::aim::DamageField::*` therefore covers **every**
  `DamageField` member in one line — the eight that the protected functions
  call across a translation unit today, and any that a later change adds.
- The entry is matched against a **declaration** in the module: a function
  that has no body here. A same-module callee is routed by the existing rule
  and needs no entry.
- The list is **operator data**. It is the operator's declaration of "these
  cross-TU edges must not be readable". The shipped
  `tools/protect/policy.example.json` documents the key with the list of the
  current tree; the operator's `tools/protect/policy.local.json` (which is
  **gitignored**, `.gitignore:35`) carries the list the build uses.

**Why not the alternatives:**

- **A name convention** ("a qualified name that starts with `cs2::` is
  internal"). It is automatic, but it puts a payload-specific assumption
  inside a build tool, and its failure mode is bad: a CRT import that looks
  internal would be routed, and the table entry would hold the **IAT slot**
  rather than the function — the dispatch would call the import slot. The
  payload check would catch it (§7.3), but only after the image was linked.
  A policy list fails the same way, but the operator is the one who writes
  it, and the list is reviewable.
- **A generated manifest** (a script that scans the payload's sources and
  emits the symbol list). This is the *right* direction for a *manifest* of
  the payload's symbols, and the chicken-and-egg the task description points
  at is real: the objects that would produce the manifest are produced by
  the same compile step that needs it. A **static, checked-in** manifest
  avoids the cycle (the file exists before the build), and the *staleness* is
  caught after the build by `llvm-nm` over the payload's libraries — which
  is exactly what §7.3 does. So the manifest is not rejected on principle; it
  is rejected here because the **policy is already the file the operator
  edits**, it already has the glob machinery, and a second file with the
  same information in it is a second thing to keep in sync. If a later task
  wants the payload's symbol set to be *generated*, the check of §7.3 is
  where it plugs in.
- **A build step that runs `llvm-nm` over the already-built `cs2_aim`
  static library.** This is the manifest, generated from the build — and it
  cannot exist before the build that needs it. It would require compiling
  the payload twice (once to produce the symbols, once with the pass), which
  is a build-time cost for a list that changes once per code change. The
  *verification* use of `llvm-nm` (§7.3) is cheap and is in this task; the
  *generation* use is not.

### 5.3 The `sret` rule, narrowed

**The rule today** (`tools/protect/src/transform.cpp:155-162`): a callee with
a `byval`, `sret`, `inalloca` or `preallocated` parameter is refused, with
the reason `indirect_parameter`. T19's spec (§2.5) gives the reason: "Windows
x64 rejects these in an indirect signature (LLVM 22)".

**That reason is not a limit of the target.** The SDK's own backend accepts
an indirect call with an `sret` parameter on `x86_64-pc-windows-msvc`.
Measured with the SDK's `llc.exe` on a three-line IR file:

```llvm
%S = type { i32, i32 }
@fp = global ptr @target
declare void @target(ptr sret(%S))
define void @f(ptr %out) {
  %f = load ptr, ptr @fp
  call void %f(ptr sret(%S) %out)
  ret void
}
```

```
llc.exe -mtriple=x86_64-pc-windows-msvc -filetype=obj   →  exit 0
```

and the emitted code is the same call the direct one would be: the hidden
pointer is in `%rcx` (where the caller's own first argument already is), and
the callee returns the pointer in `%rax`:

```
0000000000000000 <f>:
       0: 48 83 ec 28    subq $0x28, %rsp
       4: ff 15 00 00 00 00   callq *(%rip)
       b: 48 83 c4 28    addq $0x28, %rsp
       f: c3             retq
```

The same experiment with `byval` also succeeds (exit 0).

**Decided:**

- The `sret` parameter is **no longer** a reason to refuse a call, **on
  `x86_64-pc-windows-msvc` only**. The pass reads the module's target
  triple and refuses an `sret` call on any other target (the rule stays
  there) — fail closed, and honest about what was verified.
- `byval`, `inalloca` and `preallocated` **stay refused**. The payload has
  no `byval` edge (the 17 `indirect_parameter` skips in §3.1 are all `sret`),
  and the conservative choice costs nothing. The `byval` case *is* accepted
  by the backend, but the copy semantics of an unknown callee are not
  something this task needs to open.
- The **`sret` attribute stays on the call**. The transform only replaces the
  callee operand; the call's type, its parameters and its `AttributeList`
  are untouched, so the hidden pointer is placed exactly as the direct call
  placed it. The ABI is preserved **by construction**, and the differential
  test proves it (§8.2).

**The consequence for the existing tests:** `tools/protect/tests/ir_test.py`
currently asserts that the `sret` call of the fixture is *not* rewritten
(`ir_test.py:190-192`), and `tools/protect/tests/fixtures/diff_fixture.cpp:35-46`
has an `sret` callee that the pass is expected to leave alone. **Both change
in this task**: the `sret` call becomes a dispatch, and the fixture's
`cs2_fixture_big` is rewritten. The IR test then asserts the opposite, and
the differential test proves that the rewritten `sret` call returns the same
value. This is the point of the task: 12 of the 26 cross-TU edges are
`sret` edges, and they are the most interesting ones.

### 5.4 What this task does **not** hide

Stated plainly, because the claim of the protection must not be stronger than
what it is:

- **The inbound edges.** `payload/control/src/service.cpp:2038`, `:2131`,
  `:2596` and `payload/control/src/trigger.cpp:286` call
  `cs2::aim::solve` and `cs2::aim::solve_backtrack` directly. The transform
  only rewrites the calls **inside** a selected function; a caller is not
  selected, so the image still shows "the control service calls the aim
  solver". Hiding that needs the *callers* to be protected as well (their
  bodies encrypted, or virtualised) — T20's business.
- **The `invoke` edges.** 26 of them (19 in `occlude_polygon`, 2 in `solve`,
  1 in `solve_backtrack`, 4 in `project_capsule`). The pass does not model the
  EH edges. **Every one of them is same-module** (a `std::vector` template
  call or an aim helper of the same file), so the *cross-TU* graph is closed
  by this task, but the *intra-module* EH edges are not.
- **The intrinsic calls.** 179 of them (`llvm.lifetime.*`, `memset`,
  `memcpy`, `trap`, `fmuladd`, `sqrt`, `smax`). An intrinsic has no body:
  the backend expands it. They carry no call graph of the payload's code.
- **The CRT calls.** 59 (`operator new`/`operator delete`,
  `_Query_perf_counter`, `_Query_perf_frequency`, `atan2f`, `asinf`, `powf`).
  They are in the import table — or, for `operator new`/`operator delete,
  they are the CRT's own code, which the image does not import by name
  (the payload's import table holds 166 symbols and none of them is
  `??2@YAPEAX_K@Z`; the operator may still list them, and the list is the
  knob).
- **The callee set.** The table is a flat list of addresses in `.rdata`. An
  analyst who finds the table reads the **set** of functions a module can
  reach. What this task hides is the **edge**: which call site calls which
  callee. The README's phrase "the static call graph is no longer readable"
  means the edges, and after this task it means the edges of the protected
  functions — not the set of their callees, and not the code of the callees.
- **The run-time cost of the dispatch** is not measured by this repository
  (T19 §10.3: "this repository does not measure it"). 26 more dispatch sites
  on the aim's path is a real cost; the number must not be invented.

## 6. The eligibility and the refusal rules — fail closed

A call is rewritten when **all** of the following hold. The rules of T19
§2.5 apply unchanged, with the two amendments this task decides:

| rule | today | after this task |
|---|---|---|
| the callee is variadic | refused (`variadic_callee`) | unchanged |
| the callee has a `byval` / `inalloca` / `preallocated` parameter | refused (`indirect_parameter`) | unchanged |
| the callee has an `sret` parameter | refused (`indirect_parameter`) | **allowed on `x86_64-pc-windows-msvc`**, refused elsewhere |
| the call is `musttail` | refused (`musttail`) | unchanged |
| the call is already indirect | refused (`indirect_callee`) | unchanged |
| the callee is an intrinsic | refused (`intrinsic_callee`) | unchanged |
| the callee has no code in this module | refused (`external_callee`) | **allowed when the policy's `cross_module` list names it**; refused otherwise |
| the call is an `invoke` | refused (`invoke`) | unchanged |
| the callee is `optnone` | refused (`optnone_callee`) | unchanged |

**The refusal rules of this task, in addition:**

1. **A declaration that the list does not name stays direct.** It is
   reported with the reason `external_callee` (the reason name does not
   change: the pass cannot tell an import from an internal declaration, and
   saying "cross_module" would be a claim it cannot back), and its **name**
   goes into the coverage (§7.2) so that the build can look at it (§7.3).
2. **A listed symbol that is not routed is a build failure.** The coverage
   records the state of every `cross_module` entry per module
   (`matched` / `blocked:<reason>` / `unmatched`), and `check_policy.py`
   fails the build for an entry that is not `matched` — the same rule that
   already applies to a `protect` entry that matched no function
   (`tools/protect/check_policy.py:365-381`).

   The `cross_module` states need **their own priority**: for a `protect`
   entry, `matched` outranks `blocked` (a `protect` entry that matched
   *somewhere* is protected). For a `cross_module` entry the requirement is
   the opposite: the symbol must be routed **everywhere it is called**, so
   `blocked:<reason>` outranks `matched`, and a symbol that is routed in one
   module and skipped in another **fails the build**. The aggregation over
   the modules is therefore "the worst state wins", with the worst being
   `blocked` > `matched` > `unmatched`, and the failure is "the aggregate
   state is not `matched`".
3. **A direct call into the payload's own code is a build failure.** The
   payload check compares the cross-TU callees that were left direct with the
   symbol set of the payload's own static libraries (§7.3). A symbol that
   the payload defines and that a protected function calls directly fails
   the build.
4. **A half-rewritten call does not exist.** The rewrite is atomic per call
   site (T19 §4.4).
5. **No new import.** A table entry that names an import would hold the
   **IAT slot** rather than the function, and the dispatch would call the
   import slot. The payload check catches it (§7.3) and the build fails; the
   operator's list must not name an import.

## 7. The report and the coverage

**The existing structures, extended. No parallel mechanism.**

### 7.1 The report (stderr)

The transform report line per selected function keeps its shape and gains
one counter:

```
[cs2-protect] solver.cpp: transform ?solve@aim@cs2@@… (cs2::aim::solve) \
    rewritten=23 skipped=72 external_callee=26 intrinsic_callee=44 invoke=2 cross_module=16
```

- `cross_module=<n>` — the number of the rewrites that were **cross-TU**
  (the routed declarations). The other counters are as they are today; a
  reason whose count is zero is not printed at all, as today.
- With `CS2_PROTECT_VERBOSE=1`, the per-skipped-call lines gain the same
  information: a skipped cross-TU call is named with its symbol, as it is
  today.

(The numbers in the example are the ones §3 predicts for `solve`: 7 rewrites
+ 16 cross-TU sites, and the 16 routed calls leave the `external_callee` and
`indirect_parameter` counters.)

### 7.2 The coverage file

The coverage file is line-based and its format is read by
`tools/protect/check_policy.py` and `tools/protect/check_payload.py` through
`tools/protect/protect_checks.py`. **The format change must be backward
compatible**: a coverage file of an older build stays readable, and a reader
of the new format reads an old file without complaint.

- The `fn` line gains one key:

  ```
  fn ?solve@aim@cs2@@… rewritten=23 skipped=72 cross=16
  ```

  (`cross=` is the number of the cross-TU rewrites of that function. The
  reader of `protect_checks.py` picks the keys it knows and ignores the
  others — the same way it reads `rewritten=` and `skipped=` today.)

- A **new line kind** `cross` records the state of a `cross_module` entry,
  with the same vocabulary as the `entry` lines:

  ```
  cross matched cs2::aim::project_capsule
  cross blocked:invoke cs2::aim::whatever
  cross unmatched cs2::aim::never_called
  ```

  `protect_checks.py` reads them into a **separate** dictionary
  (`Coverage.cross_states()`), so that a `protect` pattern and a
  `cross_module` pattern can never collide in the reader's map.

- A **new line kind** `direct` records the **distinct** cross-TU callees
  that were left direct, per module:

  ```
  direct ?atan2f
  direct ??3@YAXPEAX_K@Z
  ```

  One line per distinct symbol (not per call site): the list is a set of
  names, and the count of the call sites is in the report. The reader
  exposes them as `Coverage.direct_callees()`.

### 7.3 The payload check (`tools/protect/check_payload.py`)

The existing checks stay exactly as they are — the exports, the imports, the
`.reloc` section, the unwind table, the mixer constants in the read-only
data, the **number** of the encrypted dispatches, and the **decode** of every
dispatch. Two of them change in scope, not in shape:

- The **count** check ("the image has N encrypted dispatches and the pass
  reported N rewrites") now covers the cross-TU dispatches as well. The
  number goes **up**; the check does not care about the value, and neither
  should the implementer: the value is a snapshot of the tree.
- The **decode** check (`check_dispatch_targets`, `check_payload.py:236-295`)
  now has to prove the new entries as well: the descriptor decodes to an
  index, the index names an entry of the table of the image, and the entry is
  **code of the image**. For a cross-TU entry the value in the table is the
  **relocated address of the callee** — the mapper applied the DIR64 fixup —
  so the check is the same check. If the entry were an **IAT slot** instead
  (a mis-classified import), the address would not be code, and the check
  would fail — with a message that says so, because "the dispatch calls an
  address that is not code" is otherwise a puzzling way to learn that the
  list names an import.

**The new check — "a protected function must not call the payload's own code
directly":**

For every symbol in the coverage's `direct` lines, the check asks: is this
symbol **defined in one of the payload's own static libraries**?

- The symbol set is read with the SDK's `llvm-nm` over the payload's
  libraries. The check already receives the SDK's `llvm-objdump`; `llvm-nm`
  is in the same directory (`tools/protect/find_objdump.py` finds the SDK's
  `bin`), so **no build wiring changes** — and the libraries are the ones in
  the build directory the check runs in.
- A symbol the payload defines (the aim functions) → **the build fails**,
  with the name of the symbol and the instruction to add it to the policy's
  `cross_module` list.
- A symbol the payload does **not** define (the CRT's `operator new`,
  `atan2f`, `_Query_perf_counter`, …) → it is an import or a CRT symbol that
  the payload does not own; it names no aim function, and it is left alone.
  The check **prints** the count of the calls it left alone, so the operator
  sees them.
- If the libraries or `llvm-nm` cannot be found, the check **says so** and
  **fails** when the coverage is present (fail closed: a rule that cannot be
  evaluated must not be reported as passed). The "no coverage, no image
  comparison" mode of the check (the manual run without `--coverage`) keeps
  skipping the rules that need the evidence.

## 8. The tests

Three levels, and **each one must be shown to be able to fail** (break the
code on purpose, watch the test fail, revert it — and say so in the report).

### 8.1 The unit test (`tests/protect_tests.cpp`)

- The **policy reader** accepts a `cross_module` list and rejects a
  malformed one (a list that is not an array, an entry that is not a string)
  as a *reported* error, never a crash. The plugin and `check_policy.py`
  must reject the same things with the same message (the rule of the policy
  reader, `tools/protect/README.md` §5).
- The **transform** routes a declaration the list names: a module with a
  `declare` and a call to it, and a `cross_module` entry, produces a table
  that holds the declaration and a call that became indirect.
- The transform **refuses** a declaration the list does not name: the call
  stays direct and the reason is `external_callee`.
- The transform **refuses** an `sret` call on a target that is not
  `x86_64-pc-windows-msvc`, and **allows** it on that target. (The unit
  test builds the module with an explicit target triple.)
- The transform still refuses `byval`.

### 8.2 The IR test (`tests/ir_test.py`)

The fixture (`tests/fixtures/calls.ll`) gains a **declaration** — a callee
with no body in the module — and a call to it; the policy fixture
(`tests/fixtures/calls.policy.json`) gains the matching `cross_module`
entry. The test asserts:

- the direct call to the external callee is **gone** and an indirect call
  took its place;
- the **table holds the external symbol** (the entry is the declaration,
  not a copy of the address);
- the descriptor still decodes to the right index (the existing machinery,
  with the cross-TU entry in the table);
- the **`sret` call is now rewritten** (the assertion of `ir_test.py:190-192`
  is inverted) and the call keeps its `sret` attribute and its signature:
  `call void %<n>(ptr sret({ i64, i64, i64 }) %out, i32 %x)`.
- the calls that must still be skipped are untouched: the variadic one, the
  `musttail` one, and the import (`@printf`).

The fixture's call-count and symbol-set assertions (today: "the caller makes
exactly four calls", `ir_test.py:201-207`) change with the two new
rewrites: the count grows and the set of symbols the caller names shrinks.
The test says what it expects, and the expectation is the *new* one.

The test must also exercise the **production path**: a fixture compiled with
`clang-cl -fpass-plugin=<dll> -emit-llvm -S`, because `opt` does not invoke
the extension points (T19 §5.1, T20a §8.2).

### 8.3 The differential test (`tests/diff_test.py`)

**This is the test that proves the behaviour is unchanged**, and it is the
one that has to cover the two new risks:

- The fixture gets a **second translation unit** (a new
  `tests/fixtures/diff_helper.cpp`) with a function the selected function
  calls. The cross-TU call to it must become a dispatch, and the two runs
  (with and without the pass) must produce the **same output and the same
  exit code**. A dispatch that reaches the **wrong** function does not
  crash — it computes something else — and this is the test that catches it.
  `diff_test.py` therefore has to compile **more than one source** (the
  driver takes a list).
- The fixture's **`sret` callee** (`cs2_fixture_big`, a 24-byte aggregate) is
  now rewritten. The differential test proves that the value the caller gets
  out of it is unchanged — the ABI of an indirect `sret` call is the thing
  §5.3 bets on.
- The existing assertions stay: the protected binary has exactly
  `plain_dispatches + rewritten` dispatches (a folded dispatch is caught),
  the mixer constants are in the data and not in the code, and the two runs
  agree.

### 8.4 The coverage readers (`tests/coverage_test.py`)

- A coverage file with the new `cross` and `direct` lines is read
  correctly: the states, the direct callees, and the `cross=` key of the
  `fn` line.
- A coverage file of an **older build** (no `cross`, no `direct`, no
  `cross=`) is still read without an error — the format change is backward
  compatible, and the test says so.
- A **malformed** coverage file is reported and ignored, never a traceback.

### 8.5 The existing tests must stay green

`protect-tests`, `protect-ir-test`, `protect-diff-test`, `protect-coverage-test`,
the host suite (`meson test -C build`), and the protected payload build with
its `check_policy.py` and `check_payload.py`.

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md` (the new rule, the new policy key, the new
  checks, and the honest statement of what is still visible)
- `tools/protect/policy.example.json` (the shipped example of the new key)

**Do not touch:** `payload/**` (nothing — not one line), `overlay/**`,
`injector/**`, the root `meson.build`, `payload/meson.build`,
`payload/meson_options.txt`, `AGENTS.md`, `docs/style-guide.md`,
`docs/decisions.md`, the other `docs/orchestration/*.md`.

`tools/protect/policy.local.json` is **gitignored** (`.gitignore:35`): the
operator's copy has to carry the `cross_module` list for the protected build
to work, and that file is not part of the commit. The *shipped* example
(`policy.example.json`) is, and it documents the list of the current tree.

## 10. The verify loop

```powershell
cd <your worktree>

# the host (must stay green)
meson compile -C build
meson test -C build

# the plugin
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build --output-on-failure

# the protected payload (the image changes: the dispatches go up)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a fresh directory name.

`CS2_PROTECT_CONFIG` must be set for **both** the configure and the compile
step: the plugin reads it in the compiler process.

**The operator's policy must carry the `cross_module` list** before the
protected build can pass (§5.2). A policy without it fails the build with
the new check's message — that is the intended behaviour, and it is the
message the implementer uses to build the list.

## 11. Acceptance criteria

1. The pass routes a **cross-TU** callee that the policy's `cross_module`
   list names through the **caller's own** table, as a relocatable pointer
   to the external symbol. The table entry is an `IMAGE_REL_BASED_DIR64`
   relocation in `.rdata` (asserted at the object level by a test, and at the
   image level by the payload check).
2. The **number of the encrypted dispatches goes up**: 37 today, at least
   **63** after this task in the current tree, and the payload check still
   ends in `ok` with the count check satisfied. The number is a snapshot of
   the tree — the criterion is that it goes **up**, and that the decode
   check proves the new entries resolve to the **right** callee.
3. **Every dispatch of the protected image decodes to a callee of the
   image** — the existing check, now covering the cross-TU entries, with a
   message that names the "the table entry is an import slot" case.
4. **The cross-TU edges of the protected functions are 0**: no protected
   function makes a direct call to a symbol the payload's own libraries
   define. The check fails the build otherwise, naming the symbol.
5. The `sret` rule is narrowed to `x86_64-pc-windows-msvc` and to `sret`
   only (`byval`, `inalloca` and `preallocated` stay refused), and the
   **differential test proves** that the behaviour of an `sret` callee is
   unchanged.
6. A `cross_module` entry that matched nothing, or that was seen but not
   routed, **fails the build** (`check_policy.py`), with the same state
   vocabulary the `protect` entries use.
7. The coverage format change is **backward compatible**: a coverage file of
   an older build is still read, and the readers of the new format read an
   old file without an error.
8. The report names the number of the cross-TU rewrites per function, and
   the coverage names the cross-TU callees that are **still direct**.
9. The **import table and the export table are unchanged**, the `.reloc`
   section still has its DIR64 entries, and the unwind table is still well
   formed (the existing checks).
10. The **host suite is green** (`meson test -C build`), the plugin's four
    tests are green, and each new test was **proven to be able to fail**.
11. `tools/protect/README.md` documents: the new policy key, the new
    routing rule, the narrowed `sret` rule (with the evidence and the
    target restriction), the new checks, and — honestly — what is still
    visible in the image (§5.4).
12. Nothing outside §9 was touched. In particular **no file under
    `payload/**` changed**.

## 12. Rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell. Say what is unverified.

## 13. The report back

What changed per file. The policy's new key and the list the operator's
policy carries. The routing rule, in the pass's own terms. The `sret`
narrowing and the evidence for it. The new report and coverage fields. The
new checks and what they fail on. The dispatch count before and after, and
the decode check's evidence that the new entries reach the right callee. The
exact tests, and how each was proven to be able to fail. What you could not
verify — in particular the run-time cost of the 26 extra dispatches, which
this repository does not measure, and the behaviour of the image in the
game.
