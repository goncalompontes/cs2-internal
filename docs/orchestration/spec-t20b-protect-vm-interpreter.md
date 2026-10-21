# T20b — the virtualizer's back end: the interpreter and the rewrite

**Goal:** the bytecode that T20a produces **runs**. The pass emits a
freestanding interpreter **as LLVM IR**, replaces the body of the selected
function with a call to it, and the function's behaviour is unchanged.

This is the second half of T20. T20a built the front end (the eligibility
gate, the lowering, the encoder); this task builds the back end (the
interpreter, the thunks, the rewrite) and — most importantly — **proves** that
the behaviour is unchanged.

**Read `spec-t20-protect-virtualization.md` and `spec-t20a-protect-vm-front-end.md`
first.** This task is the continuation of T20a: it consumes the contract that
T20a defined (the ISA, the frame layout, the bytecode container, the pool).
Where this document and those two disagree, **this document wins** — it is the
narrowest and the latest statement.

---

## 1. What this task delivers

| | |
|---|---|
| **Delivers** | the interpreter (emitted as IR), the per-call-site thunks, the rewrite of the function's body, the **`virtualized` policy level made real**, the differential test, and the payload-level checks |
| **Does not deliver** | the widening to the payload's real aim functions (T20c), the slowdown measurement (T20c), the runtime-derived key (T21) |

**This task changes the shipped image.** For the first time, the protection
pass removes code from the payload. That is why the acceptance criteria are
about *behaviour* and not about shape.

## 2. The ground rules (unchanged, from T20 §1)

- The interpreter is **emitted as LLVM IR**, not linked in as C++: the payload
  is manually mapped and has no CRT, no global constructors and no unwind
  tables it cannot justify.
- **No CRT calls**: no `memcpy`, `memset`, `malloc`, `free` — the copies are
  explicit load/store loops written in IR.
- **No RTTI, no exceptions**, no `new`, no containers, no function-local
  statics, no `atexit`, **no TLS**, no global with a non-constant initialiser.
- **No absolute address** is baked into the bytecode or into a cipher: the
  bytecode holds indices, the pool holds relocatable pointers.
- The **import table** of the protected payload must be **identical** to the
  unprotected one, and the **exports** too. The thunks and the interpreter are
  `internal`: they must not appear in the export table.
- The rewritten function keeps its **type**: the x64 ABI is preserved by
  construction.
- The **`.pdata`/`.xdata`** pair must stay valid (the mapper treats a failure
  to register the unwind table as fatal).
- The pass runs at `registerOptimizerLastEPCallback`, defines
  `isRequired() { return true; }`, and must not transform a module twice
  (the LTO pre-link phase fires the same extension point).

## 3. The contract that T20a defined (this task consumes it)

T20a delivers:

- `tools/protect/src/isa.def` — the opcodes, their canonical numbers and
  their operand layout.
- The **frame layout**: `[0, slot_count)` are the value slots, then the
  **argument stack** (64 cells), then the **call scratch** (64 cells). The
  frame is **always 2560 bytes** (T20a §6): the rewritten function allocates
  exactly that, so the slot indices are absolute.
- The **bytecode container**: a 32-byte header (`magic`, `version`,
  `slot_count`, `arg_slots`, `call_slots`, `instruction_count`, `flags`,
  `reserved`) followed by the 24-byte instructions. The header states where
  the argument stack and the call scratch begin.
- The **encryption**: the blob is encrypted with the T19-grade key (three
  globals, derived from the module seed). The interpreter **decrypts on
  fetch**.
- The **opcode permutation**: `p(x) = (x * k_a + k_b) mod N`, derived from
  the module seed. The bytecode holds `p(x)`; the interpreter maps the fetched
  byte back with `p^-1`.
- The **pool**: one entry per call site, with the callee's symbol and the
  types of its arguments and of its return value.

If T20a's contract is missing something this task needs, **change the
contract** — but change it in `isa.def`/`vm.hpp` and say so in the report. Do
not create a second, parallel definition.

## 4. The interpreter

### 4.1 The shape

One **interpreter per module**, shared by every virtualized function of that
module. Its signature:

```
define internal i64 @cs2.protect.vm(ptr %bytecode, ptr %frame, ptr %pool)
```

- `%bytecode` — the encrypted blob of the function being run.
- `%frame` — the 2560-byte frame the rewritten function allocated.
- `%pool` — the array of the thunks.
- The return value is the function's result, widened to 64 bits.

The interpreter's own locals (`alloca`, fixed size, **no heap**): the program
counter, the current instruction pointer, the argument depth, the scratch
pointers, the decoded fields.

### 4.2 The dispatcher — **no jump table**

The dispatcher is a **comparison tree** over the permuted opcode values (a
balanced binary search), **not** a `switch`. A `switch` over ~25 opcodes
compiles to a **jump table**, and that jump table is the signature every
deobfuscator keys on. The per-build permutation makes the table's *contents*
different in two builds, but its *shape* is the same — so the shape must not
be there.

The tree is built from the sorted list of the permuted opcode values. The
implementer emits it with a helper that takes the sorted values and their
handlers and produces the comparisons. The tree's shape may also be permuted
per build (the order of the two halves of every node is the seed's choice);
that is a bonus, not a requirement.

### 4.3 Decrypt on fetch

The instruction is **decrypted when it is fetched**, not when the blob is
loaded: the blob stays read-only (`private constant`), and the interpreter is
re-entrant (two invocations at once, each with its own frame, do not fight
over a decrypted buffer).

- The key parts are **globals** (three of them), read with **volatile
  loads** — T19's lesson: a 64-bit constant in the instruction stream is
  mis-materialised by this backend, and the constants in globals are written
  by the object writer, so they reach the image bit for bit.
- The rolling key advances per fetched instruction, so the same byte at two
  different addresses does not decrypt to the same value.
- The decryption must be **exactly** the inverse of T20a's encryption. The
  differential test is what proves it.

### 4.4 The handlers

One handler per opcode. A handler:

1. reads the instruction's fields (`align 1` loads — the instruction stream
   is not guaranteed to be aligned; or a byte-wise decoder);
2. reads its operands from the frame;
3. performs the operation **at the instruction's width** and truncates the
   result;
4. writes the result into the destination slot;
5. branches back to the dispatcher.

The widths, the `nsw`/`nuw`/`exact` flags, the comparison kinds, the cast
kinds and the memory sizes are T20a's, and the interpreter must honour them
**exactly**:

- `nsw`/`nuw`: when the operation overflows the way the flag forbids, the
  result is **poison** (`llvm::PoisonValue`), not a wrapped value. The
  original instruction's behaviour on that input is undefined, so poison is
  the faithful answer — but a *wrapped* value would be a **miscompile**, and
  the differential test will find it.
- `exact`: the same rule for the division instructions.
- The shift amount is masked to `width - 1` (the LLVM semantics).
- The division by zero is **undefined** in the original IR too: the handler
  does not have to be nicer than the original.

### 4.5 The calls

The interpreter calls through the **uniform signature**:

```
the interpreter:   i64 %thunk(i64* %scratch)      ; scratch[0..n-1] are the arguments
```

- The `Arg` handler pushes a value onto the argument stack (the region that
  starts at `slot_count`).
- The `Call` handler copies the top `n` arguments from the argument stack
  into the call scratch (an **explicit loop**, not a CRT `memcpy`), calls
  `pool[imm]` with the scratch pointer, stores the result in the destination
  slot and pops the arguments.
- The copy is a **loop**, so the interpreter must have a loop — a `br` back
  edge inside the interpreter. That is fine (the interpreter is not the
  virtualized code), but it must not be a `memcpy` call.

### 4.6 The interpreter is the target

Once the VM exists, the interpreter is the only native code left, so it is
what an analyst attacks. This task gives it the **opcode permutation** and
the **comparison-tree dispatcher**. The other passes (the call encryption of
the interpreter's own calls, the string encryption) are a later task — say so
in the report.

## 5. The thunks

For every call site, the pass emits an **internal thunk** with the uniform
signature:

```
define internal i64 @cs2.protect.thunk.N(ptr %scratch) {
  %a0 = load i64, ptr %scratch
  %r = call i64 @callee(i32 %a0, ptr %a1)
  ret i64 %r
}
```

- The thunk loads the arguments with the **right types** (the truncation and
  the extension the original call had), calls the callee with the original
  calling convention and its `AttributeList`, and returns the result
  widened to 64 bits.
- A callee that returns nothing returns 0.
- The thunks are `internal`: they are **not** in the export table.
- The **pool** is an `internal` global array of the thunks:
  `@cs2.protect.pool = internal global [N x ptr] [... ]`. It is a relocatable
  global: the mapper applies the DIR64 relocations (T19's table does the
  same, and the payload check counts them).

## 6. The rewrite

The function's **type never changes**. Only its body is replaced:

```
define i32 @cs2::aim::solve(i32 %a, ptr %b) {
entry:
  %frame = alloca [2560 x i8], align 16
  %slots = bitcast ptr %frame to ptr
  store i32 %a, ptr %slots, align 8      ; the argument slots
  store ptr  %b, ptr %slots+8, align 8
  %r = call i64 @cs2.protect.vm(ptr @cs2.protect.vm.blob, ptr %slots, ptr @cs2.protect.pool)
  %ret = trunc i64 %r to i32
  ret i32 %ret
}
```

- The frame is allocated in the **entry block**, with a **constant size**.
- The arguments are copied into their slots (the slot indices come from
  T20a's lowering).
- The result is converted back to the function's return type (a truncation, an
  extension, or nothing for `void`).
- The function is `noinline` (the early phase already marks it).
- The function keeps its `ret` attributes.
- The **debug info is dropped** (the body is deleted).
- The bytecode is a **`private constant`** global — no dynamic initialiser,
  no constructor.
- The interpreter and the thunks are emitted **once per module** and reused
  by every virtualized function of that module.

## 7. The `virtualized` level becomes real

- The policy's `"level": "virtualized"` entry now **works**: the function is
  virtualized.
- The selection phase's "the level is not implemented" error **disappears**
  for the `virtualized` level (it stays for any level that is still not
  implemented).
- `tools/protect/policy.example.json` must ask for a level that is
  implemented, and `tools/protect/README.md` must document both levels.
- A function is virtualized **or** call-encrypted, **never both**. A
  `virtualized` entry wins over a `call` entry for the same function (the
  stronger level), and the report must say so.

## 8. The tests — the correctness backbone

The rewrite changes the *meaning* of the code if it is wrong, so the test
must compare **behaviour**, not shape.

### 8.1 The differential test (the one that matters)

Build a fixture program **twice** from the same source — once with the pass,
once without — run both, and compare their **output** and their **exit
codes**.

The fixture must cover the cases that break naive implementations:

- a loop with a **swapping PHI** back-edge;
- a function with **many live values** across a call (the slot model is
  exercised);
- a **direct call** to another function (the native-call path, the thunk);
- a function whose result is used in a **constant-folding** context;
- `nsw`/`nuw` arithmetic **near the overflow boundary** (the rewrite must not
  change the result — and the poison case must not be *tested* as a normal
  value, because it is undefined; test the boundary that does *not* overflow);
- a value read twice where the IR says `undef` (the `freeze` requirement);
- a `switch` (lowered to a comparison chain);
- a `memcpy` with a **constant** length;
- a division and a remainder, signed and unsigned;
- a shift by an amount that is **not** a constant.

The test **must be able to fail**: break the interpreter on purpose (change
one handler's width, drop one `nsw` check, swap the parallel copy), watch the
differential test fail, revert it. Say in the report which mutations you
tried.

The existing `tools/protect/tests/diff_test.py` is the model: it already
builds a fixture twice and compares. T20b adds the `virtualized` level to it
(or adds a sibling test — the fixture's policy must ask for the
`virtualized` level).

### 8.2 The IR test

`tools/protect/tests/ir_test.py` (or a sibling) must assert, over a
checked-in fixture:

- the function's **type is unchanged** (the same parameters, the same return
  type);
- the body now contains a **call to the interpreter** and a **bytecode
  global**;
- the bytecode global is `private constant` and its bytes are **not** the
  plaintext opcode sequence;
- a fixture that must be **refused** (EH, varargs, an indirect call, a
  float) is **byte-for-byte unchanged**;
- a swapping-PHI fixture produced the **parallel copy**.

The test must exercise the **production path** as well (compile a fixture
with `clang-cl -fpass-plugin=<dll> -emit-llvm -S`), because `opt` does not
invoke the extension points, so a test that only exercises the `opt` path can
pass for the wrong reason (T19's review made this point).

### 8.3 The payload-level checks

- The protected payload must have the **same exports** and the **same import
  table** as the unprotected one.
- It must keep a valid `.reloc` with DIR64 entries and a valid
  `.pdata`/`.xdata`.
- The section list must have **no new `.CRT$XCU`** (no global constructor).
- `llvm-objdump -d` must show that the virtualized function's **original
  instruction bytes are gone**: the function is now a short prologue, a call
  to the interpreter and an epilogue. `tools/protect/check_payload.py` should
  check this (it is the check that fails if the rewrite silently did not
  happen).

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md`

Do **not** touch: `payload/**`, `overlay/**`, `injector/**`, the root
`meson.build`, `payload/meson.build`, `payload/meson_options.txt`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

**Do not widen the protection to the payload's real functions.** That is
T20c: this task proves the mechanism on fixtures and leaves the payload's
policy as it is (`tools/protect/policy.local.json` stays at the `call` level,
unless it is gitignored anyway — it is).

## 10. The verify loop

```powershell
cd <your worktree>

meson compile -C build
meson test -C build

python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build

$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect-t20b payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect-t20b"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a **fresh** directory name.

`CS2_PROTECT_CONFIG` must be set for **both** the configure and the compile
step.

## 11. Acceptance criteria

1. **One fixture function is virtualized end to end**: its original
   instruction bytes are gone from the binary, the bytecode is encrypted, and
   the **differential test** proves the protected and the unprotected fixture
   agree on their output and their exit code.
2. The differential test **can fail** — the mutations are listed in the
   report.
3. The IR test proves that a function that must be **refused** is left
   untouched, and that a swapping-PHI fixture was lowered with the parallel
   copy.
4. The import table and the exports of the protected payload are
   **unchanged**; the section list has no new `.CRT$XCU`; the `.reloc` and
   the `.pdata`/`.xdata` are valid.
5. `meson test -C build` is green, and the plugin's CTest suite is green.
6. The report states, per virtualized function: the slots, the frame bytes,
   the bytecode bytes, the instruction count and the number of calls.
7. The `virtualized` level works from the policy, and the report says what
   happens when a policy asks for `virtualized` on a function whose caller is
   also virtualized.
8. Nothing outside §9 was touched.

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

What changed per file. The interpreter's shape and the dispatcher's tree.
How the decryption, the widths, the poison and the calls work. The exact
tests, and **how each was proven to be able to fail** (the mutations you
tried and what they broke). The measurements: slots, frame bytes, bytecode
bytes. What you could not verify.
