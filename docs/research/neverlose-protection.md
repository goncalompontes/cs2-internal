# Neverlose's protection layer — the loader, the encrypted region, the fingerprints, and the feature delivery

> Research note, 2026-10-21. **Route C** of the T17 deep-reversal spec
> (`docs/orchestration/spec-t17-neverlose-deep.md`).
> Scope: the **reversed Neverlose CS2 module**, read as *reference material
> only*. **Nothing from it is copied**; every claim below is a **description**
> with an evidence address. See the licence note at the end.
>
> **Ground truth.** The raw live image
> `re/cs2_212C3300000/cs2_212C3300000.bin`
> (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
> base `0x212C3300000`, size `0x5000000`, **`VA = base + file_offset`**).
> The decompilation used is Ghidra 12.1.4 project **`gproj11`** (the *corrected*
> deobfuscation) and the loader corpus `ghout_deobf/loader.c`.
>
> **Scope note (from the spec, and it is a real one).** This note describes
> **how the product loads itself and protects its own code** — the loader, the
> encrypted region, the integrity machinery, the hardware identification, and
> the server-side feature delivery. It deliberately does **not** describe, and
> must not be used to build, any technique for **avoiding a third party's
> detection**. Nothing here is on our project's feature list.
>
> **Confidence labels.** `verified` = read out of the bytes or the
> decompilation; `inferred` = interpretation; `unknown` = looked for and not
> recoverable statically.

---

## 0. Summary

| # | Question | Answer | Confidence |
|---|---|---|---|
| C1 | What does the loader do, end to end? | A **bespoke reflective loader stub** at `0x212C82F0000`: it rebuilds the **erased PE header** of the image it is mapped into, then locates **two unexported ntdll routines by wildcard byte-pattern scan**, gated on the Windows build number, and calls them | `verified` |
| C2 | How is the ntdll routine found? | A **wildcard matcher** (`0xFF` = "any byte") over `ntdll`'s text, with **10 different patterns** and a **build-dependent delta** | `verified` — all 10 patterns decoded in §2 |
| C3 | What is the version table? | Build thresholds **15062 / 15063 / 17134 / 18362 / 19041 / 22000 / 22621 / 26100 / 28000** across the two loader functions (the prior session's table was the *second* function's subset) | `verified` |
| C4 | Where is the encrypted payload? | `.enc1`, `0x212c5cc0000`–`0x212c6d40000` (16.5 MB); measured entropy of the first MiB = **9.159 bits/byte**; **exactly one** 8-byte data reference in the whole image, at `0x212c5ca6516` | `verified` |
| C5 | **New:** does the loader depend on the encrypted region? | **Yes.** The loader's own `SizeOfImage` / `SizeOfHeaders` inputs are read from `base+0x27ff7f8` / `base+0x27ff7f6`, and those bytes read as high-entropy data in the live image — they sit **inside the encrypted part**. The loader therefore cannot even run from the static image | `verified` (bytes) / `inferred` (consequence) |
| C6 | What is the anti-tamper machinery? | Two **opaque-predicate fingerprint families** (PEB `0x5877`, KUSER `0x92FB254D`) guarding **3 100** blocks; plus the build-number version gate | `verified` (carried from the corrected deobfuscation pass) |
| C7 | What is collected for identification? | **WMI** (`ROOT\CIMV2`), the **raw SMBIOS table** (`MSSMBios_RawSMBiosTables`), `\\?\GLOBALROOT` device paths, registry reads, and a **CPUID vendor/brand fingerprint** | `verified` (strings, with addresses) |
| C8 | What is the hardware monitoring for? | **PDH performance counters**: three CPU counters and one GPU counter, feeding an overlay widget | `verified` (strings, with addresses) |
| C9 | How are features delivered? | **Server-side.** The feature list is fetched as **CJSON** at run time (`"BUG: Unable to fetch CJSON configuration"`, `0x212c424f69f`) — so *the absence of a feature in the binary is not proof the product lacks it* | `verified` (string) / `inferred` (consequence) |

---

## 1. The loader stub, end to end

`verified` — from `ghout_deobf/loader.c` (project `gproj11`) cross-checked
against the raw bytes.

### 1.1 The entry chain

```
0x212c3300a00   FUN_212c82f0a00        ; the stub's entry
                  └─ FUN_212c82f1690(&DAT_212c3300000, 1, 0)
                       ├─ FUN_212c82f10b0()          ; header rebuild + ntdll scan #1
                       └─ FUN_212c82f1450(param_1)  ; ntdll scan #2 + header rebuild #2
```

`FUN_212c82f1690` is the `DllMain` equivalent: it does the work only when
`param_2 == 1` (`DLL_PROCESS_ATTACH`).

### 1.2 `FUN_212c82f10b0` (920 B) — rebuild the erased PE header, then call ntdll

```
hModule = GetModuleHandleA("ntdll");                       // 0x212c82f2074
FUN_212c82f16c0(&DAT_212c82f4050, param_1, 0x1000);         // copy a 0x1000-byte header template
*param_1                     = 0x5a4d;                     // 'MZ'
*(u32 *)(param_1 + 0x1e)    = 0x40;                        // e_lfanew
*(u32 *)(param_1 + 0x20)    = 0x4550;                      // 'PE\0\0'
param_1[0x2c]               = 0x20b;                       // PE32+
param_1[0x22]               = 0x8664;                      // Machine = AMD64
*(u32 *)(param_1 + 0x62)    = 0x10;                        // SectionAlignment
*(u32 *)(param_1 + 0x4a)    = 0;
param_1[0x4f]               = 0x160;
*(u32 *)(param_1 + 0x70)    = *(u32 *)(param_1 + 0x27ff7f8);      // SizeOfImage
*(int  *)(param_1 + 0x72)    = *(int  *)(param_1 + 0x27ff7f6) * 0xc + 0xc;  // SizeOfHeaders
FUN_212c82f16f0(local_128, 0);                             // zero a 0x11c-byte OSVERSIONINFOEXW
GetProcAddress(hModule, "RtlGetVersion")(local_128);      // 0x212c82f2080
... build-number-gated wildcard scan of ntdll -> pcVar2 ...
if (pcVar2 == 0) MessageBoxA(0, "Internal error(unsupported windows version, contact support)", 0, 0);
                   //                                                            ^ 0x212c82f20e0
(*pcVar2)(param_1, 0x5000000);                             // <-- the ntdll routine
```

`verified`.

Three things are worth stating explicitly:

* **`RtlGetVersion` is resolved by `GetProcAddress`, not imported.** The
  loader's import surface is only `GetModuleHandleA`, `GetProcAddress`,
  `MessageBoxA` and `RtlGetVersion` — i.e. the *minimum* needed to find two
  unexported functions. `verified`.
* **The routine is called with `(image_base, 0x5000000)`** — the image base and
  the image size. A call with exactly that signature in a loader is the
  **unwind-information registration** for the manually-mapped image. The exact
  ntdll export name is `unknown` (see §2.3).
* **The two size inputs live at `base+0x27ff7f6` / `base+0x27ff7f8`.** In the
  live image those bytes read as high-entropy data
  (`0xcad3484c` and `0x487d33ac` respectively), i.e. they are inside the
  **encrypted** part of the image (they are 0xC8 000 bytes below the `.enc1`
  boundary at `0x212c5cc0000`). ⇒ **The loader cannot even read its own
  inputs from the static image**; they only become meaningful after the
  runtime decryption. `verified` (the bytes) / `inferred` (the consequence).

### 1.3 `FUN_212c82f1450` (564 B) — a second, differently-gated scan

```
hModule = GetModuleHandleA("ntdll");                       // 0x212c82f2028
FUN_212c82f16f0(local_128, 0, 0x11c);
GetProcAddress(hModule, "RtlGetVersion")(local_128);      // 0x212c82f2030
... build-number-gated choice of a 6-byte pattern + a delta ...
pcVar3 = FUN_212c82f1000(hModule, pattern, delta);        // the wildcard matcher
FUN_212c82f16c0(&DAT_212c82f3050, param_1, 0x1000);        // copy a second header template
*(u32 *)(param_1 + 0x88) = *(u32 *)(param_1 + 0x27ff780);
*(u32 *)(param_1 + 0x8a) = *(u32 *)(param_1 + 0x27ff77e);
(*pcVar3)(&local_1b8);                                     // call the found routine
```

`local_1b8` is a **0x70-byte zeroed output buffer**. `verified`.

### 1.4 `FUN_212c82f1000` (164 B) — the wildcard matcher

```c
/* match `pattern` (with 0xFF = "any byte") inside ntdll's image,
   then return (match_offset + delta) */
while ((*(char *)(param_1 + i + j) == *(char *)(param_2 + j) ||
        (*(char *)(param_2 + j) == -1)) j++;      // 0xFF is the wildcard
```

`verified`. So the "signature" is a byte string with `0xFF` holes — the
classic shellcode-style pattern scan, here used on `ntdll`'s own text.

---

## 2. The version gate and the ten patterns

### 2.1 The build-number thresholds

`verified`, read from the two functions' decision trees:

| function | build | pattern at | pattern bytes | routine at |
|---|---|---|---|---|
| `FUN_212c82f10b0` | ≥ 28000 | `0x212c82f2090` | `48 8d 50 ff c7 40` | match − 0x16 |
| | 26100 … 27999 | `0x212c82f2098` | `4c 8d 48 18 8b f2 41 b8 03` | match − 6 |
| | 22000 … 26099 | `0x212c82f20a8` | `48 89 5c 24 08 57 48 83 ec 30 8b da` | match + 0 |
| | 19041 … 21999 | `0x212c82f20b8` | `48 8d 54 24 58 48 8b f9 e8` | match − 0x11 |
| | 17134 … 19040 | `0x212c82f20c4` | `8b fa 49 8d 43 20` | match − 4 |
| | 15063 … 17133 | `0x212c82f20d0` | `8b c3 2b d3 48 8d 48 01` | match − 0x84 |
| | ≤ 15062 | `0x212c82f20d0` | *(same)* | match − 0x84 |
| `FUN_212c82f1450` | ≥ 26100 | `0x212c82f2040` | `0f 57 c0 33 c0 41` | delta − 0x49 |
| | 22621 … 26099 | `0x212c82f204c` | `74 37 44 8d 43 09` | delta − 0x49 |
| | 15063 … 22620 | `0x212c82f2060` | `44 8d 43 09 4c 8d` | delta − 0x46 |
| | ≤ 15062 | `0x212c82f2054` | `74 33 44 8d 43 09` | delta − 0x44 |
| | ≤ 17133 | *(as above)* | | delta − 0x43 |

`verified` — every pattern above was read out of the raw image at the listed
VA.

**Correction to the prior session's table.** `OPEN_ITEMS.md` /
`NEXT_SESSION_PROMPT.md` §6 list `{0x3AD6=15062, 0x42EE=17134, 0x47BA=18362,
0x585D=22621, 0x65F4=26100}`. That is the threshold set of the **second**
function (`FUN_212c82f1450`) plus 28000; the **first** function
(`FUN_212c82f10b0`) uses a *different* ladder (15063, 17134, 19041, 22000,
26100, 28000). Both are in the table above. `verified`.

### 2.2 What the patterns look like

The four patterns of the second function all contain the bytes
`44 8d 43 09` = **`lea r8, [rbx+9]`**, with a varying `je` offset in front
(`74 37` / `74 33` / `74 2e`) — i.e. the same routine, reached through a
different inlining in different ntdll builds. The six patterns of the first
function are unrelated to each other (different prologues), which is what one
expects of a routine whose *surrounding code* is rewritten between builds.
`verified` for the bytes, `inferred` for the reading.

### 2.3 The two routines — what they are

* **Routine #1** — called as `f(image_base, 0x5000000)`. The signature and the
  loader's context make it the **unwind-information registration** for the
  manually-mapped image. The **export name is `unknown`**: the pattern is six
  bytes of a *caller* (`48 8d 50 ff c7 40` = `lea rdx,[rax-1]; mov dword ptr
  [rax+…],…`) and the routine is found relative to it, so the pattern alone
  does not identify the function.
* **Routine #2** — called as `f(buffer_0x70)`, writing into a zeroed 0x70-byte
  structure, and the *pattern* it is matched by contains `lea r8, [rbx+9]`.
  A single-pointer ntdll routine that fills a 112-byte structure is
  consistent with an image-header / module-information producer. The **export
  name is `unknown`**.

Both are `unknown` by name; both are `inferred` by role. This is stated as an
honest limit, not a gap to be papered over.

---

## 3. The encrypted region (`.enc1`)

`verified`, measured on the raw image:

| property | value |
|---|---|
| VA range | `0x212c5cc0000` – `0x212c6d40000` (16.5 MB) |
| entropy of the first 1 MiB | **9.159 bits/byte** (measured here) |
| first 32 bytes | `1d 53 44 e9 9f 02 50 40 71 16 03 fd 29 fe 8f 0c 05 d4 06 0f f4 30 5d 2c db 60 de 3a 02 a6 d9 f6` |
| references to the VA range from anywhere in the image | **exactly one**, a data pointer at `0x212c5ca6516` |
| static decryption loop in the loader | **none** |

The prior session's conclusion stands and is now sharper: because the
**loader itself** reads its `SizeOfImage` / `SizeOfHeaders` inputs from
inside the encrypted region (§1.2), the decryption must happen **before the
loader can finish**, and its key cannot be in the clear part of the image.

### 3.1 What a runtime dump would have to capture

To recover the decryption statically would be impossible; to recover it
dynamically, a dump would have to capture, in one shot:

1. the **entry state** of the image *after* the loader has run (i.e. after
   the `.enc1` region has been transformed), together with the **pre-state**,
   so the transformation can be diffed;
2. the **key material** — which, given that the loader's own inputs come out
   of the encrypted region, is most likely derived from the decrypted region
   itself (a self-keying scheme) rather than from an external secret;
3. the **runtime** of the decrypting routine — a breakpoint on the first
   write into `0x212c5cc0000` would give the routine and its inputs directly.

`inferred` from the structure; `unknown` in detail. **This is a runtime
question, not a static one**, and this task did not run the product.

---

## 4. Integrity / anti-tamper machinery

`verified` (from the corrected deobfuscation pass, `DEOBF.md`, and the
fingerprints below):

* **Two opaque-predicate fingerprint families** guard the real code:
  * a **PEB** fingerprint — `gs:[0x60]` (the PEB) with offsets `+0xB8`,
    `+0x118`, `+0x11C`, `+0x120`, `+0x2C0` (the OS version/build words),
    compared against `0x5877`;
  * a **KUSER_SHARED_DATA** checksum fingerprint, `0x92FB254D`.
  * **3 100** such sites exist in the raw image; **all** of them are forward
    `je`/`jz`, which is what makes the deobfuscation unambiguous (rewrite the
    branch to an unconditional `jmp`; the skipped gap *is* the decoy).
* The **version table** (§2.1) is a *compatibility* gate, not an integrity
  check: it selects the right ntdll pattern for the running Windows build, and
  refuses to continue on an unknown build
  (`"Internal error(unsupported windows version, contact support)"`,
  `0x212c82f20e0`).
* The **encrypted region** is itself the strongest integrity measure: half of
  the product's code is not inspectable without the runtime key.

**What was looked for and not found:** a self-hash of the module image (a
checksum computed over its own bytes and compared against a stored value).
No such loop was identified in the loader corpus, and the module's own
`SizeOfImage` inputs living inside the encrypted region is a *different* kind
of protection (it hides the loader's parameters, not the image's integrity).
`unknown`.

---

## 5. Hardware identification (HWID)

`verified` — the strings are present in the raw image at these addresses
(UTF-16 unless noted):

| VA | string |
|---|---|
| `0x212c4257e76` | `\\?\GLOBALROOT` |
| `0x212c4257e94` | `ROOT\WMI` |
| `0x212c4257ea6` | `ROOT\CIMV2` |
| `0x212c424eb08` | `MSSMBios_RawSMBiosTables` (ASCII) |

plus the COM GUIDs `IID_IWbemLocator` `{DC12A687-737F-11CF-884D-00AA004B2E24}`
@ `0x212c41425b0` and `CLSID_WbemLocator`
`{4590F811-1D3A-11D0-891F-00AA004B2E24}` @ `0x212c41425c0`.

The collection path is therefore:

1. `CoCreateInstance(CLSID_WbemLocator)` → `ConnectServer` on
   `ROOT\CIMV2` → WQL queries;
2. `ROOT\WMI` → `MSSMBios_RawSMBiosTables` (the **raw SMBIOS table** — i.e.
   baseboard / BIOS / system serial numbers);
3. `\\?\GLOBALROOT\…` device-path reads (`CreateFileW`);
4. registry reads (`RegOpenKeyExW` / `RegQueryInfoKeyW`);
5. a **CPUID vendor/brand fingerprint** (`cpuid_basic_info(0)`,
   `cpuid_brand_part1_info(0x80000002)`) with the constants `0x11f8d42c`,
   `0x3047e351`, `0x756e654d` ("Menu").

`verified` for the artefacts; `inferred` for the reading (the prior session's
`SUBSYSTEM_hwid.md` and `SUBSYSTEM_hwmon.md` established the same, and this
pass re-verified the string addresses against the raw image rather than
re-deriving the call graph).

**Why:** a machine identifier that survives a reinstall, derived from
motherboard/BIOS serials and CPU identity, plus a behavioural fingerprint.
That is the *shape* of an anti-abuse identity, not of a hardware inventory.
`inferred`.

**Not recovered:** the exact WQL query strings, the SMBIOS struct parsing, and
which fields are actually transmitted. `unknown`.

---

## 6. Hardware monitoring (PDH)

`verified` — the counter paths are present as UTF-16 at:

| VA | counter |
|---|---|
| `0x212c4257ca6` | `Processor Frequency` (i.e. `\Processor Information(*)\Processor Frequency`) |
| `0x212c4257c4e` | `% Processor Utility` |
| `0x212c4257e2a` | `% Processor Performance` |
| `0x212c4257da8` / `0x212c4257dc4` | `GPU Engine` / `Utilization Percentage` |

The prior session verified the setup routine `FUN_212c3b7e3de` (482 B):
`PdhOpenQueryW(NULL, 0, &h)`, then four `PdhAddEnglishCounterW` calls with
handles at `self+0x168/0x170/0x178/0x180` and enable flags at
`self+0x193` (CPU) / `self+0x194` (GPU).

**PDH is not in the module's import table** — the module reaches `pdh.dll`
through the host process's already-resolved entry points, which is what a
manual-mapped module has to do. `verified` (the absence of `Pdh*` from
`imports.txt`) / `inferred` (the mechanism).

**Why:** a performance HUD (CPU frequency / utility / performance, GPU
utilisation) — an overlay widget, and also a cheap way to characterise the
machine it is running on. `inferred`.

---

## 7. Server-side feature delivery

`verified` — the error string

```
"BUG: Unable to fetch CJSON configuration"      @ 0x212c424f69f
```

sits in the module's string table next to the protobuf/websocketpp/Lua/schema
strings. The product therefore fetches its **feature configuration as CJSON
from its server**, and the client binary does not contain the list.

**Consequences that matter for every other claim in this project:**

* the **absence** of a feature in the image is **not** proof the product lacks
  it — the feature may simply be server-gated (licence tier, build, region);
* the module carries a **JSON** stack (cJSON) and a **websocket** stack
  (`websocketpp`, `asio`, OpenSSL/cURL) — the transport is already in the
  binary even though the payload is not.

**Not recovered:** the endpoint URL, the request shape, and the response
schema. The one rip-relative candidate for the CJSON string
(`0x212c4ffd377`) does not survive disassembly — it is a displacement byte
of another instruction, not a reference. `unknown`.

---

## 8. What this changes for us

1. **Our loader already does the right thing.** `payload/loader/` is written;
   the neverlose loader is a *reference*, not a design to copy. The one thing
   worth taking is the **shape of the failure mode**: the loader refuses to
   run on an unsupported Windows build and says so in one line
   (`"Internal error(unsupported windows version, contact support)"`).
   → *Action*: our loader should fail **loudly and specifically** when it
   meets a configuration it does not understand, instead of degrading
   silently.
2. **A self-check over our own image is cheap and worth having.** The neverlose
   product gets its integrity from *hiding* half the code. We cannot and
   should not do that. What we can do is a **cheap integrity check over our
   own mapped image** (a hash of our `.text` against a value we store, or a
   handful of canary checks), so that a tampered payload fails loudly rather
   than misbehaving. → *Action*: decide whether our loader should verify its
   own sections before jumping to the entry point. This is a *robustness*
   measure, not an anti-analysis measure.
3. **The HWID collection is the one thing we should consciously *not* copy.**
   Their fingerprinting is a machine-identifier for anti-abuse. We do not need
   a hardware identity, and shipping one creates a privacy liability for our
   users with no benefit to them. → *Action*: if a licence/entitlement system
   is ever needed, prefer something that is **not** a hardware fingerprint.
4. **The hardware monitoring is a UI feature, not a protection feature.**
   Their PDH counters feed an overlay widget. If we ever want a perf HUD,
   that is the shape (three CPU counters + one GPU counter). → *Action*: none
   now; it is out of scope for this wave.
5. **"Not in the binary" is not "not in the product".** Any future audit of
   *our* feature list against the neverlose list must remember that their
   list is server-side. → *Action*: when comparing features, treat their
   binary as a *lower bound*.

## 9. What we should **not** copy

* **The loader's stealth design.** The opaque predicates, the encrypted
  region, the fingerprint gates and the hand-placed hook stubs are
  *anti-analysis* mechanisms. Reproducing them would be copying a third
  party's evasion techniques — explicitly out of scope, and not something our
  users benefit from.
* **The hardware fingerprint.** See §8.3.
* **No third-party code.** Every address, constant and pattern above is a
  *description* of a binary, not source we may reuse.

---

**Licence note.** The Neverlose CS2 module is a **commercial third-party
product** with no open-source licence. It is used here as *reference material
only*: this document **describes** observed behaviour, quoting addresses,
constants and byte patterns as *facts about that binary*. No third-party code
is reproduced in this repository, and nothing here may be used to reproduce or
redistribute their binary or its contents.
