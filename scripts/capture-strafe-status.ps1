param([int]$Seconds = 15, [string]$OutputDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$rows = [Collections.Generic.List[object]]::new()
$result = [ordered]@{ mode='read-only payload input status'; synthetic=$false; samples=$rows }
$pipe = [IO.Pipes.NamedPipeClientStream]::new('.', 'cs2-internal-control', [IO.Pipes.PipeDirection]::InOut)
$reader = $null
$writer = $null
try {
    $pipe.Connect(3000)
    $reader = [IO.StreamReader]::new($pipe, [Text.UTF8Encoding]::new($false), $false, 4096, $true)
    $writer = [IO.StreamWriter]::new($pipe, [Text.UTF8Encoding]::new($false), 4096, $true)
    $writer.AutoFlush = $true
    Write-Output 'Capturing read-only movement status. Jump/strafe normally for 15 seconds.'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt [Math]::Min(60, [Math]::Max(1, $Seconds))) {
        $writer.WriteLine(('{"id":' + $rows.Count + ',"cmd":"input","args":{}}'))
        $pendingLine = $reader.ReadLineAsync()
        if (!$pendingLine.Wait(3000)) { throw 'Timed out waiting for payload input status.' }
        $response = $pendingLine.Result | ConvertFrom-Json
        if (!$response.ok) { throw ($response | ConvertTo-Json -Compress) }
        $sample = [ordered]@{ elapsed_ms=$clock.ElapsedMilliseconds }
        foreach ($property in $response.result.PSObject.Properties) {
            if ($property.Name -match '^(strafe|movement|camera|bhop|autostop|command_crc)') {
                $sample[$property.Name] = $property.Value
            }
        }
        $rows.Add($sample)
        Start-Sleep -Milliseconds 100
    }
} catch {
    $result['error'] = $_.Exception.Message
    Write-Output ('Capture error: ' + $_.Exception.Message)
} finally {
    if ($writer) { try { $writer.Dispose() } catch {} }
    if ($reader) { try { $reader.Dispose() } catch {} }
    try { $pipe.Dispose() } catch {}
}
$folder = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($folder) | Out-Null
$path = Join-Path $folder 'strafe-status-result.json'
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding UTF8
Write-Output ('Saved ' + $rows.Count + ' actual status samples: ' + $path)
if ($result.Contains('error')) { exit 2 }
