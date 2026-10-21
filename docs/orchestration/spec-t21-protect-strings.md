# T21 — string and constant encryption with a runtime-derived key

**Goal:** the strings that identify what the payload does must **not be
readable in the shipped DLL**. Today they are plaintext: an analyst with the
file opens it in IDA, reads the convar names, the JSON keys and the config
keys, and learns the feature list without running anything.

This is the plan's own **number-one ranked technique**
(`docs/orchestration/plan-protect.md` §1: "the only thing that makes a dumped
image useless on its own"), and the survey
(`docs/research/protect-techniques-survey.md` §4-§5) put it **ahead** of the
virtualization on payoff per unit of effort.

**Read `spec-t20-protect-virtualization.md` and
`spec-t20a-protect-vm-front-end.md` first.** This task uses the same plugin,
the same two-phase model, the same policy file and the same report and
coverage structures. Where this document and those disagree, **this document
wins** — it is the later and the narrower statement.

---

## 1. What this task delivers, and what it does not

| | |
|---|---|
| **Delivers** | the string selection (a policy section), the fail-closed eligibility gate, the **encryption** of the selected literals, the **runtime key derivation**, the **decode** emitted into the using function, the payload's publication of its image base, the report and the coverage, and the tests that prove the plaintext is gone and the behaviour is unchanged |
| **Does not deliver** | a runtime-derived key for the **call descriptors** (T19's cipher keeps its build-time key — say so in the README), the **integrity check** (T22), the **per-build polymorphism** of the rest of the image (T24) |

**This task changes the shipped image.** The protected strings must be
**absent** from it. That is the whole point, and it is checkable.

## 2. The rules this task must not break

- **No new imports.** The import table of the protected payload must be
  **identical** to the unprotected one. The decode is emitted as IR: no CRT
  call, no `memcpy`, no `memset`.
- **No absolute address** in anything the pass bakes in. The key is derived
  from a **runtime** value, and the ciphertext is data.
- **Nothing in the image is ever initialised**: the injector maps the image,
  the CRT startup never runs. So there is **no constructor-decode** strategy
  and **no cached-global** strategy: a global with a dynamic initialiser
  would be silently zero in the mapped image, and a cached plaintext would
  sit in memory forever.
- **No TLS.** The injector allocates one TLS block for the whole image and
  sets it on the entry thread only.
- **The 64-bit-immediate trap.** This backend (LLVM 22.1.8) mis-materialises
  a 64-bit constant in some IR shapes (`tools/protect/README.md` §12). Every
  64-bit constant the decode needs — the seed parts, the mixer constants — is
  a **global**, read with a **volatile load**, and never an immediate.
- The plugin's configuration channel is the **environment**
  (`CS2_PROTECT_CONFIG`, `CS2_PROTECT_SEED`, `CS2_PROTECT_VERBOSE`); a
  plugin's `cl::opt` is not reachable via `-mllvm`.
- The pass runs at `registerOptimizerLastEPCallback`, defines
  `isRequired() { return true; }`, and must not transform a module twice.

## 3. The design decisions (the orchestrator's calls — do not re-litigate)

### 3.1 Where the plaintext lives: **a local stack decode**

The chosen form is **decode into an `alloca` in the using function, once per
function, immediately before the first use**:

```
entry:
  %buf = alloca [N x i8], align 1        ; the buffer, in the entry block
  ...
  call void @cs2.protect.strdecode(ptr @blob, i32 N, i32 %index, ptr %buf)
  ; every use of the old literal now points at %buf
```

The alternatives, and why they lose:

| form | why it loses here |
|---|---|
| **micro-slot** (each byte in its own global, XORed back per use) | the strongest anti-dump property (the plaintext is never contiguous), but the payload's uses are `std::string_view` comparisons — `field == "box"` materialises the literal as a `{ptr, len}` pair, so a **contiguous buffer is required**. Micro-slot does not apply to a pointer-and-length use. |
| **cached global** (decode once, keep it) | the plaintext sits in the image's memory **forever**, and it needs an initialisation step, which needs a constructor, which is forbidden. |
| **constructor decode** | forbidden outright: nothing is initialised in a manual map. |
| **decrypt-use-reencrypt** (in place) | the buffer must be writable; the image's `.rdata` is not. A writable copy would have to be a global, which brings back the previous row. |

**The weakness, stated plainly:** the plaintext lives in the function's stack
frame from the first use to the return. A **thread-stack dump** or a **crash
dump** of the game shows it. This buys time against an **offline** analyst;
against a debugger it is nothing. The value is that the **file on disk** no
longer contains the strings.

The decode is emitted **once per function**, before the first use, not once
per use: the code size matters, and one decode per function already keeps the
plaintext out of the image.

### 3.2 What the key is a function of

```
key = mix(seed0, seed1, index)
```

- `seed0`, `seed1` — the **build seed**, derived the way T19 derives its key
  (`splitmix64` over the module seed). They live in two `private constant`
  globals of the module and are read with **volatile loads**.
- `index` — the string's index in the module's list of protected strings. A
  small integer, so it is passed as an `i32` argument (no 64-bit immediate).

The mixing is the **same mixer family T19 uses** (`splitmix64` + the
multiply/xor/rotate mixer). This is obfuscation, not cryptography: an
analyst who finds the key recovers the strings. Say so in the README.

> **CORRECTED (2026-10-21, after the implementation).** This section
> originally said the key was `mix(seed0, seed1, instance_base, index)` and
> that the image was therefore useless on its own. **That is impossible, and
> the implementer was right to say so.** The ciphertext is baked into the
> image at build time, so the value that encrypted it must be computable at
> build time: a **per-injection** value cannot key data that is already in
> the file. Mixing the instance base into the key makes the decode produce
> **garbage**, not plaintext.
>
> The implementation therefore mixes the base in **and** takes it out again
> (`runtime_key = build_key ^ base`, `stream_key = runtime_key ^ base`), so
> the base is **not** a key: it cancels, and the recovered plaintext does not
> depend on it. That is the honest shape of this task.
>
> **What this task therefore delivers:** the strings are **not plaintext in
> the image** — they are not greppable, and they are not readable without
> running the pass's decoder. **What it does not deliver:** protection
> against an analyst who reads the decoder, which is in the image. The
> **genuinely runtime-derived** key needs the ciphertext to be transformed at
> **injection** time, not at build time — that is **T21b** (the injector
> writes a per-injection mask into a reserved slot; the payload publishes it;
> the decode mixes it in and does not take it out). Until T21b lands, the
> plan's "the dumped image is useless on its own" claim is **not** true of
> the strings.

**Why the image base at all, then.** It costs nothing and it keeps the door
open for T21b: the reserved global, the publication in `DllMain` and the
volatile load are already in place, so T21b only has to change the algebra
and the injector. It also gives the decode a **runtime** input, which means
the decoder cannot be folded away by the optimiser at build time — the
volatile load is what keeps that true.

**The image base is stable** for the payload's life: the injector maps the
image once and calls the entry point once. The payload's `self_unmap` is
**denied** by the current policy, so there is no re-map path that would
invalidate a cached key.


### 3.3 The blob, the decode helper and the globals

The **blob** replaces the literal global:

```
@cs2.protect.strblob.3 = private constant [N x i8] c"<the ciphertext>"
```

It has the **same type** as the literal it replaces (`[N x i8]`), so the
users' pointer arithmetic does not change. The plaintext is the literal's
bytes XORed with a per-byte key stream derived from `key`.

The **decode helper** is emitted by the pass, once per module, `internal`
(not exported):

```
declare void @cs2.protect.strdecode(ptr %blob, i32 %len, i32 %index, ptr %out)
```

- it reads `seed0`, `seed1` and `instance_base` (volatile loads),
- derives `key`,
- writes the plaintext into `%out` with a **loop** (no CRT call, no
  `llvm.memcpy`).

The **instance base** is an **external** symbol the pass declares:

```
@cs2_protect_instance_base = external global i64
```

It is defined **once**, by the payload (§6). The name is fixed and has **C
linkage** on both sides. If the payload does not define it, the link fails
loudly — that is the right failure (a protected build that decodes garbage).

### 3.4 The policy: how a string is selected

The policy file gains a section. The existing `protect` / `deny` entries name
**functions**; strings are not functions, so they get their own list.

```json
{
  "protect": [ "cs2::aim::solve" ],
  "deny":    [ "cs2::loader::self_unmap" ],
  "strings": [ "box", "health_bar", "ammo_bar", "only_enemies", "show_dead",
               "hitbox_mode", "cs2_*" ],
  "strings_deny": [ "CS2_*" ],
  "strings_min_length": 4,
  "seed": "0xA04252B187478C00A40BC6E8B04F1C9A"
}
```

- `"strings"` — each entry is a **glob** matched against the literal's
  **content** (an exact string is a glob with no wildcard). A literal is
  selected when it matches one of them and no `"strings_deny"` entry.
- **Nothing is selected unless the section says so.** The default is
  "protect no string": a surprise is worse than a gap. The operator's
  explicit list is the policy.
- `"strings_min_length"` — literals shorter than this are **refused**
  (`too_short`). The default is **4**: a three-byte string carries too little
  to be worth the decode, and the operator can lower it.

The strings are **not** a `ProtectionLevel`: they are not functions, they do
not go through the selection phase's function machinery, and they get their
own selection step in the transform phase.

### 3.5 The strings that are worth protecting, and the ones that are not

This is the part that decides whether the task is worth doing, so it is
settled here.

**The payload's own operational strings** — the feature keys, the JSON keys,
the convar names, the console commands — are in `payload/features/src/`
(`config_store.cpp`: `if (field == "box")`, `"health_bar"`, `"ammo_bar"`,
`"name"`, `"weapon"`, `"flags"`, `"only_enemies"`, `"show_dead"`,
`"hitboxes"`, `"hitbox_mode"`) and in `payload/control/src/`. They are
**inside function bodies**, so they are protectable. **They are the target.**

**The game's event names are not the target.** `payload/control/include/cs2/control/event_names.hpp`
holds `inline constexpr std::array<std::string_view, 273> KNOWN_EVENTS` and
`KNOWN_EVENT_FIELDS` (656 fields). Those are **Valve's** names: they are
public knowledge, they identify the *game build*, not the payload, and — this
is the decisive point — **they cannot be protected by this design at all**:
a `constexpr` array of `string_view` **bakes the pointers into a global
initializer**, and a pointer to a decoded buffer cannot exist before the
program runs. They are therefore **refused** with the reason
`escapes_to_initializer` (§4).

That refusal is not a loss to be papered over: it is the honest answer, and
the report must say so. Protecting them would need a **different data model**
in the payload (an index table plus a runtime-decoded table), which is a
separate task and contradicts the "no constructor" rule.

### 3.6 The coverage must not leak the plaintext

The coverage file is written into the **build tree**, and the build tree is
not shipped — but a coverage file is still a file that contains the strings.
The coverage therefore records the protected strings by **length and a hash
of the content**, never the content itself.

## 4. The eligibility gate — **fail closed**

A literal is protected only if it passes **every** check. Otherwise it is
**left exactly as it is** and the reason is reported.

| reason | refused because |
|---|---|
| `escapes_to_initializer` | the literal's address ends up in a **global initializer** (a `constexpr` array of `string_view`, a constant pointer): the pointer is baked into the image, and a pointer to a decoded buffer cannot exist before the program runs. This is the `KNOWN_EVENTS` case (§3.5). |
| `unsupported_shape` | the literal is not a `ConstantDataSequential` of `i8` (it is a struct, an aggregate, or an array of another element type) |
| `empty` | the literal is empty |
| `too_short` | it is shorter than `strings_min_length` |
| `optnone_function` | a user of it is in an `optnone` function: the pass's rewrite would not survive the optimiser's decisions, and the function is not protected anyway |
| `virtualized_function` | a user of it is in a function selected at the `virtualized` level: the VM's blob format and the string blob format must not meet |
| `eh_use` | a user of it is in a function with an EH pad, or the use is in a landingpad: the rewritten IR must stay exception-free |
| `unsupported_use` | the use is not a plain pointer use (the literal's address is compared, stored in a structure, or the user is not an instruction) |

The gate runs in the **transform phase** (the late phase), on the **optimised
IR** — the same point T19 uses.

**Nothing else is protected.** A literal that is not selected by the policy
is not even looked at.

## 5. What the pass does

For every selected literal, in the transform phase:

1. **Check the gate** (§4). A refusal leaves the literal **untouched** and
   is reported.
2. **Encrypt** the literal's bytes with the key stream and create the blob
   global (`private constant [N x i8]`).
3. **Delete** the original literal global, so its plaintext leaves the image.
4. **Rewrite the users**: every user of the literal now points at the
   `%buf` of the using function. A user may be a constant expression (a
   folded GEP or a bitcast) — the pass must handle that case, because that is
   how a literal reaches a `std::string_view` construction.
5. **Emit the decode**: the `alloca` in the entry block of the using
   function, and the `call @cs2.protect.strdecode` immediately before the
   first use in that function.
6. **Report**: the literal's length, the number of uses, the number of
   functions, the verdict.

A literal used by **several functions** gets one decode **per function**
(each has its own buffer). A literal used by **no function** (only by a
global initializer) is refused by the gate before this matters.

## 6. The payload-side change (the one file outside `tools/protect/`)

`payload/src/payload_main.cpp`:

```cpp
// The protection pass reads this: the instance handle is the payload's own
// mapped image base -- a value that is not in the file and differs per
// injection. It is the runtime half of the string key.
//
// The definition is here because there must be exactly one of it; the pass
// declares it in every translation unit that decodes a string.
extern "C" std::uint64_t cs2_protect_instance_base = 0;
```

and in `DllMain`:

```cpp
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    cs2_protect_instance_base = reinterpret_cast<std::uint64_t>(instance);
    cs2::payload::boot();
  }
  return TRUE;
}
```

**The write must be the first thing that happens at attach**, before
`boot()`: a string decoded before the base is published would read a stale
value — a wrong behaviour, not a crash, so nothing else would notice it.
(The base does not change the recovered plaintext — see the correction in
§3.2 — but it is a **runtime input** to the decode, and it must not be
stale.)

The global is **zero-initialised** (a static initialiser, so it is correct on
disk), it is **writable** in the mapped image (the injector maps the image
`PAGE_EXECUTE_READWRITE`), and it is **not exported** (no `dllexport`), so
the export table does not change.

The definition exists in **every** build, protected or not — the pass only
*declares* it, so the unprotected build simply has one more global.

## 7. The report and the coverage

The transform report gains, per protected literal:

```
  cs2-protect: str: 12 literals protected, 3 refused
  cs2-protect: str: "box" (3 bytes, 2 uses, 1 function): protected
  cs2-protect: str: "player_death" (11 bytes): refused (escapes_to_initializer)
```

(The report may name the content — it goes to stderr. The **coverage file**
may not: §3.6.)

The coverage gains a section, per literal: the **length**, a **hash** of the
content, the number of uses, the number of functions, the verdict and the
reason. The format change must be **backward compatible** (a new optional
section), so that `tools/protect/check_policy.py` and
`tools/protect/check_payload.py` keep reading the coverage of an older build.

## 8. The tests

**Each test must be proven able to fail** — break the code on purpose, watch
the test fail, revert it, and say so in the report.

1. **The unit test** (`tests/protect_tests.cpp`, the `cs2-protect-tests`
   binary): the selection by the policy's globs, the `strings_deny`
   exception, the minimum length, and **one fixture per refusal reason**
   (at least `escapes_to_initializer`, `unsupported_shape`, `too_short`,
   `optnone_function`). No SDK tool needed.
2. **The IR test** (`tests/str_ir_test.py`, a new CTest entry): over a
   checked-in fixture, the literal is **gone** from the emitted IR (its
   plaintext does not appear), a blob and a decode call **are** there, and a
   refused literal is **byte-for-byte unchanged**. It must exercise the
   **production path** as well (`clang-cl -fpass-plugin=<dll> -emit-llvm -S`),
   because `opt` does not invoke the extension points, so a test that only
   exercises the `opt` path can pass for the wrong reason (T19's review made
   this point).
3. **The differential test** — the correctness backbone. A fixture is built
   **twice** from the same source, once with the pass and once without, and
   the two must agree.
   - The fixture must be a **DLL**, not an EXE: the key is derived from the
     instance handle, and only the **OS loader** calls `DllMain` with the
     real handle. The fixture therefore mirrors the payload: it defines
     `cs2_protect_instance_base` and publishes its own `hinst` in its
     `DllMain`.
   - The driver loads both builds and compares the **results** (the fixture
     returns a hash of what its protected strings decoded to) and the exit
     codes.
   - The fixture must cover the cases that break naive implementations: a
     string used **twice** in one function, a string used in **two**
     functions, a string that is a **format argument** (`"%s"`), a string
     compared with `==`, and a string of **exactly** the minimum length.
4. **The image test**: the protected strings are **absent** from the built
   protected DLL (a byte scan) and **present** in the unprotected one.
   `tools/protect/check_payload.py` gains this check: it is the check that
   fails if the protection silently did nothing.
5. **The existing tests stay green**: the plugin's four tests, the host
   suite, and the protected payload build with its `check_policy.py` /
   `check_payload.py`.

**Skip with CTest's skip code when the plugin has not been built**, so a
machine without the LLVM SDK still has a green suite.

## 9. File ownership

- `tools/protect/**` (including `tools/protect/tests/**`)
- `tools/protect/README.md`
- `payload/src/payload_main.cpp` — **only** the reserved global and the
  `DllMain` publication of the instance handle. Nothing else in that file.

Do **not** touch: the rest of `payload/**`, `overlay/**`, `injector/**`, the
root `meson.build`, `payload/meson.build`, `payload/meson_options.txt`,
`AGENTS.md`, `docs/style-guide.md`, `docs/decisions.md`, the other
`docs/orchestration/*.md`.

## 10. The verify loop

```powershell
cd <your worktree>

meson compile -C build
meson test -C build

python scripts/build-protect-plugin.py
ctest --test-dir tools/protect/build

$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$policy='<your worktree>\tools\protect\policy.local.json'
$plugin='<your worktree>\tools\protect\build\Cs2Protect.dll'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson setup build-payload-protect-t21 payload -Dprotect=true -Dprotect_plugin=$plugin"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && set CS2_PROTECT_CONFIG=$policy && meson compile -C build-payload-protect-t21"
```

Do **not** `meson setup --wipe` on `build`, `build-payload`, `build-overlay` or
`build-injector`; use a **fresh** directory name.

`CS2_PROTECT_CONFIG` must be set for **both** the configure and the compile
step: the plugin reads it in the compiler process.

## 11. Acceptance criteria

1. The selected literals' **plaintext is absent** from the protected image,
   and the check that proves it is in `check_payload.py`.
2. The plaintext is **present** in the unprotected image — the check compares,
   so it cannot pass vacuously.
3. The key is **not a compile-time constant in the image**: the ciphertext is
   not the plaintext under any constant the pass bakes in, the seed parts and
   the mixer constants live in globals and are read with **volatile** loads,
   and the decode reads a **runtime** input (the published instance base).
   The IR test proves the decode is emitted, the literal is gone, and the
   constants are not immediates.
   **What this does *not* claim:** that the image cannot decrypt itself —
   §3.2's correction explains why it can, and what T21b must change.
4. The **differential test** proves the protected and the unprotected
   fixture agree, and the test **can fail** (the mutations are listed in the
   report).
5. The **import table** and the **exports** of the protected payload are
   **unchanged**, and the payload has **no new exported symbol**
   (`cs2_protect_instance_base` is not exported).
6. The refusals work: the `KNOWN_EVENTS` literals are refused with
   `escapes_to_initializer` and are **still in the image** (the gate does not
   half-protect), and the report says so.
7. The **coverage** records the protected literals without their content.
8. `meson test -C build` is green, and the plugin's CTest suite is green.
9. `tools/protect/README.md` documents: the policy's new section, the key
   derivation, the **micro-slot** alternative and why it does not apply to a
   `string_view` use, the plaintext's lifetime, the fact that the **key is
   in RAM while running**, and the fact that the **call descriptors still
   use a build-time key** (that is T21's other half, not done here).
10. Nothing outside §9 was touched.

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

What changed per file. The final policy's `"strings"` list and **why those
strings and not the others** (the `KNOWN_EVENTS` case especially). The key
derivation, step by step. The eligibility rules and the verdict for **every**
literal of the payload that the gate saw: protected, or refused and why. The
exact tests, and how each was proven to be able to fail. The measurements:
how many literals, how many bytes of plaintext left the image, how much the
image grew. What you could not verify.
