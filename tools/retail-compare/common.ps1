# Shared settings for the retail comparison scripts.
# Override the install location with the FAKK2_INSTALL environment variable.
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$GameInstall = if ($env:FAKK2_INSTALL) { $env:FAKK2_INSTALL } else { 'C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2' }
# Ritual's "Heavy Metal FAKK 2 Tools" 1.02 release (a clone of github.com/Sporesirius/fakk2):
# design documents in docs\, tool sources in source\utils, example maps in fakk\maps\example,
# the tools in bin\. Set FAKK2_TOOLS to its clone; only the scripts that read it (ritual-docs,
# install-example-maps) need it. It stays outside the repository: Ritual's licence
# (LICENSE-re.MD there) does not allow copying it, and bin\ holds Miles files that are not
# redistributable. Read from it; never commit anything taken from it.
$RitualTools = $env:FAKK2_TOOLS
if ($RitualTools -and -not (Test-Path (Join-Path $RitualTools 'readme-re.txt'))) {
    Write-Warning "Ritual's tools release is not at $RitualTools; set FAKK2_TOOLS to its clone"
}
$Sandbox = Join-Path $RepoRoot 'build\sandbox'
$EngineBin = Join-Path $RepoRoot 'build\x86-Release\Release'
$LogDir = Join-Path $RepoRoot 'build\compare'
New-Item -ItemType Directory -Force $LogDir | Out-Null

# The main checkout. A worktree's .git is a file naming <main>\.git\worktrees\<name>; the
# main checkout's build\ holds what every worktree shares: the harness lock, the play home
# and the test suite's references.
$MainRoot = $RepoRoot
$gitLink = Join-Path $RepoRoot '.git'
if (Test-Path $gitLink -PathType Leaf) {
    $gitDir = ((Get-Content $gitLink -TotalCount 1) -replace '^gitdir:\s*', '').Trim()
    $MainRoot = Split-Path (Split-Path (Split-Path $gitDir -Parent) -Parent) -Parent
    $MainRoot = (Resolve-Path $MainRoot).Path
}

# Two homes. The play home, build\sandbox\home in the main checkout, is where run.cmd
# plays the game: its config.cfg, saves, crash logs and (lately) Ritual's own gamex86.dll
# belong to the player, and no harness run uses it. The harness runs OpenFAKK2 with the
# install as fs_basepath and $TestHome as fs_homepath. The home is searched first, so a
# file there shadows the install's: Reset-TestHome gives it an empty config.cfg and
# autoexec.cfg before every run (so neither the player's settings in the install, such as
# cl_run 1, nor a previous run's archived cvars carry over) and no game modules, so the
# run uses the ones built beside the executable. Set FAKK2_TEST_MODULES=retail to run
# Ritual's gamex86.dll and cgamex86.dll from the install instead.
$PlayHome = Join-Path $MainRoot 'build\sandbox\home'
# Demos kept for comparing (.dm3, either engine's): the main checkout's build\demos, shared
# by every worktree and never committed (they are made from Ritual's data). A demo a player
# records lands in its game's fakk\demos; copy it here to use it (retail-demo skill, suite
# "demo" checks with "demoFile").
$DemoDir = Join-Path $MainRoot 'build\demos'
$TestHome = Join-Path $Sandbox 'test-home'
$TestFakk = Join-Path $TestHome 'fakk'
New-Item -ItemType Directory -Force $TestFakk | Out-Null

# Both games run without networking, so Windows never asks to let a new executable through
# its firewall: ours opens no UDP port with net_enabled 0, retail none with net_noudp and
# net_noipx. A local game and the headless test player go through the loopback, which
# stays. Start-HarnessGame adds these; a runner that starts a game by itself must too.
$OursNoNet = '+set net_enabled 0'
$RetailNoNet = '+set net_noudp 1 +set net_noipx 1'
# What every OpenFAKK2 run starts with (Start-HarnessGame adds $OursNoNet). The pid file a
# killed run leaves behind would make the next start wait on the "didn't exit properly"
# dialog; com_abnormalExitDialog 0 skips it.
$OursHomeArgs = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$TestHome`" +set com_abnormalExitDialog 0"

function Reset-TestHome {
    New-Item -ItemType Directory -Force $TestFakk | Out-Null
    foreach ($cfg in 'config.cfg', 'autoexec.cfg', 'config_server.cfg') {
        Set-Content -Path (Join-Path $TestFakk $cfg) -Value "// empty on purpose: harness runs start from the engine's defaults (Reset-TestHome)" -Encoding ascii
    }
    foreach ($dll in 'gamex86.dll', 'cgamex86.dll') {
        $target = Join-Path $TestFakk $dll
        if ($env:FAKK2_TEST_MODULES -eq 'retail') { Copy-Item (Join-Path $GameInstall "fakk\$dll") $target -Force }
        elseif (Test-Path $target) { Remove-Item $target -Force }
    }
    Remove-GeneratedPaths $TestFakk
}

# A level whose paks have no path file (homes1_fullvis) makes one at spawn ("No paths
# found. Creating paths...") and saves it in the home's maps folder, where the next run
# loads it instead ("Loading nodes..."), so a run's output depended on the runs before.
# Only the top level: Ritual's example maps keep their .pth files in maps\example.
function Remove-GeneratedPaths([string]$Fakk) {
    Get-ChildItem (Join-Path $Fakk 'maps') -File -Filter '*.pth' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
}

# The retail sandboxes (build\sandbox\retail and retail-snd) keep their config.cfg between
# runs, and retail archives what a run sets (in_mouse 0, developer, r_mode, fps). Each
# retail run starts from the install's default.cfg and an empty config.cfg instead.
function Reset-RetailConfig([string]$GameDir) {
    $fakk = Join-Path $GameDir 'fakk'
    if (-not (Test-Path $fakk)) { return }
    Copy-Item (Join-Path $GameInstall 'fakk\default.cfg') (Join-Path $fakk 'default.cfg') -Force
    Set-Content -Path (Join-Path $fakk 'config.cfg') -Value '// empty on purpose: harness runs start from default.cfg (Reset-RetailConfig)' -Encoding ascii
    Remove-GeneratedPaths $fakk
}

# --- games side by side, or alone ---------------------------------------------------
# Every runner takes the harness lock before it starts a game and lets it go once the game
# has stopped. Most runs share it, so several worktrees' games run at once: each worktree
# has its own test home and retail sandbox, and two retail games, or two of ours, run side
# by side (tried 2026-10-16). A run that needs the machine to itself takes it alone:
#
#   shared     (the default) up to FAKK2_HARNESS_SLOTS games at once (4), one per worktree,
#              since a worktree's games share its home
#   exclusive  no other harness game: recording the speakers (the loopback takes every
#              game's sound), retail's sound playback (another window in front silences it),
#              and timing (frame times, held keys: a busy machine moves them, as the
#              strafe checks showed)
#
# The mode is the caller's -Mode, else FAKK2_HARNESS_MODE (run-suite.ps1 and
# refresh-retail.ps1 set it per check from suite.json's "exclusive"), else shared.
#
# The lock is files in the main checkout's build\, held open, so Windows frees them when a
# holder's process ends however it ends: harness.lock itself, open for reading by every
# shared holder and with no sharing by an exclusive one (or by a runner from before shared
# runs, which so takes it alone), one of harness.slots\slot<n> per shared holder, and the
# worktree's own build\harness.worktree.lock. Runners queue in the order they asked
# (harness.lock.queue): a shared run may pass waiting shared runs, but none passes an
# exclusive one, which so is not starved. harness.holders lists who holds it, and an
# exclusive holder also writes harness.lock.owner. Nested runners (compare.ps1 calling
# run-retail.ps1) share the lock of the process that took it.
#
# A game started by Start-HarnessGame is put in a job object that kills it when this
# PowerShell process ends, so a run cut short (Ctrl+C, a pipeline stopped early, a killed
# shell) leaves no game behind, and none that writes into a home after it has gone.
$HarnessLockFile = Join-Path $MainRoot 'build\harness.lock'
$HarnessQueue = "$HarnessLockFile.queue"
$HarnessSlots = Join-Path $MainRoot 'build\harness.slots'
$HarnessHolders = Join-Path $MainRoot 'build\harness.holders'
$HarnessWorktreeLock = Join-Path $RepoRoot 'build\harness.worktree.lock'
New-Item -ItemType Directory -Force (Split-Path $HarnessLockFile), (Split-Path $HarnessWorktreeLock) | Out-Null

if (-not ('OpenFakk2Harness.KillJob' -as [type])) {
    Add-Type -Namespace OpenFakk2Harness -Name KillJob -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern System.IntPtr CreateJobObject(System.IntPtr a, string name);
[DllImport("kernel32.dll")] static extern bool SetInformationJobObject(System.IntPtr job, int cls, byte[] info, int size);
[DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(System.IntPtr job, System.IntPtr process);
static System.IntPtr job = System.IntPtr.Zero;
// JOBOBJECT_EXTENDED_LIMIT_INFORMATION with LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
// The flags follow two 8-byte time limits; the structure's size depends on the pointer size.
public static bool Add(System.IntPtr process) {
    if (job == System.IntPtr.Zero) {
        job = CreateJobObject(System.IntPtr.Zero, null);
        int size = System.IntPtr.Size == 8 ? 144 : 112;
        byte[] info = new byte[size];
        System.BitConverter.GetBytes(0x2000).CopyTo(info, 16);
        SetInformationJobObject(job, 9, info, size);
    }
    return AssignProcessToJobObject(job, process);
}
'@
}

# The FAKK2 games running on this machine, each marked Harness when a harness started it:
# OpenFAKK2 on the test home (or the play home with the older runners' logfile 2), the
# test player or the dedicated server, or a retail fakk2.exe from a build\sandbox copy.
# Anything else (run.cmd, the installed game) is the player's.
function Get-FakkGames {
    $all = @(Get-CimInstance Win32_Process -Filter "Name='openfakk2.exe' or Name='openfakk2ded.exe' or Name='fakk2.exe'" -ErrorAction SilentlyContinue)
    foreach ($g in $all) {
        $cmd = [string]$g.CommandLine; $exe = [string]$g.ExecutablePath
        $harness = $g.Name -eq 'openfakk2ded.exe' -or $cmd -match 'test-home|sv_testplayer|logfile 2' -or $exe -match '\\build\\sandbox\\'
        $orphan = $false
        if ($harness) {
            $parent = Get-Process -Id $g.ParentProcessId -ErrorAction SilentlyContinue
            $parentStart = $null; try { $parentStart = $parent.StartTime } catch { }
            # gone, or its pid reused by a process started after the game
            $orphan = -not $parent -or ($parentStart -and $parentStart -gt $g.CreationDate)
        }
        [pscustomobject]@{ Id = $g.ProcessId; Name = $g.Name; Path = $exe; Harness = $harness; Orphan = $orphan; Started = $g.CreationDate }
    }
}

function Get-HarnessHolders {
    @(Get-ChildItem $HarnessHolders -File -ErrorAction SilentlyContinue | ForEach-Object {
        $hpid = [int]($_.BaseName -replace '\D', '')
        if (Get-Process -Id $hpid -ErrorAction SilentlyContinue) { Get-Content $_.FullName -TotalCount 1 -ErrorAction SilentlyContinue }
        else { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    })
}

# Opens a file for the lock, or $null when another holder's sharing does not allow it.
function Open-HarnessFile([string]$Path, [IO.FileAccess]$Access, [IO.FileShare]$Share) {
    try { [IO.File]::Open($Path, 'OpenOrCreate', $Access, $Share) } catch [IO.IOException] { $null }
}

function Enter-HarnessLock([string]$What = '', [ValidateSet('', 'shared', 'exclusive')][string]$Mode = '') {
    if (-not $Mode) { $Mode = if ($env:FAKK2_HARNESS_MODE -eq 'exclusive') { 'exclusive' } else { 'shared' } }
    if ($global:OpenFakk2HarnessLock) {
        if ($Mode -eq 'exclusive' -and $global:OpenFakk2HarnessLock.Mode -ne 'exclusive') { throw 'harness: a nested runner asked for the lock alone while its caller shares it; take it exclusive in the caller' }
        $global:OpenFakk2HarnessLock.Depth++; return
    }
    $waitSecs = if ($env:FAKK2_HARNESS_WAIT) { [int]$env:FAKK2_HARNESS_WAIT } else { 3600 }
    $slots = if ($env:FAKK2_HARNESS_SLOTS) { [Math]::Max(1, [int]$env:FAKK2_HARNESS_SLOTS) } else { 4 }
    $deadline = (Get-Date).AddSeconds($waitSecs)
    $lastWhy = ''
    New-Item -ItemType Directory -Force $HarnessQueue, $HarnessSlots, $HarnessHolders | Out-Null
    # a ticket named by the time it asked and the mode: the queue's order
    $ticket = Join-Path $HarnessQueue ('{0:D20}-{1}-{2}' -f [DateTime]::UtcNow.Ticks, $Mode, $PID)
    Set-Content -Path $ticket -Value "pid $PID ($Mode): $What ($RepoRoot)" -Encoding utf8
    try {
        while ($true) {
            $why = ''
            # the tickets ahead: a shared run waits only for exclusive ones, an exclusive one for all
            $ahead = $null
            foreach ($t in @(Get-ChildItem $HarnessQueue -File | Sort-Object Name)) {
                if ($t.FullName -eq $ticket) { break }
                $tpid = [int](($t.Name -split '-')[-1])
                if (-not (Get-Process -Id $tpid -ErrorAction SilentlyContinue)) { Remove-Item $t.FullName -Force -ErrorAction SilentlyContinue; continue }
                # a ticket from a runner older than shared runs has no mode: it takes the lock alone
                if ($Mode -eq 'exclusive' -or $t.Name -notmatch '-shared-') { $ahead = $t; break }
            }
            if ($ahead) { $why = "waiting in line behind $(Get-Content $ahead.FullName -ErrorAction SilentlyContinue | Select-Object -First 1)" }
            else {
                $taken = @()
                try {
                    $wt = Open-HarnessFile $HarnessWorktreeLock 'ReadWrite' 'None'
                    if (-not $wt) { $why = 'waiting for this worktree''s other game (its games share the test home)' }
                    else {
                        $taken += $wt
                        $main = if ($Mode -eq 'exclusive') { Open-HarnessFile $HarnessLockFile 'ReadWrite' 'None' } else { Open-HarnessFile $HarnessLockFile 'Read' 'Read' }
                        if (-not $main) {
                            $held = @(Get-HarnessHolders)
                            $who = Get-Content "$HarnessLockFile.owner" -TotalCount 1 -ErrorAction SilentlyContinue
                            $why = if ($held.Count) { "waiting for the harness, in use by: $($held -join '; ')" } elseif ($who) { "waiting for the harness lock, held by $who" } else { 'waiting for the harness lock' }
                        }
                        else {
                            $taken += $main
                            $slot = $null
                            if ($Mode -eq 'shared') {
                                for ($i = 0; $i -lt $slots -and -not $slot; $i++) { $slot = Open-HarnessFile (Join-Path $HarnessSlots "slot$i") 'ReadWrite' 'None' }
                                if (-not $slot) { $why = "waiting for one of the $slots shared places: $((Get-HarnessHolders) -join '; ')" }
                                else { $taken += $slot }
                            }
                            if (-not $why) {
                                $games = @(Get-FakkGames)
                                $player = @($games | Where-Object { -not $_.Harness })
                                if ($player.Count) {
                                    throw ("The player's own game is running ({0}, pid {1}, {2}). The harness does not start a game beside it: close it and run this again." -f $player[0].Name, $player[0].Id, $player[0].Path)
                                }
                                # a harness game whose runner has gone (killed before it could stop the game,
                                # by a runner older than the job object) is stopped, or later runs would wait on it
                                foreach ($o in @($games | Where-Object Orphan)) {
                                    Write-Host "harness: stopping $($o.Name) (pid $($o.Id)), left running by a harness run that has ended"
                                    Stop-Process -Id $o.Id -Force -ErrorAction SilentlyContinue
                                }
                                $busy = @($games | Where-Object { -not $_.Orphan })
                                if ($Mode -eq 'exclusive' -and $busy.Count) {
                                    $why = "waiting for a game another harness run started without the lock: $($busy[0].Name) (pid $($busy[0].Id))"
                                }
                                else {
                                    $line = "pid $PID ($Mode) since $(Get-Date -Format s): $What ($RepoRoot)"
                                    Set-Content -Path (Join-Path $HarnessHolders "$PID.txt") -Value $line -Encoding utf8
                                    if ($Mode -eq 'exclusive') { Set-Content -Path "$HarnessLockFile.owner" -Value $line -Encoding utf8 }
                                    $global:OpenFakk2HarnessLock = @{ Streams = $taken; Mode = $Mode; Depth = 1 }
                                    $taken = @()
                                    return
                                }
                            }
                        }
                    }
                }
                finally { foreach ($s in $taken) { $s.Dispose() } }
            }
            if ((Get-Date) -gt $deadline) { throw "harness: gave up after ${waitSecs}s $why" }
            if ($why -ne $lastWhy) { Write-Host "harness: $why"; $lastWhy = $why }
            Start-Sleep -Milliseconds 1000
        }
    }
    finally { Remove-Item $ticket -Force -ErrorAction SilentlyContinue }
}

function Exit-HarnessLock {
    $lock = $global:OpenFakk2HarnessLock
    if (-not $lock) { return }
    $lock.Depth--
    if ($lock.Depth -gt 0) { return }
    Remove-Item (Join-Path $HarnessHolders "$PID.txt") -ErrorAction SilentlyContinue
    if ($lock.Mode -eq 'exclusive') { Remove-Item "$HarnessLockFile.owner" -ErrorAction SilentlyContinue }
    foreach ($s in $lock.Streams) { $s.Dispose() }
    $global:OpenFakk2HarnessLock = $null
}

# Starts one game for a runner: takes the lock, resets the home (the test home for ours, the
# sandbox's config.cfg for retail), puts the no-network settings first on the command line
# and puts the process in the kill-on-exit job. Stop it with Stop-HarnessGame in a finally,
# which kills it if it is still running and lets the lock go.
function Start-HarnessGame {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('ours', 'retail')][string]$Side,
        [Parameter(Mandatory = $true)][string]$Exe,
        [string]$WorkDir = '',
        [string]$Arguments = '',
        [string]$What = '',
        [string]$Stdout = '',
        [string]$Stderr = '',
        [string]$WindowStyle = '',
        [switch]$NoNewWindow
    )
    Enter-HarnessLock $(if ($What) { $What } else { "$Side $(Split-Path $Exe -Leaf)" })
    try {
        if (-not $WorkDir) { $WorkDir = Split-Path $Exe }
        if ($Side -eq 'ours') { Reset-TestHome; $Arguments = "$OursNoNet $Arguments" }
        else { Reset-RetailConfig $WorkDir; $Arguments = "$RetailNoNet $Arguments" }
        $sp = @{ FilePath = $Exe; WorkingDirectory = $WorkDir; ArgumentList = $Arguments; PassThru = $true }
        if ($Stdout) { $sp.RedirectStandardOutput = $Stdout }
        if ($Stderr) { $sp.RedirectStandardError = $Stderr }
        if ($NoNewWindow) { $sp.NoNewWindow = $true } elseif ($WindowStyle) { $sp.WindowStyle = $WindowStyle }
        $p = Start-Process @sp
        if (-not [OpenFakk2Harness.KillJob]::Add($p.Handle)) { Write-Warning "harness: could not tie $($p.Id) to this shell; a run cut short may leave it running" }
        $p
    }
    catch { Exit-HarnessLock; throw }
}

function Stop-HarnessGame($Process) {
    try {
        if ($Process -and -not $Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
    }
    finally { Exit-HarnessLock }
}

# Ritual's example maps (fakk\maps\example in the tools release: one small map per feature)
# as test levels, named example/<map> on the command line. Copies the compiled maps, their
# level scripts and path files into both games' sandboxes' fakk\maps\example: the retail
# game's and ours' test home, which ours searches before the install. Never into the
# repository (see $RitualTools above). Files already there with the same size and time are
# left alone.
function Install-ExampleMaps {
    # the maps are in the repository (tools\retail-compare\maps\example, from Ritual's
    # fakk2-sdk release); the .bsp, .scr and .pth files are what the games load
    $source = Join-Path $RepoRoot 'tools\retail-compare\maps\example'
    if (-not (Test-Path $source)) { throw "no example maps at $source" }
    $files = Get-ChildItem $source -File | Where-Object { $_.Extension -in '.bsp', '.scr', '.pth' }
    foreach ($game in 'retail', 'test-home') {
        $dest = Join-Path $Sandbox "$game\fakk\maps\example"
        New-Item -ItemType Directory -Force $dest | Out-Null
        $copied = 0
        foreach ($f in $files) {
            $target = Join-Path $dest $f.Name
            $old = Get-Item $target -ErrorAction SilentlyContinue
            if ($old -and $old.Length -eq $f.Length -and $old.LastWriteTimeUtc -eq $f.LastWriteTimeUtc) { continue }
            Copy-Item $f.FullName $target -Force
            $copied++
        }
        "$game`: $copied of $($files.Count) files copied to $dest"
    }
}

# A map name such as example/rain, made safe for a log or folder name.
function Get-MapTag([string]$Map) { $Map -replace '[\\/]', '-' }

# The folder a probe has to be in for a map. "+script thread <probe>.scr::probe" names the
# script without a path, and the game looks for such a script in the map's own folder
# (ScriptLibrarian::GetScript), so a probe on example/rain goes in fakk\maps\example.
function Get-ProbeDir([string]$GameHome, [string]$Map) {
    $dir = Join-Path $GameHome 'fakk\maps'
    $sub = Split-Path ($Map -replace '/', '\') -Parent
    if ($sub) { $dir = Join-Path $dir $sub }
    # a run on an example map installs the maps into both sandboxes first (a copy is
    # skipped when the file is already there), so a fresh worktree needs no step of its own
    if ($Map -match '^example[\\/]') { Install-ExampleMaps | Out-Null }
    $dir
}

# Older runners started ours on the play home with "+set in_mouse 0", which the engine
# archives, so the player's next game had no mouse. No runner uses the play home now; this
# takes such a line out of its config.cfg by hand, should one still be there.
function Clear-HarnessMouse {
    $config = Join-Path $PlayHome 'fakk\config.cfg'
    if (Test-Path $config) {
        $lines = Get-Content $config
        $kept = $lines | Where-Object { $_ -notmatch '^\s*seta\s+in_mouse\s' }
        if ($kept.Count -ne $lines.Count) { Set-Content -Path $config -Value $kept -Encoding ascii }
    }
}
