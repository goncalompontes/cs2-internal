# T19 — call-site encryption: the static call graph leaves the file

**Goal:** inside a *selected* function, a direct call to an internal target
becomes an indirect dispatch through an **encrypted descriptor**, so the static
call graph is no longer readable from the binary.

This is the first task that **changes the generated code**, so it is also the
first task that can silently break the payload. §4 is the danger list; read it
before you write code.

Everything in §1 was verified empirically on this machine (and T18 is already
merged, so §1 describes code that exists — read it, do not re-derive it).

---

## 1. What already exists (T18, merged)

The plugin lives in `tools/protect/` and is built by CMake against the LLVM dev
SDK at `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc`:

```
tools/protect/
  CMakeLists.txt            # the CMake recipe; the DIA-SDK property rewrite
  src/plugin.cpp            # llvmGetPassPluginInfo + the pass
  src/config.{hpp,cpp}      # the environment configuration + the policy reader
  src/selection.{hpp,cpp}   # which functions are selected
  src/report.{hpp,cpp}      # the stderr report + the per-module coverage file
  tests/protect_tests.cpp   # the plugin's own tests (CTest: cs2-protect-tests)
  check_policy.py           # the whole-build check
  verify_plugin.py          # the stale-plugin guard
  compare_builds.py         # the binary comparison
```

There is no `ir_util` - the helpers live in `selection.{hpp,cpp}`. Do not
invent a new file if the code fits in the existing ones; if you do add a
file, say why in the report.

Facts you must not re-derive:

| Fact | Consequence |
|---|---|
| The pass runs at **`registerPipelineStartEPCallback`** and sets the module flag `cs2.protect.done` (a `llvm::Module::Warning` flag), bailing out if it is already set. | T19 needs **two** extension points and therefore **two** flags — see §2. |
| The pass is a **module** pass and has `isRequired() { return true; }`. | A *function* pass added later must have `isRequired()` too, or LLVM silently skips it on `optnone` functions. |
| The configuration is **environment variables**: `CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`, `CS2_PROTECT_VERBOSE`, `CS2_PROTECT_COVERAGE`. A `cl::opt` inside a statically linked plugin is **not** reachable via `-mllvm`. | T19 adds no new channel; it reuses the policy file. |
| The policy file is read with **`llvm::json::parse`** (the SDK's), with a depth pre-scan (limit 64) and a duplicate-key pre-scan. The format is `{"protect": [...], "deny": [...], "seed": "0x..."}`. | The policy *format* does not change in T19. A `protect` entry may carry `{"symbol": ..., "module": ..., "level": ...}`. |
| `ProtectionLevel` already exists: **`call`** ("encrypt the call sites of the function (T19)") and **`virtualized`** ("move the body into an interpreter (T20)"). `Selection` already carries a per-function `seed` and a `may_inline` flag. | **T19 implements `call` only.** See §2.7. |
| T18 registers **only** `registerPipelineStartEPCallback` - there is **no** `registerPipelineParsingCallback`, so `opt -passes=cs2-protect` does not work today. | T19 must add the parsing callback (§5.1). |
| A `protect` entry is matched against the **mangled** symbol, but the **demangled qualified name** is accepted too (`cs2::aim::solve`). `PolicyMatch` has `matched` / `blocked` / `blocked_reason`. | Reuse the existing matcher. Do not write a second one. |
| The report prints one line per selected function to **stderr**, and writes a per-module **coverage** file (name = `<policy digest>__<module id>`) for `check_policy.py`. | T19 extends the report; it does not replace it. |
| The payload build loads the plugin with **`/clang:-fpass-plugin=<dll>`** (clang-cl ignores the plain spelling) and the payload project has `-Dprotect=true -Dprotect_plugin=<abs path>`. | T19 does not change the wiring. |
| The plugin is built with `/GR-` (no RTTI), `/MT` (static CRT), `/EHs-c-` (no exceptions), C++23. | The plugin must keep matching the host compiler. |
| The payload is **manually mapped** by `payload/loader`: base relocations (DIR64/HIGHLOW) are applied; the x64 unwind table is registered and its absence is **fatal**; TLS is resolved by an ntdll pattern scan and its absence is **fatal**; imports are resolved by name; the payload is static-CRT with no global dynamic initialisers. | §2.2 and §2.3. |
| The host suite is **42/42 green** and must stay green. | |

## 2. The design

### 2.1 Two extension points, and two module flags

A selected function must still *exist* at the late point, or it cannot be
rewritten. The inliner runs in the optimisation pipeline, between the early
point and the late point, so a selected function can be inlined into its
callers and deleted before a late point ever sees it.

So the pass registers **both**:

| Point | Callback | Flags | Job |
|---|---|---|---|
| early | `registerPipelineStartEPCallback` | `cs2.protect.selected` | select the functions (as today), then **add `noinline`** to every selected function so it survives to the late point |
| late | `registerOptimizerLastEPCallback` | `cs2.protect.transformed` | **rewrite the calls** (this task) |

`registerOptimizerLastEPCallback` is the only module-level point that sees
fully optimised IR (inlined, simplified) and still runs before codegen. The
reason the rewrite must happen *there* and not earlier: the optimiser's
`SimplifyIndirectCalls` would rewrite the encrypted indirect call straight back
into a direct call, and the inliner would have eaten the function.

The flags are per phase, because T18's existing `cs2.protect.done` guard
bails out on the *second* run of the same phase - and with two phases, one flag
would make the late phase always bail. Each phase's flag is set when that
phase runs, and the phase returns immediately if its flag is already set.

Both flags must be **module flags** (`llvm::Module::Warning` behaviour, as
T18 already does) so they survive an LTO merge and the pre-link phase does not
transform twice.

### 2.2 The table and the descriptor

For each module that has at least one rewritten call, the pass emits two
module-level globals:

```
; The table holds real pointers. The linker emits DIR64 fixups for them and
; the mapper patches them, so the entries are correct at run time. A `private
; constant` global has no dynamic initialiser - a non-constant one would be a
; global constructor, which the manually mapped payload cannot survive.
@cs2.protect.table = private unnamed_addr constant [N x ptr] [ptr @callee_0, ...]

; The descriptor holds the ENCRYPTED index of each call site. No relocatable
; data may live inside the cipher: the mapper's fixups would corrupt it.
@cs2.protect.desc = private unnamed_addr constant [M x i64] [...]
```

The table's **order is permuted per build** (a build-seeded permutation), so
the index in the descriptor is not a predictable table position and the table
does not have the same shape in two builds.

The descriptor is `private constant` and is **decrypted on fetch** (the value
is read and decoded at the call site, the blob is never modified), so the image
stays read-only.

### 2.3 The key

**Design A (this task): a compile-time key, split and mixed at run time.**

The key must not appear as a single constant in the image. Split it into two
or three constants in different globals and combine them at run time with a
mixer that the pass emits **inline as IR** (no calls):

```
k0 = <const A>            ; one global
k1 = <const B>            ; another global
k2 = <const C>            ; and a third
key = mix64(k0 ^ k1 ^ k2)
```

The constants are **per build** (derived from `CS2_PROTECT_SEED` and the
module id), so two builds do not share a key.

**What this buys:** a static scanner that greps for a 64-bit constant does not
find the key; it has to be reconstructed from three constants and a mixer.
**What it does not buy:** an analyst who recognises the mixer recovers the key
in minutes. Say exactly this in the README - do not oversell it.

**Design B (a later task):** a post-link binder computes the call site's RVA
delta and encrypts the descriptor with a key derived from it, so that *no key
material exists in the image at all*. That needs a PE post-processing step and
is out of scope here. Leave a clean seam: the descriptor format and the decode
routine should be the only things that change.

### 2.4 The rewrite

For each `call` instruction in a selected function whose callee is a **direct**
call to a function **defined in this module**:

1. the callee is added to the table (if it is not already) and its **permuted**
   index is computed;
2. the descriptor slot for this call site gets `index ^ key`;
3. at the call site, the pass emits the decode **inline**, immediately before
   the call:

```
  %i   = <inline decode of the descriptor slot>     ; index
  %p   = getelementptr inbounds ptr, ptr @cs2.protect.table, i64 %i
  %f   = load ptr, ptr %p, align 8
  %r   = call <the original function type> %f(<the original arguments>)
```

The **function type is unchanged**, so the ABI is preserved by construction:
the call still has the callee's parameter types, return type, calling
convention and `AttributeList`. The call becomes an *indirect* call with the
same signature.

The decode must be **cheap and branch-free** (a few `xor`/`mul`/`rol` on
`i64`), because it runs on every call.

**No call to a shared decode helper.** That would add a stack frame per call
site, and the payload's hot paths are on the per-frame budget.

### 2.5 The skip rules (fail closed, and say why)

A call is **not** rewritten when any of these hold. Each skip is reported:

| Skip | Why |
|---|---|
| the callee is variadic | the indirect call cannot reproduce the variadic ABI reliably |
| the callee has `byval` / `sret` / `inalloca` / `preallocated` parameters | Windows x64 rejects these in an indirect signature (LLVM 22) |
| the call is `musttail` | a tail call cannot become an indirect call without breaking the contract |
| the callee is not defined in this module | it is an import; it is in the import table anyway (import hiding is a different technique) |
| the call is an `invoke` | the pass does not model EH edges yet |
| the caller is not selected | out of scope |
| the callee is `optnone` | rewriting it would defeat the `optnone` intent |

A call is either **fully** rewritten or **fully** untouched. Never half.

### 2.7 The `level` gate, and honesty about what is not implemented

The policy already carries a level per entry: `call` (this task) or
`virtualized` (T20). **T19 implements `call` only.**

So a `protect` entry with `"level": "virtualized"` names a function that this
task will **not** transform. The pass must:

- select it (the selection is the same), but report it as
  *"the level 'virtualized' is not implemented yet (T20)"*;
- **not** count it as protected, and the whole-build check must **fail** for
  it, exactly as T18 fails the build for a `protect` entry that matches a
  function that cannot be protected.

Otherwise a policy asking for virtualization would produce a build that looks
protected and is not - the precise failure mode this project exists to avoid.

`tools/protect/policy.example.json` currently contains a `virtualized` entry.
T19 must update it so the shipped example does not ask for an unimplemented
level (keep the key in the file, with the `call` level), and the README must
say that `virtualized` is reserved for T20.

### 2.6 The report

Extend the existing report (do not replace it): for each selected function, the
number of calls rewritten and the number skipped **per reason**. With
`CS2_PROTECT_VERBOSE=1`, name every skipped call. The table and descriptor
sizes go in the per-module summary.

## 3. The `noinline` marking, and what it breaks

Marking the selected functions `noinline` changes the generated code, so
T18's central evidence ("the protected payload is identical to the unprotected
one") **no longer holds**. That is expected and must be stated, not hidden:

- the difference must be **attributable**: `compare_builds.py` should show
  differences only in the selected functions (and the new globals), and the
  report must say which functions were marked;
- the *unselected* code must still be identical.

If marking `noinline` is not enough to keep a selected function alive to the
late point (for example the function is `internal` and gets deleted anyway),
the pass must **report** it, and `check_policy.py` must treat it as a failure
(the T18 rule: "a `protect` entry that matches a function that cannot be
protected is an error"). Do not let a selected function silently vanish.

## 4. Danger list - read before you write code

1. **An absolute address in the cipher invalidates the whole scheme.** The
   mapper rebases the image. Encode an index, never an address.
2. **A cipher that contains relocatable data gets corrupted by the fixups.**
   The cipher holds plain integers; the pointers live in a separate table.
3. **A `private` global with a non-constant initializer becomes a global
   constructor**, which the manually mapped payload cannot survive. Everything
   must be `constant`.
4. **Half-rewritten calls.** Rewriting the callee but not the call site (or
   the other way round) is a silent miscompile. Do the rewrite atomically per
   call.
5. **The transform must run after the optimiser** (§2.1), or
   `SimplifyIndirectCalls` undoes it.
6. **`noinline` changes codegen** (§3) - the "identical output" evidence is
   gone, and the difference must be attributable.
7. **A selected function that is deleted before the late point** - the build
   must fail, not pass quietly (§3).
8. **Stack growth.** The decode is inline arithmetic, so there is no frame
   change. Do not introduce a call to a shared helper (§2.4).
9. **`musttail`, varargs, `sret`, `byval`** - see §2.5. Skipping is correct;
   rewriting is a bug.
10. **The host suite must stay 42/42** - it does not use the pass, so any
    change there is a mistake.
11. **The module flags must be per phase** (§2.1), or the late phase always
    bails and the build produces an unprotected payload that *looks* protected.

## 5. Testing: this is where the risk is

The pass rewrites calls, so a mistake shows up as a **wrong call target**, not
as a crash. The test must catch that.

### 5.1 An IR-level test, driven by `opt.exe`

`opt.exe` ships in the SDK. A checked-in `.ll` fixture plus a Python test
that runs

```
opt.exe -load-pass-plugin=<Cs2Protect.dll> -passes=cs2-protect -S fixture.ll
```

and asserts on the output IR:

- the direct `call @target` is **gone**;
- an **indirect** call is present;
- the caller's function type is **unchanged** (same argument types, same return
  type) - the ABI is preserved by construction, and the test proves it;
- the table global exists and holds the target pointer;
- the descriptor global exists and its bytes are **not** the plain index;
- a fixture that must be **skipped** (variadic callee, `musttail`, an
  `sret` parameter) is left **untouched**.

**The pass must also be reachable by name.** T18 registers only
`registerPipelineStartEPCallback`, which `opt` does **not** invoke, so
`opt -passes=cs2-protect` does not work today. T19 must add a
`registerPipelineParsingCallback` for the name **`cs2-protect`**, so that

```
opt.exe -load-pass-plugin=<Cs2Protect.dll> -passes=cs2-protect -S fixture.ll
```

runs the pass. Use that name in the test and in the README.

**Where the test lives:** the plugin's own CTest target
(`tools/protect/tests/`) already exists and already has the plugin's
compile flags. Put the fixture and the driver there, and add the test with
`add_test` in `tools/protect/CMakeLists.txt`.

### 5.2 A differential test: protected and unprotected must behave the same

The pass rewrites calls, so a mistake shows up as a **wrong call target**, not
as a crash. The test must catch that.

Build a small fixture program twice from the same source - once with the pass,
once without - run both, and compare their output **and** their exit codes.
The fixture must call enough functions through the rewritten path (including a
variadic callee, which must be *skipped* and still work) that a mis-dispatch
would change the result.

### 5.3 The payload-level checks

- The protected payload and the unprotected payload must have the **same
  exported symbols** and the **same import table**.
- The protected payload must still have a valid `.reloc` section with DIR64
  entries (the mapper needs it) and a valid `.pdata`/`.xdata` pair (the mapper
  registers the unwind table and treats failure as fatal).
- `llvm-objdump -d` on the protected payload must **not** contain a direct
  `call` to a protected target (the original `call rel32` must be gone).
- `compare_builds.py` must show the differences are confined to the selected
  functions and the new globals (§3).

## 6. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- the root `meson.build` **only** if you must register a host test there
- `docs/orchestration/spec-t19-protect-call-encryption.md` (the orchestrator
  writes this)

Do **not** touch: `payload/**` sources, `overlay/**`, `injector/**`,
`payload/meson.build`, `payload/meson_options.txt`, `scripts/**`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, other
`docs/orchestration/*.md`.

(The meson wiring from T18 is already merged. If this task needs a change
there, **stop and report it**.)

## 7. The verify loop

```powershell
cd <your worktree>
meson compile -C build
meson test -C build                 # 42/42

python scripts/build-protect-plugin.py
# the plugin's own tests
ctest --test-dir tools/protect/build -R protect-tests

# the protected payload
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='<abs path to a policy file>'
$plugin='<abs path to tools/protect/build/Cs2Protect.dll>'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a fresh directory name.

## 8. Acceptance criteria

1. The IR test shows the direct call replaced by an indirect one, the function
   type unchanged, the descriptor not containing the plain index, and the
   skipped fixtures untouched.
2. The differential test proves the protected and unprotected fixture agree on
   output and exit code - **and the test can fail** (break the dispatch on
   purpose, watch it fail, revert).
3. The protected payload has the same exports and imports as the unprotected
   one, a valid `.reloc` with DIR64 entries, and a valid `.pdata`/`.xdata`.
4. `llvm-objdump -d` of the protected payload shows no direct call to a
   protected target.
5. `meson test -C build` is green (42/42), and the plugin's own CTest suite
   passes.
6. The report names the rewritten and skipped calls with their reasons, and
   the differences in the binary are attributable to the selected functions
   and the new globals.
7. Nothing outside §6 was touched.

## 9. Rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
  The plugin is a build-time tool, but it lives in this repo - hold it to the
  same standard.
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`; never
  amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell (session 0, no GPU). Say plainly
  what is unverified.

## 10. Report

What changed per file. The design decisions - especially the key design and
what it does and does not buy, the two extension points and their flags, and
the skip rules. The exact tests, and how each was proven to be able to fail.
What you could not verify.
