# preflight-windows.ps1 — report what a Windows dev machine still needs to build
# and run this repo.
#
# Usage (from the repo root):
#   powershell -ExecutionPolicy Bypass -File scripts\preflight-windows.ps1
#
# Exits 0 when every REQUIRED item is present, 1 otherwise. WARN items are
# optional (needed only for the live game / RE workflows) and never fail the run.
#
# This is read-only: it inspects tools, paths and a registry key, and prints a
# PASS/WARN/FAIL line per item so an agent can act on the failures.

[CmdletBinding()]
param(
  [string]$RepoRoot = ''
)

if (-not $RepoRoot) {
  # Resolve at run time: $PSScriptRoot is not reliably populated while the
  # param block is bound under `powershell -File`.
  $scriptPath = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Path }
  $RepoRoot = Split-Path -Parent (Split-Path -Parent $scriptPath)
}

$ErrorActionPreference = 'SilentlyContinue'

$script:Fail = 0
$script:Warn = 0

function Result([string]$Status, [string]$Name, [string]$Detail) {
  switch ($Status) {
    'PASS' { $colour = 'Green' }
    'WARN' { $colour = 'Yellow'; $script:Warn++ }
    'FAIL' { $colour = 'Red';    $script:Fail++ }
    default { $colour = 'Gray' }
  }
  Write-Host ("[{0}] {1,-24} {2}" -f $Status, $Name, $Detail) -ForegroundColor $colour
}

function Tool([string]$Name, [string]$Exe, [string]$VersionArg, [bool]$Required) {
  $cmd = Get-Command $Exe -ErrorAction SilentlyContinue
  if (-not $cmd) {
    Result ($(if ($Required) { 'FAIL' } else { 'WARN' })) $Name "not found on PATH ($Exe)"
    return
  }
  $version = (& $Exe $VersionArg 2>&1 | Select-Object -First 1)
  Result 'PASS' $Name ("$($cmd.Source) :: $version")
}

Write-Host "== cs2-internal preflight ==" -ForegroundColor Cyan
Write-Host "repo: $RepoRoot"
Write-Host ""

Write-Host "-- core build tools (required) --" -ForegroundColor Cyan
Tool 'git'     'git'     '--version'   $true
Tool 'meson'   'meson'   '--version'   $true
Tool 'ninja'   'ninja'   '--version'   $true
Tool 'python'  'python'  '--version'   $true
Tool 'cargo'   'cargo'   '--version'   $true
Tool 'node'    'node'    '--version'   $true
Tool 'clang-cl' 'clang-cl' '--version' $true

# MSVC: clang-cl needs the MSVC + Windows SDK headers/libs from a vcvars env.
$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($vcvars) {
  Result 'PASS' 'MSVC/vcvars64' $vcvars.FullName
} else {
  Result 'FAIL' 'MSVC/vcvars64' 'no vcvars64.bat under C:\Program Files\Microsoft Visual Studio (install the VS C++ workload)'
}

Write-Host ""
Write-Host "-- repo inputs --" -ForegroundColor Cyan
if (Test-Path (Join-Path $RepoRoot 'external\Vulkan-Headers\include\vulkan\vulkan.h')) {
  Result 'PASS' 'Vulkan-Headers' 'external\Vulkan-Headers present'
} else {
  Result 'FAIL' 'Vulkan-Headers' 'missing; clone KhronosGroup/Vulkan-Headers into external\Vulkan-Headers'
}

foreach ($b in 'build', 'build-payload', 'build-injector', 'build-overlay') {
  if (Test-Path (Join-Path $RepoRoot $b)) {
    Result 'PASS' $b 'configured'
  } else {
    Result 'WARN' $b 'not configured yet (meson setup ...)'
  }
}

Write-Host ""
Write-Host "-- live game workflow (warn only) --" -ForegroundColor Cyan

# Steam: default install or the STEAM env override used by launch-cs2.cmd.
$steam = if ($env:STEAM) { $env:STEAM } else { 'C:\Program Files (x86)\Steam\steam.exe' }
if (Test-Path $steam) {
  Result 'PASS' 'Steam' $steam
} else {
  Result 'WARN' 'Steam' "not at $steam (set STEAM before running launch-cs2.cmd)"
}

# CS2: walk Steam library folders for the 730 install.
$cs2 = $null
$libraryFile = Join-Path (Split-Path -Parent $steam) 'steamapps\libraryfolders.vdf'
if (Test-Path $libraryFile) {
  $libs = Select-String -Path $libraryFile -Pattern '"path"\s+"(.+?)"' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' }
  foreach ($lib in $libs) {
    $candidate = Join-Path $lib 'steamapps\common\Counter-Strike Global Offensive'
    if (Test-Path (Join-Path $candidate 'game\bin\win64\engine2.dll')) { $cs2 = $candidate; break }
  }
}
if ($cs2) {
  Result 'PASS' 'CS2 install' $cs2
  foreach ($dll in 'game\bin\win64\engine2.dll', 'game\csgo\bin\win64\client.dll') {
    if (Test-Path (Join-Path $cs2 $dll)) { Result 'PASS' (Split-Path -Leaf $dll) (Join-Path $cs2 $dll) }
    else { Result 'WARN' (Split-Path -Leaf $dll) "missing under $cs2" }
  }
} else {
  Result 'WARN' 'CS2 install' 'not found in any Steam library (needed for live tests/calibration)'
}

# The interactive-session launch task (AGENTS.md).
if (Get-ScheduledTask -TaskName 'CS2Launch' -ErrorAction SilentlyContinue) {
  Result 'PASS' 'CS2Launch task' 'registered'
} else {
  Result 'WARN' 'CS2Launch task' 'missing; create it so the game launches in the interactive session'
}

# Vulkan implicit layer (machine-wide, HKLM) for the overlay on native Windows.
$layer = Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers' -ErrorAction SilentlyContinue
$registered = $false
if ($layer) {
  $registered = ($layer.PSObject.Properties | Where-Object { $_.Name -like '*cs2_overlay_layer.json' }).Count -gt 0
}
if ($registered) { Result 'PASS' 'Vulkan layer' 'cs2_overlay_layer.json registered under HKLM' }
else { Result 'WARN' 'Vulkan layer' 'not registered (needed for the overlay; see docs/setup-windows.md)' }

# Payload import hygiene (docs/handoff.md gotchas): a non-POD payload global or a
# function-local `static` drags in a CRT import the manual-map injector cannot
# resolve (e.g. KERNEL32!SleepConditionVariableSRW), and injection fails with
# `unresolved import`. Import names sit in the DLL as ASCII, so scan the bytes.
$payloadDll = Join-Path $RepoRoot 'build-payload\payload.dll'
if (Test-Path $payloadDll) {
  $payloadText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($payloadDll))
  $badImports = @('SleepConditionVariableSRW', 'SleepConditionVariableCS', 'InitOnceExecuteOnce') |
    Where-Object { $payloadText.Contains($_) }
  if ($badImports.Count -gt 0) {
    Result 'WARN' 'payload imports' ("unresolvable CRT import(s): {0} (manual-map injection will fail; keep globals POD)" -f ($badImports -join ', '))
  } else {
    Result 'PASS' 'payload imports' 'no known-unresolvable CRT imports'
  }
} else {
  Result 'WARN' 'payload.dll' 'not built (meson compile -C build-payload)'
}

Write-Host ""
Write-Host "-- static RE (warn only) --" -ForegroundColor Cyan
Tool 'rea (REA)' 'rea' '--version' $false
Tool 'java' 'java' '-version' $false
Tool 'gh' 'gh' '--version' $false
if ($env:GHIDRA_INSTALL_DIR) {
  if (Test-Path $env:GHIDRA_INSTALL_DIR) { Result 'PASS' 'GHIDRA_INSTALL_DIR' $env:GHIDRA_INSTALL_DIR }
  else { Result 'WARN' 'GHIDRA_INSTALL_DIR' "$($env:GHIDRA_INSTALL_DIR) does not exist" }
} else {
  Result 'WARN' 'GHIDRA_INSTALL_DIR' 'not set in this shell (REA reads it from its MCP entry; set it for CLI use)'
}

Write-Host ""
if ($script:Fail -eq 0) {
  Write-Host "OK: no required items missing ($script:Warn warning(s))." -ForegroundColor Green
  exit 0
}
Write-Host "MISSING: $script:Fail required item(s), $script:Warn warning(s)." -ForegroundColor Red
exit 1
