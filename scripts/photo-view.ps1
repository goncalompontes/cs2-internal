param(
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
  [string[]]$Image
)
# Open images with the default association (Photos) on the SAME monitor as the
# running game. Must run in the interactive session (session 1) via a scheduled
# task — the agent shell is session 0, which has no visible desktop. Photos is a
# UWP app (hosted by ApplicationFrameHost), so the window is located by title.
$ProcessName = 'cs2'
$WidthFraction = 0.75
$HeightFraction = 0.9
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
"@

function Find-WindowByTitle([string]$needle) {
  $script:found = [IntPtr]::Zero
  $cb = [Win+EnumProc]{
    param($h, $l)
    if ([Win]::IsWindowVisible($h)) {
      $sb = New-Object System.Text.StringBuilder 512
      [Win]::GetWindowTextW($h, $sb, $sb.Capacity) | Out-Null
      $t = $sb.ToString()
      if ($t -and $t.ToLower().Contains($needle.ToLower())) { $script:found = $h; return $false }
    }
    return $true
  }
  [Win]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:found
}

$log = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\photo-view.log'
Start-Transcript -Path $log -Force | Out-Null
$code = 0
try {
  $game = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
          Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  $screen = if ($game) { [System.Windows.Forms.Screen]::FromHandle($game.MainWindowHandle) }
            else { [System.Windows.Forms.Screen]::PrimaryScreen }
  $wa = $screen.WorkingArea
  Write-Output ("game screen " + $screen.DeviceName + " workarea " + $wa.X + "," + $wa.Y + " " + $wa.Width + "x" + $wa.Height)

  $last = $null
  foreach ($img in $Image) {
    $full = (Resolve-Path -LiteralPath $img).Path
    Start-Process -FilePath $full | Out-Null
    Write-Output ("opened " + $full)
    $last = [System.IO.Path]::GetFileNameWithoutExtension($full)

    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 50 -and $hwnd -eq [IntPtr]::Zero; $i++) {
      Start-Sleep -Milliseconds 200
      $hwnd = Find-WindowByTitle $last
    }
    if ($hwnd -eq [IntPtr]::Zero) { Write-Output ("no window for " + $last); $code = 1; continue }

    $w = [int]($wa.Width * $WidthFraction)
    $h = [int]($wa.Height * $HeightFraction)
    $x = $wa.X + [int](($wa.Width - $w) / 2)
    $y = $wa.Y + [int](($wa.Height - $h) / 2)
    [Win]::ShowWindow($hwnd, 9) | Out-Null                                    # SW_RESTORE
    [Win]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, $w, $h, 0x0040) | Out-Null  # SWP_SHOWWINDOW
    [Win]::SetForegroundWindow($hwnd) | Out-Null
    Write-Output ("positioned " + $last + " at " + $x + "," + $y + " " + $w + "x" + $h + " on " + $screen.DeviceName)
  }
} finally {
  Stop-Transcript | Out-Null
}
exit $code
