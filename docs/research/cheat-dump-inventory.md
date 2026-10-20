# Third-party CS2 cheat artifacts — public inventory and triage

> Research note, 2026-10-20. Companion to
> [`neverlose-mining.md`](neverlose-mining.md): that note mines the one artifact we
> already hold; this one answers *what else is public*, what each item actually is,
> and which one is worth reversing next.
>
> **Second pass (same day).** A deeper repository sweep added the GitHub-hosted
> binaries in §3.1, the Fatality CS2 base (§2.3), the Plaguecheat tree (§2.4) and
> the Gamesense CS:GO DLL evidence (§4). The ranking is unchanged, but “the only
> public CS2 source is Weave” no longer holds: there are now three, at very
> different levels of completeness.
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
| `cacamelio/fatality-source-fixed` | **source tree** | **CS:GO** | 2026-07-12 | Complete material-override chams + full ragebot/resolver/antiaim architecture — on the old API | `verified` (content read) |
| `notphage/fatality.win-CS2` | **source tree (base)** | CS2 (2024-01) | 2024-01-18 | Real CS2 SDK, hooks, ESP, movement, GUI and Lua plumbing; **no ragebot, chams or antiaim** | `verified` (content read) |
| `csco1337/plaguecheat` | **source tree (recompiled)** | CS2 (2026-07) | 2026-07-10 | 294-file Plaguecheat tree with asmjit, rebuilt to defeat VEH; feature completeness not yet read | `verified` (tree); features `unverified` |
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

**Bottom line.** Three public CS2 source trees exist, at different completeness:
**Weave** (feature-complete), **Fatality** (a real CS2 base with no combat
features) and **Plaguecheat** (a 2026 recompiled tree whose feature set is not yet
read). There is also one complete CS:GO implementation that matches our chams plan
(Fatality) and a queue of binary dumps whose authenticity ranges from plausible to
disputed. The **Gamesense CS:GO DLL is real and obtainable**; for **Gamesense CS2**
the only public items are a README-only claim and malware-adjacent bundles — no
verified dump. No public artifact contains a damage-field ragebot; the exactness gap
in [`aim-solver-design-review.md`](aim-solver-design-review.md) stays ours to close.

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

### 2.4 Plaguecheat recompiled tree (`csco1337/plaguecheat`)

- **Size / dates:** 294 files, created 2026-07-10. `verified` (tree)
- **README:** “plaguecheat-mapper — recompiled with asmjit fuck veh”. `verified`
- **Layout:** `plague/src`, `plague/deps/asmjit` (full asmjit), `revive-engine.sln`,
  `.clang-format`. This is a buildable C++ project, not a dump. `verified`
- **Caveat:** “recompiled with asmjit” suggests a rebuilt/ported binary rather than
  the vendor's own source, and the feature set was not read for this note. Do not
  rank it above Weave until `plague/src` is inspected. `inferred`

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
| **Chams / material override** (T7 open; our tree is config-only) | **Weave CS2 `feature/chams` + `renderer`**; Fatality CS:GO `features/chams.cpp` + `draw_model_execute.cpp` | **High.** Two complete implementations, one CS2-era, one CS:GO-era. Fatality's CS2 base has **no** draw-model hook or chams (`verified`), so Weave remains the only CS2 reference. This is the strongest reason to read Weave. |
| **Ragebot exactness** (occlusion, penetration, damage field) | Fatality CS:GO `hitscan.cpp`, `wall_penetration.cpp`, `resolver.cpp`; Weave `penetration_system`, `lagcompensation` | **Low for exactness, medium for structure.** [`autowall-implementations.md`](autowall-implementations.md) already found that public implementations use per-point traces and none builds a damage field. These trees do not change that conclusion. |
| **Anti-aim** | Weave `feature/antiaim`; Fatality `antiaim.cpp` | **Low.** CS2 server gating and subtick change what is expressible; the CS:GO shape does not transfer cleanly. |
| **Movement / autostrafe** | Fatality `autostrafer.cpp`; Weave movement code | **Low.** Our open bug is a changed default that moved the strafe onto a server-gated path ([`../handoff-wave2.md`](../handoff-wave2.md) §5.1), not a missing technique. |
| **Schema / ESP class resolution** | Weave `sdk`; Neverlose already answered the class-chain walk | **Low.** [`neverlose-mining.md`](neverlose-mining.md) §7.6 covers it. |
| **Loader / protection** | Primordial loader | **Informational.** Our loader is written; adopt nothing. |
| **Renderer / D3D11 present hook** | Weave `renderer`; Fatality `present.cpp` | **Medium**, if we touch the overlay. |

The honest summary: of the five areas where we still have work, only **chams** is
materially advanced by public artifacts. Everything else is either already answered
by Neverlose, already surveyed in [`autowall-implementations.md`](autowall-implementations.md),
or is our own measurement problem.

---

## 7. Recommended order

1. **Weave CS2 tree** — read `feature/chams`, `renderer`, `feature/penetration_system`.
   Answer the T7 question (“material override under CS2: which material, which
   flags, when in the frame”) and diff the penetration structure against ours.
   No execution, source only.
2. **Fatality CS:GO tree** — read `features/chams.cpp` and `draw_model_execute.cpp`
   for the canonical material-override sequence, and `hitscan/wall_penetration` for
   ordering. Treat as historical; do not port constants.
3. **Fatality CS2 base** (`notphage/fatality.win-CS2`) — cheap, high-signal read:
   `include/sdk/*` (generated offsets, schema headers), `hooks/client.cpp`
   (sub-tick `create_move`, prediction, frame-stage), `game/visuals.cpp`. Compare
   its entity/schema access with ours. It has no combat features, so do not expect
   ragebot or chams answers here.
4. **Plaguecheat tree** (`csco1337/plaguecheat`) — inspect `plague/src` before
   trusting it. It is the most recent public tree (2026-07) and may carry the
   freshest offsets, but it is a recompile, so verify every offset against the
   game binary.
5. **Fatality CS2 dump 2025** — only if a question needs the *shipped* behaviour
   (resolver, antiaim) rather than an architecture. Reconstruct with the Neverlose
   flow, then decompile; budget it like Neverlose.
6. **Memesense dumps** — smaller, plausibly authentic; useful if Fatality 2025 is
   not.
7. **Azura / Plaguecheat / Clarity** — raw, unfixed, self-described incomplete.
   Only for a specific symbol someone needs.
8. **CS:GO provider sources and the Gamesense DLL** — Gamesense/Aimware/Onetap/
   Legendware/Nixware trees, plus `gamesense.dll`/`gs.dll`, for a known-good
   architecture when a specific subsystem is unclear. Not a source of CS2 offsets.
9. **License-clean CS2 code** — `T1GxR/Axion-CS2-RAGE-CHEAT` (MIT),
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

- **Weave provenance and completeness.** Is `cs2/counterstrike2/` a real leak, a
  decompile, or a community rebuild? Does it build? `inferred` that it is at least
  CS2-era; unverified that it is shipped code.
- **Gamesense CS2.** The only GitHub bundle (`dinaq5080-wq/...`) ships a 13.9 MB
  `skeet.dll` next to a 23.8 MB `primodial.dll`, but the account also hosts repos
  named `VIRUS`/`virus1`. Cheap settlement without execution: 4 KB HTTP range read
  of the PE header (machine, timestamp, sections, import directory), `sha256`, and
  compare its shape with the known 3.42 MB CS:GO `gamesense.dll`. Until then it is
  `unverified` and unsafe.
- **Plaguecheat tree.** Is `csco1337/plaguecheat` vendor source, a decompile, or a
  recompile? Read `plague/src` file names and look for asmjit JIT usage. The 2026-07
  date makes it the most current public tree if it is genuine.
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
