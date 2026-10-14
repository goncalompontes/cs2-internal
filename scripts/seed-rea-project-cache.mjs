#!/usr/bin/env node
// Seed the REA Ghidra project cache from an operator-built project, so the first
// cache-mode open of a target is a hit instead of a fresh import.
//
// This is the stopgap for the not-yet-upstream `rea cache import`: it computes
// the same identity REA resolves (target + provider + JVM + platform + import
// profile) and publishes the existing project under that key.
//
// Requires REA built locally (`npm run compile` in the REA checkout).
//
//   node scripts/seed-rea-project-cache.mjs \
//     --target  C:\Users\gpmpo\ghidra-analysis\client.dll \
//     --project C:\Users\gpmpo\ghidra-analysis\client-warm \
//     --program client.dll
//
// Env: REA_SRC (default C:\Users\gpmpo\rea-src), GHIDRA_INSTALL_DIR, JAVA_HOME,
// REA_GHIDRA_PROJECT_CACHE_DIR (default <home>/.rea/cache/ghidra-projects).

import { homedir } from "node:os";
import { isAbsolute, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const args = process.argv.slice(2);
const option = (name) => {
  const index = args.indexOf(`--${name}`);
  return index === -1 ? undefined : args[index + 1];
};

const targetPath = option("target");
const projectPath = option("project");
const programName = option("program");
if (targetPath === undefined || projectPath === undefined || programName === undefined) {
  console.error(
    "usage: seed-rea-project-cache.mjs --target <dll> --project <projectDir> --program <programName>",
  );
  process.exit(2);
}

const reaSrc = process.env.REA_SRC ?? "C:\\Users\\gpmpo\\rea-src";
const distUrl = (relative) =>
  pathToFileURL(resolve(reaSrc, "dist", relative)).href;
const cacheRoot =
  process.env.REA_GHIDRA_PROJECT_CACHE_DIR ??
  join(homedir(), ".rea", "cache", "ghidra-projects");
const installDir = process.env.GHIDRA_INSTALL_DIR;
if (installDir === undefined || !isAbsolute(installDir)) {
  console.error("Set GHIDRA_INSTALL_DIR to the absolute Ghidra root.");
  process.exit(2);
}

const { parseBinaryTarget } = await import(
  distUrl("application/BinaryTargetResolver.js")
);
const { resolveGhidraAnalysisProfile } = await import(
  distUrl("ghidra/GhidraAnalysisProfile.js")
);
const { inspectGhidraInstallation } = await import(
  distUrl("ghidra/GhidraInstallation.js")
);
const { GHIDRA_PROVIDER_IDENTITY } = await import(
  distUrl("ghidra/GhidraProviderCapabilities.js")
);
const { GhidraProjectStore } = await import(
  distUrl("ghidra/projectCache/GhidraProjectStore.js")
);

const installation = inspectGhidraInstallation({
  environment: process.env,
  installDir,
  ...(process.env.JAVA_HOME === undefined
    ? {}
    : { javaHome: process.env.JAVA_HOME }),
});
if (installation.status !== "available") {
  console.error(`Ghidra installation unavailable: ${JSON.stringify(installation.checks)}`);
  process.exit(1);
}

const target = await parseBinaryTarget(resolve(targetPath));
if (!target.ok) {
  console.error(`Target admission failed: ${JSON.stringify(target.error)}`);
  process.exit(1);
}

const profile = await resolveGhidraAnalysisProfile(
  target.value,
  GHIDRA_PROVIDER_IDENTITY,
  installation,
);
if (!profile.ok || profile.value.profile === null) {
  console.error("Could not resolve an analysis profile for the target.");
  process.exit(1);
}

const identity = {
  providerId: GHIDRA_PROVIDER_IDENTITY.id,
  providerVersion: installation.providerVersion,
  jvmVmName: installation.javaVmName ?? "unknown",
  platform: installation.platform,
  targetSha256: target.value.sha256,
  importProfileDigest: profile.value.profile.digest,
};

const store = new GhidraProjectStore({ root: cacheRoot, maxBytes: 10 * 1024 ** 3 });
const reservation = await store.prepare({ identity, mode: "on" });
if (!reservation.ok || reservation.value === null) {
  console.error(`Cache prepare failed: ${JSON.stringify(reservation)}`);
  process.exit(1);
}
if (reservation.value.hit) {
  console.log(`Already cached: ${reservation.value.entryPath}`);
  process.exit(0);
}

const published = await store.publish({
  reservation: reservation.value,
  identity,
  target: {
    sha256: target.value.sha256,
    kind: target.value.kind,
    format: target.value.format,
    architecture: target.value.architecture ?? null,
    executable_role: target.value.executableRole ?? null,
  },
  sourceProjectDirectory: resolve(projectPath),
  programName,
  programSha256: target.value.sha256,
  producer: { reaVersion: "local-seed", runId: "seed" },
});
if (!published.ok || !published.value.published) {
  console.error(`Publish failed: ${JSON.stringify(published)}`);
  process.exit(1);
}
console.log(`Seeded ${programName} -> ${reservation.value.entryPath}`);
