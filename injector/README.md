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
wine injector.exe <process-name|pid> <payload.dll> [--no-call] [--load-missing] [--register|--no-register]
```

`--no-call` maps and writes the image without invoking the entry point (useful
to inspect the mapped image). `--load-missing` loads import modules that the
target doesn't already have (via a remote `LoadLibraryW`); off by default, since
a payload that needs a graphics stack should target a process that has one.

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

Works end to end: map + relocations + import resolution (incl. Wine's
kernel32→kernelbase **forwarders**) + PEB loader registration + remote-thread
entry. Verified by loading `trivial.dll` (CRT-less), a minimal CRT DLL, and the
real `payload.dll` into `victim.exe` — each wrote its boot marker.

**Native Windows.** The synthetic PEB loader-list entry faults
`LdrpInitializeThread` there (a thread attaching to the process walks the list),
so it is skipped by default on native Windows and kept under Wine/Proton
(auto-detected via `ntdll!wine_get_version`). Override with `--register` /
`--no-register`. With registration skipped, `trivial.dll` loads and its marker is
written on Windows.

Remaining: registering `.pdata` exception tables (`RtlAddFunctionTable`) for
throwing code; not needed for the current payload. TLS is handled: the payload
has a TLS directory, so the injector allocates a slot in the target and the
entry stub sets the thread's slot via `gs:[0x58]` before `DllMain`.

> **Windows Defender** quarantines `injector.exe` after it runs (the heuristic
> flags manual-map injectors). Add a folder exclusion if you need to re-run it.
