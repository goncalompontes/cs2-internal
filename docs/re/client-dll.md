# `client.dll` (build 25815307)

Image base `0x180000000`. Analysis is partial (auto-analysis hit the 600 s cap);
functions/strings/references are populated, `Decompiler Parameter ID` did not run.

## Globals found

| What | Address | Notes |
|---|---|---|
| `CreateInterface` head global | `0x1827946c8` | `*global` = list head; NULL in file image |
| `CGameEntitySystem` / `Source2EntitySystem` singleton | `0x182541b20` | vtable `0x181c25200` |

### Interface list
`CreateInterface` @ `0x1819c6770` reads head from `0x1827946c8` and walks
`InterfaceReg { create@0, name@8, next@0x10 }` (identical to the other modules).

### Entity system
Static initializer `FUN_1800be600`:

```c
FUN_1803516d0(&DAT_182541b20, "Source2EntitySystem");  // register singleton
_DAT_182541b38 = 0;
DAT_182541b20 = &PTR_LAB_181c25200;                    // vtable
```

So the entity system is a static C++ object at `0x182541b20` implementing the
`Source2EntitySystem` interface. `FUN_1803516d0` is a generic singleton/registry
linker (`node[0]=create/vtable, node[1]=prev head, node[2]=name`, head global
`DAT_1823b2538`).

## Useful anchors

Debug strings present in `.rdata` (good reference points for signatures):

- `Source2EntitySystem`, `Source2EntitySystemDeallocator`
- `CGameEntitySystem::CreateEntities`, `CGameEntitySystem::InitEntity2NetworkClasses`
- `CEntitySystem::RegisterEntityClass`, `CEntitySystem::AllocateEntityIdentity`
- `EntitySystem - Entity Networking - start/finish`, `EntitySystem - Class Tables`

## TODO

- Entity-list offset inside `CGameEntitySystem`.
- Local player controller/pawn accessor.
- View matrix global.
- Game rules global.
- Resume full analysis with a longer timeout (`analyze-program timeoutSeconds:-1`).
