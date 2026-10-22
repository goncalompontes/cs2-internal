# T23a — routing the cross-TU call graph through the caller's own table

**Goal:** the aim call graph leaves the image. A direct call to a function
that is defined in **another translation unit** of the payload becomes an
indirect dispatch through the same encrypted descriptor machinery T19 already
uses — so the static call graph of the protected functions is no longer
readable in the shipped DLL.

This is the **first half** of T23. It closes the hole for the calls that are
ordinary calls. The **second half** (T23b) narrows T19's `sret` refusal and
covers the twelve struct-returning edges — it is split out because it changes
the **call ABI** and deserves its own differential test.

**Read `spec-t23-protect-cross-tu.md` first — it is the parent specification
and it is normative.** This document is the narrower, later statement: where
they disagree, **this one wins**. The parent's §3 (the measured hole), §4.5
(the decision), §5.1 (the table entry), §5.2 (the `cross_module` policy key)
and §7 (the report, the coverage and the payload check) are the material for
this task. The parent's §5.3 (the `sret` rule) is **T23b's**, not this
task's.

---

## 1. The scope of this task

**In scope** — the parent's §5.1, §5.2, §6, §7 and §8.1/§8.2/§8.4:

- the policy's new **`cross_module`** key (a list of globs over the payload's
  own cross-TU symbols),
- the **routing**: a call to a declaration that a `cross_module` entry names
  becomes an indirect dispatch through the caller's own table, with the
  callee in the table as a **relocatable pointer to the external symbol**,
- the report and the coverage for the routed calls,
- the payload check's message for the case the table entry holds an **import
  slot** instead of the function,
- the `llvm-nm` staleness check over the payload's libraries,
- the unit test and the IR test.

**Out of scope** — T23b:

- **the `sret` narrowing.** The twelve struct-returning edges stay refused
  in this task, exactly as they are today, with the reason
  `indirect_parameter`. Nothing about the `sret` rule changes here.
- the differential test for an `sret` callee.

**The number to expect.** The parent measured 26 cross-TU edges: 14 are
ordinary calls and 12 are `sret`. This task routes the **14**. The dispatch
count in the protected image goes from **37** to **at least 51**; the `sret`
edges push it to 63 in T23b. The count is a **snapshot of the tree** — it
must go **up**, never down, and the payload check must still end in `ok`.

## 2. The rules this task must not break

- **No new imports.** A table entry that holds the **IAT slot** of an import
  instead of the function would make the dispatch call the slot: the image
  would still link, and the payload would call the wrong thing. The payload
  check must catch that case by name (§7.3 of the parent).
- **No absolute address** baked into the descriptor: the table entry is a
  **relocatable pointer** (a DIR64 in `.rdata`), the descriptor holds the
  **index**.
- The table entry points at an **external symbol** (`@_Z...`), which the
  linker resolves and the mapper relocates — the same mechanism T19's table
  already uses for its same-module callees.
- Nothing in the image is ever initialised; no TLS; no new global
  initialiser; the import and export tables do not change.
- The plugin's configuration channel is the **environment**; the pass runs at
  `registerOptimizerLastEPCallback` with `isRequired() { return true; }`.

## 3. The decisions (from the parent, restated so they are not re-litigated)

1. **The policy names the cross-TU callees** (§5.2 of the parent). At the IR
   level an import and an internal cross-TU declaration are the same thing —
   a `declare` with no body — so the pass **cannot** tell them apart by
   looking at the module. The operator's `cross_module` list is the
   declaration, and the payload check is the proof.
2. The entry is a **glob**, matched exactly like a `protect` entry: against
   the mangled symbol and against the demangled qualified name
   (`qualified_name()` in `src/selection.cpp`). `cs2::aim::DamageField::*`
   covers every member in one line.
3. The entry is matched against a **declaration** in the module (a function
   with no body here). A **same-module** callee needs no entry: it is routed
   by the existing rule.
4. The table entry is a **relocatable pointer to the external symbol**
   (§5.1 of the parent), so the dispatch resolves to the real callee and the
   mapper's DIR64 fixup applies it.
5. A `cross_module` entry that matched **nothing**, or that was seen but not
   routed, **fails the build** — the same rule the `protect` entries follow
   (a policy that names a function that cannot be protected protects
   nothing). The coverage's state vocabulary gains the state it needs, and
   `blocked` outranks `matched`.

## 4. What the task must deliver

1. The policy's `cross_module` section: parsed, validated, and reported like
   the other sections.
2. The routing: the cross-TU callee goes into the **caller's own** table,
   the call becomes an indirect dispatch through the encrypted descriptor,
   and the descriptor's index points at the right entry.
3. The report names the routed calls; the coverage records the cross-TU
   callees that are **still direct** (there will be some: the `sret` ones
   until T23b).
4. The payload check (§7.3 of the parent): every dispatch of the protected
   image decodes to a callee of the image, with a message that names the
   "the table entry is an import slot" case, and the `llvm-nm` staleness
   check over the payload's libraries.
5. The tests: the unit test (§8.1), the IR test (§8.2) and the coverage
   readers (§8.4) of the parent, each **proven able to fail**.

## 5. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md`

Do **not** touch: `payload/**`, `overlay/**`, `injector/**`, the root
`meson.build`, `payload/meson.build`, `payload/meson_options.txt`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

## 6. The verify loop

The parent's §10 applies. In short:

```powershell
cd <your worktree>
meson compile -C build
meson test -C build
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build

$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='<your worktree>\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect-t23a payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect-t23a"
```

Use a **fresh** build directory name; do **not** `meson setup --wipe` the
other build directories. `CS2_PROTECT_CONFIG` must be set for **both** the
configure and the compile step.

## 7. Acceptance criteria

1. A cross-TU callee named by `cross_module` is routed through the
   **caller's own** table as a relocatable pointer — asserted at the object
   level (a DIR64 in `.rdata`) and at the image level.
2. The dispatch count goes **up** (37 today, at least 51 with this task) and
   the payload check still ends in `ok`.
3. Every dispatch of the protected image decodes to a callee of the image;
   the "the table entry is an import slot" case has its own message.
4. **Zero** cross-TU edges of the protected functions stay direct to a symbol
   the payload's own libraries define — checked with `llvm-nm` over the
   payload's libraries. The `sret` edges are excepted **by name**: this task
   does not touch them, and the check must say so rather than fail on them.
5. A `cross_module` entry that matched nothing, or that was seen but not
   routed, **fails the build**.
6. The coverage format change is **backward compatible** (an older coverage
   file is still read).
7. The import and export tables are unchanged, the `.reloc` DIR64 entries are
   intact, the unwind table is well formed.
8. The host suite and the plugin's tests are green, and each new test was
   **proven to be able to fail**.
9. `tools/protect/README.md` documents the new key, the routing rule, the
   checks, and **what is still visible** (the inbound edges, the `sret`
   edges until T23b, the intrinsics, the CRT calls).
10. Nothing outside §5 was touched — in particular **no file under
    `payload/**`**.

## 8. Rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell. Say what is unverified.

## 9. The report back

What changed per file. The routing rule and how the table entry is built. The
dispatch count before and after. The `cross_module` list the shipped
`policy.example.json` carries, and the verdict for every entry. The exact
tests, and how each was proven to be able to fail. What is **still visible**
after this task, stated plainly. What you could not verify.
