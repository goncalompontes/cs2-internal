# Native Windows build — untested marker

**Status: the native Windows build is NOT yet validated.**

The payload was developed and only ever exercised under Wine/Proton (see
ADR-0017 and `docs/handoff.md`). A build produced *natively* on Windows
therefore carries an explicit "untested" marker so it can never be confused with
a validated build.

Where the marker lives:

- **Build define** — `payload/meson.build` defines `CS2_NATIVE_WINDOWS_BUILD=1`
  when `host_machine.system() == build_machine.system() == 'windows'` (a native
  build). The Linux msvc-wine cross build instead gets
  `CS2_CROSS_WINDOWS_BUILD=1`, and a native configure prints
  `*** cs2-internal: NATIVE WINDOWS build — UNTESTED ON WINDOWS ***`.
- **Boot banner** — `payload/src/payload_main.cpp` logs
  `[cs2-payload] *** NATIVE WINDOWS BUILD - NOT YET TESTED ON WINDOWS ***`.
- **Marker file** — `C:\cs2-payload-boot.txt` records
  `build=native-windows-UNTESTED`.
- **HUD title** — `payload/features/src/overlay.cpp` appends
  `[NATIVE WINDOWS - UNTESTED]` to the overlay title, so it is visible on screen.

**Remove this marker** (the banner, marker-file value and HUD suffix, plus this
doc) only once the payload has actually been run on native Windows and the
ADR-0015 mode-change freeze has been confirmed or ruled out.
