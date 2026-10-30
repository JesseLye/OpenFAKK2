param([string]$Map = 'zoo', [string]$Probe = 'probe-save', [string]$Thread = 'probe', [int]$Secs = 120, [int]$Show = 12, [double]$Tolerance = 0.01, [string]$Extra = '', [string]$Bin = '')
# Checks that a saved game comes back as it was saved. Two runs of openfakk2ded with the
# headless test player:
#   1. plays $Map with $Probe's $Thread (probe-save.scr::probe by default, which saves
#      the game ("probesave") three seconds after the player enters, waits two seconds and
#      then reads every entity (whatis 0 to 299), levelvars and pendingevents);
#   2. starts with "+loadgame probesave". The game archives its script threads, so the
#      probe comes back in the middle of the wait after its save and takes the same
#      readings again, at the same game time.
# The readings of the two runs are compared field by field (numbers within -Tolerance),
# one by one in order, so a thread given here must read nothing before its save (the x*
# probes' "save" threads: tools\retail-compare\README.md, "Example maps").
# Logs: build\compare\logs\save-load-<map>-<probe>-saved.log and -loaded.log.
# The level clock, the area portals and every entity the game archives must survive the
# round trip; random choices made after the save may differ, as they do between runs.
# The save is written to build\sandbox\test-home\fakk\save\config_server_probesave.* (saves are named
# after the config file, which is config_server.cfg for openfakk2ded).
. (Join-Path $PSScriptRoot 'common.ps1')

$probe = $Probe
$probeFile = Join-Path $PSScriptRoot "$probe.scr"
$marker = 'pending events as of'
$homeDir = $TestHome
if (-not $Bin) { $Bin = $EngineBin }
$homeMaps = Get-ProbeDir $homeDir $Map
New-Item -ItemType Directory -Force $homeMaps | Out-Null
Copy-Item $probeFile "$homeMaps\$probe.scr" -Force
Remove-Item (Join-Path $homeDir 'fakk\save\config_server_probesave.*') -ErrorAction SilentlyContinue

function Run-Ours($name, $run) {
    $log = Join-Path $LogDir "save-load-$(Get-MapTag $Map)-$probe-$name.log"
    Remove-Item "$log.stdout", "$log.stderr" -ErrorAction SilentlyContinue
    $arguments = "$OursHomeArgs +set developer 1 +set sv_testplayer 1 +set sv_randomSeed 1 $Extra $run"
    $p = $null; $done = $false
    try {
        $p = Start-HarnessGame -Side ours -Exe "$Bin\openfakk2ded.exe" -Arguments $arguments -NoNewWindow -Stdout "$log.stdout" -Stderr "$log.stderr" -What "save-load $Map $name"
        $deadline = (Get-Date).AddSeconds($Secs)
        while (-not $done -and (Get-Date) -lt $deadline -and -not $p.HasExited) {
            Start-Sleep -Seconds 1
            foreach ($file in "$log.stdout", "$log.stderr") {
                if (-not (Test-Path $file)) { continue }
                $fs = [IO.File]::Open($file, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $text = $sr.ReadToEnd(); $sr.Dispose()
                if ($text.Contains($marker)) { Start-Sleep -Seconds 1; $done = $true }
            }
        }
    }
    finally {
        Stop-HarnessGame $p
        if (Test-Path "$log.stdout") { Get-Content "$log.stdout", "$log.stderr" -ErrorAction SilentlyContinue | Set-Content $log }
    }
    if (-not $done) { Write-Host "[$Map] $name`: the probe did not finish within ${Secs}s" }
    $log
}

. (Join-Path $PSScriptRoot 'compare-lib.ps1')
function Vars($file) { Get-Dump $file | Where-Object { $_ -match '^\S.* = ' } }

$saved = Run-Ours 'saved' "+map $Map +script thread $probe.scr::$Thread"
$saveFile = Join-Path $homeDir 'fakk\save\config_server_probesave.sav'
if (-not (Test-Path $saveFile)) { "[$Map] no save was written ($saveFile); see $saved"; return }
$loaded = Run-Ours 'loaded' '+loadgame probesave'

$time = { param($f) (Select-String -Path $f -Pattern "(\d+) $marker ([\d.]+)" | Select-Object -Last 1).Matches.Groups[2].Value }
"[$Map] pending events read at $(& $time $saved) saved / $(& $time $loaded) loaded"
$varDiff = @(Compare-Object @(Vars $saved) @(Vars $loaded))
"  level variables differing: $($varDiff.Count)"
$varDiff | Select-Object -First $Show | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', 'saved: ' -replace '=>', 'loaded:'), $_.InputObject }

$r = Compare-Readings (Get-Readings $saved) (Get-Readings $loaded) $Tolerance $Show -Labels saved, loaded
Format-ReadingsLine $r -Labels saved, loaded
$r.Examples
