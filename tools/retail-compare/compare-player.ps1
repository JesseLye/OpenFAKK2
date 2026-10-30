param([string]$Map = 'training', [string]$Probe = 'probe', [int]$Secs = 150, [int]$Show = 12, [double]$Tolerance = 0.01, [string]$Extra = '', [switch]$Refresh, [switch]$RetailTwice, [switch]$NoRetail, [switch]$RetailOnly, [string]$Bin = '')
# Compares game state with a player present: the retail game (real client, nobody at the
# controls) against OpenFAKK2 with its headless test player. Both run the same probe
# script (tools\retail-compare\<Probe>.scr), which waits for the player and then prints
# state at fixed game times: "whatis <n>" readings, levelvars and pendingevents. A probe
# must end with pendingevents; that is how the scripts know it has finished.
# The retail game shows a small window while it runs; its result is cached per map and
# probe in build\compare (use -Refresh to run it again).
# -Extra passes more command line to OpenFAKK2 only (for example "+set sv_testplayerdelay 400").
# -RetailTwice runs the retail game a second time in place of OpenFAKK2, to show how much
# retail differs from itself (the game seeds its random numbers from the clock). In that
# output "ours" is the second retail run.
# -NoRetail never starts the retail game: with no cached retail log it reports and stops
# (the test suite's routine runs). -RetailOnly runs (with -Refresh) or keeps the retail
# log and stops. -Bin runs another build's openfakk2ded.exe.
. (Join-Path $PSScriptRoot 'common.ps1')

$probeFile = Join-Path $PSScriptRoot "$Probe.scr"
if (-not (Test-Path $probeFile)) { "no such probe: $probeFile"; return }
$after = "+script thread $Probe.scr::probe"
$marker = 'pending events as of'

function Wait-ForMarker($process, $files, $seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Seconds 1
        foreach ($file in $files) {
            if (-not (Test-Path $file)) { continue }
            $fs = [IO.File]::Open($file, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $text = $sr.ReadToEnd(); $sr.Dispose()
            if ($text -match $marker) { Start-Sleep -Seconds 1; return $true }
        }
    }
    return $false
}

# --- retail game --------------------------------------------------------------------
$retailLog = Join-Path $LogDir "player-retail-$(Get-MapTag $Map)-$Probe.log"
function Run-Retail($log) {
    $dir = Join-Path $Sandbox 'retail'
    if (-not (Test-Path "$dir\fakk2.exe")) { & (Join-Path $PSScriptRoot 'run-retail.ps1') -Name retail -Map $Map -Secs 5 | Out-Null }
    $probeDir = Get-ProbeDir $dir $Map
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    Copy-Item $probeFile "$probeDir\$Probe.scr" -Force
    $console = "$dir\fakk\qconsole.log"
    Remove-Item $console -ErrorAction SilentlyContinue
    $arguments = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 +map $Map $after"
    $p = $null; $done = $false
    try {
        $p = Start-HarnessGame -Side retail -Exe "$dir\fakk2.exe" -WorkDir $dir -Arguments $arguments -WindowStyle Minimized -What "compare-player retail $Map $Probe"
        $done = Wait-ForMarker $p @($console) $Secs
    }
    finally { Stop-HarnessGame $p }
    if (Test-Path $console) { Copy-Item $console $log -Force }
    if (-not $done) { "[$Map] retail: the probe did not finish within ${Secs}s" }
}
if ($Refresh -or -not (Test-Path $retailLog)) {
    if ($NoRetail) { "[$Map/$Probe] no retail reference at $retailLog (and -NoRetail)"; return }
    Run-Retail $retailLog
}
if ($RetailOnly) { "[$Map/$Probe] retail -> $retailLog"; return }

# --- OpenFAKK2 ----------------------------------------------------------------------
if ($RetailTwice) {
    $ourLog = Join-Path $LogDir "player-retail2-$(Get-MapTag $Map)-$Probe.log"
    Run-Retail $ourLog
} else {
if (-not $Bin) { $Bin = $EngineBin }
$ourLog = Join-Path $LogDir "player-ours-$(Get-MapTag $Map)-$Probe.log"
$homeMaps = Get-ProbeDir $TestHome $Map
New-Item -ItemType Directory -Force $homeMaps | Out-Null
Copy-Item $probeFile "$homeMaps\$Probe.scr" -Force
Remove-Item "$ourLog.stdout", "$ourLog.stderr" -ErrorAction SilentlyContinue
$arguments = "$OursHomeArgs +set developer 1 +set sv_testplayer 1 +set sv_randomSeed 1 $Extra +map $Map $after"
$p = $null; $done = $false; $exited = $false
try {
    $p = Start-HarnessGame -Side ours -Exe "$Bin\openfakk2ded.exe" -Arguments $arguments -NoNewWindow -Stdout "$ourLog.stdout" -Stderr "$ourLog.stderr" -What "compare-player ours $Map $Probe"
    $done = Wait-ForMarker $p @("$ourLog.stdout", "$ourLog.stderr") $Secs
    $exited = $p.HasExited
}
finally {
    Stop-HarnessGame $p
    if (Test-Path "$ourLog.stdout") { Get-Content "$ourLog.stdout", "$ourLog.stderr" -ErrorAction SilentlyContinue | Set-Content $ourLog }
}
if ($exited) { "[$Map] ours: the server exited (code $($p.ExitCode))" } elseif (-not $done) { "[$Map] ours: the probe did not finish within ${Secs}s" }
}

# --- compare ------------------------------------------------------------------------
. (Join-Path $PSScriptRoot 'compare-lib.ps1')
$a = Get-Dump $retailLog; $b = Get-Dump $ourLog
if (-not $a.Count -or -not $b.Count) { "[$Map] no dump to compare (retail $($a.Count) lines, ours $($b.Count) lines)"; return }

$varsA = $a | Where-Object { $_ -match '^\S.* = ' }; $varsB = $b | Where-Object { $_ -match '^\S.* = ' }
$varDiff = @(Compare-Object $varsA $varsB)
$ka = Get-EventKinds $a; $kb = Get-EventKinds $b
$kindDiff = @(($ka.Keys + $kb.Keys | Sort-Object -Unique) | Where-Object { [int]$ka[$_] -ne [int]$kb[$_] })
$ta = (($a | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1); $tb = (($b | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1)
"[$Map/$Probe] variables: $($varsA.Count) retail / $($varsB.Count) ours, differing lines $($varDiff.Count); events: $($ta.Groups[1].Value) at $($ta.Groups[2].Value) retail / $($tb.Groups[1].Value) at $($tb.Groups[2].Value) ours, kinds differing $($kindDiff.Count)"
$varDiff | Select-Object -First $Show | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', 'retail:' -replace '=>', 'ours:  '), $_.InputObject }
$kindDiff | Select-Object -First $Show | ForEach-Object { "  event {0,-40} retail {1,4}  ours {2,4}" -f $_, [int]$ka[$_], [int]$kb[$_] }

$ra = Get-Readings $retailLog; $rb = Get-Readings $ourLog
if ($ra.Count -or $rb.Count) {
    $r = Compare-Readings $ra $rb $Tolerance $Show
    Format-ReadingsLine $r
    $r.Examples
}
