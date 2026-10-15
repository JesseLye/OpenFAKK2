param([string]$Map = 'training', [string]$Probe = 'probe', [int]$Secs = 150, [int]$Show = 12, [double]$Tolerance = 0.01, [string]$Extra = '', [switch]$Refresh, [switch]$RetailTwice)
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
    Copy-Item "$GameInstall\fakk\default.cfg" "$dir\fakk\default.cfg" -Force
    $probeDir = Get-ProbeDir $dir $Map
    New-Item -ItemType Directory -Force $probeDir | Out-Null
    Copy-Item $probeFile "$probeDir\$Probe.scr" -Force
    $console = "$dir\fakk\qconsole.log"
    Remove-Item $console -ErrorAction SilentlyContinue
    $arguments = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 +map $Map $after"
    $p = Start-Process -FilePath "$dir\fakk2.exe" -WorkingDirectory $dir -ArgumentList $arguments -PassThru -WindowStyle Minimized
    $done = Wait-ForMarker $p @($console) $Secs
    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
    if (Test-Path $console) { Copy-Item $console $log -Force }
    if (-not $done) { "[$Map] retail: the probe did not finish within ${Secs}s" }
}
if ($Refresh -or -not (Test-Path $retailLog)) { Run-Retail $retailLog }

# --- OpenFAKK2 ----------------------------------------------------------------------
if ($RetailTwice) {
    $ourLog = Join-Path $LogDir "player-retail2-$(Get-MapTag $Map)-$Probe.log"
    Run-Retail $ourLog
} else {
$ourLog = Join-Path $LogDir "player-ours-$(Get-MapTag $Map)-$Probe.log"
$homeMaps = Get-ProbeDir (Join-Path $Sandbox 'home') $Map
New-Item -ItemType Directory -Force $homeMaps | Out-Null
Copy-Item $probeFile "$homeMaps\$Probe.scr" -Force
Remove-Item "$ourLog.stdout", "$ourLog.stderr" -ErrorAction SilentlyContinue
$arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$(Join-Path $Sandbox 'home')`" +set com_abnormalExitDialog 0 +set developer 1 +set sv_testplayer 1 $Extra +map $Map $after"
$p = Start-Process -FilePath "$EngineBin\openfakk2ded.exe" -WorkingDirectory $EngineBin -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput "$ourLog.stdout" -RedirectStandardError "$ourLog.stderr"
$done = Wait-ForMarker $p @("$ourLog.stdout", "$ourLog.stderr") $Secs
$exited = $p.HasExited
if (-not $exited) { $p.Kill(); $p.WaitForExit() }
Get-Content "$ourLog.stdout", "$ourLog.stderr" | Set-Content $ourLog
if ($exited) { "[$Map] ours: the server exited (code $($p.ExitCode))" } elseif (-not $done) { "[$Map] ours: the probe did not finish within ${Secs}s" }
}

# --- compare ------------------------------------------------------------------------
function Dump($file) {
    if (-not (Test-Path $file)) { return @() }
    $lines = Get-Content $file
    $start = ($lines | Select-String '^Level Variables' | Select-Object -Last 1).LineNumber
    if (-not $start) { return @() }
    $lines[$start..($lines.Count - 1)]
}
function EventKinds($lines) {
    $kinds = @{}
    $lines | Select-String '^\s*[\d.]+:\d+: (\w+) \(\*\d+\)(?: ''[^'']*'')? : \w+ :(\w+)' | ForEach-Object {
        $k = "$($_.Matches[0].Groups[1].Value) $($_.Matches[0].Groups[2].Value)"; $kinds[$k] = 1 + [int]$kinds[$k]
    }
    $kinds
}
# "whatis" readings in order of appearance: a list of ordered field tables
function Readings($file) {
    $blocks = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path $file)) { return ,$blocks }
    $cur = $null
    foreach ($line in Get-Content $file) {
        if ($line -match '^Entity #\s*: (\d+)') { $cur = [ordered]@{}; $blocks.Add($cur) }
        if ($cur -and $line -match '^(\w[\w ]*?)\s*: (.*)$') { $cur[$Matches[1].Trim()] = $Matches[2].Trim(); if ($Matches[1].Trim() -eq 'Edict Owner') { $cur = $null } }
        elseif ($cur -and $line -notmatch '^\s*$' -and $line -notmatch '^(\w[\w #]*?)\s*:') { $cur = $null }
    }
    return ,$blocks
}
# equal when the text is the same, or every number in it is within the tolerance
function SameValue($x, $y) {
    if ($x -eq $y) { return $true }
    $nx = [regex]::Matches($x, '-?\d+(\.\d+)?'); $ny = [regex]::Matches($y, '-?\d+(\.\d+)?')
    if ($nx.Count -ne $ny.Count -or $nx.Count -eq 0) { return $false }
    if (([regex]::Replace($x, '-?\d+(\.\d+)?', '#')) -ne ([regex]::Replace($y, '-?\d+(\.\d+)?', '#'))) { return $false }
    for ($i = 0; $i -lt $nx.Count; $i++) { if ([Math]::Abs([double]$nx[$i].Value - [double]$ny[$i].Value) -gt $Tolerance) { return $false } }
    return $true
}

$a = Dump $retailLog; $b = Dump $ourLog
if (-not $a.Count -or -not $b.Count) { "[$Map] no dump to compare (retail $($a.Count) lines, ours $($b.Count) lines)"; return }

$varsA = $a | Where-Object { $_ -match '^\S.* = ' }; $varsB = $b | Where-Object { $_ -match '^\S.* = ' }
$varDiff = @(Compare-Object $varsA $varsB)
$ka = EventKinds $a; $kb = EventKinds $b
$kindDiff = @(($ka.Keys + $kb.Keys | Sort-Object -Unique) | Where-Object { [int]$ka[$_] -ne [int]$kb[$_] })
$ta = (($a | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1); $tb = (($b | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1)
"[$Map/$Probe] variables: $($varsA.Count) retail / $($varsB.Count) ours, differing lines $($varDiff.Count); events: $($ta.Groups[1].Value) at $($ta.Groups[2].Value) retail / $($tb.Groups[1].Value) at $($tb.Groups[2].Value) ours, kinds differing $($kindDiff.Count)"
$varDiff | Select-Object -First $Show | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', 'retail:' -replace '=>', 'ours:  '), $_.InputObject }
$kindDiff | Select-Object -First $Show | ForEach-Object { "  event {0,-40} retail {1,4}  ours {2,4}" -f $_, [int]$ka[$_], [int]$kb[$_] }

$ra = Readings $retailLog; $rb = Readings $ourLog
if ($ra.Count -or $rb.Count) {
    $n = [Math]::Min($ra.Count, $rb.Count)
    $same = 0; $fieldCounts = @{}; $examples = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $n; $i++) {
        $x = $ra[$i]; $y = $rb[$i]
        $bad = @(($x.Keys + $y.Keys | Select-Object -Unique) | Where-Object { -not (SameValue ([string]$x[$_]) ([string]$y[$_])) })
        if (-not $bad.Count) { $same++; continue }
        foreach ($f in $bad) { $fieldCounts[$f] = 1 + [int]$fieldCounts[$f] }
        if ($examples.Count -lt $Show) { $f = $bad[0]; $examples.Add(("  reading {0} (entity {1}, {2}) {3}: retail {4} | ours {5}" -f ($i + 1), $x['Entity #'], $x['Classname'], $f, $x[$f], $y[$f])) }
    }
    "  whatis readings: $($ra.Count) retail / $($rb.Count) ours; identical $same of $n" + $(if ($fieldCounts.Count) { "; fields differing: " + (($fieldCounts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ', ') } else { '' })
    $examples
}
