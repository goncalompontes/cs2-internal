# The curated offsets, resolved offline

> **The one-line version:** the payload's offsets (`calibration/offsets.json`)
> do **not** need a running game. The dumper's `--file` mode resolves the
> very same curated signatures from the module's file on disk that the live
> calibration resolves in the process' memory, and the result is
> **byte-identical**. The *interfaces* and the *schema* still need the live
> process - they come from its `CreateInterface` registry and its schema
> system.

This note records the **method** and the **measurement**, so the claim can be
re-checked on any build instead of taken on faith.

---

## 1. Why the offsets do not need the game

The curated signatures live in one place -
`external/dumper/src/analysis.rs::known_signatures()` - and the dumper
resolves them through one function, `analysis::resolve_known(module, bytes,
base)`. That function is called from **both** modes:

| mode | what it reads | where |
|---|---|---|
| live | the module's bytes **in the process' memory** | `main.rs` → `analyze()` |
| file | the module's bytes **from the PE on disk**, mapped to RVA addressing (`pe::map_file_to_rva`) | `main.rs` → `analyze()` |

Because both modes feed the same bytes (a mapped image is what the process
holds anyway) and the same resolver, the offsets are the same. The live mode
buys nothing for the offsets - it buys the **process**, which the interfaces
and the schema need.

So:

```powershell
cs2-dumper.exe --file "<game>\game\csgo\bin\win64\client.dll" `
                --resolve-offsets --json --module client.dll
```

is the whole story of the offsets.

## 2. The measurement

Machine: the primary Windows box, CS2 (appid 730) at
`F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive`:

| module | sha256 | size |
|---|---|---|
| `game\csgo\bin\win64\client.dll` | `659940c1db362ed981d1df10637cec8c2a3d99409b77fc3b73e435e4649e36b0` | 39 205 528 B |
| `game\bin\win64\engine2.dll` | `6456df18d8f954394024c2c07da8c848df15a5ace74ce7390d78970ceb39ee21` | 6 829 720 B |

The **shipped** `calibration/offsets.json` (written by the live calibration)
and the **offline** resolution (`scripts/resolve-offsets-offline.py`, which
drives the dumper's `--file` mode) agree on every entry:

| offset | the shipped `calibration/offsets.json` | the offline resolution |
|---|---|---|
| `game_entity_system` | `0x271a908` | `0x271a908` |
| `entity_list` | `0x236fa70` | `0x236fa70` |
| `game_rules` | `0x25651d0` | `0x25651d0` |
| `local_player_controllers` | `0x253cb78` | `0x253cb78` |
| `view_matrix` | `0x256b020` | `0x256b020` |
| `game_event_manager` | `0x254bc08` | `0x254bc08` |
| `event_key_hash` | `0x166050` | `0x166050` |
| `create_move` | `0xd02130` | `0xd02130` |
| `create_move_input` | `0xb65de0` | `0xb65de0` |
| `ccsgo_input` | `0x257b640` | `0x257b640` |
| `user_cmd_array` | `0x2223d00` | `0x2223d00` |
| `get_user_cmd_tick` | `0x15df9f0` | `0x15df9f0` |
| `get_user_cmd_array` | `0x9446c0` | `0x9446c0` |
| `get_user_cmd_by_sequence` | `0x9444a0` | `0x9444a0` |
| `repeated_ptr_add` | `0x1233010` | `0x1233010` |
| `subtick_step_alloc` | `0x4f74a0` | `0x4f74a0` |
| `physics_query_interface` | `0x2227058` | `0x2227058` |
| `physics_query_filter` | `0x2227070` | `0x2227070` |
| `entities_in_box` | `0x9dbd20` | `0x9dbd20` |
| `entities_along_linear_sweep` | `0x9db9a0` | `0x9db9a0` |
| `surface_properties_list` | `0x254e100` | `0x254e100` |
| `input_service` (`engine2.dll`) | `0x8dbf20` | `0x8dbf20` |
| `cbuf_add_text` (`engine2.dll`) | `0x1b87c0` | `0x1b87c0` |

**All twenty-three match** - and the file the offline path writes is
**byte-identical** to the shipped one (same SHA-256, same 2 823 bytes).

The check, on the machine:

```powershell
python scripts\resolve-offsets-offline.py --check
```

```
resolve-offsets-offline: game: F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive
resolve-offsets-offline: dumper: ...\cargo\release\cs2-dumper.exe
resolve-offsets-offline: client.dll: 21 offset(s) [...\game\csgo\bin\win64\client.dll]
resolve-offsets-offline: engine2.dll: 2 offset(s) [...\game\bin\win64\engine2.dll]
resolve-offsets-offline: no difference: ...\calibration\offsets.json matches the offline resolution (23 offset(s))
```

## 3. What still needs the live process

| dump | source | needs the game? |
|---|---|---|
| `offsets.json` | the curated signatures, resolved in the module's bytes | **no** - the file on disk is enough |
| `interfaces.json` | the `CreateInterface` registration list, walked in the process | **yes** |
| `schema.json` | the classes of the `CSchemaSystem*` | **yes** |
| `schema_system.json` | the `CSchemaSystem*` pointer per module | **yes** |

The interfaces and the schema are **runtime state** (a registry the game
fills at start-up, and a heap of schema classes), not patterns in a binary:
they cannot be read out of a file, and the live calibration
(`scripts/calibrate-live.py`) stays the way to refresh them.

## 4. Adding a signature

1. Cut the signature and add it to
   `external/dumper/src/analysis.rs::known_signatures()` (see
   [`README.md`](README.md) §5).
2. Check that it resolves, and that it is unique:

   ```powershell
   cs2-dumper.exe --file <dll> --resolve-offsets --json --module <dll>
   ```

   A signature that does **not** match is reported on stderr as
   `unresolved: <name>` - the offline path reads exactly that line, so a
   signature that silently stops matching is **named**, not dropped.
3. Refresh the dump:

   ```powershell
   python scripts\resolve-offsets-offline.py          # writes the dump
   python scripts\resolve-offsets-offline.py --check  # compares, writes nothing
   ```

   No game, no process, no operator in the interactive session: the module
   list comes from the dumper (`--list-signature-modules`, derived from the
   signature registry), so a signature for a **new module** is picked up with
   no edit anywhere else.
4. Regenerate the SDK headers and rebuild the payload
   (`scripts/gen-sdk.py`, `meson compile -C build-payload`) - the payload
   consumes `payload/sdk/offsets.hpp` via `find_offset("name")`.

> A signature that the dump does not carry is an offset the payload cannot
> resolve, and the feature that needs it is **dead** until the dump is
> refreshed. That is what the animfix's review found in the shipped dump
> (T20, round 5): the dump had none of the animfix's signatures.
