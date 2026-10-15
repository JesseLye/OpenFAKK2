param([string]$Map = 'zoo', [int]$Secs = 120, [int]$Show = 12, [double]$Tolerance = 0.01, [string]$Extra = '')
# Checks that a saved game comes back as it was saved. Two runs of openfakk2ded with the
# headless test player:
#   1. plays $Map with probe-save.scr, which saves the game ("probesave") three seconds
#      after the player enters, waits two seconds and then reads every entity (whatis 0
#      to 299), levelvars and pendingevents;
#   2. starts with "+loadgame probesave". The game archives its script threads, so the
#      probe comes back in the middle of its two-second wait and takes the same readings
#      again, at the same game time.
# The readings of the two runs are compared field by field (numbers within -Tolerance).
# The level clock, the area portals and every entity the game archives must survive the
# round trip; random choices made after the save may differ, as they do between runs.
# The save is written to build\sandbox\home\fakk\save\config_server_probesave.* (saves are named
# after the config file, which is config_server.cfg for openfakk2ded).
. (Join-Path $PSScriptRoot 'common.ps1')

$probe = 'probe-save'
$probeFile = Join-Path $PSScriptRoot "$probe.scr"
$marker = 'pending events as of'
$homeDir = Join-Path $Sandbox 'home'
$homeMaps = Get-ProbeDir $homeDir $Map
New-Item -ItemType Directory -Force $homeMaps | Out-Null
Copy-Item $probeFile "$homeMaps\$probe.scr" -Force
Remove-Item (Join-Path $homeDir 'fakk\save\config_server_probesave.*') -ErrorAction SilentlyContinue

function Run-Ours($name, $run) {
    $log = Join-Path $LogDir "save-load-$(Get-MapTag $Map)-$name.log"
    Remove-Item "$log.stdout", "$log.stderr" -ErrorAction SilentlyContinue
    $arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$homeDir`" +set com_abnormalExitDialog 0 +set developer 1 +set sv_testplayer 1 $Extra $run"
    $p = Start-Process -FilePath "$EngineBin\openfakk2ded.exe" -WorkingDirectory $EngineBin -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput "$log.stdout" -RedirectStandardError "$log.stderr"
    $deadline = (Get-Date).AddSeconds($Secs); $done = $false
    while (-not $done -and (Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep -Seconds 1
        foreach ($file in "$log.stdout", "$log.stderr") {
            if (-not (Test-Path $file)) { continue }
            $fs = [IO.File]::Open($file, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $text = $sr.ReadToEnd(); $sr.Dispose()
            if ($text.Contains($marker)) { Start-Sleep -Seconds 1; $done = $true }
        }
    }
    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
    Get-Content "$log.stdout", "$log.stderr" | Set-Content $log
    if (-not $done) { Write-Host "[$Map] $name`: the probe did not finish within ${Secs}s" }
    $log
}

# "whatis" readings in order of appearance: a list of ordered field tables
function Readings($file) {
    $blocks = New-Object System.Collections.Generic.List[object]
    $cur = $null
    foreach ($line in Get-Content $file) {
        if ($line -match '^Entity #\s*: (\d+)') { $cur = [ordered]@{}; $blocks.Add($cur) }
        if ($cur -and $line -match '^(\w[\w ]*?)\s*: (.*)$') { $cur[$Matches[1].Trim()] = $Matches[2].Trim(); if ($Matches[1].Trim() -eq 'Edict Owner') { $cur = $null } }
        elseif ($cur -and $line -notmatch '^\s*$' -and $line -notmatch '^(\w[\w #]*?)\s*:') { $cur = $null }
    }
    return ,$blocks
}
function SameValue($x, $y) {
    if ($x -eq $y) { return $true }
    $nx = [regex]::Matches($x, '-?\d+(\.\d+)?'); $ny = [regex]::Matches($y, '-?\d+(\.\d+)?')
    if ($nx.Count -ne $ny.Count -or $nx.Count -eq 0) { return $false }
    if (([regex]::Replace($x, '-?\d+(\.\d+)?', '#')) -ne ([regex]::Replace($y, '-?\d+(\.\d+)?', '#'))) { return $false }
    for ($i = 0; $i -lt $nx.Count; $i++) { if ([Math]::Abs([double]$nx[$i].Value - [double]$ny[$i].Value) -gt $Tolerance) { return $false } }
    return $true
}
function Vars($file) {
    $lines = Get-Content $file
    $start = ($lines | Select-String '^Level Variables' | Select-Object -Last 1).LineNumber
    if (-not $start) { return @() }
    $lines[$start..($lines.Count - 1)] | Where-Object { $_ -match '^\S.* = ' }
}

$saved = Run-Ours 'saved' "+map $Map +script thread $probe.scr::probe"
$saveFile = Join-Path $homeDir 'fakk\save\config_server_probesave.sav'
if (-not (Test-Path $saveFile)) { "[$Map] no save was written ($saveFile); see $saved"; return }
$loaded = Run-Ours 'loaded' '+loadgame probesave'

$time = { param($f) (Select-String -Path $f -Pattern "(\d+) $marker ([\d.]+)" | Select-Object -Last 1).Matches.Groups[2].Value }
"[$Map] pending events read at $(& $time $saved) saved / $(& $time $loaded) loaded"
$varDiff = @(Compare-Object @(Vars $saved) @(Vars $loaded))
"  level variables differing: $($varDiff.Count)"
$varDiff | Select-Object -First $Show | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', 'saved: ' -replace '=>', 'loaded:'), $_.InputObject }

$ra = Readings $saved; $rb = Readings $loaded
$n = [Math]::Min($ra.Count, $rb.Count)
$same = 0; $fieldCounts = @{}; $examples = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $n; $i++) {
    $x = $ra[$i]; $y = $rb[$i]
    $bad = @(($x.Keys + $y.Keys | Select-Object -Unique) | Where-Object { -not (SameValue ([string]$x[$_]) ([string]$y[$_])) })
    if (-not $bad.Count) { $same++; continue }
    foreach ($f in $bad) { $fieldCounts[$f] = 1 + [int]$fieldCounts[$f] }
    if ($examples.Count -lt $Show) { $f = $bad[0]; $examples.Add(("  reading {0} (entity {1}, {2}) {3}: saved {4} | loaded {5}" -f ($i + 1), $x['Entity #'], $x['Classname'], $f, $x[$f], $y[$f])) }
}
"  whatis readings: $($ra.Count) saved / $($rb.Count) loaded; identical $same of $n" + $(if ($fieldCounts.Count) { "; fields differing: " + (($fieldCounts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ', ') } else { '' })
$examples
