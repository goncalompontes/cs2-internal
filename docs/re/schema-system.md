# Schema system (`schemasystem.dll`)

## Exports

`CreateInterface`, `InstallSchemaBindings`, `ExtractModuleMetadata`,
`GetResourceManifests`, `GetResourceManifestCount`, `BinaryProperties_GetValue`,
`entry`, `tls_callback_0`.

## Loading flow

`FUN_1800107d0` (a `CSchemaSystem` method) loads schema data per module and calls
each module's exported `InstallSchemaBindings("SchemaSystem_001", CSchemaSystem*)`:

```c
pcVar8 = Plat_GetModuleProcAddress(module, "InstallSchemaBindings");
if (pcVar8) { ok = (*pcVar8)("SchemaSystem_001", param_1 /* CSchemaSystem* */); ... }
```

Log channel message: *"SchemaSystem tried to load schema data for module '%s' …"*.

## Globals

| What | Module | Address |
|---|---|---|
| `CSchemaSystem*` | `schemasystem.dll` | `0x1800715e0` (set by its `InstallSchemaBindings`) |
| `CSchemaSystem*` | `client.dll` | `0x182793aa0` (set by its `InstallSchemaBindings`) |
| Registrar list head | `schemasystem.dll` | `0x180072080` |
| Registrar list head | `client.dll` | `0x182794d08` |

## Registration mechanism

Each module exports `InstallSchemaBindings("SchemaSystem_001", CSchemaSystem*)`:

```c
if (strcmp("SchemaSystem_001", name) != 0) return 0;
if (!installed) {
    g_schema_system = schema_system;      // e.g. 0x182793aa0 in client.dll
    install(schema_system);               // e.g. FUN_1819d3d60
    installed = true;
}
return 1;
```

`install` (e.g. `FUN_1819d3d60` in client.dll, `FUN_180038c60` in schemasystem.dll)
iterates a **linked list of registrar objects** (`head = g_registrars`) and calls
each registrar's virtual `vtable[0](schema_system, pass, failure_buffer)`, up to
**999 iterations across up to 4 passes**; on success it finalizes with the
schema system's virtual method at **vtable offset `+0xe0`**.

Registrar object shape (observed in `FUN_180493b30`):

```
{ const char* class_name; void* serializer; const char* serializer_name; int flags; }
```

## `CSchemaSystem` partial layout

From `FUN_1800107d0` (`param_1` = `CSchemaSystem*`):

| Offset | Type | Meaning |
|---|---|---|
| `+0x158` | `int` | vector size |
| `+0x160` | `void**` | vector data |
| `+0x168` | `int` | vector capacity |
| `+0x16c` | `uint` | vector flags (upper 2 bits) |

## Still to recover

- `CSchemaTypeScope` and `SchemaClassInfoData` layouts (name/module/size/fields
  vector/field stride) — the definitive source is a registrar's field walk, or a
  **runtime** dump via the `CSchemaSystem` global.
- The exact type-scopes vector offset on `CSchemaSystem`.
