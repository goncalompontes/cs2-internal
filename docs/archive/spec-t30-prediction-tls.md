# T30 — the prediction's copy and the TLS defect

## The problem

`payload/control/src/input.cpp` declared the prediction's copy of the engine's
movement services as a `thread_local`:

```cpp
thread_local std::array<std::byte, k_movement_services_size> svc_copy;   // 0xFE0 bytes
```

The payload is **manually mapped**, so the Windows loader never runs for it
and never allocates the per-thread TLS block a `thread_local` resolves
against:

* the injector reserves a slot in the target (`TlsAlloc`) and writes the
  index into the image's `__tls_index`
  (`injector/src/main.cpp`, `setup_tls`);
* the entry stub installs the per-thread block **on the entry thread only**
  (`make_entry_stub`);
* `cs2::tls::ensure_thread_tls()` (`payload/tls/src/bootstrap.cpp`) installs
  one by hand — but it **refuses** an index that is not below 64, and the
  live build's index is **154** (`docs/re/camera-split.md`).

A `thread_local` access on a game's thread therefore dereferences a block
pointer that was never written. In the camera's guard that faulted (the
`read addr=0x4` crash, fixed in T27). In the prediction it fails **quietly**:
the fault-safe reader answers "false", the copy does not happen, and the
prediction is off with no error anywhere.

## The task

1. **Confirm the diagnosis** with evidence: the image's TLS index, the
   disassembly of the access, `ensure_thread_tls`'s refusal. Correct the
   diagnosis if it is wrong.
2. **Replace the buffer** with something correct on a manually mapped image:
   no `thread_local`, no per-thread state, no CRT static init. The buffer's
   lifetime is one prediction step — a local or a per-instance buffer; if it
   is shared, it needs the same protection as the rest of the prediction's
   state.
3. **Sweep the tree**: every `thread_local` in the payload is a potential
   silent failure. List them all, say whether each is on a game's thread, and
   fix the ones that are (or report them precisely if they are not the
   task's files).
4. **The test**: the prediction's logic must stay host-testable and the test
   must be able to fail. A source-level check over the repository's own
   sources is acceptable if it can fail (the camera's guard has one).
5. **Document it**: a dated section in `docs/re/prediction.md` with the
   diagnosis, the proof, the change and what is still unverified.

## The rules

* `docs/style-guide.md` and `AGENTS.md` are normative: C++23, 2-space
  indent, 100 columns, K&R braces, trailing return types on declarations
  **and** definitions, fully qualified `std::`, errors as values, no
  exceptions, `[[nodiscard]]` / `noexcept`, value-init with `{}`,
  `static_cast` / `reinterpret_cast` only, no new global mutable state
  (ADR-0010).
* The payload is manually mapped: no CRT static-init, no `<print>`, no
  exceptions, **no `thread_local` in a detour**.
* ADR-0012: no hard-coded RVAs.
* Every engine read goes through the fault-safe `game::read` helpers.

## The files

Owned: `payload/control/**` and `docs/re/prediction.md` (append only).

Not owned: `payload/features/**` (another task is under review there), the
chams, the animfix's files beyond what the prediction needs, and the dumper.
If something outside the owned list is needed, say so in the report instead of
editing it.

## What the report must state

The diagnosis (with the proof), the change and why it is correct on a
manually mapped image, the sweep of every `thread_local` and its fate, the
test and the proof that it can fail, what is still open (the live run), and
the file list with the commit hashes.
