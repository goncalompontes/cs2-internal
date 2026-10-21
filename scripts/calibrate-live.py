#!/usr/bin/env python3
"""Live calibration: dump interfaces and resolve the schema system.

Attaches to a running target (the Proton/Wine client) via the dumper's live
memory backend and writes build-specific dumps into the calibration directory:

    interfaces.json     CreateInterface registrations (authoritative linked
                        list, walked from each module's head global)
    schema_system.json  the resolved CSchemaSystem* per module
    _manifest.json      process + module listing

The head globals come from config/interfaces-heads.json and
config/schema-heads.json (VAs at image base 0x180000000); the module base at
runtime is added to each.

Reads require CAP_SYS_PTRACE (see docs/architecture.md):

    sudo setcap cap_sys_ptrace+ep external/dumper/target/release/cs2-dumper
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

IMAGE_BASE = 0x180000000

MODULE_RE = re.compile(r"^\s*(\S+)\s+base=(0x[0-9a-fA-F]+)\s+size=(0x[0-9a-fA-F]+)")


def run(binary, *args):
    return subprocess.run([binary, *args], capture_output=True, text=True, check=True)


def module_bases(binary, process):
    out = run(binary, "--process", process).stdout
    bases = {}
    for line in out.splitlines():
        m = MODULE_RE.match(line)
        if m:
            bases[m.group(1)] = int(m.group(2), 16)
    return bases


def load_heads(path):
    if not path.is_file():
        return {}
    data = json.loads(path.read_text())
    return {k: int(v, 16) for k, v in data.items() if not k.startswith("_")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump_dir", nargs="?", default=None)
    ap.add_argument("--process", default=os.environ.get("CS2_PROCESS", "cs2.exe"))
    ap.add_argument("--bin", dest="binary", default=None)
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    dump_dir = pathlib.Path(args.dump_dir) if args.dump_dir else root / "calibration"
    exe = "cs2-dumper.exe" if os.name == "nt" else "cs2-dumper"
    binary = args.binary or str(root / "external" / "dumper" / "target" / "release" / exe)
    dump_dir.mkdir(parents=True, exist_ok=True)

    if not os.path.exists(binary):
        print("calibrate-live: building cs2-dumper (release)", file=sys.stderr)
        subprocess.run(
            ["cargo", "build", "--release", "--manifest-path", str(root / "external/dumper/Cargo.toml")],
            check=True,
        )

    try:
        manifest = json.loads(run(binary, "--process", args.process, "--json").stdout)
    except (subprocess.CalledProcessError, json.JSONDecodeError) as e:
        print(f"calibrate-live: target {args.process!r} not available: {e}", file=sys.stderr)
        return 1
    (dump_dir / "_manifest.json").write_text(json.dumps(manifest, indent=2))

    bases = module_bases(binary, args.process)

    # --- interfaces ---------------------------------------------------------
    # Preferred: walk the authoritative registration list from each module's
    # head global (config/interfaces-heads.json). Those VAs are build-specific,
    # so when the head walk comes back empty we fall back to seed-based
    # discovery, which locates the InterfaceReg nodes from known name strings.
    interfaces = []
    heads = load_heads(root / "config" / "interfaces-heads.json")
    for module in ("client.dll", "engine2.dll", "schemasystem.dll"):
        base = bases.get(module)
        if base is None:
            continue
        items = []
        source = "head"
        va = heads.get(module)
        if va is not None:
            addr = base + (va - IMAGE_BASE)
            out = run(
                binary, "--process", args.process, "--interfaces", "--json",
                "--module", module, "--interfaces-global", hex(addr),
            ).stdout
            items = json.loads(out)
        if not items:
            source = "seed"
            out = run(
                binary, "--process", args.process, "--interfaces", "--json",
                "--interface-seed", "SchemaSystem_001", "--module", module,
            ).stdout
            items = json.loads(out)
        for it in items:
            it["module"] = module
        interfaces.extend(items)
        print(f"calibrate-live: {module}: {len(items)} interface(s) [{source}]", file=sys.stderr)
    (dump_dir / "interfaces.json").write_text(json.dumps(interfaces, indent=2))

    # --- schema system pointer ---------------------------------------------
    schema = []
    for module, va in load_heads(root / "config" / "schema-heads.json").items():
        base = bases.get(module)
        if base is None:
            continue
        addr = base + (va - IMAGE_BASE)
        ptr = run(binary, "--process", args.process, "--read-ptr", hex(addr)).stdout.strip()
        schema.append({"module": module, "global": hex(addr), "schema_system": ptr})
        print(f"calibrate-live: {module}: CSchemaSystem = {ptr}", file=sys.stderr)
    (dump_dir / "schema_system.json").write_text(json.dumps(schema, indent=2))

    # --- schema classes (uses the resolved CSchemaSystem pointer) ----------
    system = next((e["schema_system"] for e in schema if e["schema_system"] not in ("0x0", None)), None)
    if system:
        print(f"calibrate-live: dumping schema at {system}", file=sys.stderr)
        out = run(
            binary, "--process", args.process, "--schema", "--json",
            "--schema-system", system,
        ).stdout
        (dump_dir / "schema.json").write_text(out)
        try:
            print(f"calibrate-live: {len(json.loads(out))} class(es)", file=sys.stderr)
        except json.JSONDecodeError:
            pass
    else:
        print("calibrate-live: no CSchemaSystem resolved; skipping schema", file=sys.stderr)

    # --- curated global offsets --------------------------------------------
    # Re-resolve the target globals from their signatures. A signature that no
    # longer matches is reported on stderr ("unresolved: …") so a global that
    # moved across builds is visible rather than silent. engine2.dll carries the
    # Cbuf console entry used by the control channel's `exec` (docs/re/engine2.md).
    # scenesystem.dll carries the scene system's draw — the chams' hook target
    # (`scene_draw_primitive`, docs/re/chams.md §8).
    offsets = []
    for module in ("client.dll", "engine2.dll", "scenesystem.dll"):
        if module not in bases:
            continue
        proc = subprocess.run(
            [binary, "--process", args.process, "--resolve-offsets", "--json", "--module", module],
            capture_output=True, text=True, check=True,
        )
        for line in proc.stderr.splitlines():
            if line.strip():
                print("calibrate-live: " + line.strip(), file=sys.stderr)
        items = json.loads(proc.stdout)
        for it in items:
            it["module"] = module
        offsets.extend(items)
        print(f"calibrate-live: {module}: {len(items)} offset(s)", file=sys.stderr)
    (dump_dir / "offsets.json").write_text(json.dumps(offsets, indent=2))

    print(f"calibrate-live: wrote dumps to {dump_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
