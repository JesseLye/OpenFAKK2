param(
    [string]$Demo = '',
    [string]$Against = '',
    [string]$Probe = '',
    [string]$Map = 'zoo',
    [ValidateSet('retail', 'ours', 'both')][string]$RecordOn = 'retail',
    [ValidateSet('frames', 'sound', 'diff', 'record')][string]$Mode = 'frames',
    [ValidateSet('both', 'retail', 'ours')][string]$Play = 'both',
    [int]$Rate = 2,
    [int]$Every = 1,
    [string[]]$Keep = @(),
    [string]$Name = 'ours',
    [switch]$RetailCgame,
    [int]$Top = 6,
    [ValidateSet('openal', 'dma')][string]$Backend = 'openal',
    [double]$Volume = 0.25,
    [string]$Bin = '',
    [string]$Extra = '',
    [int]$Secs = 0
)
# Plays one demo on the retail game and on OpenFAKK2 and compares what each makes of it
# (the retail-demo skill). A demo holds what a server sent, so both clients get the same
# snapshots, entities, sounds and prints: what differs is the client side (the renderer, the
# client game module, the HUD, the sound system). Output goes to
# build\compare\demos\<demo>\.
#
#   -Demo      a .dm3: a path, or a name in the main checkout's build\demos
#   -Probe     record it first: a probe in tools\retail-compare that records
#              demos/<probe>.dm3 and quits (probe-dzoo.scr), run on -Map. -RecordOn retail
#              (default), ours or both; the takes are kept as build\demos\<probe>-<side>.dm3.
#   -Mode      frames  play it on both with cl_avidemo -Rate (frame n is n/Rate s of the
#                      demo on both games), rank the frames by how much they differ and
#                      write pair pictures of the -Top worst
#              sound   play it at its own speed on both with the sound on, recording the
#                      speakers (sound-record.ps1 -Demo), and compare the levels second by
#                      second and the sounds each started (demo_sound.py). Plays out loud.
#              diff    two demos of the same probe, one recorded on each game (-Probe with
#                      -RecordOn both, or -Demo retail's and -Against ours'), compared as a
#                      timeline of what the servers sent (demo_diff.py): gameplay, not
#                      pictures
#              record  only record (-Probe)
#   -Play      frames: both (default), or ours alone against the retail frames already in
#              the folder (after a fix), or retail alone
#   -Name      the label of ours' frames (ours, fixed, before...), so takes sit side by side
#   -RetailCgame  ours with Ritual's cgamex86.dll (and gamex86.dll): when a difference goes
#              away with it, it is in our client game module; when it stays, in the engine
#   -Keep      frames: only these windows of the demo ("from-to" in seconds)
#   -Backend   sound: ours' openal (what players hear, default) or dma, the software mixer,
#              which also logs each sound it starts (s_show) for the comparison
#   -Volume    sound: s_volume on both (0.25; 0.06 for loud weapons, under the compressor)
#   -Bin       OpenFAKK2 engine directory (default this checkout's build)
#   -Extra     more command line for both games' playback
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
. (Join-Path $RepoRoot 'tools\suite\suite-lib.ps1')   # common.ps1, Invoke-DemoPlayback, $ShotsPy, Get-SoundPython
$ErrorActionPreference = 'Stop'

function Resolve-Demo([string]$Given) {
    if (-not $Given) { return '' }
    if (Test-Path $Given) { return (Resolve-Path $Given).Path }
    foreach ($c in @((Join-Path $DemoDir $Given), (Join-Path $DemoDir "$Given.dm3"))) { if (Test-Path $c) { return $c } }
    throw "no demo $Given (a path, or a name in $DemoDir)"
}

# the demo's length in seconds, from its snapshots
function Get-DemoSeconds([string]$Path) {
    $py = "import sys; sys.path.insert(0, r'$RetailCompare'); import dm3; d = dm3.read_demo(r'$Path'); s = d.snapshots; print((s[-1]['server_time'] - s[0]['server_time']) / 1000.0 if s else 0)"
    [double](& $Python -B -c $py | Select-Object -Last 1)
}

function Invoke-Record([ValidateSet('retail', 'ours')][string]$Side) {
    $fakk = if ($Side -eq 'retail') { Join-Path $Sandbox 'retail\fakk' } else { $TestFakk }
    $recorded = Join-Path $fakk "demos\$Probe.dm3"
    Remove-Item $recorded -ErrorAction SilentlyContinue
    $out = @(& $ProbeClient -Probe $Probe -Map $Map -Name "$Side-record" -Retail:($Side -eq 'retail') -Bin $Bin -Secs $(if ($Secs) { $Secs } else { 150 }) -Extra $Extra -Classname none -Entity -1)
    $out | Select-Object -First 1 | Write-Host
    if (-not (Test-Path $recorded)) { throw "$Side recorded no demos/${Probe}.dm3 - does the probe set g_syncronousClients, record $Probe and stoprecord? (see $LogDir\client-$Side-record-$(Get-MapTag $Map)-$Probe.log)" }
    New-Item -ItemType Directory -Force $DemoDir | Out-Null
    $kept = Join-Path $DemoDir "$Probe-$Side.dm3"
    Copy-Item $recorded $kept -Force
    Write-Host "recorded on $Side`: $kept"
    $kept
}

# --- the demo or demos --------------------------------------------------------------
$retailTake = ''; $oursTake = ''
if ($Probe) {
    $sides = if ($Mode -eq 'diff') { @('retail', 'ours') } elseif ($RecordOn -eq 'both') { @('retail', 'ours') } else { @($RecordOn) }
    foreach ($s in $sides) { if ($s -eq 'retail') { $retailTake = Invoke-Record retail } else { $oursTake = Invoke-Record ours } }
    if ($Mode -eq 'record') { return }
    if (-not $Demo) { $Demo = if ($retailTake) { $retailTake } else { $oursTake } }
}
elseif ($Mode -eq 'record') { throw '-Mode record needs -Probe' }
$Demo = Resolve-Demo $Demo
if (-not $Demo) { throw 'give -Demo (a .dm3) or -Probe (to record one)' }

if ($Mode -eq 'diff') {
    $theirs = if ($retailTake) { $retailTake } else { $Demo }
    $mine = if ($oursTake) { $oursTake } else { Resolve-Demo $Against }
    if (-not $mine) { throw '-Mode diff needs two demos: -Probe (records both), or -Demo <retail take> -Against <ours take>' }
    & $Python -B (Join-Path $RetailCompare 'demo_diff.py') $theirs $mine
    return
}

$demoName = [IO.Path]::GetFileNameWithoutExtension($Demo)
$outDir = Join-Path $LogDir "demos\$demoName"
New-Item -ItemType Directory -Force $outDir | Out-Null
$length = Get-DemoSeconds $Demo
if ($RetailCgame) { $Name = "$Name-retailcgame" }
"$demoName`: $([Math]::Round($length, 1)) s of demo -> $outDir"

# Ritual's game modules for ours' run when asked (Reset-TestHome reads it at the start)
$savedModules = $env:FAKK2_TEST_MODULES
if ($RetailCgame) { $env:FAKK2_TEST_MODULES = 'retail' }
try {
    if ($Mode -eq 'frames') {
        # frames render as fast as the game can take and write them; about 0.4 s each with room
        $limit = if ($Secs) { $Secs } else { [int](60 + $length * $Rate * 0.6) }
        if (-not (Test-Path "$Sandbox\retail\fakk2.exe")) { & (Join-Path $RetailCompare 'run-retail.ps1') -Name retail -Map zoo -Secs 5 | Out-Null }
        $retailFrames = Join-Path $outDir 'retail'
        $oursFrames = Join-Path $outDir $Name
        foreach ($side in @(if ($Play -eq 'both') { 'retail', 'ours' } else { $Play })) {
            $dir = if ($side -eq 'retail') { $retailFrames } else { $oursFrames }
            if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
            New-Item -ItemType Directory -Force $dir | Out-Null
            Invoke-DemoPlayback $side $Demo $dir $Rate $Every $limit $Extra $(if ($side -eq 'ours') { $Bin } else { '' }) $Keep
        }
        if (-not (Test-Path $retailFrames)) { throw "no retail frames in ${retailFrames}: play retail first (-Play both or retail)" }
        if (-not (Test-Path $oursFrames)) { "no frames of ours as $Name yet: -Play ours"; return }
        $pairs = Join-Path $outDir "pairs-$Name"
        if (Test-Path $pairs) { Remove-Item $pairs -Recurse -Force }
        & $Python -I $ShotsPy rank $retailFrames $oursFrames --rate $Rate --mask-notify --top $Top --pairs $pairs --csv (Join-Path $outDir "rank-$Name.csv") |
            Where-Object { $_ -notmatch 'A \| B \|' }
        "pairs (retail | ours | difference x4): $pairs"
    }
    else {
        # sound: each game plays the demo at its own speed while the speakers are recorded
        $soundPy = Get-SoundPython
        $limit = if ($Secs) { $Secs } else { [int]($length + 25) }
        $rec = Join-Path $RetailCompare 'sound-record.ps1'
        & $rec -Engine retail -Demo $Demo -Secs $limit -Volume $Volume -Python $soundPy -Extra $Extra | Out-Null
        & $rec -Engine $Backend -Demo $Demo -Secs $limit -Volume $Volume -Python $soundPy -Extra $Extra -Bin $Bin -Tag "-$Name" | Out-Null
        $r = Join-Path $LogDir "sound-retail-$demoName"
        $o = Join-Path $LogDir "sound-$Backend-$demoName-$Name"
        foreach ($f in "$r.npy", "$r.log", "$o.npy", "$o.log") { Copy-IfThere $f $outDir | Out-Null }
        if (-not (Test-Path "$r.npy") -or -not (Test-Path "$o.npy")) { throw "a recording is missing ($r.npy, $o.npy)" }
        & $soundPy -I (Join-Path $RetailCompare 'demo_sound.py') "$r.npy" "$o.npy" --retail-log "$r.log" --ours-log "$o.log"
    }
}
finally { $env:FAKK2_TEST_MODULES = $savedModules }
