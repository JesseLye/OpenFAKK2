param([string]$Map = 'training', [int]$Secs = 120, [int]$Show = 12, [switch]$Refresh)
# Compares game state with a player present: the retail game (real client, nobody at the
# controls) against OpenFAKK2 with its headless test player. Both run probe.scr, which
# waits for the player, waits ten seconds of game time and dumps levelvars and
# pendingevents. The retail game shows a small window while it runs; its result is cached
# per map in build\compare (use -Refresh to run it again).
. (Join-Path $PSScriptRoot 'common.ps1')

$probe = Join-Path $PSScriptRoot 'probe.scr'
$after = '+script thread probe.scr::probe'
$marker = 'pending events as of'

function Wait-ForMarker($process, $files, $seconds) {
    # returns once every started dump has finished printing, or the time is up
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
$retailLog = Join-Path $LogDir "player-retail-$Map.log"
if ($Refresh -or -not (Test-Path $retailLog)) {
    $dir = Join-Path $Sandbox 'retail'
    if (-not (Test-Path "$dir\fakk2.exe")) { & (Join-Path $PSScriptRoot 'run-retail.ps1') -Name retail -Map $Map -Secs 5 | Out-Null }
    Copy-Item "$GameInstall\fakk\default.cfg" "$dir\fakk\default.cfg" -Force
    New-Item -ItemType Directory -Force "$dir\fakk\maps" | Out-Null
    Copy-Item $probe "$dir\fakk\maps\probe.scr" -Force
    $console = "$dir\fakk\qconsole.log"
    Remove-Item $console -ErrorAction SilentlyContinue
    $arguments = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 +map $Map $after"
    $p = Start-Process -FilePath "$dir\fakk2.exe" -WorkingDirectory $dir -ArgumentList $arguments -PassThru -WindowStyle Minimized
    $done = Wait-ForMarker $p @($console) $Secs
    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
    if (Test-Path $console) { Copy-Item $console $retailLog -Force }
    if (-not $done) { "[$Map] retail: the probe did not fire within ${Secs}s" }
}

# --- OpenFAKK2 ----------------------------------------------------------------------
$ourLog = Join-Path $LogDir "player-ours-$Map.log"
$homeMaps = Join-Path $Sandbox 'home\fakk\maps'
New-Item -ItemType Directory -Force $homeMaps | Out-Null
Copy-Item $probe "$homeMaps\probe.scr" -Force
Remove-Item "$ourLog.stdout", "$ourLog.stderr" -ErrorAction SilentlyContinue
$arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$(Join-Path $Sandbox 'home')`" +set developer 1 +set sv_testplayer 1 +map $Map $after"
$p = Start-Process -FilePath "$EngineBin\openfakk2ded.exe" -WorkingDirectory $EngineBin -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput "$ourLog.stdout" -RedirectStandardError "$ourLog.stderr"
$done = Wait-ForMarker $p @("$ourLog.stdout", "$ourLog.stderr") $Secs
$exited = $p.HasExited
if (-not $exited) { $p.Kill(); $p.WaitForExit() }
Get-Content "$ourLog.stdout", "$ourLog.stderr" | Set-Content $ourLog
if ($exited) { "[$Map] ours: the server exited (code $($p.ExitCode))" } elseif (-not $done) { "[$Map] ours: the probe did not fire within ${Secs}s" }

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
$a = Dump $retailLog; $b = Dump $ourLog
if (-not $a.Count -or -not $b.Count) { "[$Map] no dump to compare (retail $($a.Count) lines, ours $($b.Count) lines)"; return }

$varsA = $a | Where-Object { $_ -match '^\S.* = ' }; $varsB = $b | Where-Object { $_ -match '^\S.* = ' }
$varDiff = @(Compare-Object $varsA $varsB)
$ka = EventKinds $a; $kb = EventKinds $b
$kindDiff = @(($ka.Keys + $kb.Keys | Sort-Object -Unique) | Where-Object { [int]$ka[$_] -ne [int]$kb[$_] })
$ta = (($a | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1); $tb = (($b | Select-String "(\d+) $marker ([\d.]+)").Matches | Select-Object -First 1)
"[$Map] variables: $($varsA.Count) retail / $($varsB.Count) ours, differing lines $($varDiff.Count); events: $($ta.Groups[1].Value) at $($ta.Groups[2].Value) retail / $($tb.Groups[1].Value) at $($tb.Groups[2].Value) ours, kinds differing $($kindDiff.Count)"
$varDiff | Select-Object -First $Show | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', 'retail:' -replace '=>', 'ours:  '), $_.InputObject }
$kindDiff | Select-Object -First $Show | ForEach-Object { "  event {0,-40} retail {1,4}  ours {2,4}" -f $_, [int]$ka[$_], [int]$kb[$_] }
