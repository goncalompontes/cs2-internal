# HANDOFF — Full evidence-based reversal of the Neverlose CS2 module (except libraries)

> **Point the next agent at this file.** It is self-contained.
> Working dir: `/var/tmp/rea-work/`. Read `README_export.md` for the artifact inventory.

====================================================================
## 0. How to use
Tell the agent: *"Read `/var/tmp/rea-work/NEXT_SESSION_PROMPT.md` and execute it."*
Then it works one subsystem at a time (list in §7), producing verified pseudo-C++.

====================================================================
## 1. Mission
Fully reverse, **with evidence**, every **first-party (non-library)** code path in the target
`neverlose_cs2.dll`, one subsystem at a time, into **high-level pseudo-C++** (valid syntax,
non-compilable). Re-run the actual decompilation + deobfuscation. **No guessing, no invented code.**
Prior reconstructions contain inferred parts — do NOT trust `reconstruction_all.cpp` /
`reconstruction_aim_movement.cpp`; regenerate them from recovered bodies.

## 2. Target & ground truth
- `/var/tmp/rea-work/cs2_212C3300000.bin` — raw live memory image of a Windows x64 module.
  - SHA-256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`
  - Base `0x212C3300000`, size `0x5000000`; **`VA = 0x212C3300000 + file_offset`**; PE header zeroed.
- Identity: **Neverlose CS2 cheat** (`neverlose_cs2.dll`), manually mapped into `cs2.exe`
  (build path `…\neverlose\neverlose-cs2\…`).
- Reconstructed loadable PE: `/var/tmp/rea-work/neverlose_seeded3.exe`
  (imports from `imports.txt`; 57,076 export seeds from `seeds.txt`; builder `build3.py`).

## 3. Environment / hazards
- Ghidra 12.1.4: `/home/pontes/.local/share/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless`
  (`JAVA_HOME=/usr/lib/jvm/java-27-openjdk`, `GHIDRA_INSTALL_DIR=<that dir>`).
- **Work in `/var/tmp/rea-work`.** `/home/pontes` has a user quota (~226 GB) that is FULL → "Disk quota
  exceeded". `/tmp` is a small shared tmpfs.
- Ghidra project dir must pre-exist (`mkdir -p`).
- Full auto-analysis ~25–28 min; speed up with `-max-cpu 0 -analysisTimeoutPerFile 720`.
- **Large functions (>~20 KB) fail at the default 30 s decompile budget** → decompile with a **900 s**
  per-function timeout (this is the #1 gotcha; the 12 giants need it).
- REA MCP tools exist (good for evidence/grounding) but enforce a 330 s analysis timeout and cannot
  seed functions → use **Ghidra headless directly** for the heavy full decompilation.

## 4. Obfuscation (already decoded — reuse the deobfuscator)
Opaque predicate + arithmetic constant-obfuscation + junk decoy (NOT VMProtect):
- Predicate: `mov rax,gs:[0x60]` (PEB) → fingerprint vs the hidden constant
  `((0xA7277DDF-0x173171C5-0x754BC15A)^0x153A2B45)-0x0F90090E = 0x5877`, then `je <real>`.
- Deobf: keep the `je` target (`jne junk` → NOP). Constants are `movabs; not/rol/xor/sub` chains.
- Junk decoy is a never-executed block (`mov sp,si`/`bswap`/`cmc`/`rol`/`bndldx` … `jmp reg`).
- Encrypted at rest: file ~0x29c0000–0x3a40000 (`.enc1`) + parts of `.mid` = ciphertext, NOT code.
- Deobfuscator: `Deobf.java` (GhidraScript). Run with `-postScript Deobf.java`.

## 5. Exclude libraries
OpenSSL, cURL, Crypto++, protobuf, websocketpp, asio, boost, LuaJIT, lunasvg, zlib, …
Classify by RTTI/type names, library strings (`CryptoPP::`,`OSSL_`,`X509_`,`protobuf`,`boost::`,
`luaJIT`,`Sec-WebSocket`…), callee namespaces, address clusters. Record provenance; don't reconstruct.

## 6. Verified facts (reuse — don't redo)
- Loader `.stub` @0x212C82F0000: entry `0x212C3300A00` → `FUN_212c82f0a00` → DllMain
  `FUN_212c82f1690` → `FUN_212c82f10b0`/`FUN_212c82f1450` (rebuild erased PE header; SizeOfImage
  0x5000000) + `FUN_212c82f1000` (wildcard scan in ntdll) + `FUN_212c82f1720`; version table
  {0x3AD6=15062, 0x42EE=17134, 0x47BA=18362, 0x585D=22621, 0x65F4=26100}; registers the manual-map's
  unwind info via an unexported ntdll routine.
- Schema: array `ctx+0xAA0`, count `ctx+0xAA8`, stride 0x18, name +8, chain +2; `ctx = *(0x7ffcf0fe42e8)`;
  compare helper `VWSH__pH_l_pH_E_____L___`. ~41 resolvers (listed in §7.2).
- Entity handle resolver `FUN_212c36e5230`: `chunk=*(ctx+0x10+((h>>6)&0x1F8)); e=chunk+(h&0x1FF)*0x70;
  validate (*(uint*)(chunk+0x10+idx)&0x7FFF)==(h&0x7FFF)`.
- usercmd: `cmd=ctx+0xD38`, `state=ctx+0xD30` (`state+0x54A` silent flag), on-ground `ctx+0xD40`;
  `buttons+0x8` (mirror word `+0x18`), `viewangles+0x20`, `forwardmove+0x2C`, `leftmove+0x30`,
  `angles_alt+0xA8`, subtick array `+0x60/+0x68/+0x70` (stride 0x48). Buttons: ATTACK=1, JUMP=2,
  DUCK=4, FORWARD=8, BACK=0x10, MOVELEFT=0x200, MOVERIGHT=0x400. Consts 57.295776, 0.017453292,
  0.0027777778(1/360), 360, 0.5, -1.0.
- Movement `FUN_212c37beb50` (VERIFIED): silent/real angle write; `&= ~0x618` then `|= 0x08/0x10` and
  `|= 0x200/0x400`; move floats ±1; bhop = `&= ~0x61A` + append subtick step {0, button=2, pressed=1}.
- Auto-duck `FUN_212c382b860`; tick/fakelag manip `FUN_212c3617466`.
- Rage `FUN_212c37ff41e` (PARTIAL): table scan `(v%0x96)*0x98 / *0x750`; clears 0x48-stride array;
  `RandomSeed((targetHandle&0x7FFF)+weaponIdx*recordIdx)`; lerp `angle=prev+blend*(new-prev)` with
  `blend=RandomFloat(0.8,0.99)`; jitter `RandomFloat(-0.2,0.05)` / `(-0.025,0.025)`; per-record write
  `*(float*)(rec+k*4)=angle*57.295776`. UNKNOWN: target selection + FOV/visibility predicate.
- Driver `FUN_212c3790e70` call order: `FUN_212c35b7280` (ctx) → `FUN_212c35efd00` (C_CSPlayerPawn) →
  aim cluster.

## 7. Subsystems (reverse one at a time)
Build root set → transitive closure (via `all/index.tsv`, map `FUN_<hex>`→addr) → drop libraries →
decompile all (900 s) → Deobf → read every function → pseudo-C++.

**7.1 Loader/protection** — 0x212c3300a00, FUN_212c82f0a00,f1000,f10b0,f1450,f1690,f1720
**7.2 Schema** — resolvers: FUN_212c35efd00 C_CSPlayerPawn, 3614b10 C_CSPlayerPawnBase, 387ada0
  C_BaseEntity, 37b856b CCSPlayerController, 35f6d70 C_BasePlayerWeapon, 366d740 C_CSWeaponBase,
  3aa7550 C_EconEntity, 38693f0 C_EconWearable, 386c620 C_WorldModelGloves, 36507f0 C_CS2HudModelArms,
  3655e70 C_CS2HudModelWeapon, 3979b09 C_CS2HudModelBase, 35b7830 C_CS2HudModelAddon, 366cc70
  C_PlantedC4, 35e3500 C_Inferno, 35e3d10 C_BaseCSGrenadeProjectile, 3877d52 C_MolotovProjectile,
  35e2a30 C_SmokeGrenadeProjectile, 36708c0 C_Hostage, 36e5230 C_BasePropDoor, 34bdd30
  CCSItemIconGeneratorGameSystem, + the rest (grep class-name strings)
**7.3 Entity** — root FUN_212c36e5230
**7.4 Movement/autostrafe/bhop** — roots FUN_212c37beb50, FUN_212c3617466, FUN_212c382b860
  (closure pre-computed: `movement_closure.txt`, ~104 funcs / 141,594 bytes)
**7.5 Rage/aim** — roots FUN_212c3790e70, FUN_212c37ff41e, cluster FUN_212c3789290,378a9c0,378cd60,
  378dc70,378e500,378eab0,378f900,378f9c0,379f8e5,37a5e60,37a6c80,37a87e0,37a8d10,37a8fd0,37a9f00,
  37aad60,37af9b0,37afa80,37b0110,37b5160, FUN_212c3815d61, FUN_212c3820bd0
**7.6 Visuals/ESP + renderer** — roots FUN_212c38d7fa0, 387ada0, 3860600, 3ac3d10, 3ac8880, and the
  DXBC-referencing fns 36d4b00, 377b21a, 3668e6f, 36782f0, 377ac39, 39f47b0, 39f6f90, 39f4b00, 39eee66,
  3a39070 (shader CBs: Tracer/Impact/Trajectory/Polygon)
**7.7 Econ/skins** — root FUN_212c36510ce
**7.8 HW monitoring (PDH)** — 35a132b, 35a57d0, 35a6020, 35a6810, 3b7e3de, 3fdb400
**7.9 HWID (WMI/SMBIOS)** — FUN_212c3fd2f70 (+ combase glue)
**7.10 Scripting (LuaJIT)** — FUN_212c3dcfecc, 3dd54a0, 3ddb9d0
**7.11 Network/IPC glue** — first-party fns around websocketpp/asio/cURL (callers of WSA*/websocket)
**7.12 Per-tick entry** — FUN_212c3790e70 call order

## 8. Workflow per subsystem
1. Roots → closure (index.tsv); exclude libraries.
2. Write closure list; run a `DecompTargets`-style GhidraScript over it on **gproj3** (900 s timeout) →
   pseudocode + callees per fn.
3. Run `Deobf.java` (predicate forcing) so bodies are clean.
4. Read EVERY function; write pseudo-C++ mirroring real control flow/offsets.
5. Mark UNKNOWNs (obfuscated predicates; hook-invoked entries with no static callers).

## 9. Tools (in /var/tmp/rea-work)
`DumpAll.java` (decompile every fn — prior run killed at ~18k/46k; complete it), `Deobf.java` (deobf),
`DecompTargets.java` (large-fn decompile w/ long timeout — edit address list), `DumpFeatures.java`,
`MapImports.java`, `build3.py`, `imports.txt`, `seeds.txt`.
Existing: `ghout/` (string funcs + `string_refs.tsv` + `functions.tsv` + `import_refs.tsv`),
`ghout_deobf/` (`deobf.c` 347 fns, `big_functions.c` 12 giants, `patch.tsv`), `all/index.tsv`
(46,184 fns), `all/funcs_*.c` (partial). Projects: **gproj3** (FULL analysis, 277 patches — use this),
gproj5 (526 patches, 347 clean, but analysis timed out).

## 10. Deliverables
- `recon_<subsystem>.cpp` per subsystem (function addresses in header; per-construct source address/offset).
- `SUBSYSTEM_<name>.md` notes: inventory, evidence, observed-vs-inferred, unknowns.
- Overwrite `reconstruction_all.cpp`/`reconstruction_aim_movement.cpp` with verified-only logic.
- A rejected/unknown list (encrypted `.enc1`, unsolved obfuscation, hook bindings).

## 11. Start here
Begin with **§7.4 Movement/autostrafe** (closure already at `movement_closure.txt`), then proceed in order.
