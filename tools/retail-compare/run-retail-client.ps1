param([string]$Map = 'zoo', [int]$Secs = 45, [string]$Extra = '', [string]$After = '', [string]$Tag = '')
# Runs the retail game (client and server in one process) from the sandbox with a real
# local player, and saves its console log. Used as the reference when OpenFAKK2 runs with
# sv_testplayer. A small game window is shown while it runs; sound and mouse capture are
# turned off. The retail engine checks for the game disc; it must be in a drive.
. (Join-Path $PSScriptRoot 'common.ps1')

$dir = Join-Path $Sandbox 'retail'
if (-not (Test-Path "$dir\fakk2.exe")) {
    & (Join-Path $PSScriptRoot 'run-retail.ps1') -Name retail -Map $Map -Secs 5 | Out-Null
}
Copy-Item "$GameInstall\fakk\default.cfg" "$dir\fakk\default.cfg" -Force
$log = "$dir\fakk\qconsole.log"
Remove-Item $log -ErrorAction SilentlyContinue

$arguments = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 $Extra +map $Map $After"
$p = Start-Process -FilePath "$dir\fakk2.exe" -WorkingDirectory $dir -ArgumentList $arguments -PassThru -WindowStyle Minimized
$deadline = (Get-Date).AddSeconds($Secs)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Milliseconds 500 }
if ($p.HasExited) { $state = "exited with code $($p.ExitCode)" } else { $p.Kill(); $p.WaitForExit(); $state = "stopped after ${Secs}s" }

$out = Join-Path $LogDir "client-retail-$Map$Tag.log"
if (Test-Path $log) { Copy-Item $log $out -Force; "[$Map] $state, $((Get-Content $out).Count) lines -> $out" }
else { "[$Map] $state, but the game wrote no qconsole.log" }
