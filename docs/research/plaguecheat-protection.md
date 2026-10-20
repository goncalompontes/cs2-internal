# Plaguecheat protection — the reviver (not a feature source)

> Research note, 2026-10-20. Companion to
> [`cheat-dump-inventory.md`](cheat-dump-inventory.md) §2.4, where the
> classification of this artifact was corrected after reading it.
>
> **Artifact.** `csco1337/plaguecheat` — 294 files, created 2026-07-10.
> Quarantine: `C:\Users\gpmpo\nea\quarantine\src\plaguecheat`.
> README: *"plaguecheat-mapper — recompiled with asmjit fuck veh"*.
> **Licence:** none ⇒ all rights reserved; read-only reference.
>
> **Why this note exists.** The inventory originally listed this as a possible
> CS2 *feature* source. It is not. It is the **protection side** of Plaguecheat:
> the code that revives a protected module in memory. That is worth a note of
> its own because it is the only *readable* protection design we have found
> outside our own Neverlose work.

---

## 1. What the tree contains

`verified` (tree + `plague/src/core/segment/reviver/reviver.hxx`):

| Path | Size | Role |
|---|---|---|
| `src/core/segment/reviver/reviver.cxx` | 39 KB | the reviver implementation |
| `src/core/segment/reviver/reviver.hxx` | 3.1 KB | the stage list (below) |
| `src/core/segment/snapshot/snapshot.cxx/.hxx` | 16 KB + 7 KB | snapshot format + loader |
| `src/core/segment/hooks/hooks.cxx/.hxx` | 3.2 KB + 2.5 KB | segment hooks |
| `src/core/segment/native/data/iat.hxx` | 5.7 MB | embedded import table |
| `src/core/segment/native/data/network.hxx` | 613 KB | embedded network/protocol data |
| `src/core/segment/native/data/oep.hxx` | 500 B | original entry point |
| `src/core/segment/native/native.hxx` | 2 KB | native segment descriptor |
| `deps/asmjit/**` | — | full asmjit (JIT) tree |
| `src/bootstrap.cxx`, `src/common.hxx` | — | entry point / shared defs |

---

## 2. The protection model

`verified` from the reviver's own stage list (`plague/src/core/segment/reviver/reviver.hxx`):

| Stage | What it does |
|---|---|
| `extract_segment` | allocates `MEM_COMMIT \| MEM_RESERVE` with `PAGE_EXECUTE_READWRITE` and copies the protected segment into it |
| `setup_veh` | installs the custom vectored exception handler |
| `setup_patches` | applies memory patches |
| `setup_cvar_interface` / `get_cvar` | wires the console-variable interface and resolves a convar by name |
| `setup_stack` | fabricates a stack for the revived code |
| `resolve_imports` | walks the embedded import list, validates the module name, resolves addresses with `GetProcAddress` / `LoadLibraryA` |
| `invoke_oep` | jumps to the original entry point |
| `exception_handler` | the VEH body (see below) |

Reading the sequence:

1. **The payload is a "segment"** — an encrypted/protected block that is
   *extracted* into fresh `PAGE_EXECUTE_READWRITE` memory. The protection
   boundary is the segment, not the whole module.
2. **The segment is protected by an emulated execution trace.** The comment on
   `k_snapshot_path` says the file is *"the healthy snapshot emitted by
   `EmuTrace.DumpSnapshot`"*, and that the reviver's **snapshot spoofing** is
   optional (empty path disables it). So the protected code verifies that it is
   running under a *faithful* emulation of the real environment; a static
   analysis run or a naive debugger produces a different trace and is rejected.
   The producer (`EmuTrace`) is **not** in this tree — only the reviver that
   satisfies the check. `verified` for the consumer, `unknown` for the producer.
3. **The VEH is the escape hatch.** The custom handler exists to *"bypass page
   guard and some values like `KUSER_SHARED_DATA`, `CPUID`,
   `HYPERVISOR_SHARED_DATA`, `PEB`"* — i.e. it fabricates the values the
   protected code reads to decide whether it is inside a hypervisor, a
   debugger, or a virtual machine.
4. **Imports are rebuilt at runtime** from the embedded `iat.hxx` (5.7 MB of
   import descriptors) with `GetProcAddress`/`LoadLibraryA`, and the **stack is
   fabricated** before the **OEP** is invoked — the classic manual-map tail.

### 2.1 Why "recompiled with asmjit fuck veh"

`inferred`: the vendor's protected segment was obfuscated with
**exception-driven control flow** — the real code path is expressed as
dispositions that are carried by structured exceptions, and the game's VEH
dispatches them. A debugger that swallows or rewrites those exceptions breaks
the program. The reviver replaces that mechanism with **JIT-compiled code
(asmjit)** so the same behaviour is reproduced *without* relying on the game's
VEH — hence the profane summary. The reviver still installs its own VEH, but
for emulation-emulation (bypassing page guards, spoofing `PEB`/`CPUID`), not
for control flow.

---

## 3. Why this is worth having

1. **It is a complete, readable protection design** — the closest public
   analogue to our own Neverlose findings (opaque-predicate obfuscation +
   encrypted `.enc1` section) but with the *loader side* published rather than
   inferred. Compare [`neverlose-mining.md`](neverlose-mining.md) §5 (the
   Neverlose protection section) with this note: two independent products chose
   *different* strategies — Neverlose obfuscates code, Plaguecheat validates
   its *execution environment*.
2. **The reviver is a map for future work.** If we ever analyse a protected CS2
   module, the artifact tells us exactly which artefacts to look for: a
   protected segment, an IAT table, an OEP, a snapshot file, a custom VEH, and
   environment reads of `PEB`/`CPUID`/`KUSER_SHARED_DATA`.
3. **The "snapshot" idea is directly relevant to our own loader hardening.**
   A cheap variant — record a hash of the environment and refuse to run if it
   changed — is what the protected code is doing in a much more expensive form.

---

## 4. Limits

- **No cheat features.** This is protection only. It tells us nothing about
  ragebot, ESP, or chams; the inventory row was corrected accordingly.
- **The trace emulator is not here.** We see the *check being satisfied*, not
  the *check*. The environment-spoofing VEH is the interesting part and it is
  only described by a comment. `unknown`
- **Provenance:** the README implies a community reverse-engineering project
  ("recompiled … fuck veh"), not a vendor leak. `inferred`
- **Not executed, not built.** Static reading only; the code was not compiled or
  run, and the embedded `iat.hxx`/`network.hxx` tables were not parsed.

---

## 5. Cross-references

- [`cheat-dump-inventory.md`](cheat-dump-inventory.md) §2.4 (the corrected
  classification), §8 (handling rules).
- [`neverlose-mining.md`](neverlose-mining.md) — our own protected-target notes.
- [`cs2-chams-t7.md`](cs2-chams-t7.md) — the CS2 feature-side answer.
