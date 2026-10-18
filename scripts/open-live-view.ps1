param(
  [string]$Url = 'http://127.0.0.1:8765/',
  [string]$ProcessName = 'cs2',
  [double]$WidthFraction = 0.42,
  [double]$HeightFraction = 0.94,
  [ValidateSet('right', 'left', 'center')][string]$Dock = 'right',
  [string]$Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
)
# Show the live heatmap viewer on the SAME monitor as the game. Must run in the
# *interactive* session (session 1) via a scheduled task with an Interactive
# principal - the agent shell is session 0, which has no visible desktop.
# Opens Chrome in app mode (no browser chrome) with a dedicated profile, then sizes
# and focuses the window on the game's monitor, docked to one side.
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinLV {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@

$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root 'artifacts\open-live-view.log'
Start-Transcript -Path $log -Force | Out-Null
try {
  $game = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
          Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  $screen = if ($game) { [System.Windows.Forms.Screen]::FromHandle($game.MainWindowHandle) }
            else { [System.Windows.Forms.Screen]::PrimaryScreen }
  $wa = $screen.WorkingArea
  Write-Output ("game screen " + $screen.DeviceName + " workarea " + $wa.X + "," + $wa.Y + " " + $wa.Width + "x" + $wa.Height)

  if (-not (Test-Path $Browser)) { Write-Output ("browser not found: " + $Browser); exit 1 }
  $profile = Join-Path $env:LOCALAPPDATA 'cs2-internal\liveview-profile'
  $args = @(
    '--app=' + $Url,
    '--user-data-dir=' + $profile,
    '--no-first-run', '--no-default-browser-check', '--disable-features=Translate,MediaRouter',
    ('--window-size=' + [int]($wa.Width * $WidthFraction) + ',' + [int]($wa.Height * $HeightFraction)),
    '--window-position=' + $wa.X + ',' + $wa.Y
  )
  Start-Process -FilePath $Browser -ArgumentList $args | Out-Null
  Write-Output ("launched " + $Browser + " " + ($args -join ' '))

  # Find the app window by its page title (an existing app window is reused when the
  # profile is already open), falling back to the newest chrome window.
  $target = $null
  for ($i = 0; $i -lt 60 -and $null -eq $target; $i++) {
    Start-Sleep -Milliseconds 250
    $windows = Get-Process chrome -ErrorAction SilentlyContinue |
               Where-Object { $_.MainWindowHandle -ne 0 }
    $target = $windows | Where-Object { $_.MainWindowTitle -like '*heatmap*' } |
              Select-Object -First 1
    if ($null -eq $target) {
      $target = $windows | Sort-Object StartTime -Descending | Select-Object -First 1
    }
  }
  if ($null -eq $target) { Write-Output 'no chrome window found'; exit 1 }
  Write-Output ("target window: '" + $target.MainWindowTitle + "'")

  $w = [int]($wa.Width * $WidthFraction)
  $h = [int]($wa.Height * $HeightFraction)
  switch ($Dock) {
    'right'  { $x = $wa.X + $wa.Width - $w }
    'left'   { $x = $wa.X }
    'center' { $x = $wa.X + [int](($wa.Width - $w) / 2) }
  }
  $y = $wa.Y + [int](($wa.Height - $h) / 2)
  $hwnd = $target.MainWindowHandle
  [WinLV]::ShowWindow($hwnd, 9) | Out-Null                                       # SW_RESTORE
  [WinLV]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, $w, $h, 0x0040) | Out-Null # SWP_SHOWWINDOW
  [WinLV]::SetForegroundWindow($hwnd) | Out-Null
  Write-Output ("viewer window at " + $x + "," + $y + " " + $w + "x" + $h + " on " + $screen.DeviceName)
}
finally { Stop-Transcript | Out-Null }
