# Binary audit: autostop with held activation key, silent aim and penetration

Database: cs2_212C3300000.bin.i64, queried through the user's IDA MCP at port 13337 on 2026-10-17. Dump SHA256: 3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27. The installed client was inspected separately with llvm-objdump; its SHA256 and exact source sites are in manifest.json. No unrelated Lua menu sources were used. The IDA database itself was not changed.

## Verified findings and corrections

| Evidence | Observed behavior | Implementation correction |
|---|---|---|
| Dump 0x2f1389 | Reads the hit record's endpoint contact at +0x12 | Replaced the adapter's wrong +0x10 contact index. A regression deliberately gives these two contacts different actors. |
| Dump penetration consumer; current client 0x8858f0+ | An accepted target has contact type +0x33 == 1, hitbox pointer +0x10, and group from hitbox +0x38 in 0..11 | Added contact/hitgroup validation, serial-aware actor resolution, and engine hitgroup propagation into damage scaling. |
| Dump 0x50858e..0x508598 and 0x510bc4..0x510c20 | Aim commits command angles before attack state | Native translation now commits CBaseUserCmdPB.viewangles as well as history angles, with the actual protobuf presence bits; no dump-internal command offsets are copied into CUserCmd. |
| Dump stopping/movement policy; native shared command | Stop and shot movement use the same command basis | Rebase forward/left axes and existing analog subtick residuals into the shot yaw, preserving world-space braking, button events, event times and yaw deltas. No camera-global angle write is added. |
| Dump 0x50fe03, 0x50fe46 and 0x33824f | Timed correction outputs can be doubled before combination | Current client 0x852de0 calls 0x84d8b0 with the doubling flag set; 0x84e1cc+ confirms the doubling. Keep the existing query result scale; do not multiply recoil by two again. |
| Dump selected-point recheck at 0x507a07 | Selection is checked again before attack | Re-evaluate full acceptance through the world backend immediately before publishing aim. |
| Exact packaged PDB resolves logged payload+0x2af92 | Input destructor attempts to restore a client hook slot during shutdown | Restore only owned, still-mapped slots, preserve page protection, and tolerate a client image already being unmapped. This is a shutdown fault, not evidence of a firing-time fault. |

Readable protobuf default instances are not treated as writable messages. Missing mutable history still prevents synthetic attack rather than overwriting a global default. The existing history-index presence bit 0x20 was checked against the installed consumer at 0xd02833 and 0xd028f8 and retained.

## User's reported case

The affected button is the ragebot activation key (X), not merely left mouse. Autostop being gated by that key remains intentional. The correction addresses stop and shot writes in the same active command. The regression exercises 100 changing target yaws while autostop is enabled, visits each command twice to model both hooks, verifies command/history agreement, and compares resulting world-space braking with the autostop-only baseline. Separate held-fire tests preserve attack edges and non-attack buttons.

## Verification

17 host tests and 9 payload tests pass. Cases include differing entry/endpoint contacts, invalid contact type/group, wrong actor, opaque wall exit, engine hitgroup scaling, read-only protobuf defaults, protected/unmapped hook slots, combined stop/aim commands, history indices, repeated command visits, expiry, latest-snapshot worker behavior and software-D3D pixel output. The final actual payload DLL also loads and renders in the hidden WARP integration host. None of these is a CS2 shot-registration test.

## Recovery limits

The original dump does not map callbacks 0x7ffcef4a8db0 (penetration) and 0x7ffcef4a3e50 (trace/damage). Their original engine bytes cannot be decompiled from this IDB. The installed client's current bullet/penetration routines were inspected to validate the native adapter, not presented as byte-identical recovery of those original targets. Full original timing/record eligibility, all quaternion-mode policies, command-seeded spread, and server-side registration have not been established. This audit therefore does not claim perfect logic or complete source recovery. The corrected build still needs in-game verification with the user's current game.

## Subsequent user-requested activation change

The user then requested continuous activation from the menu toggle. The native and retained overlay menus now label it Ragebot, with no trigger-key selector. The runtime no longer polls X or another activation key; enabled state drives aiming and autostop continuously while target/state acceptance remains valid. Auto Fire stays separately controlled. Disabling the master toggle immediately closes the input gate and prevents an in-flight worker result from authorizing aim. The old key byte and control parameter remain only for layout/API compatibility. The worker regression now enables the ordinary menu state with key=0, without forced mode, and checks cancellation on disable.
