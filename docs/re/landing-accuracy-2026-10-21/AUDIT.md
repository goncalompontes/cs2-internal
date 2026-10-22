# Landing accuracy and tickbase: inspected client, 2026-10-21

Inspected file: `F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll`. SHA-256: `5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`. Image base `0x180000000`; all addresses below are RVAs. This is a static inspection of the installed PE. No tickbase, game memory, command timing or runtime code was modified. Other project work remains separate.

## Verified path

- `0x8de310`, at `0x8de385`, dispatches through the active weapon's vtable byte offset `0xc50`.
- The primary C_CSWeaponBase vtable resolves that slot to `0x81fc70`. This multiplies the incoming landing scalar by the mode-specific land-inaccuracy getter at slot `0xb58`, then **adds** it to `m_fAccuracyPenalty` (`weapon+0x1a18`). The slot resolves to getter `0x7cc600`, which reads `CCSWeaponBaseVData::m_flInaccuracyLand` (`vdata+0x770`). The scalar's complete upstream derivation was not reconstructed; it is not assumed to be raw fall velocity.
- `0x827be0` is the normal accuracy updater (primary weapon vtable byte offset `0xd00`). Verified indirect callers include `0x9112d9` and `0x8249ff`; the complete command/movement/fire ordering was not reconstructed.
- That updater selects a baseline using movement type, ground/duck flags, weapon mode and reload state. If the baseline exceeds the current penalty it raises the penalty to the baseline. Otherwise it exponentially approaches the baseline.
- At `0x827de0..0x827e3e`, the actual expression is `baseline + (old-baseline) * exp(-log(10) * (1/64) / recoveryTime)`. Both imports were resolved from the current PE: `tier0!V_logf` and `tier0!V_expf`. The timestep is the literal float **0.015625**, loaded from RVA `0x1ac6064`. It is not a subtraction of controller tickbase and not an elapsed-time subtraction from the last accuracy timestamp in this normal recovery branch.
- At `0x827f8c`, the updater stores its current-time result into `m_flLastAccuracyUpdateTime` (`weapon+0x1a1c`). This timestamp exists but is not used as the decay interval in the normal branch above. A separate path around `0x808d43` does use a difference against it; that path also resets recoil and turning state and must not be confused with normal per-update recovery. Its complete lifecycle classification is not claimed.
- `0x80cef0` selects the recovery time from VData standing/crouching and recoil-transition parameters. Airborne recovery uses **4 times** crouch recovery in the inspected default function. Grounded recovery uses the corresponding stance values and optional recoil transition.
- The normal final inaccuracy getter at `0x80c1a0` starts with the stored penalty and adds movement, airborne and turning/weapon components, with an upper clamp. Spread is a separate getter (`0x80d490`, VData `m_flSpread` at `+0x750`).

## What this establishes about tickbase

Changing `CBasePlayerController::m_nTickBase` (`+0x6b8`) by itself does not provide a larger timestep to the normal accuracy-decay expression. Advancing the apparent clock and invoking this updater once still applies one fixed 1/64-second decay step. Additional actual simulation/update calls would be needed to apply additional decay steps along this path. Calling updates manually or changing client fields does not establish that the server accepts matching simulated time or shot state.

Landing can add a fresh penalty after any prior recovery, so running recovery before contact does not prove accuracy at the shot's actual landing state. Even full return to the stance baseline does not imply zero spread: movement/turning components and separate weapon spread still matter. A client-only audit cannot establish a server-accepted tickbase exploit or guarantee perfect accuracy on landing.

For a constant baseline and recovery time, `N` updates leave a residual multiplier `10^(-N/(64*T))`. As a numerical illustration only, T=0.4 seconds needs 26 updates to fall below 10% residual and 52 below 1%; these are not measurements of any particular weapon's configured recovery time. Finite mathematical decay is asymptotic, subject to floating-point rounding.

The useful target is the earliest legal shot whose *post-landing, correctly predicted* inaccuracy and spread meet the desired threshold. That requires establishing movement/contact -> landing penalty -> weapon update -> shot order, and validating the matching server path; merely forcing a larger client tickbase is insufficient evidence.

## Files

`client-audit.json` records the schema, resolved functions, constants and file identity. `recovered_accuracy.cpp` contains readable simplified pseudocode. The adjacent `.asm` exports preserve the inspected branches and callers. Earlier notes for other builds were used as leads; addresses and key formulas above were checked against this PE.
