> **Note (2026-10-21):** the RE in this file was written on the unmerged
> branch `feat/t18b-neverlose-camera-split`, which also carries the camera
> split's **code** (the split's hooks, the eye-offset writer and the
> third-person camera's TLS fix). That code is **not** in the integration
> tree: the branch is far behind it and it also rewrites the auto-strafe,
> whose current form was measured afterwards (see `docs/handoff-wave2.md`
> section 5.1). The RE and the two **dumper signatures** it needs
> (`eye_angles_copy`, `view_angles_setter`) are here, so the offsets
> resolve; the hooks are a separate task.
# The camera / eye-angle split (build 659940c1)

Re-reversed 2026-10-20 on the live `client.dll`
(`sha256 659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`).
This replaces the T18b assumption that the camera reads the pawn's
`m_angEyeAngles`.

## The pipeline, in frame order

| step | address | what it does |
|---|---|---|
| the local view angles | `CCSGOInput + 0x688` | pitch/yaw/roll of slot 0 (the per-slot block, stride `0x928`). `input+0xb50/+0xb58` is the per-slot angle history; when it is empty â€” the normal case â€” this field is used directly. |
| the setter | `FUN_180b80e70(input, slot, angles)` | writes the angles it is handed into `input + slot*0x928 + 0x688` (slot 0) or the history entry. The prologue starts with a short `JNZ`, so the repo's inline relocator refuses it. |
| the per-tick copy | `FUN_180b07590(pawn, flags)` | the player post-think: reads the movement services' angle history, writes the result into `CCSGOInput + 0x688` **and** `movement_services + 0x240`, then updates the eye-angle state. Its prologue is 18 bytes (`sub rsp,0xa0`), also refused by the relocator. |
| the pawn's `EyeAngles()` | `FUN_180c74ec0` (vtable `+0x570`) | for non-local pawns it returns `pawn + 0x3600`; for the local player it calls `FUN_18093c4f0`, which reads the camera services at `pawn + 0x1328` and returns the view angles. |
| the eye-angle copy | `FUN_1808cb9b0` | calls the pawn's `EyeAngles()` virtual and stores the result into `m_angEyeAngles` (`pawn + 0x3600`). **This is downstream of the camera.** |
| the camera CalcView | `FUN_180a64570(services, origin, angles, â€¦)` | the normal `CPlayer_CameraServices::CalcView` fallback (called by `FUN_180882a00`); it fills the view setup's origin and angles. This is the last write of the rendered camera angles. |

Consequences:

* restoring `m_angEyeAngles` after `FUN_1808cb9b0` cannot keep the camera
  still â€” the camera never reads that field for the local player;
* the camera follows the local view angles / CalcView. The split therefore
  belongs at [`FUN_180a64570`] (the rendered angles) and/or at the view
  fields, not at the pawn's eye angles;
* `m_angEyeAngles` is the animation/hitbox aim. Keeping it 90Â° away from the
  view while the camera and movement stay on the view is the live proof of the
  split.

## Hooks that work on this build

| hook | why |
|---|---|
| `calc_view` = `FUN_180a64570` | 15-byte relocatable prologue; after the original, replace the view setup's angles with the player's own while the split is armed. |
| `local_eye_angles` = `FUN_18093c4f0` | 16-byte relocatable prologue; the local `EyeAngles` source â€” the eye-offset test adds its degrees here. |
| `eye_angles_copy` = `FUN_1808cb9b0` | already in the payload; still useful as the fallback restore and for diagnostics. |

The `view_angles_setter` (`FUN_180b80e70`) and `post_think`
(`FUN_180b07590`) are **not hookable** with the current relocator (short
branch / 18-byte prologue). They are kept as best-effort attempts; their
failure is logged, not fatal.

## The 90Â° test

`eye_offset(yaw=90)` holds `m_angEyeAngles = CCSGOInput + 0x688 + 90Â°`. The
engine rewrites the field several times a frame, so the control command also
starts a test-only 1 kHz writer (a skip-thread-attach thread). The camera
(`input + 0x688`) and the movement are untouched; in third person the model
aims 90Â° away from the camera.

Measured live 2026-10-20 (de_dust2, live round):

```
view = (0, -147.5, 0)      # CCSGOInput + 0x688
eye  = (0,  -57.5, 0)      # m_angEyeAngles = view + 90
held for the whole run; manual +forward moved the player (250 u/s) and the
camera stayed on the view.
```

## Crash found on the way: thread-local TLS in a detour

Enabling third person crashed with `read addr=0x4` in
`camera_update_detour`. The guard `g_camera_depth` was `thread_local`; the
manually mapped image got TLS slot **154**, above the 64 slots
`cs2::tls::ensure_thread_tls` supports, so the camera thread's TLS block was
null and the guard read `[null+4]`. The guard is synchronous, so it is now a
plain global. Do not add `thread_local` to a detour on a manually mapped
image unless the TLS bootstrap supports the allocated slot.

## Still open

* The directional auto-strafe still does not gain speed in the T18 build
  (the known T18 defect: no airborne samples in directional mode / flat in
  camera-relative). The unit of that fix is the strafe encoding, not this
  split.
* The `calc_view` hook only corrects the rendered angles; the origin is
  computed from the engine's angles before the correction. In first person
  this is irrelevant (the origin is the eye); in third person the payload's
  own camera model should use the view angles, not `m_angEyeAngles`.
