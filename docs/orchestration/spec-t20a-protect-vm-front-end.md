# T20a — the virtualizer's front end: the ISA, the eligibility and the bytecode

**Goal:** the pass must be able to *decide* whether a selected function can be
virtualized, *lower* it to the bytecode of a virtual machine, and *encode* that
bytecode — **without changing the function's code yet**.

This is the first half of T20. It is split out because the two halves fail in
completely different ways: the front end fails with a **wrong bytecode** (which
the differential test of T20b catches), and the rewrite fails with a
**miscompiled program** (which the same test catches). Splitting them means
the expensive part — the IR surgery of T20b — starts from a bytecode that is
already known to be right.

**Read `spec-t20-protect-virtualization.md` first.** This task is a subset of
it; §1 (the inherited ground rules), §4 (the budgets), §6 (the danger list) and
§11 (the rules) apply verbatim. Where this document and that one disagree,
this document wins — it is the narrower, later statement.

---

## 1. What this task delivers, and what it does not

| | |
|---|---|
| **Delivers** | the ISA (one source of truth), the fail-closed eligibility gate, the lowering of a function's IR to bytecode, the encoder (with the encryption and the per-build opcode permutation), the report and the coverage fields, and the tests that prove all of it |
| **Does not deliver** | the interpreter (T20b), the rewrite of the function's body (T20b), the thunk that adapts a call to the interpreter's uniform signature (T20b), any change to the built image |

**Nothing in the shipped image changes in this task.** The `virtualized` level
of the policy is still **not implemented**, and a policy that asks for it still
fails the build — that is unchanged behaviour and it must stay that way until
T20b lands. See §5.

What the operator gets is the **verdict**: for every selected function, the
pass now says whether it *could* be virtualized, and if not, why; and if it
could, how big the virtualized form would be (slots, frame bytes, bytecode
bytes). That is the number the operator needs before paying for the 5-50x.

## 2. The rules this task must not break

Everything in T20 §1 and §4 applies. In particular:

- The payload is **manually mapped**. The bytecode may hold **indices and
  relative offsets only** — never an absolute address. A pointer value that
  the *program* computes is a runtime value and may travel in a slot; a
  pointer value that the *pass* bakes in may not.
- The bytecode is a **`private constant` global** with no dynamic initialiser
  (a global constructor is fatal for a manually mapped payload).
- The pass must not introduce **TLS** (the mapper resolves TLS with a pattern
  scan and fails the whole injection when it does not find it).
- The pass must not introduce **new imports**.
- The pass runs at **`registerOptimizerLastEPCallback`** and must define
  **`isRequired() { return true; }`**.
- The **LTO pre-link** phase fires the same extension point: a module must not
  be transformed twice. T19's per-phase module flags are the model; reuse
  them.
- Errors are **values**, not exceptions: a pass must never throw out into the
  driver, which is built without exceptions.

## 3. The design decisions (the orchestrator's calls — do not re-litigate)

### 3.1 One `.def` file is the source of truth

The opcode numbers live in **one** file, `tools/protect/src/isa.def`, written
as an X-macro list, and it is included by every side that needs a number (the
encoder in this task, the interpreter's dispatcher in T20b). The reference
implementation had both sides holding a hand-written copy of the same numbers,
and noted that "a silent disagreement between the two would not fail to
compile". Do not repeat that.

The `.def` file defines, per opcode: the **name**, the **canonical number**
(0-based, dense) and a one-line comment saying what it does. The canonical
number is **not** what appears in the bytecode — see §3.6.

### 3.2 The slot model

- One slot per SSA value. A slot is an index into a frame array of 64-bit
  cells.
- The frame is allocated by the **rewritten** function (T20b) as an `alloca`
  in its entry block. This task only computes the frame's **size**.
- Slots hold 64-bit values. A value of a narrower type is stored
  sign/zero-extended to 64 bits according to its type, and every operation
  truncates its result back to the operation's width (§3.3).
- The frame layout is fixed and documented: `[0 .. slot_count)` are the value
  slots, then the **argument stack** region (the values pushed for a call),
  then the **call scratch** region (the arguments of the call in flight).
  The interpreter of T20b reads the layout from the bytecode header, so the
  layout is a **contract** — write it down in `vm.hpp` and in the README.

### 3.3 The widths, and `nsw`/`nuw`

This is the detail that makes or breaks the differential test, so it is
normative:

- **Every arithmetic operation is performed at the width of the original IR
  operation** (1, 2, 4 or 8 bytes), and the result is **truncated** to that
  width. An `i32` add is an `i32` add: widening it to `i64` would change the
  result on overflow (the `i32` version wraps, the `i64` version does not).
- **`nsw` and `nuw` are carried** and honoured: the interpreter must detect
  the overflow the flag forbids and produce poison/undefined the same way the
  original instruction would have. The honest way to do that in the
  interpreter is to compute the operation at the width and let the flag be
  checked by the same arithmetic the backend would use. **If you cannot
  reproduce a flag exactly, refuse the function** (see §4) — do not drop the
  flag silently.
- `exact` is only relevant for the division instructions; carry it or refuse.

### 3.4 The PHI model

- A PHI becomes **edge assignments**: each predecessor performs the moves for
  the successor's PHIs **before** its branch.
- The moves are a **parallel copy**: push every source, then pop every
  destination in reverse order. A sequential copy breaks a swapping back-edge
  (the classic `a, b = b, a` bug). The lowering must emit the parallel
  sequence, and the test fixture must contain a swapping back-edge.

### 3.5 The call model (decided here, implemented in T20b)

The interpreter cannot call a function with an arbitrary signature: LLVM IR has
no way to build a call with a dynamic signature, and there is no `libffi` in a
freestanding payload.

**The decision:** the interpreter calls through a **uniform signature**, and a
**per-call-site thunk** adapts the real callee to it.

```
the interpreter:   u64 thunk(u64* scratch)      // scratch[0..n-1] are the arguments
the thunk:         loads the arguments out of the scratch array with the
                   right types, calls the real callee, stores the result
```

- The **pool** is a global array of function pointers, one entry per call
  site. It is a *relocatable* global (the mapper applies the DIR64
  relocations), so it is **not** part of the encrypted bytecode: the bytecode
  holds the **index** of the pool entry.
- The thunk is emitted as IR by the pass (T20b), so it is freestanding: no
  CRT, no exceptions, no RTTI.
- A callee that returns a **struct** is refused (§4). A callee that returns
  nothing returns 0.

**This task's share of the work:** the lowering must record, per call site,
the callee's symbol and the **types of its arguments and of its return
value**, so that T20b can emit the thunk. The lowering does not emit the
thunk.

### 3.6 The bytecode container and the encryption

The container (the header is **not** encrypted, or is encrypted with a
different key — decide and document it):

```
u32 magic          'C','S','2','V'
u32 version        1
u32 slot_count     the number of value slots
u32 arg_slots      the size of the argument-stack region
u32 call_slots     the size of the call-scratch region
u32 instruction_count
u32 flags
u32 reserved
```

followed by the instruction stream.

- Every instruction is a **fixed 24 bytes**:
  `u8 opcode | u8 width | u8 flags | u8 pad | u16 dst | u16 src1 | u16 src2 |
  u16 src3 | u32 aux | u64 imm`.
- `width` is the operand width in bytes (1, 2, 4, 8). `flags` carries
  `nsw` (bit 0), `nuw` (bit 1) and `exact` (bit 2). `aux` carries the
  sub-opcode (the comparison kind, the branch target as an instruction
  index, the size of a memory access). `imm` carries the immediate.
- The bytecode is **encrypted** with a per-build key, derived from the module
  seed the same way T19 derives its key (`splitmix64` over the module seed),
  and split across **three** globals, so that no single constant in the image
  is the key. **This is the T19-grade key**: it stops a scanner from grepping
  for it, and it makes the blob different in two builds. It is **not** a
  runtime-derived key — that is T21's job, and the README must say so.
- The **per-build opcode permutation**: the canonical opcode number `x` is
  stored as `p(x) = (x * k_a + k_b) mod N`, where `k_a` is odd and coprime
  with `N` (the number of opcodes), and `k_a`/`k_b` come from the module
  seed. The **encoder** emits `p(x)`; the **interpreter** (T20b) maps the
  fetched byte back with `p^-1`. Both sides derive the constants from the
  same module seed, so they cannot disagree — but they must agree *by
  construction*, and the test of T20b must prove it.
- The **IR** must not contain a 64-bit constant in the instruction stream:
  the X86 backend of LLVM 22.1.8 mis-materialises one (see
  `tools/protect/README.md` §12 and the handoff). The bytecode is **data**,
  not IR, so a 64-bit constant in the bytecode is safe — but a 64-bit
  constant in the *emitted IR* is not. Keep the mixer constants in globals.

### 3.7 The ISA (the baseline — adjust only with a reason in the report)

| opcode | operands | meaning |
|---|---|---|
| `Const` | dst, imm | dst = imm (extended to the width) |
| `Move` | dst, src1 | dst = src1 (truncated to the width) |
| `Add` `Sub` `Mul` `And` `Or` `Xor` | dst, src1, src2, width, flags | the arithmetic, at the width |
| `SDiv` `UDiv` `SRem` `URem` | dst, src1, src2, width | the division, at the width |
| `Shl` `LShr` `AShr` | dst, src1, src2, width, flags | the shift amount is masked to width-1 |
| `Neg` `Not` | dst, src1 | |
| `Cmp` | dst, src1, src2, aux=the kind, width | dst = 1 if the comparison holds, else 0 |
| `Cast` | dst, src1, aux=the kind, width | zext / sext / trunc / bitcast |
| `Load` | dst, src1(ptr), aux=the size in bytes | dst = *(iN*)src1 |
| `Store` | src1(ptr), src2, aux=the size in bytes | *(iN*)src1 = src2 |
| `Gep` | dst, src1, imm | dst = src1 + imm (a **signed** byte offset) |
| `Br` | aux = the target instruction index | pc = the target |
| `BrIf` | src1 (the condition), aux = the target | if src1 != 0, pc = the target |
| `Arg` | src1 | push src1 onto the argument stack |
| `Call` | dst = the result, imm = the pool index, aux = the argument count | pops the arguments, calls the pool entry |
| `Ret` | src1 | return src1 (the return slot) |
| `MemCpy` | src1(dst), src2(src), imm = the length | copies a **constant** number of bytes |
| `MemSet` | src1(ptr), src2(the byte), imm = the length | |

The comparison kinds, the cast kinds and the memory sizes are enumerations in
`vm.hpp`, each with one canonical number.

**A `switch` is lowered to a chain of `Cmp` + `BrIf`.** It is not refused.

## 4. The eligibility gate — **fail closed**

A function is virtualizable only if it passes **every** check. If any check
fails, the function is **left completely untouched** and the reason is
reported. A half-lowered function is a miscompile, and a miscompile in a
manually mapped payload with no exception tables kills the game.

The refusal reasons (one enum, one `to_string`, in `vm.hpp`):

| reason | refused because |
|---|---|
| `eh_pad` | the function has an EH pad / `invoke` / `landingpad` / `resume`: the interpreter cannot model unwinding |
| `variadic_function` | the function itself is variadic |
| `variadic_callee` | it calls a variadic callee |
| `indirect_call` | it makes an indirect call: there is no relocatable address for the pool |
| `indirect_parameter` | a callee has `byval` / `sret` / `inalloca` / `preallocated`: Windows x64 rejects these in an indirect signature |
| `musttail` | the tail-call contract does not survive the rewrite |
| `volatile_access` | a volatile access: the ordering guarantees are not modelled |
| `atomic_access` | an atomic access: the ordering guarantees are not modelled |
| `dynamic_memcpy` | a `memcpy`/`memset` with a **non-constant** length: it would lower to a CRT call |
| `unsupported_type` | a float, a vector, or an aggregate in registers: this task handles integers (of at most 64 bits) and pointers only |
| `address_taken` | the address of the function is taken (`&F`): the rewrite changes what the address means |
| `too_many_slots` | the function needs more slots than the budget (§6) |
| `too_many_arguments` | a call needs more arguments than the budget (§6) |
| `recursion` | the function can call itself: the frame would multiply by the recursion depth |
| `virtualized_callee` | the function calls a function that is **itself** selected at the `virtualized` level: the interpreter would have to call into another interpreter |
| `unsupported_instruction` | **the catch-all**: any IR instruction the lowering does not model. The lowering must have a default branch that refuses. |

Two of these are **calls**, and the reasoning matters:

- `recursion` — the reference implementations refuse a virtualized function
  that calls another virtualized one, because the interpreter would have to
  call into another interpreter. **This task refuses both** (a virtualized
  callee and a direct self-recursion), because both multiply the frame and
  both are cheap to detect. The reason is reported, not guessed.
- `unsupported_instruction` — the lowering enumerates the instructions it
  models. **Anything else is refused.** There is no "best effort" path.

Note that the existing `CallSkipReason` in `transform.hpp` already covers
`variadic_callee`, `indirect_parameter`, `musttail`, `external_callee`,
`intrinsic_callee`, `optnone_callee`, `invoke` and `indirect_callee`. The new
gate must **reuse** that reasoning where it applies (do not write a second
copy of the same rules), and extend it with the VM-specific ones.

## 5. What the pass does with the verdict, and what it does **not** do

- The `virtualized` level of the policy **stays unimplemented** in this task.
  A policy that asks for it **still fails the build** with the message it
  fails with today. That is deliberate: a build that *looks* protected and is
  not is the failure mode this project exists to avoid.
- The **analysis runs for every selected function**, whatever its level, and
  its verdict goes into the **report** and into the **coverage file**. The
  operator therefore sees, for the functions the policy protects today at the
  `call` level, what the virtualized form would cost.
- The analysis must be **side-effect free**: the function's IR is **not**
  changed in this task. A test asserts that the module is byte-for-byte
  identical before and after the analysis.

## 6. The budgets (from T20 §4 — the numbers are fixed)

| budget | value | on overflow |
|---|---|---|
| value slots per function | 256 | refuse (`too_many_slots`) |
| call arguments | 64 | refuse (`too_many_arguments`) |
| frame size | 256 slots × 8 bytes + the argument region (64 × 8) + the call scratch (64 × 8) = **2560 bytes** | fixed |

The frame size is a **constant**: the rewritten function allocates exactly
that, so the interpreter's frame is always the same shape and the slot
indices are absolute. Report it.

## 7. The report and the coverage

The transform report gains, per selected function:

```
  cs2-protect: vm: cs2::aim::solve: virtualizable
                slots 42, frame 2560 bytes, bytecode 268 bytes, 17 calls
  cs2-protect: vm: cs2::aim::occlude_polygon: refused (unsupported_type: float)
```

and the coverage file gains the same numbers in a machine-readable form. The
coverage file's format is read by `tools/protect/check_policy.py` and
`tools/protect/check_payload.py`: **those two must keep working**, and the
format change must be backward-compatible (a new optional section, or new
optional keys) so that a coverage file of an older build is still readable.

The coverage must record, per function: the symbol, the verdict
(`virtualizable` / `refused`), the reason (when refused), the slots, the
frame bytes, the bytecode bytes, the number of calls and the number of
instructions.

## 8. The tests

Three levels, and **each one must be shown to be able to fail** (break the
code on purpose, watch the test fail, revert it — and say so in the report).

1. **The unit test** (`tests/protect_tests.cpp`, the `cs2-protect-tests`
   binary). It builds IR with the `IRBuilder` and runs the eligibility and
   the lowering on it, then asserts the verdict, the slot count, the
   instruction count and the refusal reasons. This is the fast loop: it needs
   no SDK tool, no `opt` and no `clang-cl`, and it must cover:
   - a function that is virtualizable, with the expected slot count;
   - a function with a **swapping PHI back-edge**, whose lowered form has the
     parallel copy (push all, pop in reverse);
   - one function per refusal reason — at least: `eh_pad`, `variadic_callee`,
     `indirect_call`, `unsupported_type` (a float), `dynamic_memcpy`,
     `address_taken`, `too_many_slots`;
   - an `nsw`/`nuw` operation: the flags are carried into the bytecode;
   - a `undef` read twice: the lowering freezes it.
2. **The IR test** (`tests/vm_ir_test.py`, a new CTest entry). It runs
   `opt.exe -load-pass-plugin=<dll> -passes=cs2-protect` over a checked-in
   fixture and asserts on the **report and the coverage**: the verdict per
   function, the refusal reasons, and the fact that **the function's IR is
   unchanged** (the module is identical before and after the pass). It must
   exercise the **production path** as well: a fixture compiled with
   `clang-cl -fpass-plugin=<dll> -emit-llvm -S`, because `opt` does not
   invoke the extension points, so a test that only exercises the `opt` path
   can pass for the wrong reason (T19's review made this point).
3. **The existing tests must stay green**: `protect-tests`,
   `protect-ir-test`, `protect-diff-test`, `protect-coverage-test`, and the
   payload build with its `check_policy.py` / `check_payload.py`.

**Skip with CTest's skip code when the plugin has not been built**, so a
machine without the LLVM SDK still has a green suite (T19 does this).

The differential test — the one that proves the *behaviour* is unchanged —
belongs to **T20b**, because the behaviour only changes when the function is
actually rewritten. This task must produce the bytecode that T20b's
differential test will run.

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md` (the new level, the analysis, the ISA)

Do **not** touch: `payload/**`, `overlay/**`, `injector/**`, the root
`meson.build`, `payload/meson.build`, `payload/meson_options.txt`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

## 10. The verify loop

```powershell
cd <your worktree>

# the host (must stay green)
meson compile -C build
meson test -C build

# the plugin
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build -R protect-tests

# the protected payload (the image must not change: the analysis is read-only)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a fresh directory name.

Note that `CS2_PROTECT_CONFIG` must be set for **both** the configure and the
compile step: the plugin reads it in the compiler process.

## 11. Acceptance criteria

1. The ISA lives in **one** file (`tools/protect/src/isa.def`) and the
   encoder is its only consumer in this task.
2. The eligibility gate refuses **every** function that cannot be lowered,
   with one reason per function, and the catch-all (`unsupported_instruction`)
   is reachable.
3. The lowering produces a bytecode blob whose **header, instruction count,
   slot count and encryption** are all recorded in the report and in the
   coverage.
4. The **unit test** and the **IR test** are green, and **each was proven to
   be able to fail**.
5. The function's IR is **unchanged** by the analysis — asserted by a test.
6. The plugin's four existing tests are green, the host suite is green, and
   the protected payload still builds with **37 encrypted dispatches** and an
   unchanged import and export table.
7. The `virtualized` level still **fails the build** when a policy asks for
   it, with a message that says it is not implemented yet.
8. `tools/protect/README.md` documents: the ISA, the frame layout, the
   budgets, the refusal reasons, and the fact that the key is a build key and
   **not** a runtime key.
9. Nothing outside §9 was touched.

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

What changed per file. The ISA you settled on and why (and every deviation
from §3.7, with the reason). The frame layout. The eligibility rules and
which real payload functions they admit — **the verdict for every function of
`tools/protect/policy.local.json`**, with the numbers. The exact tests, and
how each was proven to be able to fail. What you could not verify.
