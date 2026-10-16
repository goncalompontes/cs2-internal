param(
  [string]$Out = "$env:LOCALAPPDATA\cs2-internal\screen.png",
  [string]$ProcessName = "cs2"
)
# Capture the game from the *interactive* session (run via a scheduled task in
# session 1; session 0 has no visible desktop). Brings the game window to the
# foreground first, because a fullscreen game only renders while it is active.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Fg {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alt);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($proc) {
  $h = $proc.MainWindowHandle
  if ([Fg]::IsIconic($h)) { [Fg]::ShowWindow($h, 9) | Out-Null }   # SW_RESTORE
  [Fg]::ShowWindow($h, 5) | Out-Null                                # SW_SHOW
  [Fg]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, 0, 0, 0x0043) | Out-Null  # TOP|NOSIZE|NOMOVE|SHOW
  [Fg]::SwitchToThisWindow($h, $true)
  [Fg]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 1500
  Write-Output ("brought to front: " + $proc.ProcessName + " hwnd " + $h)
} else {
  Write-Output "no game window found"
}

$b = [System.Windows.Forms.SystemInformation]::VirtualScreen
$src = $b.Location
$size = $b.Size
if ($proc) {
  $r = New-Object Fg+RECT
  if ([Fg]::GetClientRect($h, [ref]$r)) {
    $tl = New-Object Fg+POINT
    $tl.X = $r.Left; $tl.Y = $r.Top
    [Fg]::ClientToScreen($h, [ref]$tl) | Out-Null
    $w = $r.Right - $r.Left
    $hh = $r.Bottom - $r.Top
    if ($w -gt 100 -and $hh -gt 100) {
      $src = New-Object System.Drawing.Point($tl.X, $tl.Y)
      $size = New-Object System.Drawing.Size($w, $hh)
    }
  }
}
$bmp = New-Object System.Drawing.Bitmap($size.Width, $size.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($src, [System.Drawing.Point]::Empty, $size)
$g.Dispose()
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("saved " + $Out)
