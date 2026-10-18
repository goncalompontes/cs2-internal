param(
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
  [string[]]$Path
)
# Open images with the shell's default viewer. This must run in the *interactive*
# session (session 1) — the agent/SSH shell is in session 0, which has no visible
# desktop, so a viewer started there is never seen. Launch it via a scheduled task
# (`schtasks /create ... /it`, or Register-ScheduledTask with an Interactive
# principal), exactly like `scripts/capture-screen.ps1`.
foreach ($p in $Path) {
  $full = (Resolve-Path -LiteralPath $p).Path
  $proc = Start-Process -FilePath 'mspaint.exe' -ArgumentList ('"' + $full + '"') -PassThru
  Write-Output ("opened " + $full + " (pid " + $proc.Id + ")")
}
