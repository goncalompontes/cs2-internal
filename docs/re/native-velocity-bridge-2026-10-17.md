# Native command bridge and camera integration

The Velocity reference supplies the native input/camera integration. Existing dump-derived targeting, heatmap selection, penetration and autostop calculations remain in place. This work does not complete recovery of the original binary.

## Command submission

Native Windows now edits once after input CreateMove (slot 5), leaving slot 25 unchanged. Current command lookup uses the controller command-base sequence directly. Before submission, all three legacy button words are synchronized into CInButtonStatePB and move_crc is rebuilt from the final button and base-angle protobuf fields. The serialized move_crc field is protobuf data, not a CRC32 checksum. Engine assignment copies a borrowed 32-byte MSVC string view into engine-owned storage; the reference's undersized 24-byte temporary is not reproduced.

A dedicated view-handler hook (slot 8) captures/restores visible camera angles separately from submitted aiming angles. The menu Ragebot toggle remains continuous; Auto Fire is separate. Input status exposes native_command_ready, command_crc_writes, command_crc_failures and camera_restores. First successful finalization is logged, with failed finalization clearing pending aim.

## Camera menu

The Camera tab has Third person, Distance (35–200), Collision hull (0–20), and Remove scope overlay. Both features default off. Third person uses visible angles and the local eye position, with a symmetric hull sweep using Velocity's collision filter. World hits shorten the camera distance; other hit entities follow the reference behavior. Dead/missing local pawns leave the camera alone. Scope removal clears only the render output flag after the original scope callback; weapon zoom and FOV are preserved.

## Installed client binding evidence

All values below are RVAs in the installed client.dll and are resolved through checked patterns at runtime, rather than used as absolute pointers.

| Binding | Current RVA |
| --- | --- |
| Controller command base | 0x944530 |
| Command by sequence | 0x9444a0 |
| Get visible view | 0xb71010 |
| Set visible view | 0xb80e70 |
| View handler | 0xcea280 |
| Assign move_crc | 0x1232ac0 |
| Button PB allocation | 0x4f7420 |
| OverrideView | 0xb9c970 |
| Scope renderer | 0x8e15e0 |
| Camera TraceShape | 0xa1a000 |
| Trace filter initialization | 0x352920 |

The old Velocity TraceShape entry signature is stale. The replacement was located from the current TraceShape string and checked against its disassembly/argument layout. Button allocation is additionally validated against CInButtonStatePB RTTI and the 0x30-byte constructor layout.

## Verification

Host tests: 17/17. Payload tests: 11/11. Legacy overlay tests: 1/1. Native command tests cover independent protobuf bytes, arena tagging, button allocation/synchronization, final encoding and camera restoration. Camera tests cover placement, world collision, other-entity behavior, symmetric hull, distance limits, disabled/dead state and scope-only output modification. An offline PE probe validates all command and camera signatures against the installed client bytes without executing client code. The actual payload loads and renders through its own DXGI hook in an agent-created hidden WARP process.

These checks do not establish in-game shot registration or camera rendering. User testing in a restarted CS2 process is still required.

Installed client SHA256: `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0`
