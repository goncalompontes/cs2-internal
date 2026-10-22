param([int]$TargetPid = 0, [string]$ProbeDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$probeFolder = [IO.Path]::GetFullPath($ProbeDirectory)
$dll = Join-Path $probeFolder 'move_clock_live.dll'
$injector = Join-Path $probeFolder 'injector.exe'
$statusPath = Join-Path $probeFolder 'move-clock-live-status.json'
$resultPath = Join-Path $probeFolder 'move-clock-live-result.json'
$nonce = [guid]::NewGuid().ToString()
if (!(Test-Path -LiteralPath $dll) -or !(Test-Path -LiteralPath $injector)) {
    throw 'Place the diagnostic DLL and injector beside this launcher.'
}
if ($TargetPid -eq 0) {
    $gameProcesses = @(Get-Process -Name cs2 -ErrorAction SilentlyContinue)
    if ($gameProcesses.Count -ne 1) { throw 'Start one CS2 instance in a local match, then run this launcher.' }
    $TargetPid = $gameProcesses[0].Id
}
@{nonce=$nonce; extra=1} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $probeFolder 'move-clock-live-config.json') -Encoding ascii
Write-Output "Running a one-shot live command-clock probe in CS2 pid $TargetPid."
Write-Output 'Equip a loaded rifle/sniper. Make a manual jump and land when ARMED appears.'
$ErrorActionPreference = 'Continue'
& $injector "$TargetPid" $dll --load-library 2>&1 | Tee-Object -FilePath (Join-Path $probeFolder 'live-loader.log')
$loaderResult = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($loaderResult -ne 0) {
    Write-Output 'Loading failed. If Windows reports access denied, run from the same Windows privilege context as CS2.'
    Write-Output 'The diagnostic hook was not loaded by this attempt.'
    exit $loaderResult
}
$deadline = [DateTime]::UtcNow.AddSeconds(120)
$lastState = ''
$finished = $false
while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-Path -LiteralPath $statusPath) {
        try { $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json } catch { $status = $null }
        if ($null -ne $status -and $status.nonce -eq $nonce) {
            if ($status.state -ne $lastState) {
                Write-Output ("{0}: {1} (sample_error={2})" -f $status.state.ToUpperInvariant(), $status.detail, $status.sample_error)
                $lastState = $status.state
            }
            if ($status.state -in @('completed','incomplete','error')) { $finished=$true; break }
        }
    }
    Start-Sleep -Milliseconds 100
}
if (!$finished) {
    throw 'No matching completed capture. If this DLL was already loaded, restart CS2 for a fresh run; it stays resident after disabling its hook.'
}
if ($lastState -ne 'completed') {
    Write-Output 'No completed live adjustment. Result/status files contain the reason; no accuracy benefit is established.'
    exit 2
}
$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
if ($result.nonce -ne $nonce -or $result.synthetic -ne $false) { throw 'Result identity/mode mismatch.' }
$experiment = $result.experiment
$before = $experiment.before
$after = $experiment.after
Write-Output ''
Write-Output ("LIVE extra={0} commands={1} tickbase={2} processed={3} server_executed={4}" -f $experiment.extra, ($after.command-$before.command), ($after.tickbase-$before.tickbase), ($after.processed-$before.processed), ($after.server_executed-$before.server_executed))
Write-Output ("Penalty {0:F6} -> {1:F6}; total inaccuracy {2:F6} -> {3:F6}; airborne {4:F6} -> {5:F6}" -f $before.penalty,$after.penalty,$before.inaccuracy,$after.inaccuracy,$before.air_component,$after.air_component)
Write-Output ("Hook disabled: {0}" -f $result.hook_disabled)
Write-Output ("Saved actual baseline, adjustment and follow-up snapshots: {0}" -f $resultPath)
Write-Output 'These are measured counters; a generated command alone does not establish faster server-accepted recovery.'
exit 0
