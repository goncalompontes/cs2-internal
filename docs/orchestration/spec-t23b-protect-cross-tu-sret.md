# T23b — the `sret` edges: the rule that was a policy, not a limit

**Goal:** the last cross-TU edges of the protected functions leave the image.
Twelve of the twenty-six cross-TU call sites are **struct-returning**: the
callee takes a hidden pointer to the caller's temporary, writes the struct
there, and returns nothing. Today those calls stay **direct** in the shipped
DLL, because T19 refuses a callee with an `sret` parameter.

**That refusal is a policy, not a limit of the target.** The implementer of
T23a measured it: the SDK's `llc` **accepts** an indirect `sret` call on
`x86_64-pc-windows-msvc` (exit 0, the same register placement as the direct
call). So the rule can be narrowed, and this task narrows it.

**Read `spec-t23-protect-cross-tu.md` first — it is the parent specification
and it is normative.** This document is the narrower, later statement: where
they disagree, **this one wins**. The parent's §3.1 (the measured hole),
§5.1 (the table entry), §5.2 (the `cross_module` key) and §7 (the report, the
coverage, the payload check) are the material. The parent's §5.3 is **this**
task's.

**Read `spec-t23a-protect-cross-tu-routing.md` too** — it is the task that
routed the ordinary cross-TU edges, and this task continues its work.

---

## 1. What is left, measured

Of the 332 call sites of the six protected functions, T19 rewrites 37 (the
same-module ones) and T23a rewrote **14** (the ordinary cross-TU ones). The
**12** that remain are the `sret` edges: the callee takes a hidden pointer to
the caller's temporary.

The parent's §3.1 named the 24 cross-TU callees; the `sret` ones among them
are the ones that return an aggregate.

**The number to expect:** the dispatch count goes from **51** (after T23a) to
**63**. The count is a **snapshot of the tree** — it must go **up**, never
down, and the payload check must still end in `ok`.

## 2. The rules this task must not break

- **No new imports**; the import and export tables do not change.
- **No absolute address** in the table: the entry is a relocatable pointer to
  the external symbol, as in T23a.
- The **`.pdata`/`.xdata`** pair stays well formed: the sret call has a
  caller frame that the unwind table must describe, and the *caller* of a
  struct-returning function has a temporary on its stack.
- The payload is **manually mapped**: the mapper applies the DIR64 fixups and
  registers the unwind table; a wrong unwind entry is a crash in the game.
- The plugin's configuration channel is the environment; the pass runs at
  `registerOptimizerLastEPCallback` with `isRequired() { return true; }`.
- **The sret pointer is a real pointer to the caller's stack**: the thunk
  must pass it **unchanged**, and the callee writes through it. The
  interpreter's frame is not involved: the temporary belongs to the *caller*,
  which is the function being virtualized (or the thunk's caller).

## 3. The decisions

### 3.1 The rule is narrowed to `sret`, and only `sret`

`byval`, `inalloca` and `preallocated` stay refused.

**Why.** The evidence is not symmetric:

| attribute | the evidence | the verdict |
|---|---|---|
| `sret` | the SDK's `llc` accepts an indirect `sret` call on `x86_64-pc-windows-msvc` — the same register placement as the direct call (measured by the T23a implementer) | **narrow the rule: `sret` is routable** |
| `byval` | the argument is **copied** onto the callee's stack by the caller: the copy is a *caller-side* action, and the thunk's uniform signature cannot express it (the thunk does not know the size) | **stay refused** |
| `inalloca`, `preallocated` | the callee's frame is allocated by the caller, and the **size and the layout** are part of the call | **stay refused** |

The refusal reason changes: `indirect_parameter` covered all four, and it
must now name which one it is. A refusal that says "byval" is actionable; a
refusal that says "indirect_parameter" is not.

### 3.2 The thunk adapts the `sret` call

The interpreter calls through the **uniform signature** — `u64 thunk(u64*
scratch)` — and the scratch holds the arguments in the call's order. The
`sret` pointer is **already one of them**: in LLVM IR the hidden pointer is an
**explicit argument of the call**, so the call site already puts it in the
scratch, in the right position.

So the thunk is:

```llvm
define internal i64 @cs2.protect.thunk.N(ptr %scratch) {
  %sret = load ptr, ptr %scratch          ; the hidden pointer, the first argument
  %a0   = load i64, ptr %scratch + 8       ; the rest, in the call's order
  call void @callee(ptr sret, i64 %a0)      ; with the `sret` attribute
  ret i64 0
}
```

- The thunk's **own** signature is the uniform one: it takes a plain pointer,
  so the *indirect* call (the interpreter → the thunk) carries no `sret` and
  needs no special support.
- The thunk's **call** to the callee is a direct call with the `sret`
  attribute, exactly as the original call had it. The backend lowers it the
  way it lowers any sret call.
- The callee **returns nothing** (the result is in the caller's temporary),
  so the thunk returns 0. A callee that returns a value *and* takes an `sret`
  is not a thing in the x64 ABI; the pass must refuse it if it meets one.
- The `sret` pointer is **not** a slot in the frame: it is a value the program
  computed, and it travels like any other pointer.

**The temporary is the caller's.** When the caller is a virtualized function,
its temporary is in *its* frame (an `alloca` of the frame arena — the
virtualizer's `Alloca` opcode, T20a §3.2), and the interpreter's frame holds
the pointer to it. The thunk writes through it and the caller reads it back
from the same place. **The two mechanisms must agree**: the sret pointer is a
pointer into the caller's frame arena, and the callee writes there.

### 3.3 The `cross_module` entry is the same one

No new policy key. The `cross_module` list already names the callees; the
narrowed rule makes the *routing* of the `sret` ones possible, and the
coverage's "the cross-TU callees that are still direct" list becomes empty.

**If that list is empty, the check that produced it must say so** — and not
print a stale entry. A check that reports a callee as "still direct" when it
is not is the same lie the image scan used to tell (§1 of the plan's
correction), and the fix is the same: measure, then print.

## 4. What the task must deliver

1. The **narrowed rule**: a callee with an `sret` parameter is routable; a
   callee with `byval`, `inalloca` or `preallocated` is not, and the refusal
   names which one it is.
2. The **thunk** that adapts the `sret` call, emitted as IR (freestanding: no
   CRT, no exceptions, no RTTI), with the `sret` attribute on the call.
3. The **report and the coverage**: the `sret` edges are reported as routed,
   and the list of the cross-TU callees that are still direct is empty (and
   the check says so).
4. The **payload check**: the dispatch count goes up, every dispatch decodes
   to a callee of the image, and the "no direct call into the payload's own
   code" check (T23a's) now finds **zero** — the `sret` edges are not
   excepted by name any more, because there are none left.
5. The **tests**, each proven able to fail. The **differential test** is the
   one that matters: a fixture with a **struct-returning** callee, built twice
   (with and without the pass), and the two must agree — the struct the
   callee writes through the hidden pointer must come back **the same**.

## 5. The test that proves it

The **differential** test is the correctness backbone, and it is the one this
task exists for: a struct-returning call is the case where a mistake is a
**wrong value**, not a crash.

The fixture must cover:

- a callee that returns a **big** struct (bigger than a register: 16 bytes
  and more, so the sret path is really taken);
- a callee that returns a **small** struct (16 bytes or less — the ABI
  returns it in registers, and the rule must not break that path);
- a struct with **mixed members** (an integer, a float, a pointer) so that
  the copy back is not a memcpy of something uniform;
- the struct **used** after the call (the caller must read the values the
  callee wrote, through the hidden pointer);
- a callee that returns a struct **and** takes arguments;
- a call whose result is **discarded** (the callee still writes through the
  pointer).

The differential fixture must be a **DLL** (like T21's), so that the loader
and the DllMain path are the same as the payload's.

**The mutation that proves the test can fail:** make the thunk pass the
`scratch` pointer itself where the hidden pointer belongs (an off-by-one in
the scratch), and the test must fail with a wrong value.

## 6. The file ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md`

Do **not** touch: `payload/**`, `overlay/**`, `injector/**`, the root
`meson.build`, `payload/meson.build`, `payload/meson_options.txt`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

## 7. The verify loop

```powershell
cd <your worktree>
meson compile -C build
meson test -C build
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build --output-on-failure

$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='<your worktree>\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect-t23b payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect-t23b"
```

Use a **fresh** build directory name; do **not** `meson setup --wipe` the
other build directories. `CS2_PROTECT_CONFIG` must be set for **both** the
configure and the compile step.

## 8. The acceptance criteria

1. The dispatch count goes **up** (51 → **63**) and the payload check still
   ends in `ok`.
2. **Zero** cross-TU edges of the protected functions stay direct to a symbol
   the payload's own libraries define — and T23a's check, which excepted the
   `sret` edges **by name**, now excepts **nothing**. The excepted list is
   empty; the check says so.
3. A callee with an `sret` parameter is routed, and the thunk passes the
   hidden pointer **unchanged** (asserted at the IR level: the thunk's call
   carries the `sret` attribute and the first scratch cell).
4. A callee with `byval`, `inalloca` or `preallocated` is still refused, and
   the refusal **names the attribute**.
5. The **differential test** proves that a struct-returning call behaves the
   same with and without the pass, and the test **can fail** (the mutation
   in §5).
6. The import and export tables are unchanged, the `.reloc` DIR64 entries
   are intact, the unwind table is well formed, and the **caller's** frame
   with the sret temporary is described by the `.pdata` (the image's unwind
   table must still cover the frames that now exist).
7. The coverage format change is **backward compatible** (an older coverage
   file is still read).
8. The host suite and the plugin's tests are green, and each new test was
   **proven to be able to fail**.
9. `tools/protect/README.md` documents the narrowed rule, the thunk, and
   **what is still visible** (the inbound edges, the intrinsics, the CRT
   calls, and the fact that the callee *set* is still in the table).
10. Nothing outside §6 was touched.

## 9. The rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell. Say what is unverified.

## 10. The report back

What changed per file. The narrowed rule and the evidence for it (the
`llc` measurement, and your own). The thunk, and how the hidden pointer
travels. The dispatch count before and after. The list of the cross-TU
callees that are still direct (it must be empty). The exact tests, and **how
each was proven to be able to fail** — the mutation in §5 especially. What
you could not verify.
