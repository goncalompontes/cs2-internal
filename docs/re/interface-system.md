# Interface system (`CreateInterface`)

## Mechanism

Every module exports `CreateInterface(const char* name, int* out_ok)`. It walks a
**singly linked list** of registrations.

```c
struct InterfaceReg {            // 24 bytes, x86-64
    void* (*create)();           // +0x00
    const char* name;            // +0x08
    InterfaceReg* next;          // +0x10
};
```

The list head lives in a **module-local global**:

| Module | Head global (VA) |
|---|---|
| `schemasystem.dll` | `0x180072030` |
| `engine2.dll` | `0x1806972b8` |
| `client.dll` | TBD (from its `CreateInterface` decompilation) |

The global is **NULL in the on-disk image**: the list is built at load time by
static initializers.

## `CreateInterface` (schemasystem.dll @ `0x180037560`)

```c
puVar3 = *s_pInterfaceRegs;                 // head
do {
    if (!puVar3) { if (out_ok) *out_ok = 1; return 0; }
    name = (char*)puVar3[1];                // +0x08
    if (strcmp(param_1, name) == 0) {       // inline compare
        if (out_ok) *out_ok = 0;
        return ((code*)puVar3[0])();        // call create()  (+0x00)
    }
    puVar3 = (void**)puVar3[2];             // next  (+0x10)
} while (true);
```

## Registration constructor (schemasystem.dll @ `0x180037540`)

```asm
mov [rcx+8],  r8      ; node->name   = name
mov [rcx],    rdx     ; node->create = create
mov rax, [rip+head]   ; rax = *s_pInterfaceRegs
mov [rcx+0x10], rax   ; node->next   = old head
mov [rip+head], rcx   ; *s_pInterfaceRegs = node
ret
```

So each registration is a static `InterfaceReg` **global** linked onto the head
at load.

## Implication for tooling

- **Static:** the descriptors are 24-byte `{create, name, next}` globals whose
  `name` points to a versioned string (e.g. `SchemaSystem_001`). `next` is 0 in
  the file image until the loader links them.
- **Runtime:** read `*head_global`, then walk `next` — this is the authoritative
  list. `cs2-dumper --interfaces-global <addr>` does this.

Note: an earlier heuristic full-scan in the dumper latched onto an unrelated
16-byte `{name, x}` table; the linked-list model above supersedes it.
