#!/usr/bin/env node
// rea-mcp.mjs — Windows-friendly equivalent of scripts/rea-mcp.sh.
//
// Keeps the local Ghidra "project cache" patch applied to the *compiled*
// rea-agents dist, which `rea update` (npm) overwrites. This file is the MCP
// `command`, so on every server start it (re)applies the patch when missing and
// stays out of the way otherwise.
//
// Conflict handling: if the patch no longer applies (upstream changed those
// files) it leaves the installed files untouched, records the conflict under
// artifacts/, and still runs REA unpatched so the server keeps working. Resolve
// it by regenerating scripts/rea-project-cache.patch against the new version.
//
// stdout is the MCP stdio protocol: all diagnostics go to stderr only.
import { existsSync, readFileSync, appendFileSync, mkdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const patchFile = path.join(here, "rea-project-cache.patch");
const marker = "Persistent, content-addressed Ghidra project cache";

const reaDir =
  process.env.REA_DIR ||
  path.join(process.env.APPDATA || "", "npm", "node_modules", "rea-agents");
const target = path.join(reaDir, "dist", "ghidra", "GhidraLauncher.js");

const log = (m) => process.stderr.write(`rea-project-cache: ${m}\n`);

function have(cmd, args) {
  const r = spawnSync(cmd, args, { stdio: "ignore" });
  return r.status === 0;
}

function alreadyPatched() {
  try {
    return readFileSync(target, "utf8").includes(marker);
  } catch {
    return false;
  }
}

function applyPatch() {
  const patch = readFileSync(patchFile);

  // 1) GNU patch (mirrors the shell wrapper exactly).
  if (have("patch", ["--version"])) {
    const dry = spawnSync(
      "patch",
      ["-p1", "--forward", "--dry-run", "-d", reaDir],
      { input: patch, stdio: ["pipe", "ignore", "ignore"] },
    );
    if (dry.status === 0) {
      const res = spawnSync("patch", ["-p1", "--forward", "-d", reaDir], {
        input: patch,
        stdio: ["pipe", "ignore", "inherit"],
      });
      if (res.status === 0) {
        log(`applied to ${reaDir}`);
        return true;
      }
    }
  }

  // 2) git apply (git for Windows is normally on PATH).
  if (have("git", ["--version"])) {
    const chk = spawnSync(
      "git",
      ["-C", reaDir, "apply", "-p1", "--check", "--unsafe-paths", patchFile],
      { stdio: "ignore" },
    );
    if (chk.status === 0) {
      const res = spawnSync(
        "git",
        ["-C", reaDir, "apply", "-p1", "--unsafe-paths", patchFile],
        { stdio: ["ignore", "ignore", "inherit"] },
      );
      if (res.status === 0) {
        log(`applied to ${reaDir} (git apply)`);
        return true;
      }
    }
  }

  log(`patch does not apply to ${reaDir} (rea-agents changed?).`);
  log(`running unpatched; regenerate ${patchFile} to resolve.`);
  try {
    const adir = path.join(here, "..", "artifacts");
    mkdirSync(adir, { recursive: true });
    appendFileSync(
      path.join(adir, "rea-project-cache-conflict.log"),
      `conflict at ${new Date().toISOString()}\n`,
    );
  } catch {
    /* best effort */
  }
  return false;
}

if (!existsSync(target)) {
  log(`${target} not found; running unpatched`);
} else if (!existsSync(patchFile)) {
  log(`${patchFile} missing; running unpatched`);
} else if (alreadyPatched()) {
  /* already applied */
} else {
  applyPatch();
}

// REA_MCP_PATCH_ONLY=1 applies the patch and exits (handy for setup/testing)
// instead of starting the server.
if (process.env.REA_MCP_PATCH_ONLY) {
  const ok = alreadyPatched();
  log(ok ? "patch present" : "patch NOT present");
  process.exit(ok ? 0 : 1);
}

// Run the REA MCP server, passing stdio straight through.
const cmd = process.env.ComSpec || "cmd.exe";
const r = spawnSync(cmd, ["/c", "rea", "mcp"], { stdio: "inherit" });
process.exit(r.status === null ? 1 : r.status);
