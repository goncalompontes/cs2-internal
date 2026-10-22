param([switch]$Remove,[switch]$Preview)
$ErrorActionPreference='Stop'
$manifest=Join-Path $PSScriptRoot 'cs2_overlay_layer.json'
if (!(Test-Path -LiteralPath $manifest)) { $manifest=Join-Path (Split-Path -Parent $PSScriptRoot) 'build-overlay\cs2_overlay_layer.json' }
$manifest=(Resolve-Path -LiteralPath $manifest).Path
$config=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$library=$config.layer.library_path
if (![IO.Path]::IsPathRooted($library)) { $library=Join-Path (Split-Path -Parent $manifest) $library }
if (!(Test-Path -LiteralPath $library)) { throw 'Overlay DLL is missing. Extract the whole package first.' }
$launch='-insecure -novid -vulkan'
$options=Join-Path $PSScriptRoot 'Steam Launch Options.txt'
$launch | Set-Content -Encoding ASCII -LiteralPath $options
if ($Preview) { Write-Output ('Manifest: '+$manifest);Write-Output ('Steam launch options: '+$launch);exit 0 }
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=[Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'))
  if ($Remove) { $arguments+='-Remove' }
  $child=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru -Wait
  exit $child.ExitCode
}
$key='HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers'
if ($Remove) {
  if (Test-Path -LiteralPath $key) {Remove-ItemProperty -LiteralPath $key -Name $manifest -ErrorAction SilentlyContinue}
  Write-Output 'Removed this package overlay registration.'
} else {
  New-Item -Path $key -Force | Out-Null
  New-ItemProperty -LiteralPath $key -Name $manifest -PropertyType DWord -Value 0 -Force | Out-Null
  $value=(Get-ItemProperty -LiteralPath $key).PSObject.Properties[$manifest].Value
  if ($value -ne 0) {throw 'Overlay registration verification failed.'}
  Write-Output 'Overlay registered. Windows activation is automatic for cs2.exe; no environment wrapper is needed.'
  Write-Output ('Paste the line in '+$options+' into CS2 Properties > Launch Options in Steam.')
  Write-Output 'Then restart CS2 and press Insert. Inject ONCE for payload features.'
}
