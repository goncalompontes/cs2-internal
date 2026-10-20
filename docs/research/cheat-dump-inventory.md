# Third-party CS2 cheat artifacts — public inventory and triage

> Research note, 2026-10-20. Companion to
> [`neverlose-mining.md`](neverlose-mining.md): that note mines the one artifact we
> already hold; this one answers *what else is public*, what each item actually is,
> and which one is worth reversing next.
>
> **Second pass (same day).** A deeper repository sweep added the GitHub-hosted
> binaries in §3.1, the Fatality CS2 base (§2.3), the Plaguecheat protection tree
> (§2.4) and the Gamesense CS:GO DLL evidence (§4). The ranking is unchanged, but
> “the only public CS2 source is Weave” no longer holds: there are now three, at very
> different levels of completeness.
>
> **Third pass (same day, after the read).** The four artifacts that were read are
> now written up: [`cs2-chams-t7.md`](cs2-chams-t7.md) (the T7 answer),
> [`gamesense-csgo-re.md`](gamesense-csgo-re.md) (the gamesense reversal),
> [`fatality-cs2-base.md`](fatality-cs2-base.md) (the Fatality CS2 base and its
> config schema), and [`plaguecheat-protection.md`](plaguecheat-protection.md) (the
> Plaguecheat reviver). Read those first; this inventory is now the *index*.
>
> **Fourth pass (same day).** The Weave CS2 **ragebot** was read as well —
> [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) — and it is the most useful
> single artifact found so far: the only public CS2 implementation of a
> hit-chance model, an engine-priced penetration check, a lag-record backtrack
> and the CS2 command-acceptance plumbing. A CS2 cheat module (Primordial
> crack, Dec 2025) was also acquired and hashed; its code section is
> **string-obfuscated**, so static reads of it yield little (see §3.2).
>
> **Fifth pass (same day).** The **CS:GO** Fatality tree was read for its
> *algorithms* — [`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) —
> and it is the only readable implementation of a top-tier ragebot anywhere:
> point generation with a complete radius-scale formula, a 64-sample hit
> chance with hitbox-pinned counting, the penetration damage model, the
> **safe-point tiers**, and a **resolver that is a hypothesis set updated from
> server feedback**. That last one is the shape our own exactness gap has been
> missing; §9 of that note lists the ten techniques that transfer.
>
> **Scope.** Inventory and triage only. Nothing listed here was downloaded or
> executed for this note, and no artifact is included in the repository. Every
> claim is a description; code bodies are never copied.
>
> **Confidence labels.** `verified` = I opened the repository/file listing or read
> the source; `inferred` = interpretation from what I read; `snippet-only` = only
> the forum/thread metadata and search snippet were reachable (pages returned
> 403/429/Cloudflare); `unverified` = claimed by the poster, not checked.
>
> **Evidence limits.** GitHub rows were checked through the API/HTML (size, last
> push, file tree, and for the two source trees the file contents). Forum rows
> (Hack vs Hack, YouGame) are `snippet-only`: the sites block automated fetches, so
> dates, sizes and passwords below are the poster's claims, **not** verified bytes.
> No hashes exist for anything except our own Neverlose image, because nothing was
> downloaded.

---

## 0. Summary

| Artifact | Type | Era | Public since | What it is actually good for | Confidence |
|---|---|---|---|---|---|
| `cacamelio/weave-v3-csgo-and-cs2` | **source tree** | CS2 (2024–25) | 2025-05-19 | Only public **CS2** feature source with chams, ragebot, antiaim, lagcomp, penetration, prediction, hooks, renderer, SDK | `verified` (content read) |
| `cacamelio/fatality-source-fixed` | **source tree** | **CS:GO** | 2026-07-12 | The **only readable top-tier ragebot anywhere**: point generation + radius-scale formula, 64-sample hit chance with hitbox-pinned counting, the penetration damage model, the **safe-point tiers**, and a **resolver as a hypothesis set updated from server feedback** | `verified` (content read) |
| `notphage/fatality.win-CS2` | **source tree (base)** | CS2 (2024-01) | 2024-01-18 | Real CS2 SDK, hooks, ESP, movement, GUI and Lua plumbing; **no ragebot, chams or antiaim** | `verified` (content read) |
| `csco1337/plaguecheat` | **protection source** | CS2 (2026-07) | 2026-07-10 | **Not** a feature source: a module *reviver* (segment extraction, IAT, OEP, fake stack, custom VEH, snapshot spoofing) for Plaguecheat's protected segment | `verified` (read) |
| Gamesense **CS:GO** crack DLLs | **unprotected DLL** | CS:GO | 2022–2024 | The real gamesense module for CS:GO (2.5–3.4 MB) — **not** CS2 | `verified` (listing + size) |
| Primordial / Aimware / Plague CS2 cracks | **unprotected DLLs** | CS2 | 2024–2026 | Shipped modules with protection stripped, if authentic | `verified` (listing); authenticity `unverified` |
| `qcold/primordial` | **loader/crack source** | CS2 (2025) | 2025-07 | Modern CS2 manual-map loader, `connect`/`recv`/`send` hooks. **No feature source** | `verified` (content read) |
| Fatality CS2 dump 2025 (HvH 6491) | binary dump | CS2 2025-04 | 2025-04-17 | Shipped ragebot/visuals of a top-tier product, if authentic | `snippet-only` |
| Fatality `fatdump.bin.zip` 2026 (HvH 13730) | binary dump | CS2 2026-09 | 2026-09-09 | Same, newer — but replies call it fake | `snippet-only`, **disputed** |
| Memesense dumps (HvH 6492, 6713) | binary dumps | CS2 2025-04/05 | 2025-04-18, 2025-05-15 | Smaller feature set; plausible provenance | `snippet-only` |
| Azura full dump (HvH 12508) | binary dump | CS2 2026-06 | 2026-06-21 | “Just put it on IDA” — no reconstruction done | `snippet-only` |
| Plaguecheat dump (HvH 13330) | raw x64dbg `.bin` | CS2 2026-08 | 2026-08-17 | Unfixed image, no imports — reconstruction work required | `snippet-only` |
| Clarity TK v3/v2 dump (HvH 13808) | partial dump | CS2 2026-09 | 2026-09-14 | Self-described “no imports” | `snippet-only` |
| Gamesense / Onetap / Nixware / Legendware CS2 | — | — | — | **No verified CS2 dump or source found** | `not found` |
| Gamesense / Aimware / Onetap / Legendware / Nixware **CS:GO** sources | source trees | CS:GO | 2021–2024 | Complete architectures, but CS:GO semantics only | `verified` (repo metadata) |
| `T1GxR/Axion-CS2-RAGE-CHEAT`, `nildotdev/leerware`, `soren-source/trap-cs2` | **MIT** source | CS2 | 2024–25 | The only CS2 code we may legally **copy** | `verified` |

**Bottom line.** After five passes: three public CS2 source trees exist at very
different completeness (**Weave** — the only CS2 feature implementation;
**Fatality** — a real CS2 base with no combat code; **Plaguecheat** — a
*protection* loader, not features), one **readable top-tier ragebot in CS:GO**
(Fatality: point generation, hit chance, penetration model, safe-point tiers,
and a resolver as a hypothesis set updated from server feedback), one real
**gamesense CS:GO module**, and a queue of binary dumps whose authenticity
ranges from plausible to disputed. No public artifact contains a **damage-field**
ragebot — the damage field stays ours — but the *point scoring*, the *safe-point
definition* and the *resolver update rule* are now known, and they are the
shape our exactness gap has been missing. See
[`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) §9 for the ten
techniques that transfer. The **Gamesense CS:GO DLL is real and obtainable**;
for **Gamesense CS2** the only public items are a README-only claim and
malware-adjacent bundles — no verified dump.

---

## 1. How to read the inventory

Artifact types are not interchangeable. The four that matter:

| Type | What you get | RE cost | Typical tell |
|---|---|---|---|
| **Source tree** | Names, structure, logic, constants | Read + build | `.cpp/.hpp`, a project file, no `.text` to reconstruct |
| **Reconstructed PE** | Real compiled code, imports, strings | Medium — decompile | Valid PE headers, section table, imports |
| **Raw memory dump** | Real compiled code, **no** headers/imports | High — must rebuild the PE first (our Neverlose flow) | `.bin`, a base address, `SizeOfImage` |
| **Unprotected build / loader** | Shipped code with protection removed | Low–medium | Loader + module, no VM/obfuscation |

A **loader/crack** is not a cheat dump. It contains the injection and
communication path, while the paid module is fetched or decrypted at runtime. It
answers loader questions, not feature questions.

Two traps in this space, both present in the list below:

1. **Era mismatch.** CS:GO sources dominate because CS:GO was leakier. They use
   `C_CSPlayer`, `IVModelRender`, `IStudioRender`, `CGlowObjectManager`, netvars
   and pre-subtick movement. They are useful for *architecture*, misleading for
   *current offsets and semantics*.
2. **SEO/scam repos.** A GitHub repo named `GamesenseCS2-2026` with 2–3 KB of
   content is a placeholder, not a leak. Several exist. See §5.

---

## 2. Tier 1 — CS2 source trees

### 2.1 Weave v3 (`cacamelio/weave-v3-csgo-and-cs2`)

- **Size / last push:** ≈37 MB (GitHub size field), last push 2025-05-19.
  `verified`
- **Layout:** a CS:GO tree and a real CS2 tree under `cs2/counterstrike2/`, with
  `feature/chams`, `feature/ragebot`, `feature/antiaim`,
  `feature/lagcompensation`, `feature/penetration_system`,
  `feature/prediction_system`, `hooks`, `renderer`, `sdk`. `verified` (tree read)
- **Why it is first:** it is the only public tree that is both CS2-era and
  feature-complete in the areas we have open — in particular **chams**, which our
  payload has as config only (`ChamsMode`, `chams_color`) with no implementation,
  and which Neverlose answered with a custom renderer rather than a material
  override.
- **Caveats:** `inferred` — the repo name says “v3 csgo and cs2”, so the CS2 tree
  may be a port-in-progress rather than a shipped build; expect stale offsets,
  and treat any interface/vtable index as needing verification against the
  current binary. Provenance is unstated (leak vs community rebuild); check for
  build files and duplicated open-source code before trusting it as “what Weave
  ships”.
- **What to read first:** `feature/chams` and `renderer` (our T7 open question),
  then `feature/penetration_system` vs our `payload/aim/src/penetration.cpp`.
  **Both have now been read** — see [`cs2-chams-t7.md`](cs2-chams-t7.md) and
  [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) (the ragebot, hitchance,
  lagcomp, prediction and the CS2 command plumbing).

### 2.2 Primordial (`qcold/primordial`)

- **Size / last push:** ≈39 MB, 2025-07. `verified`
- **What it is:** a **crack/loader** for the Primordial CS2 cheat. `primo/primo.cpp`
  carries a hardcoded module base `0x1D8CBC30000` and hooks `connect`, `recv` and
  `send`; resources and the loader live in the same tree. `verified` (content read)
- **What it is not:** there is no static feature source — the cheat module itself
  is not in the repo in readable form. `verified`
- **Value:** the only modern, public CS2 **manual-map + network-hook** implementation
  we can read. Our loader is already written, so this is comparative only.
- **Related:** `Skeetcrackuser1/primordial-source` (≈9 MB, 2025-02) and a
  `primordial.bin` + mapper pair posted on YouGame (thread 336786, 2024-12-06,
  `snippet-only`).

### 2.3 Fatality CS2 base (`notphage/fatality.win-CS2`)

- **Size / dates:** 268 files, created and pushed 2024-01-18, 32 stars at read.
  `verified` (tree + contents)
- **What it is:** a genuine **CS2** internal base, not the CS:GO tree. README is a
  one-line taunt (“Idk how your cheat source code gets leaked twice by the same
  person”), which matches the two-leak history. `verified`
- **Layout:** `include/sdk/{generated.h,offsets.h,offsets_schema.h,proto.h}` plus
  `schema/{client,engine2,global_types,particles,server,worldrenderer}.h`;
  `src/game/{cfg,draw_manager,game,hook_manager,movement,visuals}`;
  `src/hooks/client.cpp`; `src/gui`, `src/lua`, `src/menu`, `src/resources`,
  `src/sdk`, `src/utils`. `verified`
- **What it actually implements:** `prediction_update`, `create_move`,
  `frame_stage_notify`, `override_view`, `get_fov` and `on_render_start` hooks;
  ESP/visuals (`visuals.cpp` reads `game_entity_system`, player pawns, bounding
  boxes, name/health/weapon), movement (bunnyhop/strafe), config, GUI, Lua engine.
  `verified` (source read)
- **What it does not have:** no `draw_model`/`DrawModelExecute` hook, no chams, no
  ragebot, no antiaim, no resolver. The `game/` folder has no combat files at all.
  `verified`
- **Updated fork:** `YEAHMAN563/fatality.cs2` — 346 files, 2026-10-17, adds
  `contrib/minhook`, `contrib/renderer`, `contrib/steam_sdk`, LuaJIT, and “fixed
  the patterns offsets schemas”. Its README states it “doesn’t have any features
  yet”, which is consistent with the base above. `verified` (metadata + README)
- **Value:** the cleanest public **CS2 SDK/offset/hook skeleton** (schema headers,
  interface access, subtick `create_move` shape). Read it for plumbing, not for
  combat features. It supersedes the “partial/older” characterisation in §4.

### 2.4 Plaguecheat reviver (`csco1337/plaguecheat`)

- **Size / dates:** 294 files, created 2026-07-10. `verified` (tree)
- **README:** "plaguecheat-mapper — recompiled with asmjit fuck veh". `verified`
- **What it actually is — corrected after reading it:** a **module reviver**, not a
  cheat. `plague/src/core/segment/reviver/reviver.{cxx,hxx}` (39 KB) implements:
  `extract_segment` (allocates `PAGE_EXECUTE_READWRITE` and copies the segment),
  `resolve_imports` (via `GetProcAddress`/`LoadLibraryA`), `setup_veh` (custom
  vectored exception handler), `setup_patches`, `setup_cvar_interface`,
  `setup_stack` (fabricated stack), `invoke_oep` (original entry point), plus a
  `k_snapshot_path` for “snapshot spoofing” — the healthy snapshot emitted by
  `EmuTrace.DumpSnapshot`. The VEH comment lists what it bypasses: page guards,
  `KUSER_SHARED_DATA`, `CPUID`, `HYPERVISOR_SHARED_DATA`, `PEB`. `verified`
- **Why it matters:** Plaguecheat protects its module behind an **emulated
  execution trace** whose snapshot must be replayed, and the reviver defeats it
  with a custom VEH and JIT-friendly rebuild (hence "asmjit … fuck veh"). That
  is a concrete protection technique, and it is the *only* protection detail in
  this inventory that is readable rather than inferred.
- **What it is not:** no features, no ragebot, no ESP. Do not read it expecting
  a cheat. `verified`

See [`plaguecheat-protection.md`](plaguecheat-protection.md) for the write-up.

---

## 3. Tier 2 — CS2 binary dumps

None of these were downloaded; the table records what the poster claims. All would
need the Neverlose reconstruction flow (find the PE headers, rebuild sections and
imports, then decompile) before they are readable.

| Product | Thread | Posted | Claimed artifact | Notes |
|---|---|---|---|---|
| **Fatality** | HvH `threads/fatality-win-cs2-dump-2025-for-ida.6491` | 2025-04-17 | dump “for IDA”, reposted from YouGame | Best candidate of the binary tier: top-tier ragebot, 2025 build, a poster who names IDA explicitly |
| **Fatality** | HvH `threads/fatality-win-dump-08-09-2026.13730` | 2026-09-09 | `fatdump.bin.zip`, 12.9 MB, base `0x19AAF8B0000`, `SizeOfImage 0x3322000`, entry RVA `0x266F1E4`, password `hvh.net` | **Disputed** — replies call it fake. The metadata is self-consistent (base + image size + entry point), which is what a real x64dbg dump looks like, but that is also easy to fabricate |
| **Memesense** | HvH `threads/memesense-last-dump-2k25.6492` | 2025-04-18 | dump | Memesense was widely cracked/free, so dumps are plausible (`inferred`) |
| **Memesense** | HvH `threads/memesense-latest-cs2-2025-dump-for-ida.6713` | 2025-05-15 | dump “for IDA” | Newer of the two |
| **Azura** | HvH `threads/azura-full-dump.12508` | 2026-06-21 | “full dump … just put it on IDA” | No reconstruction, no import fix claimed |
| **Plaguecheat** | HvH `threads/plaguecheat-dumped-from-cs2-exe-not-fixed.13330` | 2026-08-17 | raw x64dbg `.bin`, “not fixed” | Expect no imports; highest reconstruction cost |
| **Clarity** | HvH `threads/clarity-tk-v3-v2-dump-or-whatever-no-imports.13808` | 2026-09-14 | partial dump, “no imports” | Self-described incomplete |

### 3.1 GitHub-hosted CS2 binaries (cracks)

These are *unprotected builds*, not raw dumps: the module is shipped ready to
inject, so the RE cost is lower — but this is also the standard malware vector.
Repository size is GitHub's size field; where a DLL size is stated it came from the
raw HEAD response. Authenticity is `unverified` for every row.

| Product | Repository (created) | Artifact | Notes |
|---|---|---|---|
| **Primordial** | `MStackj/Primordial-Crack` (2025-10) | `primordial.dll` + `steam_module.dll`, ≈8.5 MB repo | “By anonteam” |
| **Primordial** | `saveliimrsamurai-commits/Primordial_Crack` (2026-04) | `femordial.dll`, ≈10.5 MB repo | Single DLL |
| **Primordial** | `SmiFy412/PrimordialCrack` (2024-12) | `primordial_reverse.dll` + `Primordial/` | “Primordial recode” |
| **Primordial** | `dinaq5080-wq/Gamesense-Neverlose-primordial` (2026-08) | `primodial.dll`, **23.8 MB** | Account also hosts repos named `VIRUS`/`virus1` — treat as contaminated |
| **Aimware** | `Miki14B/Aimware-Cs2-Crack` (2025-04) | `Aimware.zip`, ≈11.6 MB repo | “Use at your own risk” |
| **Aimware** | `hugissvart/Aimware` (2025-03) | `slipknot.dll`, ≈11.8 MB repo | “Aimware crack Cs2 cheat” |
| **Aimware** | `zawertuoz/soft-cs2-aimware-crack` (2025-05) | `AimWare.exe` | Loader-shaped |
| **Plaguecheat** | `Firzex/plaguecheat` (2024-09) | `plague.dll` + `steam inject.dll` | “crack by anonteam” |
| **Fatality** | `atraenop/FATALITY-CRACKED` (2025-03) | `Fatality crack.zip`, ≈13 MB repo | |
| **Fatality** | `KlodHas/FatalityCrack-dll` (2025-04) | `FatalityCrack.exe`, 3.2 MB repo | Loader, not a module |
| **Fatality** | `Bitun122/fatality-crack` (2025-06) | ≈51.7 MB repo | |
| **Fatality** | `kaarle234-lgtm/Fatality-Crack` (2026-09) | ≈11.3 MB repo | README: “the exe file should take you to the loader” — red flag |
| **Neverlose (CS:GO)** | `hvhinvestor/Neverlose-crack` (2026-05) | `neverlose.dll` (PE32 x86, entry RVA `0x306A78`) + Russian RE notes | Confirms CS:GO module internals (Lua, DX9, manual map); **not CS2** |
| **Mixed** | `ibcsz12/Endless-Loader-csgo` (2026-08) | ≈220 MB C# loader tree | Bundles “crack of primordial” and “crack of neverlose” |

The **Gamesense CS:GO DLL** is the one class where the binaries are old enough to
be well known: `CherryBarracuda/gamesense` (2024-01) ships `gamesense.dll`
(3.42 MB) and `RAdam1233/GamesenseDLL` (2022-10) ships `gs.dll` (2.51 MB). Both are
CS:GO-era module sizes and are documented by the community as gamesense cracks.
`verified` (listing + size); provenance `inferred`.

### 3.2 Acquired CS2 modules (static triage)

Three CS2-era module binaries were fetched, hashed and PE-checked, and kept in
`C:\Users\gpmpo\nea\quarantine\cs2-modules\`. **None was executed.**

| File | Size | SHA-256 | PE facts | Verdict |
|---|---|---|---|---|
| `femordial.dll` (Primordial crack, 2025-12-28) | 23,786,496 | `2c53efde9b9cc5f5eab54d30aef44c4e7a6a4eedd4abdb2cfbe95dba186792dd` | PE32/x86, `.text` 0x5C4C27 (6.0 MB), `.rsrc` 17.7 MB, no exports, 9 imports (no `d3dx9`, no `WS2_32`) | **6 MB of real code, but string-obfuscated**: a local ASCII sweep of 1.03 M strings yielded 75 snake_case identifiers, only 3 meaningful (`hit_player`, `hit_pos`) plus a `curl` artifact. Plaintext feature names are absent. |
| `primordial_mstackj.dll` (2024-12-07) | 19,817,984 | `e380b8ac8d0894c0f0d3ad46f9156dab288e2d47a4f2dd973f20b3377827b44b` | PE32/x86, `.text` only 649 KB, **`.rsrc` 18.9 MB** | The payload is almost certainly **inside the resources**; a resource-embedded module would need extraction before analysis. |
| `steam_module.dll` (2024-11-28) | 20,480 | `294914bd5ed9b63cada2f0f792b89f202ef19320542874cd6653d746bf74ed77` | PE32/x86, 10 KB of code | A tiny helper, not the module. |

**Two things worth recording.**

1. `femordial.dll` and the `primodial.dll` in the `dinaq5080-wq` bundle are
   **byte-identical in size** (23,786,496) — almost certainly the same file, so
   that bundle's "primordial" is this artifact, not a separate build.
2. A CS2 cheat module from 2025 is **not automatically analysable**: the
   protection that gamesense (CS:GO) does *not* have is present here, in the
   form of runtime string encryption. Our Neverlose work (§
   [`neverlose-mining.md`](neverlose-mining.md)) found the same shape — opaque
   predicates and an encrypted section — and needed a dedicated deobfuscation
   pass. Anything we want from this module would cost the same.

**Not pursued:** downloading the remaining crack archives (Aimware, Fatality,
Plaguecheat) — they are the same shape, and the CS:GO module already gave us
the config-vocabulary answer. The CS2-native value came from the Weave
*source*, not from a binary.

**Authenticity triage.** For any of these, the cheap checks before spending time:

1. `sha256` on arrival; search the hash (VirusTotal lookup, never upload).
2. Does the blob start with `MZ` at offset 0? If not, is there a PE header at the
   claimed base offset (our Neverlose image needed a seeded reconstruction)?
3. Section table sanity: `SizeOfImage` ≥ sum of virtual sizes, entry RVA inside
   `.text`, timestamps in a plausible range for the claimed build.
4. Imports: an empty or 3-entry import table on a 30 MB image means the dump is
   unfixed, not that the cheat has no imports.
5. Strings: product name, build date, and `CS2`-era interface names
   (`Source2Client`, `SchemaSystem`, `GameEntitySystem`) must all be present.

Fatality 2026 fails step 5 only if the replies are right — that is exactly what the
check settles.

---

## 4. Tier 3 — CS:GO-era sources (historical technique reference)

These are complete, readable implementations, but **CS:GO only**. Use them to
learn shape and ordering, never to lift constants or interface indices.

| Product | Repository | Size | Last push | Notes |
|---|---|---|---|---|
| **Fatality** | `cacamelio/fatality-source-fixed` | ≈89 MB, 645 entries | 2026-07-12 | The single most useful CS:GO tree here. `features/chams.cpp` (31 KB) is a full material-override implementation: `FindMaterial`, `ForcedMaterialOverride`, `AlphaModulate`, `MATERIAL_VAR_IGNOREZ`, `ColorModulate`, `DrawModel` ghosts; `hooks/draw_model_execute.cpp` is the hook; also `aimbot.cpp`, `hitscan.cpp`, `resolver.cpp`, `antiaim.cpp`, `autostrafer.cpp`, `tickbase.cpp`, `wall_penetration.cpp`, `setup_bones_calls.cpp`, `render_view.cpp`, `present.cpp`. `verified` (content read) |
| Fatality (older) | `sezzyaep/Fatality.win-Source` (≈3.5 MB, 2023-12), `linwys/Fatality-Framework` (≈5 MB, 2024) | — | — | Partial/older CS:GO trees. The 2024 **CS2** base is now §2.3, not this row. `verified` (metadata) |
| **Gamesense** | `Bros070/gamesense-source` (≈5 MB), `avasterisback/SkeetSDK` (`skeet-master.zip`, 5.8 MB, 2026-10), `sogod1888-web/skeet-source` (`skeet (1).zip`, 3.6 MB, 2026-10) | — | — | CS:GO source. `h4xrOx/gamesense.pub` is **not** the cheat: it is the gamesense.pub **forum/backend** dump (5,544 files incl. `Skeet-Loader.exe`, `auth.sql`, admin PHP/JS). The `*.zip` repos are unopened. `verified` (listing) |
| **Gamesense (DLL)** | `CherryBarracuda/gamesense` (`gamesense.dll`, 3.42 MB, 2024-01), `RAdam1233/GamesenseDLL` (`gs.dll`, 2.51 MB, 2022-10) | — | — | The actual **CS:GO** modules. Community crack ecosystem (underical/unk.is, QHide, Expandera/RAZE) ships loaders around them. `verified` (listing + size) |
| **Aimware** | `3kacnap/aimware-hypr` (≈12 MB, 2024-11) | — | — | Loader tree with a `.hdmp`/`.hseg` memory-dump pair plus `aimware/` source; CS2-vs-CS:GO era unverified |
| **Onetap** | `h4xrOx/onetap_v4-crack`, `Chief9917/OTC-Source`, `xTR0JAN/onetap.su-CSGO` | — | — | v2/v4, CS:GO |
| **Legendware** | `CSGOLeaks/Legendware-V3`, `K4HVH/LWK` | — | — | v2/v3, CS:GO |
| **Nixware** | `Shaxzy/NIXWARE-CSGO` (≈12 MB), `Oglinder/-nixware-loader-leak` (≈4.6 MB) | — | — | CS:GO |
| **Rawetrip** | 2021–2022 repos | — | — | CS:GO |

**Era markers.** If a tree mentions `C_CSPlayer`, `IVModelRender`,
`IStudioRender`, `CGlowObjectManager`, `m_iTeamNum`-style netvars, or
`CreateMove` without subtick, it is CS:GO. Fatality's tree has all of these —
hence the CS:GO classification despite the 2026 push date.

---

## 5. Not artifacts — placeholders, scams, and dead ends

Recorded so nobody re-investigates them:

- **`GamesenseCS2-2026`-named repos** — 2–3 KB each, no code. SEO/scam
  placeholders, and the survivors point at the same external archive host
  (`gitgora.live/skeetcs2`, password `2026`): `d-nsh/GameSense-Skeet-CS-2026`,
  `SmuttyHackr/GamesenseCS2-2026`, `superphaserecord/GamesenseCS2-2026-rpocket-team`,
  and the same pattern in `neverLcs/neverlose` (34 KB). `verified`
- **`Ventrundershien/CS2-WH`** — README-only SEO page with screenshots, no archive
  in the repo; claims “Skeet CC 2026” with a driver and VAC bypass. `verified`
- **`demongabrych/gamesense-cs2-dump`** — README only: “Dump of … gamesense/skeet.
  Its not fully cracked…”. No file, no link, no hash. This is the only public claim
  of a Gamesense **CS2** dump and it contains nothing. `verified`
- **`Sewazew/SkeetCrack`** — 488 KB repo containing `Skeet Crack.zip` (loader);
  “Cheat for CS2 … Loader is in winrar file.” Not a dump. `verified`
- **`dinaq5080-wq/Gamesense-Neverlose-primordial`** — the only GitHub bundle with
  a `skeet.dll` (13.9 MB) next to `primodial.dll` (23.8 MB), but the same account
  hosts repos literally named `VIRUS`/`virus1`. Treat as a malware bundle, not a
  leak. `verified` (listing); authenticity `unverified`
- **`Brickpebewitch/primordial-cs2-dump-toolkit`** — name-squats “Primordial”; it is
  an offline entity dumper, not the cheat. `verified`
- **`Jollycistaken/cheat-dumps`** — 20 DLLs + 1 EXE named by hash, README is one
  line (“Cheat dumps for analysis purposes”); no product attribution, so it cannot
  be ranked. `verified` (listing)
- **`lukrhtew1/Dumps-and-analysis-of-disconnect-cheats`** — a genuinely good
  full dump + writeup of the **Disconnect** cheat (loader, `cormem.sys` driver,
  IOCTLs, obfuscation), but that product is a **Rust** cheat, not CS2. Useful as a
  methodology model only. `verified` (README read)
- **Fatality 2026 dump** — disputed in-thread; see §3.
- **Forum “fix” binaries and loaders** — the standard malware vector in this
  space. The Fatality source repo's README links an external site
  (`meliohub.github.io`); that pattern is a red flag, not a download instruction.

**Not found (looked, did not find — which is not proof it does not exist):**
a **verified** public CS2 dump or source for Gamesense, Onetap, Nixware,
Legendware, Midnight, Pandora or Platinum. For Gamesense CS2 specifically, the
public trail is a README-only claim plus malware-adjacent DLL bundles; the real,
obtainable Gamesense artifact is the **CS:GO** module.

---

## 6. Fit against our open questions

| Our open question | Best public reference | How much it actually helps |
|---|---|---|
| **Chams / material override** (T7) | **Answered** — see [`cs2-chams-t7.md`](cs2-chams-t7.md) | **High.** CS2 path: KV3 material creation (`materialsystem2.dll` pattern + `KeyValues3`/`LoadKV3`), then a **per-scene-object** override (`scene_data->material`) inside the `scenesystem.dll` `draw_scene_object` hook, colour written at `scene_data+0x40`, two passes (visible + `F_IGNOREZ`). CS:GO contrast (gamesense): VMT + global `OverrideMaterial` + one flag (`0x8000`). Every index still needs re-verification against the live client. |
| **Ragebot exactness** (occlusion, penetration, damage field) | Fatality CS:GO `hitscan`/`wall_penetration`/`resolver` ([`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md)); Weave CS2 `penetration_system` ([`weave-cs2-ragebot.md`](weave-cs2-ragebot.md)) | **Medium-high, and new.** The shape is now known: candidate polygons are computed **per hypothesis** and the safe point is the *intersection* across hypotheses, scored by the surviving **area**; the resolver is a hypothesis set pruned by server feedback (miss ⇒ eliminate hypotheses that would have hit). The damage field stays ours — but the *point scoring* (shots-to-kill, safety area, "HP+N" min-damage convention) and the *hypothesis dimension* are adoptable. |
| **Safe points / resolver** | Fatality CS:GO safety tiers + hypothesis set ([`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) §6, §7) | **High** — this is the concrete shape our exactness gap has been missing: three safety tiers, a per-hypothesis polygon intersection, and a server-feedback elimination rule. The *generator* of hypotheses is CS:GO-specific; the *rule* is not. |
| **Anti-aim** | Weave `feature/antiaim`; Fatality `antiaim.cpp` | **Low.** CS2 server gating and subtick change what is expressible; the CS:GO shape does not transfer cleanly. |
| **Movement / autostrafe** | Fatality `autostrafer.cpp`; Weave movement code | **Low.** Our open bug is a changed default that moved the strafe onto a server-gated path ([`../handoff-wave2.md`](../handoff-wave2.md) §5.1), not a missing technique. |
| **Schema / ESP class resolution** | Weave `sdk`; Neverlose already answered the class-chain walk | **Low.** [`neverlose-mining.md`](neverlose-mining.md) §7.6 covers it. |
| **Loader / protection** | Primordial loader; **Plaguecheat reviver** ([`plaguecheat-protection.md`](plaguecheat-protection.md)) | **Medium.** The reviver is a readable protection *loader*: segment extraction, IAT rebuild, OEP, fabricated stack, custom VEH, snapshot spoofing. Our loader is written; the transferable idea is environment validation (`PEB`/`CPUID`/`KUSER_SHARED_DATA` checks). |
| **Renderer / D3D11 present hook** | Weave `renderer`; Fatality `present.cpp` | **Medium**, if we touch the overlay. |

The honest summary: of the five areas where we still have work, only **chams** is
materially advanced by public artifacts. Everything else is either already answered
by Neverlose, already surveyed in [`autowall-implementations.md`](autowall-implementations.md),
or is our own measurement problem.

---

## 7. Recommended order

1. **Implement the T7 chams path** from [`cs2-chams-t7.md`](cs2-chams-t7.md) —
   KV3 material creation + the `draw_scene_object` override, with a pass-model
   fallback. Re-verify every index against the live client first.
2. **Implement the CS2 command-acceptance checklist** from
   [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) §9 — input-history angle
   writes (silent aim), attack-history index + `ATTACKnSTART` bits, the
   `override_tick` fake tick, and the move CRC when the movement is modified.
   This is server-acceptance plumbing, not aim math, and it is the part we are
   most likely missing.
3. **Try the hash-seeded hit test** (same note, §3) — if the engine's shot
   hash is computable, the hit test becomes exact and cheap instead of a
   256-sample Monte-Carlo. This is the highest-value single experiment left.
4. **Adopt the point-scoring rules** from
   [`fatality-csgo-algorithms.md`](fatality-csgo-algorithms.md) §9 — these are
   small, self-contained changes to our solver, in descending value:
   (a) score candidate points by the **area of the surviving visibility
   polygon** rather than a boolean; (b) rank by **shots-to-kill**, not raw
   damage; (c) **pin the spread samples to the intended hitbox/hitgroup** so
   the hitchance percentage means what the user thinks; (d) adopt the
   **"mindmg > 100 ⇒ target HP + N"** convention and cap the threshold by the
   target's health; (e) add the **"+5 HP per extra penetrated surface"**
   world-drift correction.
5. **Design the hypothesis dimension** (same note, §6–§7) — a per-target
   hypothesis set, the intersection of their visibility polygons as the safe
   region, and a server-feedback rule that eliminates hypotheses. The
   *generator* is the open design question in CS2 (timing? position? which
   side is the target on?), and the autowall-as-oracle trick
   (§9.10 of that note) is the cheapest way to get a first hypothesis.
6. **Adopt the industry config vocabulary** from
   [`fatality-cs2-base.md`](fatality-cs2-base.md) §3 — `safety` conditions,
   `fallback_mode` (off / adaptive / prefer_safepoint), `autostop` (lethal /
   inbetween / predictive), the 7 hitboxes, `pointscale`, `mindmg` — cross-read
   with the gamesense registry ([`gamesense-csgo-re.md`](gamesense-csgo-re.md)
   §1.1) and the Weave names (`weave-cs2-ragebot.md` §14).
7. **Re-derive the server's interpolation maths** (Weave §7) — it is a
   decompiled copy of the server's own lerp, and our backtrack window should be
   bounded the way theirs is (`maxunlag/3`, `/2`, `/1`).
8. **Fatality CS2 dump 2025** — only if a question needs the *shipped* behaviour
   (resolver, antiaim) rather than an architecture. Reconstruct with the Neverlose
   flow, then decompile; budget it like Neverlose.
9. **Memesense dumps** — smaller, plausibly authentic; useful if Fatality 2025 is
   not.
10. **Azura / Plaguecheat / Clarity** — raw, unfixed, self-described incomplete.
    Only for a specific symbol someone needs. (Note: the *Plaguecheat source* is
    protection, not features — see §2.4.)
11. **CS:GO provider sources and the Gamesense DLL** — for a known-good
    architecture when a specific subsystem is unclear. The gamesense module has
    now been reversed at the architectural level; use that note, not a new pass.
12. **License-clean CS2 code** — `T1GxR/Axion-CS2-RAGE-CHEAT` (MIT),
    `nildotdev/leerware` (MIT), `soren-source/trap-cs2` (MIT),
    `Valthrun/valthrun-cs2` (GPL-2.0, ideas only). This is the only tier we may
    actually copy from; prefer it whenever it covers the need.

**Do not** spend time on the Gamesense CS2 claims (README-only or malware-adjacent),
the 2026 Fatality zip before the hash/PE check, or any “fixed” forum binary.

---

## 8. Handling procedure and risk

The project scope is offline `-insecure` CS2 and static analysis; nothing here
requires running a third-party binary. If one is ever fetched:

1. **Decide first whether source answers the question.** Source is cheaper and
   safer; most of §6 is answerable from source.
2. **Quarantine outside the repo** — e.g. under `C:\Users\gpmpo\nea\` where the
   Neverlose material already lives. Never inside the working tree: `*.bin` is
   gitignored but archives are not, and these are tens of MB of copyrighted code.
3. **Hash on arrival**, before extraction. Record `sha256`, file type, archive
   listing, and the password source.
4. **Look the hash up** on VirusTotal; do **not** upload the file — uploading
   publishes the sample and can breach the forum's terms.
5. **Static only.** No execution, no loader, no “crack fix”, no DLL registration,
   no `rundll32`. If dynamic analysis is ever unavoidable, use a disposable VM
   with no network and no shared folders.
6. **Describe, never copy.** Findings go into notes as addresses, function shapes
   and constant *values* — the same rule as [`neverlose-mining.md`](neverlose-mining.md).
   No decompiled bodies, no lifted code, no artifacts committed.

**Legal.** Leaked commercial software is copyrighted; analysis is a gray area in
most jurisdictions and redistribution is straightforward infringement. Treat every
artifact as reference material for interoperability research, not as a source of
code. Forum archives in this scene are a well-known malware vector, and a
password-protected zip from an anonymous poster is exactly the shape of a RAT
dropper.

---

## 9. Open questions

- **Weave provenance and completeness.** *Partly answered* by
  [`cs2-chams-t7.md`](cs2-chams-t7.md): the CS2 tree's chams path is internally
  consistent and complete enough to implement from. Provenance (leak vs port) is
  still `unverified`, so treat every index as needing re-verification.
- **Gamesense CS2.** The only GitHub bundle (`dinaq5080-wq/...`) ships a 13.9 MB
  `skeet.dll` next to a 23.8 MB `primodial.dll`, but the account also hosts repos
  named `VIRUS`/`virus1`. Cheap settlement without execution: 4 KB HTTP range read
  of the PE header (machine, timestamp, sections, import directory), `sha256`, and
  compare its shape with the known 3.42 MB CS:GO `gamesense.dll`. Until then it is
  `unverified` and unsafe.
- **The Plaguecheat trace emulator.** The reviver is readable, but the producer
  (`EmuTrace.DumpSnapshot`) is not in the tree, and the VEH's spoofed values
  (`PEB`/`CPUID`/`KUSER_SHARED_DATA`) are described only by a comment. `unknown`
  — see [`plaguecheat-protection.md`](plaguecheat-protection.md).
- **`Jollycistaken/cheat-dumps`.** 20 hash-named DLLs and one EXE with no
  attribution. Identify from PE metadata (product strings, imports, timestamps)
  before ranking.
- **Fatality 2025 dump authenticity.** Needs the hash + PE-header check in §3; the
  thread is `snippet-only` from here.
- **Fatality 2026 dump.** Settle the “fake” claim with the same checks before
  anyone spends time on it.
- **Aimware `aimware-hypr` era.** The dump pair may be CS:GO; the repo date alone
  does not settle it. The 2025 CS2 cracks in §3.1 are a separate, also-unverified
  set.
- **Current-build dumps.** Almost everything public is 2025 or early 2026. A
  current-build artifact would need a fresh thread, not an old one.
- **The ragebot's *implementation*.** Three independent sources now agree on the
  **shape** (hitbox set → point → safety predicate → damage/hitchance gates →
  stop), and the fourth — the Weave CS2 source — finally shows an
  **implementation**: a 256-sample hitchance over the game's own RNG, a
  hash-seeded exact alternative, engine-priced penetration, and a lag-record
  backtrack. It is a CS:GO algorithm in CS2 clothing, with several broken
  parts, so the exactness gap is still ours to close. See
  [`weave-cs2-ragebot.md`](weave-cs2-ragebot.md) §13.
- **The engine's shot hash.** Weave's `no_spread()` path asks the game for the
  hash of the current shot and then tests one trace — if that hash is
  computable outside the game, we get an *exact* hit test. The single
  highest-value unknown found in this pass.
- **Weave's `.TMP` merge backups** hold a *different revision* of several
  modules (they are larger than the checked-in files) — a diff may recover a
  working multipoint implementation, since the checked-in one is broken by a
  double division.
- **The CS2 module binaries** (§3.2) are string-obfuscated; extracting value
  from them would cost a deobfuscation pass comparable to our Neverlose one.
  Lower priority than the source.
