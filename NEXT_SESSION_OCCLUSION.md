# NEXT SESSION — make the occlusion enumeration match the engine's trace **exactly**

> **✅ DONE (2026-10-18).** The goal below is met: the world geometry is now read in
> full (the missing walls were **collision meshes**, now projected as triangle soups),
> the enumeration is camera-independent (an AABB over the eye + every enemy's hitbox
> geometry, 8-unit padding), the chart is absolute world yaw/pitch, and the render
> shows only the solids *between* the eye and a target. The engine's own bullet path
> is reversed in `docs/re/bullet-trace.md`. **Resume from `docs/handoff.md`
> §"Wrap-up (2026-10-18)"**, which carries the prioritised next-actions list (feed the
> occluders into the ragebot solve; the `EntitiesAlongLinearSweep` BVH step; compound
> shapes; surface modifiers; a live trace cross-check). The text below is kept as the
> original brief.

> **Point the next agent at this file.** Self-contained. It defines the goal, what is
> already reversed (cite, don't re-infer), how to reverse the rest with the `rea`
> plugin, the environment/gotchas, the live workflow, and the acceptance criteria.

Repo: `C:\Users\gpmpo\cs2-internal` (this is also the session working dir).
Read also: `docs/ragebot.md` (ADR-0022, the ragebot design), `docs/re/occlusion.md`
(the occlusion RE), `docs/re/aim.md` (the shot/trace model), `docs/re/vphysics2.md`
(collision geometry), `docs/handoff.md` (session wrap-ups), and
`C:\Users\gpmpo\.opencode\plan\occlusion-handoff.md` (the running plan).

====================================================================
## 0. The goal (one sentence)

**Replace our ad-hoc occlusion enumeration with logic that is provably identical to the
engine's own bullet/trace path, so the Tier-2 chart (project the solids — no ray traces
of our own) contains exactly the same solids the engine's bullets hit — including the
world/floor/walls and every mesh/shape type the engine handles.**

**Hard rule:** reverse the engine with the **`rea` plugin** and **infer nothing**. Every
offset, slot, struct field and code path must be cited to a decompiled function /
address. Where the engine's behaviour is ambiguous, resolve it by decompiling more, not
by reasoning from context.

====================================================================
## 1. Current state (2026-10-18, end of session)

**Architecture (keep):** Tier 2 — project world solids into the shared angular *chart*
and attenuate by per-cell depth (`ray_span`), **no per-ray traces in our code**.
- `payload/aim/{geometry,occlusion,heatmap,field,solver}.{hpp,cpp}` — the platform-
  independent model (host-tested: `aim-geometry/field/solver/records/heatmap/occlusion/spread`).
- `payload/control/occlusion_query.cpp` — the game bridge: enumerates solids and reads
  each shape's geometry.
- `payload/control/src/service.cpp` `do_heatmap` — projects the enumerated solids into
  the chart and builds the render polygons (`walls.occluder_polygons`).

**Working / done:**
- Shape geometry read live for **hull (2)**, **sphere (0)**, **capsule (1)**, **mesh (3)**;
  **compound (4)** mapped but not implemented (typed POD descriptors).
- The heatmap broad phase, the conservative depth lower bound, per-cell `ray_span`.
- **Player collision bodies are excluded** (a target, not a solid). Root cause found by
  reversing the engine: the **static default filter accepts every entity** (see §2).
- **View-volume enumeration** (Tier 2, no trace): the `EntitiesInBox` box now spans
  eye → camera-forward·1400 (plus the targets) ±96, so the world body is enumerated in
  view. Diagnostics: `walls.world_cache_hulls/world_box_kept/world_box_rejected`.
- Occluder silhouettes clipped to the chart; caps raised
  (`k_max_occluders=8192`, `k_max_records=8192`).
- Live debug viewer + screenshots (§5).

**Broken / unknown (the reason for this session):**
- The world geometry does **not** reliably appear/attenuate. Standing with a wall
  between eye and enemy, the enemy head cells stay at **full damage** even though
  occluders overlap ~60–70% of them; the render shows **scattered brush fragments**,
  not a coherent wall/floor.
- Verified **not** the cause: empty hull planes (live `hulls_no_planes=0` after a
  vertices→planes fallback was added).
- The suspected cause: our enumeration is **`EntitiesInBox` (a box overlap of whole
  bodies) + walk every shape**, whereas the engine **traces the segment** and takes the
  distance-sorted, filter-accepted layers — so the set of solids (and which shape of a
  body) differs. This must be settled by reversing the engine (§3), not guessed.

====================================================================
## 2. Already reversed (cite these; do not re-derive from context)

**`CGamePhysicsQueryInterface::TraceShape` = `FUN_180a1a000`** (string
`"CGamePhysicsQueryInterface::TraceShape"`, source
`C:\buildworker\csgo_rel_win64\build\src\game\shared\gamephysicsquery.cpp:0x3a9`).
The sorted-layer branch:
```
(*(*iface + 0x560))(world, results, descriptor, &start, &delta, filter)   // world query
sort(results)                             // 0x48-byte layers, distance-sorted
for each layer:
    handle = layer.hit->vtbl[0x478]()     // entity handle
    if handle == 0x8000: accept           // worldspawn, unconditional
    else { entity = resolve(handle); if filter->vtbl[8](filter, entity): accept }
```
Dump: `artifacts/client_dump.txt` (from `scripts/ghidra/DumpOne.java`).

**The engine's static default filter accepts everything.**
`*(client.dll + 0x2227070)` (the object our payload used to pass) → vtable
`client.dll + 0x1ad4838` → `ShouldHitEntity` at **`client.dll + 0x22d900`** is
literally `mov al, 1; ret`. So every entity (players included) came back as an occluder.
We now keep the engine filter and drop the players payload-side by resolving the
record's `+0x18` handle through the paged identity table (mirrors
`FilterOverlapResults`).

**Overlap record (0x70 B), from `ConvertOverlapMultiple` = `FUN_1809d2dc0`
(client.dll `659940c1`):**
| field | producer |
|---|---|
| `+0x00..0x20` | `hit->vtbl[0x50]()` — transform blob |
| `+0x18` | `body->vtbl[0x478]()` — entity **handle** (`0x8000` = worldspawn) |
| `+0x50` | `*(hit->vtbl[0x30]() + 0x28)` — **a pointer** (live `0x26c45d77d00`); the
  pointed-to object showed surface-like fields `+0x28=0.8 +0x2C=0.25 +0x30=900
  +0x34=0.1`. Slot `0x30` is on the client-side **hit object**, not the vphysics2 shape |
| `+0x58` | `hit->vtbl[0x10]()` = **`CPhysicsBody*`** (`body+8` data, `data+0x60` count,
  `data+0x68` shape array; `body->vtbl[0x1b0]` world transform; `body->vtbl[0x478]` handle) |
| `+0x60` | 0 |
`FilterOverlapResults` = `FUN_1809df4e0`. `EntitiesInBox` = `0x9dbd20`,
`EntitiesAlongLinearSweep` = `0x9db9a0` (both now curated signatures, §4).

**vphysics2 shape geometry (`vphysics2.dll` sha `1d768045…`):** sphere centre `+0xB8`,
radius `+0xC4`; capsule A `+0xB8`, B `+0xC4`, radius `+0xD0`; hull `RnHull_t* +0xC0`
(verts `+0x70` count / `+0x78` ptr stride `0xC`; planes `+0x88` count / `+0x90` ptr
stride `0x10`); mesh `RnMesh_t* +0xC8` (verts `+0x30` count / `+0x38` ptr stride
`0xC`; triangles `+0x18`/`+0x20`; indices `+0x48`/`+0x50`); compound `RnCompound_t* +0xC0`
(0x160 B; typed sub-array pool — hulls stride `0xF8`, meshes `0xC0`, spheres `0x10`,
capsules `0x1C`; a 0x30-byte instance-record vector at `shape+0xD0`/`+0xD8`).

====================================================================
## 3. THE TASK — reverse the rest (no inference) and make ours identical

Reverse, on the **current live build** (`client.dll` sha `659940c1…`, `server.dll`), and
record each with its decompiled body + address:

1. **Client bullet path.** `FX_FireBullets` (`client.dll 0xd22c50`) → the trace it
   performs: which query method, the `Ray_t`/descriptor, the **filter it builds**
   (vtable + fields: mask/contents, group, skip entity), and the client impact path.
   Also `FUN_180d23460` (the spread cone) if it touches the trace.
2. **The query methods** (client.dll): `TraceShape`, `TraceShapeWithSortedResults`,
   `TraceLineIntervals`, `EntitiesInBox`, `EntitiesInShape`,
   `EntitiesAlongLinearSweep`, `FilterOverlapResults` — **exact arguments**, the filter
   contract (which vtable slots are called, with what args), the result record layouts,
   and how the layer stack is ordered. (The name strings are around
   `client.dll 0x1c321xx`; the entry points can be recovered by the name-string xrefs.)
3. **The authoritative penetration** (`server.dll`): `FireBullets` /
   `HandleBulletPenetration` / `ScaleDamage` / the trace it uses (`UTIL_TraceLine`) —
   the trace mask, the filter, how it enumerates solids, thickness, surface
   `GetPenetrationModifier`, and which entity types it hits vs skips.
4. **The world/BSP world geometry.** How the engine represents the map (brushes and
   displacements) and how it traces against them — `CVPhysics2Interface` /
   `VPhysics2_Interface_001` (`vphysics2.dll 0x1803fcb30`) / `CRnWorld::Query` — so
   Tier 2 can read the *same* geometry without tracing (the map's large surfaces are
   likely displacements/meshes; confirm which shape type the engine traces for them).
5. **Displacements & non-convex geometry**: identify the exact primitive the engine
   uses for them and make our reader produce the same solid.

Then change `payload/control/occlusion_query.cpp` (and the aim side if needed) so the
enumerated solid set is **identical to the engine's trace result** for the same segment,
and add a **host test** + a **live cross-check** (use the engine trace itself as an
oracle: fire the same segment via `TraceShapeWithSortedResults` and diff the solids).

====================================================================
## 4. How to reverse with the `rea` plugin

- Call `tools.rea.*` from Code Mode `execute` (use `search({namespace:"rea"})` to
  discover). Open a target: `tools.rea.open_binary({path})`; decompile:
  `tools.rea.batch_decompile({addresses:[...]})`; disassemble/read:
  `procedure_assembly`, `read_bytes`, `read_function_instructions`, `xrefs`,
  `search_strings`, `search_procedures`, `inspect_native_data_type`.
- **Worked for vphysics2.dll (4.8 MB).** For the big DLLs REA's analysis **fails**
  (`execution_failure`): `client.dll` (39 MB) and `server.dll` (34 MB). Use the
  **persistent Ghidra project** instead (already analyzed `client.dll`):
  ```powershell
  $env:JAVA_HOME='C:\Program Files\Java\jdk-21'
  & 'C:\Users\gpmpo\ghidra\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat' `
      'C:\Users\gpmpo\ghidra-analysis' clientoccl -process 'client.dll' -noanalysis `
      -scriptPath 'C:\Users\gpmpo\cs2-internal\scripts\ghidra' -postScript DumpOne.java `
      'C:\Users\gpmpo\cs2-internal\artifacts\client_dump.txt' 0xADDR 0xADDR ...
  ```
  (`scripts/ghidra/DumpOne.java` takes `outPath addr…`; `DumpTargets.java`,
  `DumpPtr.java` also exist.) To analyse `server.dll`, import it into a new project
  (`-import <path>`, full analysis ~25 min; add `-max-cpu 0 -analysisTimeoutPerFile 720`).
  Verify which build a project holds (the live client.dll is `659940c1…` =
  `C:\Users\gpmpo\ghidra-analysis\client-25947352.dll`; `client.dll` there is the older
  `44af2aa0…`).
- **Static fallback (no Ghidra), used a lot this session:** a tiny PE parser +
  **capstone** (installed). Scripts live in `C:\Users\gpmpo\AppData\Local\Temp\opencode\`
  and show the patterns: `derive_sig.py` (RVA→file off, RIP-relative refs, uniqueness),
  `client_record.py` (disassemble the record build), `surface.py`, `filter_re.py`
  (read the **live** process), `overlap.py` (chart overlap), `read_material.py`,
  `live_probe{2,4,5}.py`.
- **Read the live process** (analysis, no rebuild): `ctypes` `OpenProcess` +
  `ReadProcessMemory` on `cs2.exe` — see `filter_re.py`. Get live addresses from the
  payload's `walls` JSON (`iface`, `world`, `filter`, `material_token`).

====================================================================
## 5. Live workflow (already set up; game runs in session 1)

- Agent is **session 0** (no GPU). Launch/inject via the scheduled task:
  `python scripts/run-scenario.py status --keep-running` (kills stale `cs2.exe`, runs
  `CS2Launch`, injects `payload.dll` **once**). Attach afterwards:
  `python scripts/run-scenario.py <scenario> --no-launch --no-inject --keep-running`.
- **Never inject twice** (two pipe servers → random client).
- Debug viewer (Tier-2 chart): `python scripts/heatmap-live.py --png artifacts/live.png`
  (run in the background) → `http://127.0.0.1:8765`. Dock it on the game monitor:
  `schtasks /run /tn CS2LiveView`. Screenshot the whole screen:
  `schtasks /run /tn CS2Capture` → `artifacts/liveview-desktop.png`. (The payload pipe
  is single-client: while the viewer runs, drive the game through its `/_exec` /
  `/_look` passthrough, or stop the viewer.)
- **Saved test position** (teleport back here): `artifacts/live-position.txt` —
  `pos -181.5 -524.2 0.5`, `eye … 64.4`, `yaw -151.7`, `pitch -19.4`. In a scenario:
  `game.teleport(-181.5, -524.2, 0.5); game.look(yaw=-151.7, pitch=-19.4)`.
- Bot match / setup: `awaiting.ensure_bot_match(game, map_name="de_dust2", per_team=3,
  bot_stop=True, map_timeout=300)`; then `game.exec("sv_cheats 1")`, `game.exec("god")`.
- Game play is `-insecure` (offline). Sims: `client.dll` in the live game == sha
  `659940c1…`.

====================================================================
## 6. Acceptance criteria

1. **Exactness.** For a spot with a wall between eye and enemy, our enumerated +
   projected + attenuated set matches the engine's trace **exactly** (same solids, same
   hit/penetration). Cross-validate live by firing the engine's own
   `TraceShapeWithSortedResults` for the same segment and diffing the layers.
2. **All shapes.** Every shape/mesh type the engine handles is handled by us — verify
   each explicitly (`type 0/1/2/3/4`, displacements, the world body), with the engine's
   own treatment cited.
3. **Evidence.** Every offset/slot/struct field/function cited to a decompiled body;
   nothing inferred. Extend `docs/re/occlusion.md` and `docs/re/vphysics2.md` with the
   new bodies.
4. **Tests.** A host test for the new enumeration semantics and a live probe (extend
   `%TEMP%\opencode\live_probe5.py` — it computes cell/occluder coverage and head
   values) that shows the wall attenuates exactly as the engine predicts.

====================================================================
## 7. Known open items (do not lose)
- **Compound (4) instance transform** — the `RnCompound_t` pool is mapped; the 0x30-byte
  instance record's transform is not.
- **Surface / penetration field** — the record `+0x50` is a pointer (live); the
  penetration-modifier field inside the pointed-to object is unidentified; the schema
  has no `CPhysSurfaceProperties`.
- **Struct-offset signatures** (`CPhysicsBody`/`RnHull_t`/`RnMesh_t`) still literals
  (RVA signatures `physics_query_interface`/`physics_query_filter`/`entities_in_box`/
  `entities_along_linear_sweep` are done, in `external/dumper/src/analysis.rs` +
  `calibration/offsets.json` → `payload/sdk/offsets.hpp`).
- Nothing is committed (both sessions' work is uncommitted in the tree).

====================================================================
## 8. Build / test / hygiene
```powershell
meson compile -C build-payload        # Windows payload (clang-cl)
meson test -C build                    # host tests (15/15 expected)
cargo test --manifest-path external/dumper/Cargo.toml
powershell -ExecutionPolicy Bypass -File scripts/preflight-windows.ps1
```
Gotchas: keep payload globals **POD** (a non-POD global or a function-local `static`
adds a CRT import the manual-map injector cannot resolve → injection fails; preflight
checks this). A **payload-built trace filter is unsafe** — the engine reads more of the
filter vtable than slot `+8`, so a minimal one makes the query return nothing and
crashes the game; use the engine filter and exclude payload-side.
