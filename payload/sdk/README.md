# payload/sdk — generated headers

Headers here are **generated** by the external dumper; do not edit them by
hand. They are git-ignored (the build is version-specific) and regenerated from
a dump of the target.

## Regenerate

```sh
# 1. dump the pieces you need as JSON (a running target is required for this)
cs2-dumper --interfaces --json --module client.dll > build/interfaces.json
cs2-dumper --schema --json --schema-system 0x7ff6XXXXXXXX > build/schema.json

# 2. render the C++ headers (no target needed)
cs2-dumper --gen --interfaces-json build/interfaces.json --schema-json build/schema.json
```

`--gen` defaults to this directory; pass a path to override it.

## Layout

| File | Contents |
|---|---|
| `interfaces.hpp` | `cs2::sdk::INTERFACES` — `CreateInterface` registrations + `find_interface` |
| `offsets.hpp` | `cs2::sdk::OFFSETS` — resolved RVAs + named constants + `find_offset` |
| `schemas.hpp` | umbrella include for the per-module schema headers |
| `schemas/<module>.hpp` | `cs2::sdk::schemas::<module>` — class/field metadata + lookups |

All generated identifiers follow the project conventions (ADR-0010).

## Caveat

The schema walker still uses **placeholder** `SchemaLayout` offsets until
calibrated against a live build (see `docs/roadmap.md`, `docs/re/schema-system.md`),
so class/field output is not yet trustworthy. The interfaces list is derived
from the real registration descriptors.
