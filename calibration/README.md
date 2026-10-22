# Calibration dumps

This directory holds the JSON dumps the [SDK header generator](../../scripts/gen-sdk.sh)
consumes:

| File | Produced by | Contents |
|---|---|---|
| `interfaces.json` | `cs2-dumper --interfaces --json` | `CreateInterface` registrations |
| `schema.json` | `cs2-dumper --schema --json` | class/field metadata |
| `offsets.json` | `cs2-dumper` offset resolution | resolved RVAs |

The dumps are **build-specific**: they describe one target build's layout. They
are produced by [live calibration](../../scripts/calibrate-live.sh) against a
running target, or by hand from static analysis.

The Meson build renders `payload/sdk/*.hpp` from whatever is present here (see
the `sdk-headers` target and the `sdk_gen` option). Missing files simply yield
empty sections, so a dump-free build still succeeds.

To populate this directory from a running target:

```sh
scripts/calibrate-live.sh
```

Names are intentionally the same as the `--*-json` inputs, so the directory can
be passed straight to the dumper:

```sh
cs2-dumper --gen calibration --interfaces-json calibration/interfaces.json ...
```
