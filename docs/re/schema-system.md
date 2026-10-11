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

## `CSchemaSystem` partial layout (from `FUN_1800107d0`)

`param_1` is the `CSchemaSystem*`. Observed fields (offsets relative to it):

| Offset | Type | Meaning (observed) |
|---|---|---|
| `+0x158` | `int` | vector size (loaded modules / type scopes) |
| `+0x160` | `void**` | vector data |
| `+0x168` | `int` | vector capacity |
| `+0x16c` | `uint` | vector allocation flags (upper 2 bits) |

So a `CUtlVector`-like block sits at `+0x158` with **size at `+0x158`, data at
`+0x160`, capacity at `+0x168`, flags at `+0x16c`** (note: not the classic
`{ptr,alloc,grow,size}` order). This differs from the placeholder layout in the
Rust dumper and must be reconciled when calibrating `SchemaLayout`.

## TODO

- Locate the `SchemaSystem_001` create function and the `CSchemaSystem` singleton
  global.
- Recover `CSchemaTypeScope` and `SchemaClassInfoData` layouts (name, module,
  size, fields vector, field stride).
- Confirm the type-scopes vector offset on `CSchemaSystem`.
