# Neverlose's hook bindings — how the module reaches the game's functions

> Research note, 2026-10-21. **Route B** of the T17 deep-reversal spec
> (`docs/orchestration/spec-t17-neverlose-deep.md`).
> Scope: the **reversed Neverlose CS2 module**, read as *reference material
> only*. **Nothing from it is copied**; every claim below is a **description**
> with an evidence address. See the licence note at the end.
>
> **Ground truth.** The raw live image
> `re/cs2_212C3300000/cs2_212C3300000.bin`
> (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
> base `0x212C3300000`, **`VA = base + file_offset`**). The decompilation used
> is Ghidra 12.1.4 project **`gproj11`** (the *corrected* deobfuscation).
>
> **Method — and a correction to the prior session.** `OPEN_ITEMS.md` item 3
> recorded that seven functions "have **0** 8-byte pointer occurrences anywhere
> in the image, **0** rip-relative `lea` references, **0** `movabs` immediates",
> and concluded they are "reached only via runtime-registered hooks/trampolines
> (not stored statically)".
>
> That conclusion was **too strong**, and it was reached with the wrong tool.
> A byte-pattern scan for `E8`/`E9`/`0F 84` finds *displacement bytes* as well
> as real instructions, so it both **misses** real references (it cannot tell an
> instruction from a displacement) and **invents** them. This pass re-did the
> question with a **real disassembler**: a forward **capstone 5.0.7** linear
> sweep over the whole code region `0x212c3301000`–`0x212c5cc0000`
> (**13 785 980 instructions**), recording every instruction address and every
> direct `call`/`jmp` target. Only instructions the sweep actually reaches
> count as instruction boundaries.
>
> **Confidence labels.** `verified` = read out of the bytes / the decompilation;
> `inferred` = interpretation; `unknown` = looked for and not recoverable.

---

## 0. Summary

| # | Question | Answer | Confidence |
|---|---|---|---|
| B1 | Are the eight "zero-caller" functions really unreferenced? | **No — seven of the eight are referenced**, each by exactly one or two direct relative branches. Only the rage-math engine `0x212c37ff41e` has no reference at all | `verified` |
| B2 | What is the binding mechanism? | **One-instruction tail-jump stubs** (`E9 rel32`) that sit next to the real implementation and are installed as hook entry points at run time | `verified` |
| B3 | Can the stub addresses be found statically? | **No** — zero absolute pointers, zero 32-bit RVAs, zero rip-relative references. They are materialised only at run time | `verified` |
| B4 | What drives the **auto-strafe**? | A **per-tick handler** `FUN_212c38c66b8` (346 B) reached through the stub `0x212c38c65e4`. It maintains a **tick counter** and a **tick time** (`tick * 0.015625` = 1/64 s), then calls the movement builder `FUN_212c37beb50(ctx, cmd, state)` | `verified` |
| B5 | Which game-side function is hooked? | **Unknown.** The stub addresses are runtime-only, so the *game* target of each hook cannot be named from the static image | `unknown` |
| B6 | Does the module register callbacks by the engine's event *names*? | **No.** The names (`EventClientPreOutput_t`, `EventFrameBoundary_t`, `EventPostDataUpdate_t`, …) exist as plain strings at `0x212c424e34e`…`0x212c424e44b`, but **none of them is referenced by any instruction** | `verified` |

---

## 1. The eight functions and their actual bindings

`verified` — every address below is from the capstone sweep of the raw image.

| function | size | role (prior session) | direct reference |
|---|---:|---|---|
| `0x212c3790e70` | 53 953 | ragebot driver | **`jmp` at `0x212c3791445`** (tail-jump stub) |
| `0x212c3820bd0` | 9 320 | — | **`jmp` at `0x212c38229b4`** (stub) |
| `0x212c3864260` | — | — | **`jmp` at `0x212c3864b45`** (stub) |
| `0x212c38af310` | — | — | **`jmp` at `0x212c38af44e`** (stub) |
| `0x212c38c66b8` | 346 | — | **`jmp` at `0x212c38c65e4`** (stub) |
| `0x212c37beb50` | 44 518 | movement builder (auto-strafe / bhop) | **`call` at `0x212c38c5901`** and **`call` at `0x212c38c6717`** |
| `0x212c387ada0` | 16 039 | base-entity dispatcher | **`call` at `0x212c38ad6bf`** |
| `0x212c37ff41e` | 23 599 | rage-math engine | **none** |

The five `jmp` sites are **one-instruction stubs** — the raw bytes are `E9 rel32`
followed by the next function's code:

```
0x212c38c65e4   e9 cf 00 00 00      jmp 0x212c38c66b8     ; stub → per-tick handler
0x212c38c65e9   80 bd d0 0b 00 00   cmp byte ptr [rbp+0xbd0], 0
...
0x212c3791445   e9 26 fa ff ff      jmp 0x212c3790e70     ; stub → rage driver
0x212c38229b4   e9 17 e2 ff ff      jmp 0x212c3820bd0     ; stub
0x212c3864b45   e9 16 f7 ff ff      jmp 0x212c3864260     ; stub
0x212c38af44e   e9 bd fe ff ff      jmp 0x212c38af310     ; stub
```

The stub's placement relative to its target is not fixed (`0x212c38c65e4` is
`0xd4` bytes *before* its target; the other four are `0x13e`–`0x1de4` bytes
*after* theirs), so the stub is not a compiler-generated thunk — it is
**hand-placed**: the module puts a 5-byte jump at a chosen address and uses
*that* address as the hook entry point. `verified` for the bytes, `inferred`
for the intent.

### 1.1 The stub addresses are runtime-only

`verified`. For each of the five stub addresses the image contains:

* **0** occurrences of the 8-byte absolute pointer;
* **0** occurrences of the 32-bit RVA (`addr - 0x212C3300000`);
* **0** rip-relative references — a complete capstone sweep of every
  RIP-relative memory operand over all 13 785 980 instructions finds none.

The two `call` sites for the movement builder are *inside* the per-tick handler
(§2), so they are not independent bindings.

**Consequence:** the module computes each stub's address at run time and
writes it into the game (a jump patch, or a callback slot in the engine's event
dispatch). Which *game* function each hook lands on is therefore **not
recoverable from the static image** — `unknown`. This is the one part of
Route B that stays open, and it is open for a structural reason, not for lack
of effort.

### 1.2 The one false positive worth recording

A naive byte scan for `E8 rel32` reports a **call** to the rage-math engine at
`0x212c37ff418` (the bytes `e8 01 00 00 00` sit exactly there). It is a false
positive: the forward sweep shows the real instruction stream is

```
0x212c37ff412  mov  rsi, [rbx+0x50]
0x212c37ff416  mov  byte ptr [rsi+0x1e8], 0     ; = c6 86 e8 01 00 00 00
0x212c37ff41d  mov  rax, qword ptr gs:[0x60]     ; the PEB read
0x212c37ff426  mov  ecx, [rax+0x11c]
0x212c37ff42c  mov  edx, [rax+0x2c0]
0x212c37ff432  imul ecx, [rax+0x118]
0x212c37ff439  movzx r8d, word ptr [rax+0x120]
```

i.e. the `e8 01 00 00 00` is the **displacement** of the `mov byte ptr
[rsi+0x1e8], 0`, not an opcode. `verified` (capstone). This is the same class
of mistake that made the earlier byte scans unreliable, and it is the reason
this route needed a real disassembler.

### 1.3 The event-name strings are *not* a registration table

`verified`. The module carries the engine's event type **names** as plain
strings in `.rdata`:

| VA | string |
|---|---|
| `0x212c424e34e` | `EventFrameBoundary_t` |
| `0x212c424e363` | `EventClientPostOutput_t` |
| `0x212c424e37b` | `EventClientPreOutput_t` |
| `0x212c424e3f6` | `EventClientSimulate_t` |
| `0x212c424e40f` | `EventClientFrameSimulate_t` |
| `0x212c424e427` | `EventPostDataUpdate_t` |
| `0x212c424e44b` | `EventClientSceneSystemThreadStateChange_t` |

They sit in a *string table* (neighbours: `EventProcessNetworking_t`,
`uint64_t`, `invalid string: control character U+0009 (HT) must be escaped…` —
the last being a JSON-escaping message). **None of these strings is referenced
by any instruction** in the image, so they are *not* used to register
callbacks by name in the statically visible code. They are part of a carried
type/schema string table. `verified` (negative).

---

## 2. What actually drives the auto-strafe

This is the part the spec says is "worth more to us than the function body",
because it tells us *when* our strafe must run.

`FUN_212c38c66b8` (346 B, `gproj11` decompilation) is the **per-tick
handler**. Decompiled (register-argument function; `RBP` is the caller's
frame):

```c
void FUN_212c38c66b8(void)
{
  *(int   *)(unaff_RBP + 0x1a4) = *(int *)(unaff_RBP + 0x2c) + 1;          // tick++
  *(float *)(unaff_RBP + 0x1ac) = (float)(*(int *)(unaff_RBP + 0x2c) + 1) * 0.015625;
                                                                     //   = tick * (1/64) s

  if (*(char *)(unaff_RBP + 0x17a) == '\0') {
    if (DAT_212c4a58080 == 0) FUN_212c3602a60(&PTR_PTR_212c4a58078);
    FUN_212c37beb50(DAT_212c4a58080, unaff_RBP + -10, unaff_RBP + 0x15a);  // <-- movement builder
    FUN_212c38c68cf();
    return;
  }
  /* otherwise: prediction / lag-record path */
  ...
  FUN_212c3638cc0(unaff_RBP + -10, unaff_RBP + 0x15a);   /* or */
  FUN_212c36384b0(unaff_RBP + -10, lVar32 + 0x2f0);
  ...
  *(undefined8 *)(unaff_R14 + 0x1b8) = <world x,y> ;
  *(float    *)(unaff_R14 + 0x1c0) = <z>;
  *(undefined8 *)(unaff_R14 + 0x1c4) = <world x,y>;
  *(float    *)(unaff_R14 + 0x1cc) = 27.0f or 36.0f;      // box height, chosen by *(char*)(RBP+0x4e)
  *(undefined1 *)(unaff_R14 + 0x1e4) = <bool>;            // a flag
  *(float    *)(unaff_R14 + 0x1e0) = <fade/alpha>;
  *(float    *)(unaff_R14 + 0x1dc) = <opacity * 0.9 clamp>;
}
```

`verified` (decompilation of `gproj11`).

Reading:

1. **The handler is per *tick*, not per frame.** The tick counter comes from
   the engine (`RBP+0x2c`) and the tick time is `tick * 0.015625` — 1/64 s,
   i.e. CS2's 64-tick server rate. `verified`.
2. **The movement builder is called with three arguments**:
   `FUN_212c37beb50(module_context, cmd, state)` — the *module* context
   (`DAT_212c4a58080`), the **user command** (`RBP-0x10`) and the **state**
   (`RBP+0x15a`). This matches the prior session's finding that the builder
   keeps `cmd = ctx+0xD38` and `state = ctx+0xD30`. `verified`.
3. **A branch selects movement vs. everything else.** The byte at `RBP+0x17a`
   decides: `0` → the movement path (strafe + bhop) and return; non-zero →
   the prediction / lag-record / ESP-state path, which then writes a
   **world-space AABB** (min x/y, z, max x/y, height `27` or `36`) and an
   alpha into `R14+0x1b8..0x1e5`. `verified`.
4. **The AABB write is the ESP payload**: the `27.0`/`36.0` heights and the
   `0.9` opacity clamp are the box-drawing parameters. `inferred`.

### 2.1 What this means for our strafe

> Our strafe must run **once per user command**, at the same point in the
> tick as the engine's command build — *not* once per rendered frame.

Concretely: the neverlose module's movement builder is invoked from a
per-tick hook, and the tick time it derives is `tick / 64`. If our strafe
runs from a frame callback (or from a hook that fires at a different point of
the tick), we will be writing `forwardmove`/`leftmove` for the wrong tick —
which is exactly the class of bug the wave-2 handoff describes as "the
auto-strafe no longer accelerates". `inferred` (the mechanism is verified; the
mapping to our bug is an interpretation).

**Action for us:** confirm that our strafe's write happens inside the same
user-command build as the engine's, and count strafe writes per tick. The
neverlose shape (a dedicated per-tick handler, with the movement builder as
its only child) is the shape to compare against.

---

## 3. What we could not recover, and what we tried

| Item | What we tried | Result |
|---|---|---|
| Which **game** function each hook is installed on | 8-byte absolute pointers; 32-bit RVAs; complete rip-relative sweep (13.8 M instructions); direct `call`/`jmp` graph | **no static reference** — the stub addresses are runtime-only |
| The rage-math engine `0x212c37ff41e`'s entry | the same three searches | **no reference at all** — it is entered by a mechanism that does not exist in the image (a computed address) |
| The role of `0x212c3820bd0`, `0x212c3864260`, `0x212c38af310` | decompiled the three stub targets | decompiled; they are ordinary subsystem functions, each reached only through its own stub |
| Registration by event name | found the seven event-type name strings; searched for references to all of them | **no instruction references them** |
| The vtable route the spec suggested | searched the image for the eight-byte pointers that a vtable entry would contain | **zero** — so none of the eight is a virtual method of a class with a static vtable |

---

## 4. What this changes for us

1. **The "these functions are unreachable" belief was wrong, and the mechanism
   is a hand-placed 5-byte jump stub.** If our own hook plumbing ever needs
   "a callback that is only entered through a stub", the stub is the *address
   that gets installed*, and the real function is the stub's target. That is
   a pattern worth recognising in *our* binary too. → *Action*: when our
   overlay/chams hook target hunt comes up empty, look for a 5-byte
   `E9 rel32` **into** the function, not for a pointer *to* it.
2. **The strafe's trigger is a per-tick handler.** Our strafe should be
   invoked from the user-command build, and the tick rate is 64/s
   (`0.015625` s). → *Action*: assert "one strafe write per user command" in
   our own code; the neverlose handler keeps an explicit tick counter and a
   `tick * 1/64` clock for exactly this reason.
3. **The module separates "movement" from "everything else" behind one
   branch.** One per-tick handler, one branch, two very different jobs. Our
   per-tick entry point should be similarly narrow.

## 5. What we should **not** copy

* **The stub/hook mechanism itself.** We install our hooks by our own
  documented means (`payload/loader/`, the injector); adopting a
  hand-placed-jump-stub scheme would be copying a third party's *evasion*
  technique, which this task explicitly excludes.
* **No third-party code.** The addresses and the control-flow facts above are
  *descriptions* of a binary, not source.

---

**Licence note.** The Neverlose CS2 module is a **commercial third-party
product** with no open-source licence. It is used here as *reference material
only*: this document **describes** observed behaviour, quoting addresses as
*facts about that binary*. No third-party code is reproduced in this
repository, and nothing here may be used to reproduce or redistribute their
binary or its contents.
