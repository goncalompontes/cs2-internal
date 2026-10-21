# T24 — the shipped DLL is not signaturable: the seed is mandatory, and it drives the protection's own bytes and the layout

**Goal:** the file on disk must not be a signature. Two builds of the payload
with two different seeds must not share their `.text`, and the build must
**fail** when it is given no seed at all.

This is the task the plan lists as **T24** (`docs/orchestration/plan-protect.md`
§2: *"the shipped DLL is not signaturable: the build seed is mandatory and
drives the layout, the keys and the tables"*). The survey's claim that it
needs doing is right; the survey's description of what the seed does today is
incomplete, and §1 below is the measurement that corrects it.

**Everything in §1 was measured on this machine, on 2026-10-21, on the tree at
`a5329fd`.** The numbers are reproducible with the commands in §1.6. Nothing
in §1 is estimated. Where a number is *not* measured, §1 says so.

---

## 1. The measurement: what differs between two builds today

### 1.1 The six images

Six builds of the payload, in fresh directories **outside** the repository
(`C:\Users\gpmpo\cs2-build\t24-probe\`), with the build directory names of the
same length (`a1`, `b1`, `c1`, `d1`, `e1`, `f1`) so that the embedded PDB path
has the same length in every image — a PDB path of a different length shifts
the layout of the image and makes the comparison meaningless
(`tools/protect/README.md` §13).

| build | what | size | sha256 of `payload.dll` |
|---|---|---|---|
| `e1` | **unprotected** (`-Dprotect` off) | 2,442,240 | `5A4DD0E1E588DB6825443678C48EE43864B41D64D13B0FB59EC0CB3AD9C9AD25` |
| `a1` | protected, policy seed `0x1111111111111111` | 2,444,800 | `45756B57E824EE249291EA985E478EBA65F547D0A7E6C0BF84D053924F4DAEF4` |
| `b1` | protected, policy seed `0x2222222222222222` | 2,444,800 | `2FFF3964895166071F1E231E22E8CDCE5B189F9AE2F3AFD9B275875AED0C93F3` |
| `f1` | protected, policy seed `0x1111111111111111` **again** | 2,444,800 | — (see §1.3) |
| `c1` | protected, **no seed** | 2,444,800 | `16854B2428C709E688CE59151F728B4C115188D2D846B4389BA9C8F10649C465` |
| `d1` | protected, **no seed** again | 2,444,800 | — (see §1.3) |

The policy is the shipped one (`tools/protect/policy.local.json`: the six
`cs2::aim::*` functions, `cs2::loader::self_unmap` denied), copied into the
probe directory with only the `seed` line changed. The plugin is
`tools/protect/build/Cs2Protect.dll`, built with `scripts/build-protect-plugin.py`.

**The calibration did not move during any of the six builds**: the SHA-256 of
`calibration/offsets.json` was `863851CBFD79AF412F59237556730E3FF0A77C9B1005454953C2CDE92AD66C84`
before and after every one of them. The base relocation count is therefore
usable as a tell, and it did not move either: **13,092 DIR64 entries in every
protected image**, 13,066 in the unprotected one (the protection adds 26).

> **Corrected (2026-10-22, by the implementer).** The **counts** above are
> stale: they were measured on the tree at `a5329fd`, and the tree has moved
> since (T21 landed, and it adds the strings' globals and their
> relocations). The implementer's own builds measure **13,053** in the
> protected image and **13,029** in the unprotected one — the protection adds
> **24**, not 26. The *shape* of the finding is unaffected (the count is
> still the tell that an input moved), and the per-section table of §1.2
> **reproduced exactly** on the probe's images. The implementer reported the
> disagreement before doing anything else, which is the behaviour this
> project wants; the numbers are corrected here so that the next reader
> does not chase a 39-entry difference that is a moved tree, not a defect.

### 1.2 The decisive table

The comparison is the repository's own definition of "the same image":
`tools/protect/compare_builds.py --ignore-link-stamps` normalises the PE
timestamp, the debug directory's own stamp and the CodeView GUID, age and PDB
path (`tools/protect/compare_builds.py:116-171`). The per-section numbers
below are the same comparison, per section.

| section | bytes | **seed A vs seed B** | **no seed vs no seed** | **seed A vs seed A again** | unprotected vs protected |
|---|---|---|---|---|---|
| `.text` | 1,707,008 | **0 differ — identical** | 0 | 0 | 517,933 differ |
| `.rdata` | 667,648 | **441 differ** | 0 | 0 | 107,427 differ |
| `.data` | 1,536 | 0 | 0 | 0 | 0 |
| `.pdata` | 40,448 | 0 | 0 | 0 | 11,530 differ |
| `.reloc` | 27,136 | 0 | 0 | 0 | 371 differ |
| outside every section (the PE headers) | 1,024 | 0 | 0 | 0 | 23 differ |
| **the whole file** | **2,444,800** | **441 differ** (454 before the link stamps are normalised) | **0** (11) | **0** (13) | 1,051,865 differ |

The five sections are the image's whole section list: `llvm-readobj --sections`
on `a1/payload.dll` reports `.text`, `.rdata`, `.data`, `.pdata`, `.reloc` and
nothing else. **There is no `.tls` section and no `.idata` section** in this
image (the imports live in `.rdata`), and the **TLS data directory is empty
too** (`TLSTableRVA = 0`). The plan's statement that the payload carries three
`thread_local`s and a 4 KB `.tls` section is **not true of this build**
(`docs/orchestration/plan-protect.md` §4) — the image it was written about is
not the image this tree builds.

### 1.3 The answers

**Does the `.text` differ at all between two builds with different seeds?**
**No. Not one byte.** The SHA-256 of the `.text` of `a1` and of `b1` is the
same (`2759104ea506c578…`). The `.text` is 1,707,008 bytes — 69.8 % of the
file — and it is *the same file* in both builds.

> **The decisive answer: a scanner that hashed only the `.text` would match
> across builds.** So would a scanner that hashed the whole file *minus* the
> linker's per-link stamps: 441 of 2,444,800 bytes differ, 0.018 % of the
> image.

**What is identical between two builds with different seeds?**

| what | measured |
|---|---|
| the **function order** | identical — the `.text` is byte-identical, so the order of the functions in the image is the order in `a1` |
| the **instruction bytes** | identical — the same statement |
| the **string literals** | identical: the two images hold the same 30,282 printable strings (14,953 distinct); the only differences are the embedded PDB path and the CodeView GUID |
| the **import table** | identical — 180 lines of `llvm-readobj --coff-imports`, byte for byte |
| the **relocations** | identical — the `.reloc` section is byte-identical (the *entries* are at the same addresses with the same types) |
| the **unwind table** | identical — `.pdata` is byte-identical, 3,350 entries in both |
| the **data section** | identical — `.data` is byte-identical |
| the **export table** | there is none: `ExportTableRVA = 0` in both images, and in the unprotected one too |

**What does differ?** Exactly 441 bytes, all in `.rdata`, and all of them are the
plugin's own globals. The coverage files name the four modules that carry a
table (`aim/src/solver.cpp` table=7 descriptor=11, `aim/src/damage.cpp`
table=13 descriptor=21, `aim/src/field.cpp` table=2 descriptor=2,
`aim/src/geometry.cpp` table=2 descriptor=3), and the layout of each module's
globals in `.rdata` is `[table][descriptor][key0][key1][key2][mixmul][mixxor]`.
Predicting the descriptor from the coverage (`index ^ mix64(k0^k1^k2)`) finds
it exactly, and the differing ranges are exactly those blocks:

| what | bytes | where |
|---|---|---|
| the four **tables** (the permuted callee pointers) | 24 × 8 = 192 | 49 — only the bytes of the pointers that actually moved |
| the four **descriptors** (the encrypted indices) | 37 × 8 = 296 | all of them |
| the four modules' **three key constants** | 4 × 24 = 96 | all of them |
| the **mixer's two constants** (`0x9E3779B97F4A7C15`, `0xBF58476D1CE4E5B9`) | 4 × 16 = 64 | **none of them — they are identical in every build**, at the same four addresses (`0x221270`, `0x223390`, `0x225cb8`, `0x225e60`) |

584 bytes of the image are seed-dependent; 441 of them actually differ
(the rest are the high bytes of the pointers, which did not move).

**And the two "same seed" cases.** The seed **fully determines the image**:

* two builds with **the same seed** (`a1` and `f1`) differ in **0 bytes** after
  the link stamps are normalised out (13 before);
* two builds with **no seed** (`c1` and `d1`) differ in **0 bytes** after the
  link stamps are normalised out (11 before).

That is the fallback working as designed: `module_seed()`
(`tools/protect/src/cipher.cpp:52-63`) derives the base from
`derive_seed(module_id, "cs2.protect")` when there is no seed, and
`derive_seed` is an FNV-1a over the module id and a string
(`tools/protect/src/selection.cpp:318-320`) — **the module id is the source
file's name** (`tools/protect/src/transform.cpp:318`), so the fallback is a
constant of the payload, the same in every build and in every version.

> **So the unseeded build is not merely "the same build with a different
> seed": it is a *constant file*.** Every release built without a seed ships
> the same 441 bytes of `.rdata` and the same 1.7 MB of `.text`.

**One more thing the measurement shows, and it is not about the seed at all:**
the whole-file hash was **never** a signature. Two links of the *same* build
already differ (the PE timestamp, the CodeView GUID, the embedded PDB path).
The signature that matters is a **pattern** one, and the pattern that survives
every build today is not the code — it is the **mixer's two constants**: 16
bytes at fixed addresses in `.rdata`, in every build, in every version. §4.2
is about them.

### 1.4 What the linker decides, measured

Two further measurements, because the plan's alternatives have to be costed
against facts and not against the literature:

1. **The layout of the image is decided by the order of the inputs on the
   link line.** The payload is linked with `lld-link` (the ninja rule:
   `lld-link /MACHINE:x64 /OUT:payload.dll …`), with the static libraries in a
   fixed order (`payload/meson.build:257-265`). Relinking the *same* objects
   with the libraries **reversed** produced an image of the **same size**
   (2,444,800), with the **same** number of functions (3,350), the **same**
   import table (180 identical lines) and **no** export table — in which
   **1,411,585 of the 1,707,008 `.text` bytes differ (82.7 %)**. The layout
   is a lever, and it is a big one.
2. **A plugin cannot change the order of the inputs, but it can change the
   order of the functions inside a translation unit.** The chain
   *module's function list → the object's COMDAT sections → the image's
   layout* was measured end to end on a 200-function translation unit:
   * the same bodies with the **names** permuted produce a **byte-identical**
     image — the layout follows the *definition* order, not the name, so
     **renaming a symbol per build changes nothing in the image** (only the
     PDB's names);
   * the same functions **reordered** produce a different image: the code
     blocks move;
   * and the step that matters was measured directly: taking one `.ll`,
     reversing its `define` blocks (which *is* the module's function list
     order) and compiling it produces an object whose COMDAT sections are in
     the reversed order, and an image whose `.text` differs. A pass that
     splices the module's function list therefore moves the code in the image.

> **Correction to the plan's framing.** `docs/orchestration/plan-protect.md`
> §2 offers "the plugin could rename the internal symbols per build" as a way
> to permute the image. Measured: **renaming does not move anything.** The
> linker sorts nothing and follows the *definition* order; the lever is
> **reordering the module's function list**, not renaming it. The good news is
> that the reorder **keeps every name**, so nothing else in the tree has to
> change.

### 1.5 What a per-build "filler" costs, measured

A per-build filler function (the "junk code" candidate) was measured on this
machine:

* an **unreferenced** filler is **dead-stripped by the linker**: the image was
  byte-identical with and without it. It has to be kept alive explicitly
  (`llvm.used` → `/INCLUDE:`), and the plugin has to do that;
* with the keep-alive, a filler of a 15-store body cost **160 bytes of
  `.text`** and **0 bytes of `.pdata`** (a leaf function with no frame needs no
  `RUNTIME_FUNCTION` entry — the image's `.pdata` held the same two entries
  with and without it) and **0 bytes of `.rdata`** (no `.xdata` for a leaf
  function);
* a filler **hides nothing**: a decompiler removes it, and the analyst is
  left with exactly the same code. It buys a byte pattern and nothing else.

### 1.6 How to reproduce the measurement

```powershell
$repo='C:\Users\gpmpo\cs2-internal'
$probe='C:\Users\gpmpo\cs2-build\t24-probe'
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$plugin = python $repo\scripts\build-protect-plugin.py   # prints the DLL's path

# one protected build, with a seed in the policy file
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$probe\policy-seedA.json && meson setup $probe\a1 $repo\payload -Dprotect=true -Dprotect_plugin=$plugin -Dprotect_reference=$probe\e1\payload.dll"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$probe\policy-seedA.json && meson compile -C $probe\a1"

# the same, with the other seed / with no seed, into a same-length directory
# (policy-seedB.json, policy-noseed.json)

# the comparison
python $repo\tools\protect\compare_builds.py --ignore-link-stamps $probe\a1\payload.dll $probe\b1\payload.dll
```

The per-section numbers of §1.2 were taken with the same normalisation
(`compare_builds.py`'s `link_stamps()` and `normalise()`) applied per section;
the helper that does it in one go is §6's `--section` option, which this task
adds.

**The calibration must not move between the builds.** Its SHA-256 was checked
before and after every one of the six builds and did not change; the base
relocation count (13,092) is the tell, and it did not move either.

---

## 2. The scope of this task

**In scope**

- the **mandatory seed**: the plugin refuses to run without one, and the
  payload's wiring refuses to configure without one;
- the **per-build constants** of the protection: the mixer's multiplier, its
  xor constant and its rotation amount become derived from the seed, so the
  protection leaves **no fixed bytes** in the image;
- the **per-build layout**: the pass permutes the functions of each module,
  seeded, so the `.text` of two builds with different seeds differs;
- the report, the coverage and the checks that go with them;
- the acceptance measurement: a helper that compares two images **per
  section**, and the procedure that builds the payload twice with two seeds
  and runs it.

**Out of scope**

- the strings (T21's line is closed — `docs/orchestration/plan-protect.md`
  §1.2, the corrected note);
- the import table and the export table (they must not change: §3);
- the virtualizer (T20b/T20c are deferred — `plan-protect.md` §1.3);
- the cross-TU routing (T23a — it is in flight and this task must not change
  its rules);
- anything that hides the algorithm. **This task does not make the payload
  harder to read.** It makes the *file* different from build to build.

## 3. The rules this task must not break

- **The export table must not change.** The payload has **no export table**
  (measured: `ExportTableRVA = 0` in the protected and the unprotected image);
  `check_payload.py` compares the two images' exports
  (`tools/protect/check_payload.py:88-116`) and must keep doing so. The
  injector finds the entry point in the PE header
  (`injector/src/main.cpp:881`: `image.entry_point_rva`), never by name.
- **The import table must not change.** The mapper resolves the imports by
  name (`docs/orchestration/plan-protect.md` §4); the 180 import lines must
  stay exactly as they are. Nothing this task adds may import anything.
- **The image must stay a valid manually mapped PE.** The mapper applies the
  DIR64 and HIGHLOW fixups (`payload/loader/src/mapping.cpp:43-74`) and
  registers the x64 unwind table. No relocatable value may end up inside a
  cipher; no absolute address may be baked into the image; the `.pdata`/
  `.xdata` pair must stay well formed.
- **No TLS, no new global initialiser, nothing that needs the CRT.** The image
  is never initialised: the map is done by the injector and the CRT startup
  never runs, so any runtime code the pass injects must be freestanding.
- **The plugin's configuration channel is the environment** (`README.md` §4):
  `CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`, `CS2_PROTECT_VERBOSE`,
  `CS2_PROTECT_COVERAGE`. The policy's `seed` field is the second source.
- The pass runs at `registerOptimizerLastEPCallback` with
  `isRequired() { return true; }`, and the two module flags
  (`cs2.protect.selected`, `cs2.protect.transformed`) keep their meaning.

## 4. The decisions

### 4.1 The seed is mandatory

**Decision.** The plugin **fails the build** when neither `CS2_PROTECT_SEED`
nor the policy's `seed` is present. The message names both sources and says
what the fallback would be.

**Why.** The fallback is not "a default", it is a **constant of the payload**:
`module_seed()` falls back to `derive_seed(module_id, "cs2.protect")` with
`module_id` = the source file's name (`cipher.cpp:52-63`,
`transform.cpp:318`), and `derive_seed` is an FNV-1a over that name and a
string (`selection.cpp:318-320`). §1.3 measured the consequence: **two builds
without a seed are byte-identical** (0 differing bytes after the link stamps).
A release that ships the fallback ships a file that every other release
without a seed also ships, byte for byte.

**What it buys.** The guarantee that no release ships the fallback. It changes
**no byte** of any image — it is a gate, not a transform. It is cheap: the
check belongs in `load_config()` (`config.cpp:381-431`), where the seed is
already resolved, and it is one unit test.

**The rejected alternative.** Leaving the seed optional and relying on the
release process to set it. Rejected: the failure is silent (the build is
green, the image is a constant), and this project has already paid for a
silent no-op once (the dispatch the optimiser folded away — `README.md` §10.3).

### 4.2 The protection's own constants become per-build

**Decision.** The mixer's **multiplier**, its **xor constant** and its
**rotation amount** are derived from the module's seed, like the key parts
already are.

**Why.** These are the protection's **only fixed bytes**. Measured (§1.3):
`0x9E3779B97F4A7C15` and `0xBF58476D1CE4E5B9` occur **four times each, at
the same four addresses, in every build**, and the rotation amount 31 is an
**immediate byte** (`rolq $0x1f, %rax`) at **37 places** — the 37 encrypted
dispatches the payload check counts. A scanner that knows the protection only
needs those 16 bytes to recognise every build of this payload; the layout
permutation of §4.3 does not touch them.

* The **multiplier** and the **xor constant** live in globals already
  (`tools/protect/src/transform.cpp:370-377`, `k_multiplier_name`,
  `k_xor_name`). Making them per-build is a change of *value*, not of shape:
  the constants stay in the data, never in the instruction stream (that is
  what §10.2 of the README is about — the X86 backend of this LLVM
  mis-materialises a 64-bit immediate).
* The **rotation amount** is an immediate, so seeding it changes the
  `.text`: **37 bytes** change, one per dispatch site (the backend folds the
  `shl`/`lshr`/`or` of the mixer into a single `rolq`, so the rotation is one
  immediate byte per site). The rotation must be in **[1, 63]**: a rotation
  of 0 would make the `lshr` by 64 undefined behaviour in the IR
  (`transform.cpp:409-410`).

**What it does not buy.** It hides nothing: the analyst reads the rotation out
of the instruction stream in one instruction, and the mixer out of the globals.
It buys **the absence of a signature**, which is what the task is for.

**The rejected alternative.** Leaving the mixer fixed and relying on the
layout permutation. Rejected: the mixer's constants are a *better* signature
than the layout, because they are 16 bytes at *known* addresses, and because
they survive every layout change.

**The coupling this creates, and it is not small.** `mix64()` is shared by the
call encryption and by the virtualizer's encoder
(`tools/protect/src/cipher.hpp:3-11`, `vm_encode.cpp:133-167`). The mixer
becomes a **per-build** function, so its rotation and its constants have to
travel with the module's seed. The virtualizer's encoder already takes the
module's seed (`vm_encode.cpp:206`), so it can derive them; the *interpreter*
(T20b, not written) will have to read the rotation and the constants out of
the image rather than assume them. **The spec for T20b has to say so**, and
this task has to leave a clean seam for it: the rotation and the constants
become **values the pass derives from the seed and writes into the image**,
and the contract is "the same value the pass encrypted with".

### 4.3 The layout is per-build: the pass permutes the functions of each module

**Decision.** At the end of the transform phase, the pass **permutes the
module's function list** with a permutation derived from the module's seed,
and the permutation is reported and recorded in the coverage.

**Why.** §1.4 measured both halves of this:

* the linker keeps the order of an object's COMDAT sections, and the object
  writer emits the functions in the module's function list order — so a pass
  that reorders the list **does** move the code in the image (measured end to
  end: the module's function list → the object's sections → the image's
  layout);
* renaming the symbols instead **does not** move anything (measured: the same
  bodies with the names permuted give a byte-identical image).

The permutation keeps **every name**, so the coverage, the report, the policy
matching and `check_payload.py` (which works on **addresses**, not names —
`tools/protect/check_payload.py:236-295`) all keep working untouched. That is
the whole reason this is the chosen mechanism.

**The reach, stated plainly.** The permutation is **inside** a translation
unit: it moves a function within its own unit's block of code, so it moves
the *offsets* of that unit's functions and the *displacements* of every
reference to them, and it moves **nothing** in the units the plugin does not
compile. Measured on this image: `payload/third_party/imgui` alone is
**43 % of the `.text`** (four objects: 17.9 %, 11.4 %, 9.8 %, 3.9 %) and the
payload's own units add more; the **MSVC STL's `vector_algorithms.obj`
(11.2 % of the `.text`) is not the payload's code and the plugin never sees
it**, so those functions keep their offsets. A signature that names an STL
function's offset survives this task. That is why §4.2 matters: the
*protection's* bytes are gone whatever the layout does.

**The magnitude on the real payload is NOT measured.** It cannot be: it needs
the implementation. The two measured proxies are the across-unit permutation
(the link order: **82.7 %** of the `.text` changes — an upper bound this task
does not reach) and the synthetic within-unit permutation (200 functions: the
code blocks move; in that fixture the functions had no internal references,
so only 199 bytes changed). **The acceptance criterion in §8 is therefore
written as a measured threshold, not as a promise.**

**What it does not hide.** The code is *identical*, only its position in the
file changes. An analyst reads the same functions. It buys the *absence of a
fixed-offset signature*, nothing else.

**The rejected alternatives, and why**

| alternative | measured | verdict |
|---|---|---|
| **rename the internal symbols per build** | the same bodies with the names permuted give a **byte-identical** image: the layout follows the definition order, not the name | **rejected on the measurement.** It changes the PDB's names and nothing else. |
| **reorder the objects on the link line** | reversing the libraries changes **82.7 %** of the `.text`, with the same size, the same function count and the same imports | **rejected as the mechanism, kept as the yardstick.** It needs the seed at *configure* time (`payload/meson.build` reads the environment at configure time already — `payload/meson.build:119-134`), it restructures the payload's build description, and a permutation can fail to link. The plugin's per-unit permutation needs neither. |
| **per-build junk code** | a filler must be kept alive or the linker strips it; it costs its own bytes; a decompiler removes it | **rejected.** It hides nothing, it costs image size, and the layout it buys is bought better by §4.3. |
| **seed the rotation only** | 37 bytes of the `.text` change; the offsets do not move | **kept as part of §4.2, not as a substitute for §4.3.** It defeats a hash, not an offset. |
| **a per-build *size* shift instead of a permutation** (a filler whose size is seeded, placed first in the unit, so that every unit after it shifts) | it moves the units the plugin does not compile too | **rejected, and it is the closest thing to the truth.** It costs image size for the same effect the permutation gives for free, and the shift is bounded while the permutation is not. It is named here so that a reviewer does not have to guess why it was not chosen. |

### 4.4 The seed must differ between two released builds

**Decision.** The policy's `seed` is the release's seed: a fresh 64-bit value
per release, recorded next to the release. The plugin cannot check it (it has
no memory of the previous build), so the *check* is procedural and the
*evidence* is the coverage file, which records the seed.

**Why.** §1.3 measured it: **two builds with the same seed are byte-identical**
(0 differing bytes after the link stamps). The seed is the whole of the
polymorphism; using it twice throws the task away.

**What the plugin can do about it.** The coverage file records the seed (it
already records the key parts and the mixer's constants —
`tools/protect/src/report.cpp:455-458`). Two coverage files of two releases
with the same seed are then visibly the same. That is evidence, not a gate.

## 5. What the task must deliver

1. **The mandatory seed.** `load_config()` fails when there is no seed; the
   message names `CS2_PROTECT_SEED` and the policy's `seed`.
2. **The per-build mixer.** The multiplier, the xor constant and the rotation
   are derived from the module's seed and written into the image; the decode
   uses the rotation the pass encrypted with. The rotation is in [1, 63].
3. **The per-build layout.** The pass permutes the module's function list with
   the module's seed, after the call encryption, and reports how many
   functions moved.
4. **The report and the coverage**: the permutation's seed and the number of
   moved functions are in the coverage (a new, **backward compatible**
   section, like the `vm` section of `README.md` §14); the report names the
   rotation and the mixer's constants it used.
5. **The checks**:
   * the payload check asserts that the mixer's constants in the image are the
     ones the coverage names (it already asserts the constants it knows about
     — `README.md` §11 — and it must now assert the *rotation* too, which is
     the only per-build value that is in the instruction stream);
   * the payload check asserts that the number of functions the pass reports
     having moved is **not zero** for every module that has more than one
     definition;
   * a **new** per-section comparison, so that the acceptance criterion of §8
     can be *measured* by the build (see §6).
6. **The tests**, each **proven able to fail** (§7).

## 6. The acceptance measurement, and the helper that performs it

`tools/protect/compare_builds.py` compares two images as a whole and
normalises the linker's per-link stamps. This task needs the same comparison
**per section**, because the acceptance criterion is about the `.text` alone
and the whole-file comparison would be satisfied by the 441 bytes of `.rdata`.

**Decision.** `compare_builds.py` grows a `--section <name>` option (and the
section list it prints becomes per section in every mode — the current
`--attribute` mode already prints the section sizes). It prints, for the named
section: its size in both images, the number of differing bytes, and whether
the two sections are identical. Exit code 0 when the two sections are
identical, 1 when they differ — the same sense as the whole-file mode.

The acceptance procedure (§8, criterion 4) is then: build the payload twice,
with two seeds, into two fresh directories **of the same name length** (the
PDB path shifts the layout otherwise — `README.md` §13), and run
`compare_builds.py --section .text A.dll B.dll`. **It must report that the
two `.text` sections differ.**

## 7. The tests, each proven able to fail

| test | what it asserts | how it is proven able to fail |
|---|---|---|
| the plugin's unit test: **no seed** | `load_config()` fails without `CS2_PROTECT_SEED` and without a policy `seed` | run it with the seed removed: the test must fail |
| the plugin's unit test: **the seed's precedence** | `CS2_PROTECT_SEED` wins over the policy's `seed` (`config.cpp:388-398`, `cipher.cpp:52-63`) | set both to different values and change the precedence: the test must fail |
| the plugin's unit test: **the mixer is per-build** | two module seeds give two different multipliers, xor constants and rotations, and the rotation is in [1, 63] | pin the rotation to 31 and the test must fail |
| the plugin's unit test: **the permutation is seeded** | the same seed gives the same permutation; two seeds give two different ones; the permutation is a permutation (no function is lost or duplicated) | change the seed and the test must fail |
| the IR test (`tests/ir_test.py`) | the rewritten IR has the **six volatile loads** of the decode, the rotation the pass used, and the module's function list in the permuted order | revert the permutation (or the rotation) and the test must fail |
| the payload check | the mixer's constants in the image are the ones the coverage names; the number of moved functions is not zero | patch one byte of the image and the check must fail |
| the new per-section comparison | `--section .text` reports "identical" for two copies of one image and "differ" for two images with different seeds | compare the image with itself (must be identical) and compare `a1` with `b1` (must differ) |

## 8. The acceptance criteria

1. **The build fails without a seed.** A protected build with neither
   `CS2_PROTECT_SEED` nor a policy `seed` fails, with a message that names
   both. The unprotected build is unaffected.
2. **The mixer's constants and rotation are per-build.** Two builds with
   different seeds hold different multipliers, xor constants and rotations, and
   the rotation in the image's instruction stream is the one the pass used.
   The **rotation is in [1, 63]** — a rotation of 0 fails the build.
3. **The `.text` differs between two builds with different seeds.** Measured
   with `compare_builds.py --section .text` (§6): the two `.text` sections
   differ. **The floor is 0.5 % of the section's bytes** (8,535 of
   1,707,008) — the permutation's magnitude on the real payload was **not
   measured** (§4.3), so this is a floor and not a prediction; but a floor
   below it would be satisfied by the 37 bytes of the rotation alone, which
   is not what this task is for. **The implementer reports the number it
   measured**, whatever it is.
4. **Two builds with the same seed are identical** apart from the linker's
   per-link stamps — the seed still fully determines the image, and §4.4's
   procedural rule is the only thing that keeps two releases apart.
5. **Two builds with no seed cannot be built at all** (criterion 1), so the
   constant-fallback image of §1.3 can no longer be shipped.
6. **The export table and the import table are unchanged** (the 180 import
   lines; the export table is empty and stays empty), the `.reloc` DIR64
   entries are intact, the unwind table is well formed, and the image is
   still a valid manually mapped PE: the mapper's DIR64 fixups
   (`payload/loader/src/mapping.cpp:43-74`) apply and the entry point is
   reachable from the PE header.
7. **The dispatch check still passes**: the 37 encrypted dispatches decode to
   a callee of the image, with the image's **own** key
   (`tools/protect/check_payload.py:236-295`). A change of the rotation or of
   the mixer's constants must not break it — and the check must *catch* it if
   it does.
8. **The coverage format change is backward compatible**: a coverage file of
   an older build is still read (the new section is optional, like the `vm`
   section — `README.md` §14).
9. **The host suite and the plugin's tests are green**, and each new test was
   **proven to be able to fail**.
10. `tools/protect/README.md` documents the new behaviour and **what is still
    visible**: the code (only its position changes), the string literals, the
    import table, the layout of the units the plugin does not compile (§4.3's
    reach), and the fact that a per-build key is not a secret (§10.2's
    wording).
11. Nothing outside §9 was touched — in particular **no file under
    `payload/src/**`**, and no change to `payload/meson.build` unless the
    implementer can argue for it from the measurements.

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md`

Do **not** touch: `payload/src/**`, `payload/include/**`, `payload/**/meson.build`,
`payload/meson_options.txt`, `overlay/**`, `injector/**`, the root
`meson.build`, `AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the
other `docs/orchestration/*.md`.

**`payload/meson.build` is the one exception the implementer may argue for**:
the wiring has to fail the *configure* step when there is no seed, in the same
way it fails when there is no policy (`payload/meson.build:126-134`). If the
implementer changes it, the change must be the smallest one that expresses
that rule, and it must be in the report.

## 10. The verify loop

```powershell
cd <your worktree>
meson compile -C build
meson test -C build
python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build --output-on-failure

$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='<your worktree>\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && set CS2_PROTECT_SEED=0x1111111111111111 && meson setup build-payload-protect-t24 payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && set CS2_PROTECT_SEED=0x1111111111111111 && meson compile -C build-payload-protect-t24"
```

Use a **fresh** build directory name; do **not** `meson setup --wipe` the other
build directories. `CS2_PROTECT_CONFIG` must be set for **both** the configure
and the compile step.

**The acceptance measurement needs a second build, and the two build
directories must have names of the same length** (the embedded PDB path is
the same length in both, or the layout shifts and the comparison is
meaningless — `README.md` §13):

```powershell
# the second build: a different seed, a same-length directory name
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && set CS2_PROTECT_SEED=0x2222222222222222 && meson setup build-payload-protect-t24b payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && set CS2_PROTECT_SEED=0x2222222222222222 && meson compile -C build-payload-protect-t24b"

python tools/protect/compare_builds.py --section .text `
    build-payload-protect-t24\payload.dll build-payload-protect-t24b\payload.dll
```

## 11. The report back

- what changed per file;
- the mandatory seed: where the check is, what it says, and the proof that
  the build fails without one;
- the mixer: how the multiplier, the xor constant and the rotation are
  derived, the range of the rotation, and the **measured** number of bytes of
  the image and of the `.text` that change because of them;
- the permutation: how it is derived, how many functions it moved per module,
  and the **measured** fraction of the `.text` that differs between the two
  seeded builds (criterion 3) — with the number, not an estimate;
- the acceptance measurement: the two builds, their directories, their seeds,
  and the output of `compare_builds.py --section .text`;
- the two images' import and export tables, the relocation count and the
  unwind table count;
- what is **still visible** after this task, stated plainly (the code, the
  strings, the imports, the units the plugin does not compile);
- what you could not verify.

## 12. The rules

- `docs/style-guide.md` is normative (C++23, trailing return types,
  `std::expected`, `[[nodiscard]]`, no globals, no `using namespace std;`).
- Errors are values, not exceptions; a pass must never throw out of a pass.
- Never `git reset --hard`, `git checkout -- .`, `git clean`, `git stash`;
  never amend or rebase.
- Commit message style: `type(scope): summary`.
- **Prove each new test can fail.**
- You cannot launch CS2 from the agent shell. Say what is unverified.
- **The measurement in §1 is the reference.** If an implementer's own build
  disagrees with it, the disagreement is the finding, and it goes in the
  report before anything else.
