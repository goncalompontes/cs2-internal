# T22 — the integrity check: a post-link binder and a run-time self-check

**Goal:** a **patched** copy of the payload must fail **loudly** instead of
quietly. The check compares the image that is running in the game with the
image the build produced, and refuses to install when they differ.

This is **anti-tamper, not anti-reversal**. It does not make the payload
harder to read; it makes it **not modifiable without being noticed**. The
survey's ranking is right about the value and right about the limit
(`docs/research/protect-techniques-survey.md` §0, item 5, and §7.6 item 5):
"a checker does not hide anything. Its value is that a *tampered* payload
fails loudly instead of misbehaving".

The expensive half is **not** the run-time check. It is the **post-link
binder**: the expected value cannot be known before the link, because the
bytes that are hashed are produced by the backend, not by the pass. A check
whose expected value is computed **before** codegen is a **fake check** — it
"would look like protection while providing none" (the survey §7.4, quoting
kagura's `bbcheck` lesson). **This task ships a binder.** A pass that only
marks sites is not T22; it is the defect the survey warns about.

Read `docs/research/protect-techniques-survey.md` §7 in full before starting.
Read `docs/orchestration/plan-protect.md` §1.1, §1.2 and §4 for the ranking
and the constraints.

---

## 1. What this task delivers, and what it does not

| | |
|---|---|
| **Delivers** | the pass's **block** (a small, read-only structure the binder fills), the **run-time check** emitted as IR (freestanding: no import, no CRT, no TLS, no global constructor), the **post-link binder** (`tools/protect/bind_integrity.py`), the **payload check** that re-reads the image and proves the bound value is the hash of the image, the payload's **refusal path** (the payload does not install when the check fails), the **Meson wiring**, the tests, and the README |
| **Does not deliver** | any hiding, any anti-debug, any anti-dump, a *continuous* check (the check runs **once**, at attach), a check that survives an attacker who reads it (§6), the injector's per-injection nonce (§4.2), the strings' protection (T21), the virtualisation (T20) |

The check is **on by default in every protected build**. There is no policy
switch: a protected build that looks protected and is not is the failure mode
this project exists to avoid, and a knob that turns the check off is a knob
somebody will turn off.

## 2. The rules this task must not break

Everything in `plan-protect.md` §4 applies. In particular:

- The payload is **manually mapped**. The check must not bake an **absolute
  address** into the image, and the block must not hold a pointer the mapper
  would have to fix up in a way the binder cannot see (§4.4).
- The pass must not introduce **TLS** (the injector sets one TLS block, on
  the entry thread only — `plan-protect.md` §4).
- The pass must not introduce **new imports** (the injector resolves by name
  and fails the whole injection if a module is missing —
  `injector/src/main.cpp:807-818`).
- **No global of the image is initialised at run time.** The plan states the
  rule as "the CRT startup never runs"; §3.6 measures the built image and
  finds the *mechanism* different (a CRT startup **is** linked into the image
  and **is** entered) but the *conclusion* the same: the user module's
  `.CRT$XCU` table is never walked, so a global with a dynamic initialiser is
  still zero in the mapped image. The check's state is therefore a **plain
  zero-initialised global** in `.data`, and the block is a **constant**
  global in `.rdata`.
- The run-time code the pass injects must be **freestanding**: no `new`, no
  `std::vector`, no CRT, no exception handling, no unwind table it cannot
  justify.
- The pass runs at **`registerOptimizerLastEPCallback`** and must define
  **`isRequired() { return true; }`**. The **LTO pre-link** phase fires the
  same extension point: a module must not be transformed twice (T19's
  per-phase module flags are the model; reuse them).
- Errors are **values**, not exceptions: a pass must never throw out into the
  driver, which is built without exceptions.
- **Every 64-bit constant of the check lives in a global and is read with a
  volatile load.** The X86 backend of this LLVM mis-materialises a 64-bit
  immediate in the shape the transform emits (`tools/protect/README.md` §10.2
  and §12) — the check would hash with a *different* constant and never
  match.

## 3. What the payload knows about its own image — the measurements

Everything in this section was measured on the built image
`build-payload-merged/payload.dll` (2,444,800 bytes on disk, built
2026-10-21 23:50) with `llvm-readobj` 22.1.8 and with the probes in
`C:\Users\gpmpo\cs2-build\t22-probe\` (the numbers are reproducible from the
image).

### 3.1 The base

The injector calls the payload's entry point as
`entry(base, DLL_PROCESS_ATTACH, 0)` — the stub pushes the mapped base into
`rcx` and the reason `1` into `edx` (`injector/src/main.cpp:605-612`), and
the entry is `remote_base + image.entry_point_rva`
(`injector/src/main.cpp:881`). The payload's `DllMain` receives it as
`HINSTANCE` and publishes it in `cs2_protect_instance_base`
(`payload/src/payload_main.cpp:795-804`).

**So the payload knows its own mapped base, with no import and no TLS.** That
value is the *address* of the range the check hashes; it is not a key (§4.2).

### 3.2 The headers and the section table are mapped

`map_sections` copies `min(SizeOfHeaders, file size)` bytes to the base of
the view (`payload/loader/src/mapping.cpp:24-25`) and then copies every
section's raw bytes to its virtual address
(`payload/loader/src/mapping.cpp:27-39`). The whole image is allocated
`PAGE_EXECUTE_READWRITE` (`injector/src/main.cpp:780-781`).

**The payload is a fully mapped PE image in the target**: the DOS header, the
NT headers, the data directories and the section table are readable as data
at `hinst`. In the built image `SizeOfHeaders` is `0x400` and the section
table ends at `0x2C0`, so the table is inside the mapped headers.

### 3.3 What the mapper changes between the file and the target

| what | the file | the target | source |
|---|---|---|---|
| the headers | as linked | **identical** | `mapping.cpp:24-25` |
| every section's raw bytes | as linked | **identical** | `mapping.cpp:27-39` |
| the base relocations | link-time VAs | **fixed up** (DIR64) | `mapping.cpp:43-74` |
| the IAT | the ILT's hint/name RVAs | **the target's real addresses** | `injector/src/main.cpp:820` |

The last row is the trap the survey §7.3 names: **the IAT in the target does
not match the IAT in the file**. The injector overwrites every import slot
with the address it resolved in the target process
(`std::memcpy(bytes + symbol.iat_rva, &value, sizeof(value))`,
`injector/src/main.cpp:820`). A check over the IAT would therefore compare
two different things. **The check does not touch the IAT** (§4.1).

### 3.4 The relocation census — the measurement that decides the design

The base relocation table of the built image holds **13,053 entries, all of
them `IMAGE_REL_BASED_DIR64`**, and they fall in exactly two sections:

| section | RVA | `SizeOfRawData` | DIR64 relocations |
|---|---|---|---|
| `.text` | `0x1000` | `0x1A0E00` (1,707,520) | **0** |
| `.rdata` | `0x1A2000` | `0xA2E00` (667,136) | 13,042 |
| `.data` | `0x245000` | `0x600` (1,536) | 11 |
| `.pdata` | `0x339000` | `0x9E00` | 0 |
| `.reloc` | `0x343000` | `0x6A00` | 0 |

The import address table (`RVA 0x2277C0`, `0x5A0` bytes, 14 import
descriptors) holds **0** relocations: its entries are RVAs, not addresses.

**This confirms the survey's §7.2 requirement on this image: the code
section contains no relocation, so the bytes of `.text` in the target are
bit-identical to the bytes of `.text` in the file.** A hash computed over
`.text` in the file and a hash computed over `.text` in the process are the
same number.

The binder does **not** trust that measurement: it parses `.reloc` and
**refuses** any range that a fixup touches (§4.8). The census is the
evidence that the design is feasible; the refusal is the guarantee.

### 3.5 The payload's PE parser cannot be used on the mapped image

`cs2::loader::pe::parse_image` translates an RVA to a file offset with
`section.raw_offset + (rva - section.virtual_address)`
(`payload/loader/src/pe.cpp:55-76`). That is the **file** layout. On a
mapped image the offset of an RVA *is* the RVA. The header of the parser
claims it parses "either a file image or an already-mapped image"
(`payload/loader/include/cs2/loader/pe.hpp:86-87`); **that claim is not
true of the implementation**, and this task must not rely on it.

The check therefore does **not** parse the PE at run time: the *range* is
carried in the block, and the binder writes it (§4.4). The payload's parser
stays a file parser ✓.

### 3.6 The CRT startup is in the image — and it does not run the user module's initialisers

The PE entry point of the built image is `0x1A0DF0`, and the PDB's public
symbol table names it `_DllMainCRTStartup`: the **static** CRT's startup is
linked into the image (the symbols `__scrt_initialize_crt`,
`__scrt_dllmain_uninitialize_c`, `_initterm`, `_initterm_e` are in it). The
payload's own `DllMain` is at `0x6AF0` (the public `DllMain`), and it is
reached from that startup.

So the plan's phrasing — "the CRT startup never runs" (§4 of the plan) — is
**wrong about the mechanism**: a startup *is* entered. But the *conclusion*
the plan draws from it holds, and that is what matters here: the image
defines `__xc_a` (`0x2270A0`) and `__xc_z` (`0x2270B8`), the boundaries of
the user module's `.CRT$XCU` table — and **no code in the image references
them** (the whole `.text` was disassembled and searched for those two
addresses: no reference). The user module's C++ dynamic initialisers are
**never run**.

**Therefore a global with a dynamic initialiser is still zero in the mapped
image**, and the check's state may be a plain zero-initialised global ✓.

## 4. The design decisions

### 4.1 What is checked

**Two ranges, both in `.text`, both located by the binder from the image's
own metadata:**

| # | the range | how the binder finds it | what it buys |
|---|---|---|---|
| **0** | **the whole `.text` section**: `[VirtualAddress, VirtualAddress + SizeOfRawData)` | the section table of the file | every byte of the payload's code: the protected functions, the hooks, the dispatch code, **and the check's own code** |
| **1** | **the first 32 bytes of the entry point** (`AddressOfEntryPoint`) | the optional header of the file | a *precise* diagnosis of the most likely single-target patch (the boot path) |

Range 0 is a **superset** of range 1: range 1 adds no coverage, it adds a
**diagnosis** ("the boot path was patched" versus "the code was patched").
The operator gets the file back from whoever patched it and diffs it; the
check says *whether* it was patched and *roughly where*.

**What is deliberately not checked:**

- **The IAT.** The injector rewrites it (`injector/src/main.cpp:820`); the
  file's IAT and the target's IAT are different numbers (§3.3). Hashing it
  would be a permanent false positive.
- **The import *name* table.** It is stable (the ILT is 1,328 bytes, the
  names 2,658 bytes, measured) and it could be hashed, but the import names
  are not a tampering target: patching a name does not lift a feature, and
  the code that *calls* the import is in `.text`, which range 0 covers.
- **The whole image.** `.rdata` holds 13,042 relocated pointers: the image's
  data **cannot** be compared between the file and the target without
  replaying the fixups. The code can.
- **`.data`.** It is writable at run time and the payload writes to it; a
  hash of it would fail for reasons that have nothing to do with tampering.

**The check is over the bytes as they are in the process**, which is the
survey's §7.1 point: it detects a `0xCC` software breakpoint and an inline
patch, which are the two things an analyst actually does first.

### 4.2 Where the expected value comes from

**From the post-link binder, and from nowhere else.**

The value the run-time code compares against is `hash(range bytes, seed)`,
computed by `tools/protect/bind_integrity.py` **over the linked image, after
the link**. The binder writes it into the block (§4.4). The build then
*fails* if the binder did not run (§4.8).

**The seed** is the build's seed (`Config::seed` / `module_seed()`,
`tools/protect/src/config.hpp:71`), so two builds of the same source have
two different expected values. The seed is in the image (the run-time code
needs it), which is a limit, not a secret (§6).

**The mapped image base cannot be part of the comparison.** The plan §1.2's
correction applies here verbatim: the expected value is computed at **build**
time, so it may only be keyed by a value that is known at build time. The
base is per-injection and known only at run time, so mixing it into the
comparison would make the check fail **every** time. The base is used as the
**address** of the range, and for nothing else.

> This corrects one sentence of the plan (§1.1): "a runtime-derived key that
> is *verified* needs the same post-link binding machinery". The *machinery*
> is indeed the same, but a **runtime-derived key cannot take part in a
> verification** whose expected value is bound at build time: it can only
> take part in a **decryption** (T21's case), where the plaintext is
> produced at run time. An integrity comparison has no such freedom.

**The kagura rule.** The expected value is **never** computed by the pass.
The pass emits `expected = 0` and the binder fills it in. A pass that
computed a "checksum" over the IR, or over anything that is not in the final
image, would ship exactly the fake check the survey §7.4 warns about.

### 4.3 The hash

A 64-bit, non-cryptographic, **word-wise** hash, fixed by this
specification, implemented **twice** — once in Python (the binder, the
payload check) and once in IR (the run-time check) — and the two must agree
bit for bit.

```
mix(x):                                   # splitmix64's finaliser
    x ^= x >> 30
    x *= 0xBF58476D1CE4E5B9
    x ^= x >> 27
    x *= 0x94D049BB133111EB
    x ^= x >> 31
    return x

hash(bytes, seed):
    h = seed
    for each 8-byte little-endian word w of bytes:      # the tail is zero-padded
        h = mix(h ^ w)
    return h
```

Properties that matter:

- **Serial dependency.** `h` depends on the previous `h`, so the loop
  **cannot be vectorised** by the optimiser: the order of the combination is
  fixed, and the Python and the IR implementations compute the same number.
  A hash whose combination order the compiler may change is a hash whose
  expected value is a coin flip.
- **The constants live in a global** and are read with **volatile loads**
  (§2). The X86 backend's 64-bit-immediate bug would otherwise change the
  constant and the check would never match.
- **The length and the range come from the block** with volatile loads, so
  the optimiser cannot fold the loop into "hash nothing" (the T19 lesson,
  `tools/protect/README.md` §10.3). The range's fields are `0` in the
  linked image and are filled in by the binder — the run-time code reads
  them from memory, not from the image's constant pool.
- The hash is **not cryptographic** (the survey §7.1: "the v1 hash is not
  cryptographic"). It detects the *unaware* patch; §6 says what it does not
  detect.

**Measured agreement** (the two implementations, the same bytes, the same
seed `0x0123456789ABCDEF`):

| range | the reference implementation (C++, `/O2`) | the Python implementation |
|---|---|---|
| the whole `.text` (1,707,520 bytes) | `0xd03aa061a10c17d0` | `0xd03aa061a10c17d0` |
| the entry point's first 32 bytes | `0x13918bc027af2462` | `0x13918bc027af2462` |

The reference implementation is the program in
`C:\Users\gpmpo\cs2-build\t22-probe\hashcost.cpp`: it is the algorithm the
IR must emit, written out once in C++ so that the numbers can be checked
against the Python side. **The pass emits no C++ copy of the hash** — the
algorithm exists twice in the shipped tree, in the IR and in Python — so
§8's cross-implementation test compiles and *runs* the emitted IR and
compares it with Python. That test is the guard against the two drifting.

### 4.4 The block — the contract between the pass, the binder and the run time

One global, emitted by the pass into **the module that owns `DllMain`**,
`private constant`, in `.rdata`. The binder finds it by its tag and fills
three fields of each range.

```
struct IgBlock {                       # 8-byte aligned; the offsets are a contract
    u8     tag[8];                     # 0x00  "CS2IGv1\0"      <- the binder's anchor
    u32    range_count;                # 0x08  2
    u32    reserved;                   # 0x0C  0
    u64    seed;                       # 0x10  the build seed
    u64    nonce;                      # 0x18  0 (reserved; see §4.2)
    struct {                           # 0x20  range_count entries, 16 bytes each
        u32 rva;                       #        the binder writes it
        u32 length;                    #        the binder writes it
        u64 expected;                  #        the binder writes it
    } ranges[range_count];
};
```

- The **tag** is how the binder finds the block without guessing (the
  survey §7.2: "the pass emits them as a named section or as a well-known
  global, so the binder can find them without guessing"). A byte search over
  the image for `CS2IGv1\0` costs a few milliseconds and is unambiguous
  (the binder **fails** if it finds 0 or more than 1 occurrence).
- The tag is a **fixed** string, so it is greppable. That is a limit, not an
  oversight (§6). A per-build tag would remove the greppable signature but
  would make the binder depend on the coverage directory for the tag; the
  tag is fixed and the limit is stated.
- The **seed** is in the block because the run-time code needs it and the
  binder needs it to recompute the hash.
- The **nonce** is reserved and **zero**. The run-time code compares
  `actual == (expected ^ nonce)`; with `nonce = 0` that is
  `actual == expected`. The field exists so that a future injector-supplied
  per-injection value (the survey §4.4's stronger design) has a home — but
  see §4.2: a per-injection value **cannot** strengthen a comparison whose
  expected value is bound at build time, so the field stays zero and is
  documented as reserved.
- The block holds **no pointer** and **no absolute address** (§2): the
  `rva` is an offset from the base the run-time code already has, and the
  mapper does not touch the block's fields (they are plain integers — they
  need no DIR64 fixup, and the census of §3.4 shows that the only relocated
  slots in `.rdata` are the 13,042 pointers).
- The **state** (which range failed, the actual and the expected value) is
  **not** in the block: the block is read-only and the state is written at
  run time. The state is a **separate, externally linked, zero-initialised
  global** in `.data`, defined by the payload and declared by the pass:

  ```cpp
  // payload/src/payload_main.cpp
  extern "C" struct Cs2ProtectIntegrityResult {
      std::uint32_t state;    // 0 = ok, 1 = failed
      std::uint32_t range;    // the range that failed (0 = .text, 1 = the entry point)
      std::uint64_t actual;
      std::uint64_t expected;
  } cs2_protect_integrity_result = {0, 0, 0, 0};
  ```

  The pass declares the same layout in IR (`{ i32, i32, i64, i64 }`) and
  writes it. The definition is in the payload so that the **unprotected**
  build links too (§4.6).

### 4.5 The run-time check

Emitted by the pass as IR into the `DllMain` module, freestanding:

```
cs2.protect.ig.check(ptr %base) -> i32:
    %blk    = load volatile ptr, ptr @cs2.protect.ig.block
    %count  = load volatile i32, ptr (%blk + 8)
    %seed   = load volatile i64, ptr (%blk + 16)
    %nonce  = load volatile i64, ptr (%blk + 24)
    ; for each range:
    ;     %rva / %length / %expected are volatile loads out of the block
    ;     if %rva == 0 or %length == 0 -> the range is UNBOUND -> fail
    ;     %actual = hash(%base + %rva, %length, %seed)
    ;     if %actual != (%expected ^ %nonce) -> record and fail
    ; write the result into @cs2_protect_integrity_result
```

Rules for the emitted code:

- **Every load out of the block is volatile.** The block's fields are `0` in
  the linked image; a non-volatile load would let the optimiser fold the
  whole check into "compare 0 with 0" and **delete it** — the image would
  then be byte-identical to an unprotected one, and the check would be a
  lie. The IR test asserts that the volatile loads are still there (the
  T19 precedent, `tools/protect/README.md` §10.3).
- **The hash's constants are volatile loads** out of a `private constant`
  global (§2).
- **The check writes to an external global**, so the optimiser cannot
  delete it: the write is observable.
- The check is **one function** with a **small loop over the block's range
  array** (two iterations) — the shape is small on purpose: the check is a
  few hundred bytes of code, and a big check is a big target.
- **No allocation, no CRT, no import, no TLS, no unwind table it cannot
  justify.** The function is a leaf that only reads memory and calls
  nothing.

### 4.6 Where the check runs, and what it does when it fails

**Where:** in the payload's `DllMain`, at `DLL_PROCESS_ATTACH`, **once**,
**before** `boot()`. The pass finds the module's `DllMain` (the symbol is
unmangled in the image — the PDB's public table names it `DllMain` at
`0x6AF0`) and injects the call at the entry of its entry block, before the
existing code.

The pass's failure modes are **loud**:

- The pass does **nothing** in a module that has no `DllMain` — most modules
  do not. It reports that it did nothing, and it touches no IR.
- The **global** invariant — that exactly one module of the build owns the
  block — is enforced by the **binder**: it fails the build if the image
  holds **no** block, or more than one (§4.8). A protected build in which
  the check could not be installed is a build that looks protected and is
  not, and it fails at the binder, not at the injection.

**What happens on a mismatch:**

1. The check writes `state = 1`, the failing range, the actual value and
   the expected value into `cs2_protect_integrity_result`.
2. The payload's `DllMain` sees `state != 0` and **refuses to install**:
   it writes **one line** to the payload's log
   (`%LOCALAPPDATA%\cs2-internal\cs2-payload.log`, `payload/game/src/log.cpp:66-104`)
   and **returns without calling `boot()`** — no install thread, no hooks,
   no features, no overlay. The game is left exactly as it was.
3. It **does not crash, and it does not kill the game.** A crash in the
   game is a **worse** outcome than a refusal: the operator loses the
   session, and a fault in the game's process is the most visible thing
   that can happen to it. A refusal is quiet, reversible and diagnosable.
   The survey's §7.5 alternative — the mismatch becomes a *wrong result* —
   is **rejected**: a wrong aim is a silent failure, which is exactly what
   this task exists to prevent.

The log line is the operator's channel. The control channel (the named
pipe) is **not** available: it starts in `boot()`, which the refusal skips.

**The check runs once.** It is a **start-up** check, not a watchdog: a patch
made in the *running* process after the attach is **not** detected (§6).

### 4.7 The pass

New: `tools/protect/src/integrity.hpp/.cpp`. It runs in the transform phase
(`registerOptimizerLastEPCallback`, `tools/protect/src/plugin.cpp:353-355`),
**after** the T19 transform (the transform phase's order matters — the
handoff §5 records that the virtualizer's analysis must run before the call
encryption; the integrity pass does not interact with the calls, but it
must see the final IR of `DllMain`).

It does four things and nothing else:

1. finds `DllMain` in the module; a module that has none is left
   **untouched** (and that is reported);
2. emits the block (`private constant`, the tag, the seed, two empty
   ranges);
3. emits the check function and injects its call into `DllMain`'s entry
   block;
4. reports: the number of ranges, the seed, the tag, and the fact that the
   expected values are **unbound** (the binder's job).

The pass **never** computes a hash of anything (§4.2).

### 4.8 The binder

New: `tools/protect/bind_integrity.py`. It runs **after the link** and
**before** the payload check.

```
1. read the image
2. find the block: search the file for the tag "CS2IGv1\0"
   - 0 occurrences  -> FAIL ("the integrity block is not in the image:
                          the pass did not run, or the image is not the
                          protected one")
   - >1 occurrences -> FAIL ("the integrity block is ambiguous")
3. read the seed out of the block
4. determine the ranges:
   - range 0: the section named ".text" -> (VirtualAddress, SizeOfRawData)
   - range 1: (AddressOfEntryPoint, 32)
   - a range that is not file-backed, or that is empty -> FAIL
5. for each range:
   - check that no DIR64 fixup of the image overlaps it
     (parse the base relocation directory; a fixup that overlaps the range
     -> FAIL: "the range contains a relocation")
   - check that the range does not overlap the IAT
     ("the injector rewrites the IAT"; a range that overlaps it -> FAIL)
   - compute hash(file bytes of the range, seed)
6. write range_rva, range_length and expected into the block, in the file
7. write the image back, preserving its modification time (§4.9)
```

The binder **refuses** rather than guesses. Every refusal is a **build
failure** with a named reason, in the style of the existing checks:

| refusal | why |
|---|---|
| `no_block` | the pass did not run, or the image is not the protected one |
| `ambiguous_block` | more than one block in the image |
| `not_file_backed` | the range has no bytes in the file |
| `range_overlaps_relocation` | the mapper would change the bytes of the range |
| `range_overlaps_iat` | the injector would change the bytes of the range |
| `unbound` | the range's `rva`/`length` could not be determined |

The binder is **idempotent**: it recomputes and rewrites the block on every
run, so a build that runs it twice ends with the same image.

### 4.9 The build wiring

The payload's build is Meson (`payload/meson.build`). The existing pattern
is a `custom_target` that depends on the library and runs a Python check
(`payload/meson.build:279-287` and `:298-308`). The binder is the same
shape, with one addition: **it writes to the linked image**.

```
custom_target('protect-integrity-bind',
  input : payload_lib,
  output : 'protect-integrity-bind.stamp',
  command : [protect_bind, '--image', payload_lib.full_path(), '--output', '@OUTPUT@'],
  depends : [payload_lib],
  build_always_stale : true,
  build_by_default : true,
  console : true,
)
```

and the payload check (`protect-payload-check`) **depends on the binder**,
so the order is: link → bind → check.

**The image is patched in place**, so that the artifact the operator injects
is still `build-payload\payload.dll` and the workflow does not change. To
keep Ninja from re-linking the payload on every build (Ninja re-runs an
edge whose output was modified after the build), the binder **restores the
image's modification time** after it writes it. The binder is the only thing
that touches the image, and the check verifies the result, so the mtime
restoration is safe.

**If the binder does not run, the build fails.** The payload check asserts
that the block exists, that it is bound (the `rva`/`length` are not zero)
and that the expected value equals the hash of the range it names. A build
that skipped the binder therefore fails at the check, not at the injection
(the survey §7.6 item 1: "the binder and the image disagree → the payload
misbehaves"; the ordering must make that impossible, and the check makes
it visible).

### 4.10 The report and the coverage

The transform report gains one line per module that owns a block:

```
cs2-protect: integrity: 2 ranges, seed 0x…, unbound (the binder binds them)
```

The coverage gains an `integ` line:

```
integ ranges=2 seed=0x… tag=435332494731 start=…
```

The coverage's format stays **backward compatible**: an older coverage file
has no `integ` line, and a reader that does not know it still reads the
file (`tools/protect/protect_checks.py`'s reader is a chain of `if/elif` on
the line's key — an unknown key is ignored ✓). `check_policy.py` and
`check_payload.py` must keep working.

## 5. The cost — measured

| what | cost | how it was measured |
|---|---|---|
| the **run-time** check, whole `.text` (1,707,520 bytes) | **0.703 ms** best, **0.765 ms** worst (20 runs) | the C++ implementation of §4.3, `/O2`, `clang-cl` 22.1.8, on this machine |
| the run-time check, the entry point's 32 bytes | **< 1 µs** | the same program |
| the **binder**, per build | **256 ms** (of which the hash is **167 ms** and the parse of the image **121 ms**) | `hashpy.py` and `probe.py` in `C:\Users\gpmpo\cs2-build\t22-probe\`, Python 3.11 |
| the **payload check**, per build | the same 167 ms of hashing, on top of the checks that already run | the check recomputes the hash with the Python implementation |
| the **image** | 40 bytes of block + a few hundred bytes of code in `.text` | — |
| the **build** | one extra Python run per build (a quarter of a second) | — |

The run-time cost is paid **once per injection**, on the injector's remote
thread, before anything is installed. It is not on the per-frame path, and
it is not paid per call of a protected function (the survey's §7.6 item 3:
"a 32-byte hash per call on the per-frame path is a real cost" — this design
does not have that cost at all).

## 6. What it does not buy

Stated plainly, because a checker that is oversold is worse than no
checker:

- **It is anti-tamper, not anti-reversal.** It does not make the code
  harder to read, and it does not hide the check.
- **It is bypassed by anyone who patches the check.** The block carries the
  tag, the seed, the ranges and the expected value; an analyst who finds it
  (one `grep` for `CS2IG`) can recompute the expected value for a patched
  image and write it in. The check stops the *unaware* tamperer, not the one
  who reads it. Against a debugger it is one `jz`.
- **It is a start-up check, not a watchdog.** A patch applied to the
  *running* process after the attach is not detected.
- **The hash is not cryptographic.** A patch that is *crafted* to preserve
  the hash is not detected. A random one-byte change changes all 64 bits.
- **It does not cover the data.** `.rdata` holds 13,042 relocated pointers;
  the file's data and the target's data are different numbers, and no
  hash of the file's data can be compared with the target's without
  replaying the fixups. Only the code is covered.
- **It does not protect the DLL on disk.** The file is still a file; an
  anti-cheat that hashes it still sees a DLL (T24's problem, not this
  one's).
- **It does not hide the check's presence.** The block's tag is a fixed
  string in `.rdata`; the check is a few hundred bytes of code in `.text`.
- **A refusal is not a crash.** The operator's session survives; the
  features are simply absent, and the log says why.

## 7. The failure modes

1. **The binder and the image disagree** (the binder ran before the link, or
   the image was relinked afterwards) → the payload refuses to install and
   says so in the log. The Meson ordering (link → bind → check) makes this
   impossible, and the payload check makes it visible.
2. **A range contains a relocation** → the bytes in the process differ from
   the bytes in the file → a permanent mismatch → a permanent refusal. The
   binder refuses such a range (and §3.4 measures that `.text` has none).
3. **The two hash implementations disagree** (a change to one and not the
   other) → a permanent refusal, with the actual and the expected values in
   the log. The cross-implementation test (§8) is the guard.
4. **The optimiser folds the check away** → the image would be
   byte-identical to an unprotected one and the check would be a lie. The
   volatile loads and the write to an external global are the barriers, and
   the IR test asserts that they are still there.
5. **The injector rewrites a byte of the range** → the same as a tamper. The
   IAT is excluded for exactly this reason (§4.1).
6. **The payload refuses to install for a reason that is not a tamper** (a
   broken build) → the operator sees a dead payload and a log line, and the
   build's own check fails first.

## 8. The tests

Three levels, and **each one must be shown to be able to fail** (break the
code on purpose, watch the test fail, revert it — and say so in the report).

1. **The unit test** (`tests/protect_tests.cpp`, the `cs2-protect-tests`
   binary). It builds IR with the `IRBuilder` and runs the integrity pass
   over it, then asserts:
   - the block is emitted with the tag, the seed and two ranges;
   - the ranges' `rva`/`length`/`expected` are **zero** (the pass does not
     bind anything — the kagura rule, asserted);
   - the check function exists and is called from `DllMain`'s entry block,
     **before** the original instructions;
   - the loads out of the block are **volatile** (the fold-away guard);
   - the hash's constants are volatile loads out of a global;
   - a module **without** a `DllMain` is left **untouched** (and that is
     reported, not silently skipped).
2. **The IR test** (a new `tests/integrity_ir_test.py`, a CTest entry). It
   runs `opt.exe -load-pass-plugin=<dll> -passes=cs2-protect` over a
   checked-in fixture that has a `DllMain`, and asserts on the emitted IR:
   the block, the injected call, the volatile loads, and that the hash's
   loop has a **serial dependency** (the `mix` chain) so that it cannot be
   vectorised. It must also exercise the **production path** (a fixture
   compiled with `clang-cl -fpass-plugin=<dll> -emit-llvm -S`), because
   `opt` does not invoke the extension points (T19's review made this
   point).
3. **The binder, the hash and the tamper tests** (a new
   `tests/integrity_bind_test.py`):
   - the **cross-implementation test**: the hash **the pass emits as IR** is
     compiled (`clang-cl -emit-llvm -S` → an object) and **executed** over a
     known buffer, and the result is compared with the Python
     implementation's. The numbers in §4.3 are the reference; this test is
     what keeps the two implementations from drifting, and it is the only
     way to know that the *IR* agrees with the binder (the pass has no C++
     copy of the hash — the algorithm exists twice: in the IR and in
     Python);
   - the **binder refuses**: a range that overlaps a relocation, a missing
     block, an ambiguous block → each is a *named* failure;
   - the **tamper test**: take the built image, **flip one byte of
     `.text`**, and assert that the check now fails — the bound value no
     longer matches. This is the test that proves the check *can* fail, and
     it is the one that matters most.
4. **The existing tests must stay green**: `protect-tests`, `protect-ir-test`,
   `protect-diff-test`, `protect-coverage-test`, the string tests, the VM
   tests, and the payload build with its `check_policy.py` /
   `check_payload.py`.

**Skip with CTest's skip code when the plugin has not been built**, so a
machine without the LLVM SDK still has a green suite (T19 does this).

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`): the pass
  (`src/integrity.hpp/.cpp`), the binder (`bind_integrity.py`), the
  additions to `check_payload.py` and `check_policy.py`, the tests, the
  README.
- `payload/src/payload_main.cpp`: the result global, the refusal, the log
  line.
- `payload/meson.build`: the binder target and the ordering.

Do **not** touch: `injector/**` (the nonce is out of scope, §4.2), the root
`meson.build`, `payload/meson_options.txt`, `overlay/**`, `AGENTS.md`,
`docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

## 10. The verify loop

```powershell
cd <your worktree>

# the host (must stay green)
meson compile -C build
meson test -C build

# the plugin
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build --output-on-failure

# the protected payload (the image must carry the check, and the binder
# must have bound it)
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='C:\Users\gpmpo\cs2-internal\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect"

# the binder, on its own, against the linked image (it refuses, loudly)
python tools/protect/bind_integrity.py --image build-payload-protect\payload.dll
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay`
or `build-injector`; use a fresh directory name.

Note that `CS2_PROTECT_CONFIG` must be set for **both** the configure and
the compile step: the plugin reads it in the compiler process.

**The tamper check, by hand** (the acceptance criterion that matters):

```powershell
# a copy of the image with one byte of .text flipped
copy build-payload-protect\payload.dll $env:TEMP\tampered.dll
# … flip a byte in .text …
python tools/protect/check_payload.py --protected $env:TEMP\tampered.dll `
    --objdump <llvm-objdump.exe> --coverage <dir> --policy $policy
# it must FAIL: the bound value does not match the range any more
```

## 11. Acceptance criteria

1. The **expected value is bound after the link**, by
   `tools/protect/bind_integrity.py`, and the pass **never** computes a hash
   (the kagura rule, asserted by a test).
2. The check covers **the whole `.text` section and the entry point's first
   32 bytes**, both located by the binder from the image's metadata, and
   **no** range that a DIR64 fixup or the IAT touches (the binder refuses
   such a range; §3.4 measures that `.text` has none).
3. The two hash implementations agree, and the agreement is **measured**
   (§4.3) and **tested** (§8).
4. The check runs **once**, at `DLL_PROCESS_ATTACH`, **before** `boot()`.
5. On a mismatch the payload **refuses to install** and **logs** one line
   with the actual and the expected value — and **does not crash** and does
   not kill the game.
6. The binder is **part of the build**, after the link and before the
   payload check; a build in which the binder did not run **fails**.
7. The payload check proves the bound value: it re-reads the image,
   recomputes the hash and compares. A **tampered** copy of the image
   **fails** the check (§8, item 3).
8. The plugin's existing tests are green, the host suite is green, and the
   protected payload still builds with an unchanged import and export table
   and `check_payload.py` ending in `ok`.
9. The **cost** is the measured one (§5): 0.703 ms at run time, 256 ms per
   build, 40 bytes of block.
10. `tools/protect/README.md` documents: the model, the block's layout, the
    hash, the binder's refusals, the cost, and **what the check does not
    buy** (§6 of this document, nearly verbatim).
11. Nothing outside §9 was touched.

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

What changed per file. The hash you settled on and why. The block's layout.
The ranges, and the measurements that made them possible (the relocation
census of §3.4 and the cross-implementation agreement of §4.3). The exact
tests, and how each was proven to be able to fail. The measured cost. The
refusals the binder implements. What you could not verify — in particular
whether the payload actually refuses to install in the game, which needs a
live injection this repository cannot do.
