# Silent air-strafe and active mouse gain — 2026-10-22

## Findings and changes

The user reported visible steering and poor live speed gain after the prior planner-only gain correction. Agent-side read-only status capture against CS2 pid 19328 failed with Windows error 13 (permission denied on the existing named pipe). No samples were collected; build-tools/strafe-camera-live-before.json records that failure. No live gain/camera claim is derived from it.

Reference Velocity core/features/combat/impl/misc.cpp appends an inverse accumulated yaw delta at nextafter(1,0), and core/hooks/impl/cheat.cpp uses a function detour around handle_view_angles to save/restore real view. Our previous adapter omitted the restoration tail and only changed the object's vtable camera slot.

This build reserves a step slot for a restoration tail at nextafter(1,0), canceling only accumulated synthetic yaw. Physical mouse yaw remains part of the completed command. Silent shot correction rebases both the first steering step and the inverse tail so the completed command retains the requested shot frame. The camera handler now uses MinHook on the resolved function, covering direct calls as well as vtable dispatch. It saves visible view, calls the original, restores the view, then rereads it to verify pitch/yaw restoration. The installed client's handler resolved to RVA 0xcea280; actual prologue patch/restore was checked on a private mapped PE without executing client code.

The prior event-prefix policy deferred all assistance until after the last physical event, including mouse yaw. Late mouse input therefore left little or no time to accelerate despite a correct empty-timeline model. Now direction/analog/jump transitions retain their protected prefix, while mouse yaw timestamps are included in the planner's actual event times. The encoder adds steering corrections into existing mouse entries at those shared timestamps instead of doubling their event count. Original and modified semantic payloads are tracked, so same-command rebuilds and disabling the feature restore original physical data. Engine-written replacement data is left untouched. Semantic ownership ignores protobuf cached-size metadata and padding; changing those during serialization no longer makes our steps look like new physical input and accumulate duplicates.

The planner integrates actual timestamp intervals. It does not extend a short suffix into a whole tick or alter velocity/tickbase. A reserved tail consumes practically zero time. Capacity remains 32 entries; insufficient capacity passes through to native input rather than overwriting unrelated events. The first synthetic entry remains separate so silent shot-frame correction applies before the gain window.

## Verification

Native fixture tests decode the actual command timeline into velocity and verify positive gain with a mouse event at 0.875, and with sixteen physical mouse samples. Both must exceed a modeled three-unit/second gain from an initial 300 units/second, stay within capacity, and finish with only the player's original yaw. They also verify camera getter/setter save/restore, same-command tail uniqueness, disabling restores original physical input, serialized cached-size changes do not accumulate steps, and final silent-shot yaw remains requested. Existing pure-model, input, autostop, rage and host suites remain required.

These tests establish fixture behavior, not successful live server acceleration or perfect camera behavior in the user's session. No new live recording has been collected. Server physics inputs still have the previously documented defaults/settings; automatic discovery of all physics convars is not part of this fix.

## Read-only desktop capture

Run Capture Strafe Status.cmd beside the package after loading the new build. It calls only the existing input-status request, never changes settings or sends player input, and writes strafe-status-result.json. The output includes client movement velocity/command counters, active steps/reason, modeled gain and gain-window start, camera function-hook status, restoration counters, camera yaw before/original-candidate/restored, and whether the reread matches the saved view. Samples are asynchronous client observations, not server-authoritative accuracy or movement measurements.

Use 16 subticks and 100% steering strength for comparison. Compare with rage/autostop and command choking off first to isolate steering, then test those features separately. The capture launcher runs in the user's desktop session to avoid the agent session's denied pipe access.
