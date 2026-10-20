# Neverlose's renderer — the four shader constant buffers, the hull record, and the depth question

> Research note, 2026-10-21. **Route A** of the T17 deep-reversal spec
> (`docs/orchestration/spec-t17-neverlose-deep.md`).
> Scope: the **reversed Neverlose CS2 module** (`neverlose_cs2.dll`), read as
> *reference material only*. **Nothing from it is copied**; every claim below is a
> **description** with an evidence address. Third-party material is described,
> never copied — see the licence note at the end.
>
> **Ground truth.** The raw live image
> `re/cs2_212C3300000/cs2_212C3300000.bin`
> (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
> base `0x212C3300000`, size `0x5000000`, **`VA = base + file_offset`**).
> The decompilation used is Ghidra 12.1.4 project **`gproj11`** (the *corrected*
> deobfuscation, `DEOBF.md`).
>
> **Method (how the reflection was obtained).** The module ships **28 DXBC
> shader containers** in `.rdata`. Each is a `DXBC` container with the chunk
> set `RDEF / ISGN / OSGN / SHEX / STAT`. The `RDEF` chunk carries the
> D3D11 constant-buffer reflection, so the layouts can be read **straight out of
> the bytes** — no disassembly of the shader is needed.
>
> The `RDEF` chunk format is not documented, so it was **derived empirically and
> then validated**: a reference shader with the *same* cbuffer and variable
> names was compiled locally with the OS's own HLSL compiler
> (`d3dcompiler_47!D3DCompile`, `ps_5_0`), and its `RDEF` chunk was parsed with
> the same parser. The parser reproduces the reference exactly
> (`inverse_view_projection` @0/64 B, `hull_color` @64/16 B, `viewport_size`
> @80/8 B, `point_count` @88/4 B, `group_count` @92/4 B) and then reproduces the
> neverlose layouts. The record layouts used are:
>
> | record | size | fields |
> |---|---|---|
> | variable | 40 B | `name_off`, `byte_offset`, `size`, `kind`, `type_rec_off`, `0`, `0xffffffff`, `0`, `0xffffffff`, `0` |
> | type | 14 B+ | `u8 class`, `u8 ?`, `u8 type`, `u8 ?`, `u16 rows`, `u16 cols`, `u16 elements`, `u16 members`, … `u32 name_off` at +32 |
> | cbuffer | 24 B | `name_off`, `var_count`, `?`, `total_size`, `0`, `0` |
> | struct type | 48 B | `?`, …, `u16 member_count` at +10, `u32 self_off`, …, `u32 name_off` at +32 |
> | struct member | 12 B | `name_off`, `type_class_off`, `byte_offset` |
>
> **Confidence labels.** `verified` = read out of the bytes and cross-checked
> against an independently compiled reference; `inferred` = interpretation of
> what was read; `unknown` = looked for and not recoverable statically.

---

## 0. Summary

| # | Question | Answer | Confidence |
|---|---|---|---|
| A1 | How many shader families, and which constant buffers? | **Four** families — `PolygonConstants` (5 shaders), `TrajectoryConstants` (7), `ImpactConstants` (6), `TracerConstants` (2) — plus 8 other shaders (UI/text and SDF passes) | `verified` |
| A2 | Exact layout of each constant buffer? | **Recovered byte-for-byte** — see §2. Every field's offset, size and type is in the tables | `verified` |
| A3 | What is the hull record? | `HullPoint` = **48 B**: `float4 position_radius` @0, `float4 parent_growth` @16, `float4 appearance` @32 — an array of **256** of them (`points`, 12 288 B) | `verified` |
| A4 | Is the colour per-hull, per-entity or per-draw? | **Per-draw.** `hull_color` is *one* `float4` in the constant buffer; the per-hull colour/alpha lives in `HullPoint::appearance` | `verified` |
| A5 | Which family draws the player model? | **`PolygonConstants`** — it is the only family with a hull-point array, a hull colour, a global alpha and glow/outline/shadow opacities. Five variants = the sub-passes (base / shadow / outline / glow / …) | `verified` for the family; `inferred` for "the player model" (follows the prior session's code-level finding) |
| A6 | Is the draw depth-tested? | **See §5.** The depth state is *not* in the shader and *not* in a fixed table — it is assembled per draw into a `D3D11_GRAPHICS_PIPELINE_STATE_DESC` whose `DepthStencilState` is copied from a caller-supplied template | `verified` for the mechanism; the *value* is per-call and `unknown` statically |
| A7 | Does the colour live in the constant buffer or in a vertex/instance buffer? | In the **constant buffer** (`b0`-style register space). There is no per-instance colour stream in any of the four families | `verified` |

---

## 1. The 28 shader containers

All 28 `DXBC` containers, by VA (the VA *is* the address in the neverlose
image; `VA = 0x212C3300000 + file_offset`). The family is taken from the
`RDEF` chunk's cbuffer name — `verified` for all 28.

| VA | bytes | constant buffer | stage (from `ISGN`/`OSGN`) |
|---|---|---|---|
| `0x212c41c9050` | 4 768 | `PolygonConstants` | no signature names recovered |
| `0x212c41ca2f0` | 10 696 | `PolygonConstants` | PS (`SV_POSITION,COLOR,TEXCOORD` → `SV_Target`) |
| `0x212c41ccd20` | 10 812 | `PolygonConstants` | PS (`SV_POSITION,COLOR,TEXCOORD` → `SV_Target`) |
| `0x212c41cf760` | 6 400 | `PolygonConstants` | PS (`SV_POSITION,COLOR,TEXCOORD` → `SV_Target`) |
| `0x212c41d1080` | 6 992 | `PolygonConstants` | PS (`SV_POSITION,COLOR,TEXCOORD` → `SV_Target`) |
| `0x212c41d2c30` | 2 992 | `TrajectoryConstants` | VS (`SV_VertexID,SV_InstanceID` → `SV_Position,TEXCOORD`) |
| `0x212c41d37e0` | 2 452 | `TrajectoryConstants` | PS (`SV_Position,TEXCOORD` → `SV_Target`) |
| `0x212c41d4180` | 1 664 | `TrajectoryConstants` | PS (`SV_Position` → `SV_Target`) |
| `0x212c41d4840` | 452 | *(none)* | PS (`SV_Target`) |
| `0x212c41d4a70` | 21 748 | `TrajectoryConstants` | no signature names recovered |
| `0x212c41d9fa0` | 2 464 | `TrajectoryConstants` | VS (`SV_VertexID,SV_InstanceID` → `SV_Position,TEXCOORD`) |
| `0x212c41da940` | 2 116 | `TrajectoryConstants` | VS (`SV_VertexID` → `SV_Position,TEXCOORD`) |
| `0x212c41db190` | 4 204 | `TrajectoryConstants` | PS (`SV_Position,TEXCOORD` → `SV_Target`) |
| `0x212c41e2f40` | 94 000 | `ImpactConstants` | no signature names recovered |
| `0x212c41f9e70` | 724 | *(none)* | PS (`SV_Target`) |
| `0x212c41fa150` | 9 880 | `ImpactConstants` | no signature names recovered |
| `0x212c41fc7f0` | 3 072 | `ImpactConstants` | VS (`SV_VertexID,SV_InstanceID` → `SV_Position`) |
| `0x212c41fd3f0` | 7 116 | `ImpactConstants` | PS (`SV_POSITION` → `SV_Target`) |
| `0x212c41ff040` | 23 560 | `ImpactConstants` | PS (`SV_POSITION` → `SV_Target`) |
| `0x212c4204c70` | 7 604 | `ImpactConstants` | PS (`SV_POSITION` → `SV_Target`) |
| `0x212c4206ae0` | 1 372 | `TracerConstants` | VS (`SV_VertexID` → `SV_Position,TEXCOORD`) |
| `0x212c4207060` | 3 316 | `TracerConstants` | PS (`SV_Position,TEXCOORD` → `SV_Target`) |
| `0x212c42265a0` | 1 024 | *(none)* | PS (`SV_POSITION,COLOR,TEXCOORD` → `SV_Target`) |
| `0x212c42269b0` | 1 728 | `VS_CONSTANT_BUFFER` | PS |
| `0x212c42274a0` | 1 200 | *(none)* | PS |
| `0x212c423c840` | 2 384 | `VS_CONSTANT_BUFFER` | PS |
| `0x212c423d2c0` | 17 224 | `VS_CONSTANT_BUFFER`, `VS_CONSTANT_BUFFER_IS_SDF` | PS (`SV_PrimitiveID` present) |
| `0x212c4243450` | 984 | `VS_CONSTANT_BUFFER` | PS |

Counts match the prior session's census exactly — **7 `TrajectoryConstants`,
6 `ImpactConstants`, 5 `PolygonConstants`, 2 `TracerConstants`** — but the
*layouts* are new: `OPEN_ITEMS.md` item 1 listed "per-pass constant-buffer
layouts" as still open.

**How the module reaches these blobs** (`verified`): the renderer builds a
table of pass objects; the *pixel* shader byte-code pointer/size pairs are
written at fixed offsets inside each pass object:

```
FUN_212c3668e6f  (the pass/shader table builder, 1 344 B)
  *(lVar6 + 0x50)  = &DAT_212c41ca2f0 ; *(lVar6 + 0x58) = 0x29c8 ;   // 10 696 B
  *(lVar6 + 0xf0)  = &DAT_212c41ccd20 ; *(lVar6 + 0xf8) = 0x2a3c ;   // 10 812 B
  *(lVar6 + 0x168) = &DAT_212c41cf760 ; *(lVar6 + 0x170) = 0x1900 ;  //  6 400 B
  *(lVar6 + 0x1e0) = &DAT_212c41d1080 ; *(lVar6 + 0x1e8) = 0x1b50 ;  //  6 992 B
  *(lVar6 + 0x280) = &DAT_212c41d4180 ; *(lVar6 + 0x288) = 0x680 ;   //  1 664 B
```
(the sizes match the container sizes in §1 exactly, `verified`). The four
`PolygonConstants` pixel shaders at offsets `0x50 / 0xf0 / 0x168 / 0x1e0` are
therefore **four distinct passes of the same family** — consistent with a
base/shadow/outline/glow decomposition.

---

## 2. The four constant-buffer layouts

All offsets are **byte offsets inside the constant buffer**, as recorded by the
compiler in the `RDEF` chunk. Sizes are in bytes. `RDEF` field `total` is the
compiler's own total size for the buffer.

### 2.1 `PolygonConstants` — the chams / hull pass

`verified`. Identical in all five shaders (`0x212c41c9050`, `0x212c41ca2f0`,
`0x212c41ccd20`, `0x212c41cf760`, `0x212c41d1080`); 20 variables,
`total = 12 464 B` (`RDEF` cbuffer record at chunk offset `0xc8` in the
`0x212c41ca2f0` container).

| off | size | name | type | meaning (see §3) |
|---:|---:|---|---|---|
| 0 | 64 | `inverse_view_projection` | `float4x4` | world → clip, so the hull is drawn in **world space** |
| 64 | 8 | `viewport_size` | `float2` | target size in pixels (for the pixel-space widths below) |
| 72 | 4 | `point_count` | `dword` | number of live hull points |
| 76 | 4 | `group_count` | `dword` | number of hull *groups* (chunks of the hull) |
| 80 | 16 | `hull_color` | `float4` | **the** hull colour — RGBA, per **draw** |
| 96 | 4 | `smooth_k` | `float` | edge smoothing coefficient |
| 100 | 4 | `fade_offset` | `float` | fade distance offset |
| 104 | 4 | `global_alpha` | `float` | master alpha for the pass |
| 108 | 4 | `shadow_opacity` | `float` | shadow sub-pass opacity |
| 112 | 4 | `outline_width_px` | `float` | outline width **in pixels** |
| 116 | 4 | `glow_width_px` | `float` | glow width **in pixels** |
| 120 | 4 | `geometry_type` | `dword` | which geometry the hull describes |
| 124 | 4 | `render_pass` | `dword` | **which sub-pass this draw is** |
| 128 | 16 | `mask_bounds` | `float4` | min/max xy of the hull's screen mask |
| 144 | 8 | `texture_origin` | `float2` | atlas cell origin |
| 152 | 8 | `texture_size` | `float2` | atlas cell size |
| 160 | 4 | `gradient_opacity` | `float` | gradient sub-pass opacity |
| 164 | 4 | `outline_opacity` | `float` | outline sub-pass opacity |
| 168 | 8 | `opacity_padding` | `float2` | padding (never read as data) |
| 176 | 12 288 | `points` | `HullPoint[256]` | **the hull geometry** — 256 × 48 B |

The 176-byte scalar part is followed by a 12 288-byte array; `176 + 12 288 =
12 464` = the buffer's `total`. `verified`.

### 2.2 `TrajectoryConstants` — the grenade/projectile prediction pass

`verified`. Identical in all seven shaders (`0x212c41d2c30` … `0x212c41db190`);
11 variables, `total = 224 B` (cbuffer record at chunk offset `0x98`).

| off | size | name | type |
|---:|---:|---|---|
| 0 | 64 | `view_projection` | `float4x4` |
| 64 | 16 | `viewport` | `float4` |
| 80 | 16 | `line_color` | `float4` |
| 96 | 16 | `endpoint_color` | `float4` |
| 112 | 16 | `endpoint` | `float4` |
| 128 | 16 | `shape` | `float4` |
| 144 | 16 | `prediction` | `float4` |
| 160 | 16 | `config` | `uint4` |
| 176 | 16 | `throw_state` | `float4` |
| 192 | 16 | `state` | `uint4` |
| 208 | 16 | `projectile_origin` | `float4` |
| — | — | `Record` (SRV element type, 64 B) | `Record` = `float4 a@0, b@16, c@32, d@48` |

### 2.3 `ImpactConstants` — the bullet-impact / glow pass

`verified`. Identical in all six shaders (`0x212c41e2f40` … `0x212c4204c70`);
15 variables, `total = 49 360 B` (cbuffer record at chunk offset `0xcc`).

| off | size | name | type |
|---:|---:|---|---|
| 0 | 64 | `view_projection` | `float4x4` |
| 64 | 64 | `inverse_view_projection` | `float4x4` |
| 128 | 8 | `viewport` | `float2` |
| 136 | 4 | `point_count` | `dword` |
| 140 | 4 | `radius` | `float` |
| 144 | 32 | `colors` | `float4[8]` — eight impact colours |
| 176 | 4 | `glow_width_px` | `float` |
| 180 | 4 | `rounding` | `float` |
| 184 | 4 | `fill_opacity` | `float` |
| 188 | 4 | `outline_opacity` | `float` |
| 192 | 4 | `glow_opacity` | `float` |
| 196 | 4 | `tile_metadata_valid` | `dword` |
| 200 | 4 | `tile_width` | `dword` |
| 204 | 4 | `tile_count` | `dword` |
| 208 | 49 152 | `points` | `ImpactPoint[1024]` (48 B each) |

### 2.4 `TracerConstants` — the bullet-tracer pass

`verified`. Identical in both shaders (`0x212c4206ae0`, `0x212c4207060`);
3 variables, `total = 16 B` (cbuffer record at chunk offset `0x98`).

| off | size | name | type |
|---:|---:|---|---|
| 0 | 8 | `viewport` | `float2` |
| 8 | 4 | `start_fade` | `float` |
| 12 | 4 | `end_fade` | `float` |
| — | — | `TracerRecord` (SRV element type, 80 B) | `endpoints@0`, `perspective@16`, `color@32`, `shape@48`, `glow@64` — all `float4` |

---

## 3. The hull record

`verified` — the struct member table is in the `RDEF` chunk of every
`PolygonConstants` shader (struct type record at chunk offset `0x68c`, member
records at `0x668 / 0x674 / 0x680` of the `0x212c41ca2f0` container):

```
HullPoint            (48 B, 12 × 4-byte components)
  +0   float4  position_radius    ; xyz = world position, w = radius
  +16  float4  parent_growth      ; xyz = growth direction/amount, w = ?
  +32  float4  appearance         ; per-hull colour / alpha / style
```

This **confirms the mining report's `HullPoint {position_radius, parent_growth,
appearance}`** (`docs/research/neverlose-mining.md` §7.3) at the byte level, and
adds the offsets.

`points` is declared in the `RDEF` as a **256-element array** of `HullPoint`
(12 288 B at offset 176). `point_count` (offset 72) and `group_count`
(offset 76) say how many are live and how many hull *groups* there are.

### 3.1 Is the colour per-hull, per-entity or per-draw?

**Per draw.** `verified`:

* `hull_color` is a *single* `float4` in the constant buffer (offset 80) — one
  colour for the whole draw call, not per hull and not per entity.
* The **per-hull** colour/alpha is `HullPoint::appearance` (`float4` at hull
  offset +32) — one `float4` per hull point.
* The **per-entity** quantities are `point_count` / `group_count` and the
  `geometry_type` / `render_pass` selectors.

So a hull is drawn as: one draw call per hull group, with the group's points
uploaded into `points[]`, the group's colour in `hull_color`, and each point's
own `appearance` modulating it.

### 3.2 Which pass is the player model?

`PolygonConstants` is the **only** family with a hull-point array, a hull
colour, a global alpha and glow/outline/shadow opacities — i.e. the **chams**
pass. `verified` for the family; `inferred` for "the player model" (the prior
session's code-level finding, `neverlose-mining.md` §3/§7.3, is the source).

The five `PolygonConstants` pixel shaders share **one** constant-buffer layout,
so the four/five sub-passes differ in *shader*, not in *data*: they are the
same geometry drawn several times with different `render_pass` /
`shadow_opacity` / `outline_opacity` / `gradient_opacity` / `glow_width_px` /
`outline_width_px` values. `inferred` from the layout.

---

## 4. What the other 8 shaders are

Not part of the four families, and not the chams:

* `VS_CONSTANT_BUFFER` (`0x212c42269b0`, `0x212c423c840`, `0x212c423d2c0`,
  `0x212c4243450`) and `VS_CONSTANT_BUFFER_IS_SDF` (`0x212c423d2c0`) are the
  **UI / text** passes. The `VS_CONSTANT_BUFFER` at `0x212c423d2c0` carries a
  356-byte `PrimData` struct with named members — `rect_col_tl@80`,
  `rect_col_tr@96`, `circle_color@144`, `circle_inside_color@160`,
  `shadow_color@48`, `circle_gradient@208`, `multi_colors@224` … — which is a
  **text/quad primitive description**, not gameplay geometry. `verified`.
* The remaining four shaders (`0x212c41d4840`, `0x212c41f9e70`,
  `0x212c42265a0`, `0x212c42274a0`) have no constant buffer at all. `verified`.

---

## 5. The depth question

**The shader does not answer it, and it is not a fixed table.** `verified`:

* The `RDEF` chunk carries **no** depth state — it describes the constant
  buffer only. The DXBC `SHEX` chunk carries the program type and the SM
  bytecode; there is **no** `DXIL` magic in any of the 28 containers, so these
  are D3D11 DXBC shaders (SM4/5-era), and in D3D11 the depth state comes from
  the **pipeline state object**, not from the shader.
* The module drives `ID3D11DeviceContext` **directly through the vtable**
  (there is no `d3d11` import — the device is taken from the game). The
  vtable slots used in the renderer were confirmed by call shape:
  `0x160` = `OMSetDepthStencilState` (3 args), `0x168` = `OMSetBlendState`
  (3 args), `0x158` = `OMSetRenderTargets`, `0x140` = `RSSetViewports`,
  `0x120` = `CSSetShaderResources`. `verified`.

**Where the depth state is decided** (`verified`, `FUN_212c36bde17`, 1 461 B):
that function assembles a full `D3D11_GRAPHICS_PIPELINE_STATE_DESC` on the
stack and copies it into the object it is given:

```
  /* 0x212c36bde17 — the PSO descriptor builder */
  puVar1[0 .. 0x13]           = stack[0x3320 .. 0x33c0]   ; 0xA0 bytes copied
  *(u32 *)(puVar1 + 0xa0)     = unaff_R15D ;
  *(u64 *)(puVar1 + 0xa4)     = stack[0x33c0] ;
  *(u64 *)(puVar1 + 0xac)     = stack[0x33c8] ;
  *(u64 *)(puVar1 + 0xb4)     = stack[0x33d0] ;
  *(u64 *)(puVar1 + 0xbc)     = stack[0x33d8] ;
  *(u32 *)(puVar1 + 0xc4)     = stack[0x33e0] ;
```

In the `D3D11_GRAPHICS_PIPELINE_STATE_DESC` layout, `DepthStencilState` sits
at **offset `0x48`** — i.e. **inside the copied 0xA0-byte block**. The depth
state is therefore *not* a constant in this function: it comes from a
**caller-supplied template** (the 0x100-byte block at `RBP+0x3320`), and the
per-call overrides only touch the *blend*/`SampleDesc`/`RenderTargets` tail
(`0xA0` onwards), never `0x48`.

**What that means for us (the important half):**

* The renderer is **pass-driven**: every draw chooses its own depth-stencil
  state from a template that the *pass object* supplies.
* The pass object carries the state, and the state is applied with
  `OMSetDepthStencilState` (`+0x160`) immediately before the draw.
* **The actual `DepthEnable` / `DepthWriteMask` / `DepthFunc` values are not
  recoverable from the static image** — `unknown`. They are written into the
  template by the code that *builds* the pass, and the values live in the
  pass-object tables that are populated from configuration. To pin the numbers
  statically one would have to walk the pass-table initialisers; to pin them
  for certain, one would have to dump the live constant buffer.

**What is recoverable, and is the useful half:** the *structure* says the
module **does** set a depth-stencil state per draw (it does not rely on a
default), and the depth state is per-pass, not global. Since a "see through
walls" chams needs `DepthEnable = FALSE` (or a `DepthFunc` that always
passes) on the chams pass, and the module has five `PolygonConstants`
sub-passes with independent `shadow_opacity` / `outline_opacity` /
`gradient_opacity`, the architecture it uses is exactly
"one depth state per pass", which is the shape our renderer would need too.

> **Honest limit.** I did **not** recover the numeric `DepthEnable` value for
> the chams pass. What I recovered is *where the value comes from* (a
> caller-supplied template copied into the PSO descriptor at `FUN_212c36bde17`)
> and *that it is applied per draw* (`OMSetDepthStencilState` at vtable
> `+0x160`). Everything else about the depth state — enable, write mask,
> comparison function — is `unknown` statically.

---

## 6. What this changes for us

1. **The hull recipe is confirmed and complete.** A neverlose hull is
   `256 × {float4 position_radius, float4 parent_growth, float4 appearance}`,
   drawn as a **world-space** convex hull (`inverse_view_projection` in the
   constant buffer), with a **per-draw** RGBA colour and alpha and a
   **per-hull** `appearance` float4. Our overlay can reproduce this exactly
   from the studio hitboxes we already read: build a hull per hitbox group,
   upload the points, and modulate with `appearance`.
   → *Action*: our chams renderer should carry **one** `float4` colour per
   draw plus a **per-hull** `float4`, not a per-point colour stream.
2. **The pass decomposition is the reusable idea.** Neverlose splits one
   geometry into several passes (base / shadow / outline / glow) that share
   *one* constant-buffer layout and differ only in shader + a `render_pass`
   selector. We should do the same: one buffer, one layout, a pass id, and
   per-pass opacities — instead of four different buffer structs.
3. **Depth state is per pass, not global.** Our chams pass must own its
   depth-stencil state (and the "visible through walls" behaviour is a
   *property of that state*, not of the shader). → *Action*: our draw path
   needs a per-pass `D3D11_DEPTH_STENCIL_DESC`, set with
   `OMSetDepthStencilState`, and the chams pass should set
   `DepthEnable = FALSE` if we want x-ray.
4. **The byte offsets are reusable as a *reference layout*, not as code.** The
   tables in §2 are the most valuable thing here: they are a published,
   compiler-generated description of what a "world-space hull + per-draw colour
   + per-hull appearance + glow/outline/shadow opacities" chams pass needs.
   Copy the *shape*, not the bytes.
5. **Our chams hook problem is not solved by this.** Our chams are refused
   because the hook target is wrong (`payload/features/src/chams.cpp`,
   `k_draw_hook_target_verified = false`). Nothing in the neverlose renderer
   changes that: their renderer is a **separate D3D11 device** reached through
   the game's device vtable, and the pass table is built by *their* code, not
   hooked. What we now have is the *data model* to render once the draw path
   is ours.

## 7. What we should **not** copy

* **No third-party code.** Nothing from the neverlose image is copied into our
  tree. The tables in §2 are a *description* of a binary's data layout,
  reproduced so that a reader can check our renderer against it; the numbers
  are facts about their binary, not source we may reuse.
* **Not the obfuscation, not the encrypted region, not the loader.** Those are
  Route C's subject and are *not* on our list; see
  [`neverlose-protection.md`](neverlose-protection.md).
* **Not the separate-device renderer.** Their renderer owns a D3D11 device and
  a pass-table framework. Ours draws into the game's own device; copying the
  framework would mean copying the thing we deliberately do not want.

---

## 8. Reproducing this

The reflection is reproducible with three steps, no Ghidra needed:

1. Read the 28 `DXBC` containers out of the raw image (the `DXBC` magic
   occurs exactly 28 times; the container header is
   `magic / hash[16] / version / total_size / chunk_count / chunk_offsets`).
2. For each container, take the `RDEF` chunk and parse the records of §0
   (variable 40 B, type 14 B+, cbuffer 24 B, struct member 12 B).
3. Cross-check against a locally compiled shader with the same names.

All of it is plain byte work on `re/cs2_212C3300000/cs2_212C3300000.bin`; no
disassembly and no Ghidra project are required, which is why this route paid
off where the earlier code-only pass did not.

---

**Licence note.** The Neverlose CS2 module is a **commercial third-party
product** with no open-source licence. It is used here as *reference material
only*: this document **describes** observed behaviour, quoting field names,
offsets and constants as *facts about that binary*. No third-party code,
shader byte-code or data table is reproduced in this repository, and nothing
here may be used to reproduce or redistribute their binary or its contents.
