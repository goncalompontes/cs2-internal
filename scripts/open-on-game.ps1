param(
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
  [string[]]$Image,
  [string]$ProcessName = 'cs2',
  [double]$WidthFraction = 0.72,
  [double]$HeightFraction = 0.92
)
# Open images on the SAME monitor as the running game. Run via a scheduled task in
# the interactive session (session 1) — the agent shell is session 0, which has no
# visible desktop. Finds the game's monitor, launches Paint for each image, then
# sizes and focuses the Paint window on that monitor. (Win11 Paint is
# single-instance, so multiple images become tabs of one window; the last opened
# is the active tab.)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@

$log = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\open-image.log'
Start-Transcript -Path $log -Force | Out-Null
try {
# Which monitor is the game on? Fall back to the primary screen.
$game = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$screen = if ($game) { [System.Windows.Forms.Screen]::FromHandle($game.MainWindowHandle) }
          else { [System.Windows.Forms.Screen]::PrimaryScreen }
$wa = $screen.WorkingArea
Write-Output ("game screen " + $screen.DeviceName + " workarea " + $wa.X + "," + $wa.Y + " " + $wa.Width + "x" + $wa.Height)

Get-Process mspaint -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
foreach ($img in $Image) {
  $full = (Resolve-Path -LiteralPath $img).Path
  Start-Process -FilePath 'mspaint.exe' -ArgumentList ('"' + $full + '"') | Out-Null
  Write-Output ("opened " + $full)
  Start-Sleep -Milliseconds 900
}

# Size and focus the (single) Paint window on the game's monitor.
$paint = $null
for ($i = 0; $i -lt 50 -and $paint -eq $null; $i++) {
  $paint = Get-Process mspaint -ErrorAction SilentlyContinue |
           Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($paint -eq $null) { Start-Sleep -Milliseconds 200 }
}
if ($paint -eq $null) { Write-Output 'no Paint window'; exit 1 }
$hwnd = $paint.MainWindowHandle
$w = [int]($wa.Width * $WidthFraction)
$h = [int]($wa.Height * $HeightFraction)
$x = $wa.X + [int](($wa.Width - $w) / 2)
$y = $wa.Y + [int](($wa.Height - $h) / 2)
[Win]::ShowWindow($hwnd, 9) | Out-Null                                   # SW_RESTORE
[Win]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, $w, $h, 0x0040) | Out-Null  # SWP_SHOWWINDOW
[Win]::SetForegroundWindow($hwnd) | Out-Null
Write-Output ("paint window at " + $x + "," + $y + " " + $w + "x" + $h + " on " + $screen.DeviceName)
