param(
    [Parameter(Mandatory = $true)][string]$Probe,
    [string]$Map = 'zoo',
    [string]$Bin = '',
    [string]$Name = '',
    [int]$Secs = 90,
    [string[]]$Classname = @('Projectile'),
    [int[]]$Entity = @(0),
    [string[]]$Fields = @('Animname', 'Origin', 'Velocity', 'Movetype', 'Solidtype', 'Health', 'Edict Owner'),
    [string]$Extra = '',
    [switch]$Retail
)
# Runs a probe script through a real game client (not the headless test player), so the
# probe can use client keys such as +lookdown that the test player lacks. By default that
# is the OpenFAKK2 client; with -Retail it is the retail fakk2.exe, run from the sandbox
# copy in build\sandbox\retail (made on first use), so the same probe gives the retail
# reference. The client joins a local game on $Map and the probe is started with
# "+script thread" before the player spawns, so its waitforplayer returns on both engines.
# The mouse is off (in_mouse 0): with it on, input reaching the window turned the player
# mid-probe. The engine archives in_mouse, so the run takes it out of the home's
# config.cfg afterwards (Clear-HarnessMouse), or the next game played there has no mouse. The run ends when the probe prints "pending events as of" (so a probe must end
# with pendingevents), when the client exits, or after $Secs seconds. The console log is
# copied to build\compare\client-<Name>-<Map>-<Probe>.log and the selected readings are
# printed.
#
#   -Probe      probe name in tools\retail-compare (without .scr) or a path to a .scr file
#   -Retail     run the retail game instead of OpenFAKK2 (the game disc must be in a drive)
#   -Bin        OpenFAKK2 engine directory; default this repo's build\x86-Release\Release.
#               Point it at another checkout's build to compare versions.
#   -Name       label in the log name: "before", "fixed", ...; default "ours", or "retail"
#   -Classname  whatis blocks to print, by classname (default Projectile)
#   -Entity     whatis blocks to print, by entity number (default 0, the player)
#   -Extra      more engine command line, before +map. On retail, an archived cvar set here
#               is saved in the sandbox's config.cfg and carries into later runs.

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
. (Join-Path $RepoRoot 'tools\retail-compare\common.ps1')   # $GameInstall, $Sandbox, $LogDir

$probeFile = if (Test-Path $Probe) { (Resolve-Path $Probe).Path } else { Join-Path $RepoRoot "tools\retail-compare\$Probe.scr" }
if (-not (Test-Path $probeFile)) { throw "no such probe: $probeFile" }
$probeName = [IO.Path]::GetFileNameWithoutExtension($probeFile)
if (-not $Name) { $Name = if ($Retail) { 'retail' } else { 'ours' } }

if ($Retail) {
    $gameDir = Join-Path $Sandbox 'retail'
    if (-not (Test-Path "$gameDir\fakk2.exe")) {
        # first use: copies fakk2.exe and the game modules, hard-links the paks
        & (Join-Path $RepoRoot 'tools\retail-compare\run-retail.ps1') -Name retail -Map $Map -Secs 5 | Out-Null
    }
    Copy-Item "$GameInstall\fakk\default.cfg" "$gameDir\fakk\default.cfg" -Force
    $exe = Join-Path $gameDir 'fakk2.exe'
    $workDir = $gameDir
    $maps = Get-ProbeDir $gameDir $Map
    $console = Join-Path $gameDir 'fakk\qconsole.log'
    $arguments = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 " +
        "$Extra +map $Map +script thread $probeName.scr::probe"
}
else {
    if (-not $Bin) { $Bin = Join-Path $RepoRoot 'build\x86-Release\Release' }
    $exe = Join-Path $Bin 'openfakk2.exe'
    if (-not (Test-Path $exe)) { throw "no client at $exe (build it, or pass -Bin)" }
    $homeDir = Join-Path $Sandbox 'home'
    $workDir = $Bin
    $maps = Get-ProbeDir $homeDir $Map
    $console = Join-Path $homeDir 'fakk\qconsole.log'
    $arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$homeDir`" +set com_abnormalExitDialog 0 " +
        "+set r_fullscreen 0 +set r_mode 4 +set in_nograb 1 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 " +
        "$Extra +map $Map +script thread $probeName.scr::probe"
}

New-Item -ItemType Directory -Force $maps | Out-Null
Copy-Item $probeFile (Join-Path $maps "$probeName.scr") -Force
Remove-Item $console -ErrorAction SilentlyContinue
$log = Join-Path $LogDir "client-$Name-$(Get-MapTag $Map)-$probeName.log"

$p = Start-Process -FilePath $exe -WorkingDirectory $workDir -ArgumentList $arguments -PassThru

$marker = 'pending events as of'
$deadline = (Get-Date).AddSeconds($Secs)
$done = $false
while ((Get-Date) -lt $deadline -and -not $p.HasExited -and -not $done) {
    Start-Sleep -Seconds 1
    if (Test-Path $console) {
        $fs = [IO.File]::Open($console, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs)
        $text = $sr.ReadToEnd(); $sr.Dispose()
        if ($text -match $marker) { $done = $true; Start-Sleep -Seconds 1 }
    }
}
$exited = $p.HasExited
if (-not $exited) { $p.Kill(); $p.WaitForExit() }
if (-not $Retail) { Clear-HarnessMouse }

if (Test-Path $console) { Copy-Item $console $log -Force } else { "no console log was written (did the client start?)"; return }
$status = if ($done) { 'probe finished' } elseif ($exited) { "client exited early (code $($p.ExitCode))" } else { "probe did not finish within ${Secs}s" }
"[$Name/$Map/$probeName] $status -> $log"

& (Join-Path $PSScriptRoot 'readings.ps1') -Log $log -Classname $Classname -Entity $Entity -Fields $Fields
