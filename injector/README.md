# injector

Manual-map injector for loading the payload DLL into the target process.

The target blocks `LoadLibrary` on unsigned modules and runs a signature check,
so we map the PE ourselves: parse it, map sections, apply base relocations,
resolve imports against the target's own modules, write the image, and call the
entry point on a remote thread. `LoadLibrary` is never used.

`injector.exe` (Windows x64) runs inside the **same Proton/Wine prefix** as the
target so `OpenProcess`/`VirtualAllocEx`/`CreateRemoteThread` see it.

## Usage

```sh
# inside the target's prefix (WINEPREFIX points at the game's pfx)
wine injector.exe <process-name|pid> <payload.dll> [--no-call]
```

`--no-call` maps and writes the image without invoking the entry point (useful
to inspect the mapped image).

## Build

```sh
BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
meson setup build-injector injector --cross-file toolchain/windows-msvc.ini
meson compile -C build-injector
```

This also builds two test aids: `victim.exe` (a benign sleeping process) and
`trivial.dll` (a CRT-less logging DLL that writes `C:\cs2-trivial-boot.txt`).

## Validate offline (no game needed)

```sh
export WINEPREFIX=/path/to/prefix
PW=/path/to/proton/files/bin/wine
"$PW" build-injector/victim.exe &            # start a target
"$PW" build-injector/injector.exe victim.exe 'Z:\...\trivial.dll'
cat "$WINEPREFIX/drive_c/cs2-trivial-boot.txt"
```

## Status

Works end to end for a CRT-less DLL (map + relocations + import resolution +
remote-thread entry). The static-CRT payload currently faults on entry because
manual mapping does not set up the loader's TLS / CRT init; that is the
remaining Phase 2 item.
