param(
    [Parameter(Mandatory = $true)][string]$Probe,
    [string]$Map = 'training',
    [ValidateSet('both', 'retail', 'ours')][string]$Side = 'both',
    [string]$Bin = '',
    [string]$Name = 'ours',
    [int]$Secs = 120,
    [int]$Mode = 3,
    [string]$Extra = '',
    [string]$RetailExtra = '',
    [string]$OursExtra = '',
    [switch]$MaskNotify
)
# Runs one screenshot probe on the retail game and on OpenFAKK2, and pairs the shots.
#
# The probe is a level script in tools\retail-compare (or a path) that starts with
# waitforplayer, takes its pictures with stuffcmd "screenshot" and ends with
# stuffcmd "quit". Both games start it with "+map <m> +script thread <probe>.scr::probe"
# before the player spawns, so the same file runs unchanged on both and its waits land on
# the same game time. Never add a +wait to these command lines: the player would spawn
# before the thread starts, waitforplayer would never return, and the shots would show a
# different moment.
#
# Output goes to build\compare\captures\<probe>-<map>\: <label>-<n>.tga and .png for each
# shot (label "retail", and -Name for ours), then pair-<name>-<n>.png (retail | ours | |diff|x4)
# for each shot both sides took, with the difference figures printed. Logs are
# build\compare\capture-<label>-<map>-<probe>.log.
#
#   -Side         both (default), retail or ours. One side keeps the other's earlier shots
#                 in the folder, so -Side ours after a fix pairs against the last retail run.
#   -Bin          OpenFAKK2 engine directory (default this repo's build\x86-Release\Release);
#                 point it at another checkout's build, with -Name before, to compare builds.
#   -Mode         r_mode on both; 3 is 640x480 on both engines.
#   -Extra        engine command line for both games; -RetailExtra / -OursExtra for one.
#                 On retail, an archived cvar set here is saved in the sandbox's config.cfg
#                 and carries into later retail runs. Set it back or check that file.
#   -MaskNotify   ignore the "Wrote screenshots/..." line in the difference figures.

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
. (Join-Path $RepoRoot 'tools\retail-compare\common.ps1')   # $GameInstall, $Sandbox, $LogDir
$shots = Join-Path $PSScriptRoot 'shots.py'

$probeFile = if (Test-Path $Probe) { (Resolve-Path $Probe).Path } else { Join-Path $RepoRoot "tools\retail-compare\$Probe.scr" }
if (-not (Test-Path $probeFile)) { throw "no such probe: $probeFile" }
$probeName = [IO.Path]::GetFileNameWithoutExtension($probeFile)
$text = Get-Content $probeFile -Raw
if ($text -notmatch '(?m)^\s*waitforplayer') { "warning: $probeName has no waitforplayer; its waits count from the thread's start, not the player's entry" }
if ($text -notmatch 'stuffcmd\s+"quit"') { "warning: $probeName does not end with stuffcmd ""quit""; each run lasts the full ${Secs}s" }

$outDir = Join-Path $LogDir "captures\$probeName-$(Get-MapTag $Map)"
New-Item -ItemType Directory -Force $outDir | Out-Null

function Invoke-Side([string]$label, [string]$exe, [string]$workDir, [string]$gameHome, [string]$arguments, [string]$windowStyle) {
    $maps = Get-ProbeDir $gameHome $Map
    $shotDir = Join-Path $gameHome 'fakk\screenshots'
    $console = Join-Path $gameHome 'fakk\qconsole.log'
    New-Item -ItemType Directory -Force $maps | Out-Null
    Copy-Item $probeFile (Join-Path $maps "$probeName.scr") -Force
    Remove-Item $console -ErrorAction SilentlyContinue
    $before = @{}
    if (Test-Path $shotDir) { Get-ChildItem $shotDir -File | ForEach-Object { $before[$_.Name] = $_.LastWriteTimeUtc } }

    $p = Start-Process -FilePath $exe -WorkingDirectory $workDir -ArgumentList $arguments -PassThru -WindowStyle $windowStyle
    if (-not $p.WaitForExit($Secs * 1000)) { $p.Kill(); $p.WaitForExit(); $state = "stopped after ${Secs}s" } else { $state = "exited with code $($p.ExitCode)" }

    $log = Join-Path $LogDir "capture-$label-$(Get-MapTag $Map)-$probeName.log"
    if (Test-Path $console) { Copy-Item $console $log -Force } else { $log = '(no console log)' }
    # this run's shots: names that are new, or rewritten since the run started. Retail
    # cuts the file's path at 127 characters, so from a deep worktree its shots can be
    # shot0000.tg (still a TGA); any shot* name counts
    $new = @()
    if (Test-Path $shotDir) {
        $new = @(Get-ChildItem $shotDir -File -Filter 'shot*' |Where-Object { -not $before.ContainsKey($_.Name) -or $before[$_.Name] -ne $_.LastWriteTimeUtc } | Sort-Object Name)
    }
    Get-ChildItem $outDir -File -Filter "$label-*" -ErrorAction SilentlyContinue | Remove-Item
    $n = 0
    foreach ($f in $new) {
        $dest = Join-Path $outDir ("{0}-{1}.tga" -f $label, $n)
        Copy-Item $f.FullName $dest -Force
        python -I $shots png $dest ([IO.Path]::ChangeExtension($dest, '.png')) | Out-Null
        $n++
    }
    "[$label] $state, $n screenshot(s), log $log"
}

if ($Side -ne 'ours') {
    $gameDir = Join-Path $Sandbox 'retail'
    if (-not (Test-Path "$gameDir\fakk2.exe")) {
        # first use: copies fakk2.exe and the game modules, hard-links the paks
        & (Join-Path $RepoRoot 'tools\retail-compare\run-retail.ps1') -Name retail -Map $Map -Secs 5 | Out-Null
    }
    Copy-Item "$GameInstall\fakk\default.cfg" "$gameDir\fakk\default.cfg" -Force
    $arguments = "+set r_fullscreen 0 +set r_mode $Mode +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 " +
        "$Extra $RetailExtra +map $Map +script thread $probeName.scr::probe"
    Invoke-Side 'retail' (Join-Path $gameDir 'fakk2.exe') $gameDir $gameDir $arguments 'Minimized'
}

if ($Side -ne 'retail') {
    if (-not $Bin) { $Bin = Join-Path $RepoRoot 'build\x86-Release\Release' }
    $exe = Join-Path $Bin 'openfakk2.exe'
    if (-not (Test-Path $exe)) { throw "no client at $exe (build it, or pass -Bin)" }
    $homeDir = Join-Path $Sandbox 'home'
    $arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$homeDir`" +set com_abnormalExitDialog 0 " +
        "+set r_fullscreen 0 +set r_mode $Mode +set in_nograb 1 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 " +
        "$Extra $OursExtra +map $Map +script thread $probeName.scr::probe"
    Invoke-Side $Name $exe $Bin $homeDir $arguments 'Normal'
    Clear-HarnessMouse
}

$i = 0
while ((Test-Path (Join-Path $outDir "retail-$i.tga")) -and (Test-Path (Join-Path $outDir "$Name-$i.tga"))) {
    $pairArgs = @('pair', (Join-Path $outDir "retail-$i.tga"), (Join-Path $outDir "$Name-$i.tga"), (Join-Path $outDir "pair-$Name-$i.png"))
    if ($MaskNotify) { $pairArgs += '--mask-notify' }
    "--- shot $i"
    python -I $shots @pairArgs
    $i++
}
if (-not $i) { "no shot pairs in $outDir (one side took no screenshots: read its log)" }
