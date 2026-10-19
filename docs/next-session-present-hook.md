# Next session — in-process Present rendering + ragebot follow-ups

> Written at the end of the 2026-10-19 session. Start here next time.
>
> **Two workstreams:** (A) retire the external Vulkan-layer renderer for the ESP/menu and
> render in-process from the game's Present path; (B) finish the ragebot items that were
> still janky/broken at the end of the session (auto-stop, recoil/spray, menu cursor).

Current commits (branch `feat/occlusion-penetration`):

```
18138fe docs(input): correct the auto-stop speed-threshold comment
0a7824b feat(rage): multi-target aim, full recoil punch, silent-aim fix, menu cursor release
b0ff706 docs(rage): clarify auto-stop speed semantics (0 = 34% default, not full stop)
596c7c1 feat(rage): predictive assists, speed-limited stop, recoil, sub-tick fallback, firearm gate
7a477c2 feat(rage): bullet-mask filter, mesh clusters, angle-correct aim, assists, per-weapon configs
```

## Verification status (2026-10-19)

Live-verified on de_dust2 (native Windows, `CS2Launch` → session 1):

- occlusion bullet-mask filter (`walls.filter_mask = 0x1c300b`) and the mesh-cluster
  fix (`mesh_clusters = 83`, world cache 10148 → 10226);
- the aim chart metric / ±π seam, and global point selection (unit tests);
- **silent aim** (camera held at −145° while the aim was 179°) and **multi-target
  selection** (chosen target changed 17 → 15 with the best aimpoint);
- the firearm gate (knife/C4/grenade → no solve; rifle → solves);
- per-weapon profile persistence across a restart.

**Broken / janky at the end of the session (workstream B):**

1. **Auto-stop is janky.** The counter-strafe stop (`Input::apply`, `rage_stop_speed`)
   decelerates to the threshold but oscillates/chatters. Hypotheses to check:
   - the braking direction is computed in the aim frame (`rage_yaw_`) but the engine
     rotates the move axes by the *command* view yaw; if they differ by a tick the
     counter-strafe points wrong → hunt in circles. Try using the command's own view
     yaw (read `base+0x40`) instead.
   - no hysteresis: at the threshold the code flips braking/release each tick. Add a
     deadband (stop braking below 0.9× the limit, resume above 1.1×).
   - `velocity()` reads `m_vecAbsVelocity` (last simulated); it lags a tick. Consider
     using the *predicted* velocity from the command or the pawn's velocity at the
     current tick.
   - the brake writes full ±1 axes; the engine quantises them; a short tap can overshoot.
2. **Recoil / spray correction does not seem to work.** `read_aim_punch` sums
   `m_pPredictableBaseAngle` (+0x50) and `m_unpredictableBaseAngle` (+0xa4) and
   subtracts from the silent aim. Check:
   - whether the shot actually inherits the punch in the direction we assume (reverse
     `FX_FireBullets`/`CCSWeaponBase::PrimaryAttack` on the live build: does the bullet
     use `viewangles + punch`, or does the punch only move the camera?);
   - the `CsPlayerAimPunchServices` layout/encoding (the fields may be bit-packed
     `QAngle`s or have a different offset than 0x50/0xa4 — re-derive from the schema
     `cs_player_aim_punch_services_fields`, which lists `m_predictableBaseAngle` 0x50 /
     `m_unpredictableBaseAngle` 0xa4 as `QAngle`);
   - the per-weapon **recoil pattern table** (`m_flRecoilAngle`, `m_flRecoilMagnitude`,
     `m_flRecoilAngleVariance`, `m_flRecoilMagnitudeVariance`, `m_nRecoilSeed`) — if the
     total-punch subtraction is not enough, implement the per-shot incremental kick
     matching `CCSPlayer_AimPunchServices::Predict`.
3. **Menu cursor still locked.** `ClipCursor(nullptr)` on open + every frame did not
   release it. The game re-clips (or re-centres / set-captures) on its window thread.
   Next: hook `ClipCursor`/`SetCursorPos`/`ShowCursor` in the **payload** (in-process,
   all threads) or intercept `WM_SETCURSOR`/`WM_ACTIVATE`; verify whether the game calls
   `ClipCursor` each frame (set a breakpoint / a counter) and whether `ShowCursor`
   count is the real issue. This goes away as a side effect of workstream A (an
   in-process WndProc hook replaces the layer's).

## Workstream A — retire the external renderer, render in-process

**Goal:** the ESP + menu are rendered from the game's own Present path with the in-process
ImGui D3D11 backend; the Vulkan layer (`overlay/`) and its shared-memory model are no
longer the default (keep them as an optional DXVK/Vulkan fallback).

**The infrastructure already exists** (built, wired but currently not the default):

| piece | file | state |
|---|---|---|
| DXGI swapchain method-table interposer | `payload/features/src/dxgi_interposer.cpp`, `present_hook.cpp` | compiles; `PresentHook::install/on_present` renders ImGui via `render::UiBackend` |
| in-process ImGui D3D11 backend | `payload/render/src/imgui_backend.cpp` (+ `ui_backend.hpp`) | compiles |
| present hook that drives the publisher | `payload/features/src/game_present_hook.cpp` | **the current default** (`payload_main.cpp` installs it, then the shm + Vulkan layer draws) |
| Vulkan layer + shm model | `overlay/` (`wire.hpp`, `transport.cpp`, `renderer.cpp`, `menu.cpp`) | current renderer |

**Steps:**

1. `payload_main.cpp` — install the in-process `PresentHook` (DXGI) by default when
   `dxgi.dll`/`d3d11.dll` are loaded; fall back to `GamePresentHook` + the layer when the
   game runs Vulkan/DXVK (`GetModuleHandleW(L"vulkan-1.dll")`), or behind an env switch.
2. Move the overlay model building + the menu UI onto the present thread using the
   existing `render::OverlayModel`/`Overlay::update` and `ImGuiBackend` (already
   parameterised by `UiBackend`), so `overlay/`'s `wire`/transport/renderer are unused on
   that path.
3. Bring the menu input to the payload: subclass the game window's WndProc from the
   payload (the layer's `menu.cpp` `hooked_wndproc` is a good reference) and drive ImGui
   directly; this also lets us release the mouse (`ClipCursor(nullptr)`, `ShowCursor`)
   on the game's own window thread — likely fixing the cursor bug.
4. Settings: with no shm, the payload owns `EspConfig` directly — the menu mutates it on
   the present thread. Keep the control channel (`payload/control`) for the harness.
5. Cleanup: mark ADR-0016 superseded (or "fallback only") and add a new ADR for the
   Present-renderer decision; ADR-0015 stays (we still use the DXGI method-table
   interposer); ADR-0017's "no Present work under Wine" note is why the Vulkan layer
   stays as a fallback.
6. Test: ESP + menu render, windowed/fullscreen toggle, resize stress, no freeze
   (the historic freeze was DXVK/Wine-only — native Windows was clean, see
   `docs/windows-validation.md`), and the cursor release.

## How to resume (live loop)

```powershell
meson compile -C build            # host tests
meson test -C build               # 16/16 expected
meson compile -C build-payload    # payload.dll (clang-cl)
meson compile -C build-overlay    # only when CS2 is stopped (the layer DLL is locked)
# launch (session 1, GPU) + inject once + bot match, leave running:
python scripts/run-scenario.py bot-match --keep-running --set map_name=de_dust2 --set per_team=4 --set bot_stop=false
# reattach after the scenario (do NOT inject twice):
python scripts/run-scenario.py status --no-launch --no-inject --keep-running
```

Gotchas (unchanged):

- Never inject twice (two pipe servers → random client).
- The overlay DLL cannot relink while CS2 is running; kill CS2 → relink → relaunch.
- The menu (shared settings block) is the owner of the rage settings: the payload
  re-pushes the block every frame, so `rage`-command changes to menu-owned fields are
  reverted. For automated tests use `force` (command-only) and/or write the shm block
  (`artifacts/menu_probe.py` has the offsets, regenerate with
  `artifacts/wire_offsets.cpp` after any `wire::Settings` change).
- Diagnostic scripts live in `artifacts/` (gitignored): `floor_probe.py`,
  `seam_probe.py`, `weapon_probe.py`, `silent_probe.py`, `menu_probe.py`,
  `re_filter.py`, `wire_offsets.cpp`.

## Reference docs

- `docs/ragebot.md` §0 (status), `docs/ragebot-review.md` (audit + fixes),
  `docs/re/bullet-trace.md` (filter/mesh RE), `docs/re/occlusion.md`,
  `docs/re/penetration.md`, `docs/re/subtick.md` (the silent-angle seams),
  `docs/re/aim.md` (inaccuracy/recoil fields), `docs/architecture.md`,
  `docs/decisions.md` (ADR-0015/0016/0017/0020/0021/0022).
