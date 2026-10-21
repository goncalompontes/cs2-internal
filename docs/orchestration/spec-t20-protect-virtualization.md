# T20 — virtualization: the function body becomes bytecode

**Goal:** the selected function's machine code **stops existing**. Its body is
compiled to a small custom bytecode, and a tiny interpreter - emitted as LLVM
IR, so it is freestanding - executes it. This is the VMProtect-shaped part of
the project, and the expensive one.

Read the "honest assessment" in §7 before you start. This task is large; it is
deliberately staged so that a partial result is still a working binary.

Everything in §1 was verified empirically on this machine. Do not re-derive it.

---

## 1. Ground rules inherited from T18/T19

- The plugin lives in `tools/protect/`, is built by CMake against the LLVM dev
  SDK at `F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc`, and is loaded
  by the payload build with **`/clang:-fpass-plugin=<dll>`**.
- Configuration travels through **environment variables**
  (`CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`, `CS2_PROTECT_VERBOSE`).
- The payload is **manually mapped** by `payload/loader`:
  - the mapper applies base relocations (DIR64/HIGHLOW) ⇒ **never bake an
    absolute address into the bytecode or a cipher**; the bytecode may hold
    **indices** and **relative offsets** only;
  - the mapper registers the x64 unwind table and **treats failure as fatal** ⇒
    the protected DLL must keep a valid `.pdata`/`.xdata` pair;
  - the mapper resolves TLS by an ntdll pattern scan and **fails the whole
    injection** if it is not found ⇒ **no thread-local VM state**;
  - imports are resolved by name ⇒ no new imports of modules the target
    process does not have;
  - the payload is static-CRT, has **no global dynamic initialisers** and no
    exceptions.
- The pass runs at **`registerOptimizerLastEPCallback`** (added in T19): the
  only module-level point that sees fully optimised IR and still runs before
  codegen. It must define **`isRequired() { return true; }`** (a plain
  `PassInfoMixin` is an *optional* pass and is skipped on `optnone`).
- `OptimizerLast` also fires in the **LTO pre-link** phase ⇒ guard against
  transforming a module twice. T19 already established the per-phase module
  flags; T20 reuses them.
- T19 also established the **two-phase** model: an early phase
  (`registerPipelineStartEPCallback`) that selects the functions, marks them
  `noinline` and writes the report + coverage, and a late phase
  (`registerOptimizerLastEPCallback`) that transforms. **T20 replaces the late
  phase's transform with the virtualizer**; the early phase does not change.
- The policy carries a `level` per entry: `call` (T19) or `virtualized`
  (**T20**). T20 implements `virtualized`; the `call` level keeps working.
- The plugin registers a **`registerPipelineParsingCallback`** for the name
  `cs2-protect`, so `opt.exe -load-pass-plugin=<dll> -passes=cs2-protect` runs
  the pass. T20's IR test uses the same name.
- The plugin's own **CTest** target exists (`tools/protect/tests/`, target
  `cs2-protect-tests`, driven by `add_test` in `tools/protect/CMakeLists.txt`).
  T20's tests go there.
- The existing code surface T20 must reuse (do not write a second one):
  - `tools/protect/src/config.hpp`: `ProtectionLevel` (`call` / `virtualized`),
    `PolicyEntry`, `Policy`, `Config` (with `policy_digest`), `ConfigError`;
  - `tools/protect/src/selection.hpp`: `SkipReason`, `Selection` (with a
    per-function `seed`), `PolicyMatch` (with `matched` / `blocked` /
    `blocked_reason`), `SelectionResult`;
  - `tools/protect/src/report.hpp`: the report and the coverage writer;
  - `tools/protect/src/plugin.cpp`: the entry point, the pass, the per-phase
    module flags, the extension-point registrations.

## 2. The architecture

Follow the shape of `fuqiuluo/amice` (the interpreter is **emitted as LLVM
IR**, so it needs no CRT and no global constructors), the discipline of
`qusamulab/kagura` (one shared opcode table, **all-or-nothing** eligibility,
**decrypt-on-fetch** so the blob stays read-only and the interpreter is
re-entrant), and the per-build polymorphism of `90th/llvm-obfus` (opcode
permutation, handler shapes, rolling keystream, edge-assignment PHI model).

**The transform lives in the late phase** (`registerOptimizerLastEPCallback`),
which T19 already established. The early phase (selection, `noinline`, report,
coverage) is unchanged.

**One shared opcode table.** The opcode numbers must be defined **once** and
included by both the emitter and the interpreter - if they disagree, the
bytecode is silently wrong. The reference implementation had both sides
holding a hand-written copy of the same numbers, and noted that "a silent
disagreement between the two would not fail to compile". Do not repeat that:
use one `.def` file, or generate the interpreter's switch from the same
source of truth.

### 2.1 The pipeline

```
selected function F
   |
   +-- eligibility  ->  refuse (and report) OR continue
   |
   +-- lower  ->  a flat list of micro-instructions + a slot table
   |
   +-- encode  ->  bytecode blob (private constant) + per-function state
   |
   +-- rewrite  ->  F's body becomes: marshal args -> call the interpreter -> return
```

### 2.2 Eligibility - **fail closed**

A function is virtualized only if it passes **every** check. If any check
fails, the function is **left completely untouched** and the pass reports the
reason. A half-virtualized function is a miscompile.

Refuse, at least:

| Refuse | Why |
|---|---|
| any EH pad / `invoke` / `landingpad` / `resume` | the interpreter cannot model unwinding |
| the function is variadic, or calls a variadic callee | the indirect call cannot reproduce the variadic ABI |
| an indirect call | no relocatable address to place in the pool |
| a call with `byval` / `sret` / `inalloca` / `preallocated` | Windows x64 rejects these in an indirect signature |
| `musttail` | the tail-call contract does not survive the rewrite |
| a volatile or atomic access | the ordering guarantees are not modelled |
| `memcpy`/`memset` with a **non-constant** length | it would lower to a CRT call (see §3) |
| an unsupported type (float, vector, aggregate in registers) | start with integers and pointers only |
| the function's address is taken (`&F`) | the rewrite changes what the address means |
| a function with more slots than the VM has | a fixed budget, see §4 |

**Integer and pointer types only** in this task. Floats, vectors and aggregates
are refused and reported. That is the reference implementation's scope
(`kagura`: "only `iN (N<=64)`, pointers, void") and it is enough to protect
the payload's aim maths and hash helpers.

### 2.3 Lowering

- **One slot per SSA value** (a spill-everything model). A slot is an index
  into a frame array. The frame lives in an `alloca` in the entry block of the
  rewritten function - **not** a global, **not** a thread-local.
- **PHI nodes become edge assignments**, not instructions. Each predecessor
  performs the moves for the successor's PHIs **before** its branch. The moves
  must be a **parallel copy** (push every source, then pop every destination
  in reverse order) so that a swapping back-edge is correct.
- **Calls** become a `call` micro-instruction that the interpreter performs
  **natively** (the callee is a real function in the module, reached through
  the pool). The call keeps its calling convention and its `AttributeList`.
- **Memory** is real memory: the VM's pointer values are the real addresses.
  `load`/`store` are real loads and stores. `alloca` gets a byte offset into
  the frame arena.
- **`nsw`/`nuw`/`exact`** must be carried, or the rewrite changes behaviour on
  overflow (dropping `nsw` turns undefined behaviour into a defined wrap).
- **`undef`/`poison`**: a value used more than once must be `freeze`d, or the
  two uses may disagree.
- **Debug info is dropped** - the original body is deleted.

### 2.4 The interpreter

**Emitted as LLVM IR** by the pass. Not a linked C++ object: that is what
keeps the payload CRT-free.

- A **fixed-size frame** in the entry block: the slot array, the program
  counter, the return slot, the call-argument scratch. All `alloca`, all
  fixed size, **no heap**.
- A **switch** dispatcher over the decoded opcode, one basic block per opcode
  handler. The opcode values are **permuted per build** (a build-seeded
  permutation), so the dispatcher's shape differs between builds.
- The bytecode is a `private unnamed_addr constant [N x i8]` global: no dynamic
  initialiser, no constructor.
- The bytecode is **encrypted** with a per-build key and **decrypted on
  fetch**, so the blob stays read-only and the interpreter is re-entrant. The
  key is derived from the build seed and split across several constants, so no
  single constant in the image is the key.
- **No CRT calls**: the interpreter must not call `memcpy`, `memset`,
  `malloc`, `free`, or anything else. A `llvm.memcpy` with a constant size of
  about 64 bytes or less is inlined by the X86 backend; anything larger or
  variable must be an explicit load/store loop written in IR.
- **No RTTI, no exceptions** in the emitted code.

### 2.5 The rewrite

The function's **type never changes**, so the x64 ABI is preserved by
construction - only the body is replaced:

1. allocate the frame in the entry block;
2. copy each incoming `llvm::Argument` into its slot;
3. call the interpreter with (bytecode pointer, frame pointer);
4. return the value from the return slot.

Add `noinline` to the rewritten function. The function keeps its `ret`
attributes.

### 2.6 The `level` gate

The policy already carries a level per entry: `call` (T19) or `virtualized`
(**this task**). T20 implements `virtualized`.

A `protect` entry with `"level": "call"` is still transformed by T19's call
encryption and must keep working. A `protect` entry with `"level":
"virtualized"` is transformed by this task.

The two transforms must not both apply to the same function: a function is
virtualized **or** call-encrypted, not both. Decide what happens when a policy
asks for `virtualized` on a function whose *callers* are also virtualized
(the reference implementations refuse a virtualized function that calls
another virtualized one, because the interpreter would have to call into
another interpreter). Say what you chose and why.

`tools/protect/policy.example.json` must end up asking for a level that is
implemented, and the README must say which levels exist.

## 3. The freestanding rules, concretely

The payload is manually mapped and has no CRT. The emitted interpreter must:

- use **no** `new`/`delete`, `std::vector`, `std::string`, `std::map`,
  `std::function`, or any container;
- use **no** function-local statics (they need `__cxa_guard_acquire`);
- use **no** `atexit` / `__cxa_atexit`;
- emit **no** calls to `memcpy`/`memset` from the CRT - write the loops;
- use **no** thread-local storage;
- add **no** global with a non-constant initialiser.

Verify it: after building the protected payload, check the import table
(`llvm-readobj --coff-imports`) - the set of imports must be **unchanged** from
the unprotected payload - and check the sections for a non-empty `.CRT$XCU`.

## 4. Budgets and the cost model

Virtualization is **5-50x** slower. It is only acceptable off the per-frame
path.

- A fixed **slot budget** per function (start at 256 slots and 64 call
  arguments). A function that exceeds it is **refused**, not truncated.
- A fixed **frame size** (a few KB). A virtualised recursion multiplies it by
  the recursion depth - the payload runs on a game thread with a normal stack,
  but a deep recursion is still a crash. The pass should report the frame size
  so the operator can see the risk.
- The report must state, per function: slots used, frame bytes, bytecode
  bytes, and whether it was refused and why.

## 5. Testing

### 5.1 An IR-level test, driven by `opt.exe`

`opt.exe` ships in the SDK. A checked-in `.ll` fixture plus a Python test:

```
opt.exe -load-pass-plugin=<Cs2Protect.dll> -passes=cs2-protect -S fixture.ll
```

asserting that:
- the function's **type is unchanged** (same params, same return type);
- the body now contains a call to the interpreter and a bytecode global;
- the bytecode global is `private constant` and its bytes are **not** the
  plaintext opcode sequence;
- a fixture that must be **refused** (EH, varargs, an indirect call) is
  **byte-for-byte unchanged**;
- a fixture with a **swapping PHI back-edge** produces the right parallel copy.

Wire it into the plugin's **CTest** target (`tools/protect/tests/`, target
`cs2-protect-tests`) - the same place T19's IR test lives. **Skip** with
CTest's skip code when the plugin has not been built, so a machine without the
LLVM SDK still has a green suite.

The test must exercise the **production path** as well: compile a fixture with
`clang-cl -fpass-plugin=<dll> -emit-llvm -S` and assert on that IR too, so a
test that only exercises the `opt` path cannot pass for the wrong reason
(T19's review made this point: `opt` does not invoke the extension points, so
the named pipeline must add the same sequence of passes the EPs do).

### 5.2 A differential test - the correctness backbone

The rewrite changes the *meaning* of the code if it is wrong, so the test must
compare behaviour, not shape.

Build a fixture program **twice** from the same source - once with the pass,
once without - run both, and compare their output **and** their exit codes.
The fixture must cover the cases that break naive implementations:

- a loop with a **swapping PHI** back-edge;
- a function with **many live values** across a call (so the slot model is
  exercised);
- an **indirect-free** call to another function (so the native-call path is
  exercised);
- a function whose result is **used in a constant-folding context**;
- `nsw`/`nuw` arithmetic near the overflow boundary (the rewrite must not
  change the result);
- a value read twice where the IR says `undef` (the `freeze` requirement).

Put the fixture under `tools/protect/tests/` and register it.

### 5.3 The payload-level checks

- The protected payload must have the **same exports** and the **same import
  table** as the unprotected one (§3).
- It must keep a valid `.reloc` with DIR64 entries and a valid
  `.pdata`/`.xdata`.
- `llvm-objdump -d` must show the original function's instruction bytes are
  **gone** - the body is now a call to the interpreter.

## 6. Danger list

1. **Failing open.** A half-lowered function computes garbage and can hang.
   The reference implementation shipped exactly that bug once: an
   unrecognised instruction became a NOP while the eligibility check still
   said "yes", and a lowered branch pushed a literal 0, so the VM took the same
   edge forever. **Refuse, never guess.**
2. **PHI parallel copy.** A sequential copy breaks a swapping back-edge.
3. **Losing `nsw`/`nuw`.** Changes the result on overflow.
4. **No `freeze`.** Two uses of the same `undef` may disagree.
5. **Stack blow-up.** A virtualised recursion uses a multi-KB frame per level.
6. **A CRT call in the interpreter.** Fatal for a manually mapped payload.
7. **A global constructor.** Fatal for a manually mapped payload.
8. **The interpreter is the target.** Once the VM exists, the interpreter is
   the only native code left, so it is what an analyst attacks. Give it the
   per-build polymorphism (opcode permutation, handler shapes) - and remember
   that the *other* passes (call encryption, string encryption) should be
   applied to it too, in a later task.
9. **The optimizer is not looking at your output.** At `OptimizerLast` the
   optimiser has already run, so nothing will clean up your IR. It must be
   valid SSA, with no PHI without a predecessor.
10. **A `switch` dispatcher compiles to a jump table** whose shape is the
    signature every deobfuscator keys on. The per-build opcode permutation is
    what makes the table useless as a signature. Consider a comparison tree
    instead of a `switch` if the jump table shows up in the disassembly.

## 7. The honest assessment - read this before you start

Against a reverser **with a debugger**, nothing here is a wall: they will
single-step the interpreter and read the bytecode out of memory. The value is
in what this buys against an **offline** analyst (someone with the DLL, opening
it in IDA): they get a dispatcher loop and an encrypted blob instead of the
algorithm, and they cannot read the bytecode without running the program.

Virtualization is also the **most expensive** item in the plan: it is a
multi-week build in its full form, it costs 5-50x at run time, it risks
miscompiles, and it is trivially bypassed by anyone with a debugger.

So this task is deliberately scoped to **integers and pointers only**, to a
**small eligible set**, and to a **fixed budget**. Get one function virtualized
correctly and provably (the differential test) before widening it.

## 8. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- the root `meson.build` **only** to register the new tests
- `docs/orchestration/spec-t20-protect-virtualization.md` (the orchestrator
  writes this)

Do **not** touch: `payload/**` sources, `overlay/**`, `injector/**`,
`payload/meson.build`, `payload/meson_options.txt`, `AGENTS.md`,
`docs/style-guide.md`, `docs/decisions.md`, other `docs/orchestration/*.md`.

## 9. The verify loop

```powershell
cd <your worktree>
meson compile -C build
meson test -C build                 # 42/42 + the new tests

python scripts/build-protect-plugin.py
# the plugin's own tests (the IR test and the differential test)
ctest --test-dir tools/protect/build -R protect-tests

# the protected payload
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=<abs path>"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C build-payload-protect"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a fresh directory name.

## 10. Acceptance criteria

1. One function is virtualized: its original instruction bytes are gone from
   the binary, replaced by a call to the interpreter, and the bytecode is
   encrypted.
2. The **differential test** proves the protected and unprotected fixture
   agree on output and exit code - and the test **can fail** (break the
   interpreter on purpose, watch it fail, revert).
3. The IR test proves a function that must be **refused** is left untouched,
   and that a swapping-PHI fixture is lowered correctly.
4. The import table and the exports of the protected payload are **unchanged**;
   the section list has no new `.CRT$XCU`.
5. `meson test -C build` is green.
6. The report states, per function: slots, frame bytes, bytecode bytes, and
   the refusal reasons for the ones that were refused.
7. Nothing outside §8 was touched.

## 11. Rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`; never
  amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell. Say what is unverified.

## 12. Report

What changed per file. The ISA you settled on and why. The eligibility rules
and which real payload functions they admit. The exact tests, and how each was
proven to be able to fail. The measurements: slots, frame bytes, bytecode
bytes, and the slowdown if you could measure it. What you could not verify.
