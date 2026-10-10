# injector

Manual-map injector for loading the payload DLL into the target process.

The target blocks `LoadLibrary` on unsigned modules and runs a
catalog-signature check, so a manual map (map the PE ourselves, resolve imports/relocs, call the entry)
is required.

Planned:
- Windows manual-map injector (a small PE) run inside the same Proton prefix:
  `wine injector.exe <pid|name> payload.dll`.
- Later: a hand-rolled loader for learning, and scripted injection from
  `scripts/`.

Build (cross, Meson — separate project once implemented):

```sh
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-injector injector --cross-file toolchain/windows-msvc.ini
meson compile -C build-injector
```
