# Shared settings for the retail comparison scripts.
# Override the install location with the FAKK2_INSTALL environment variable.
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$GameInstall = if ($env:FAKK2_INSTALL) { $env:FAKK2_INSTALL } else { 'C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2' }
# Ritual's "Heavy Metal FAKK 2 Tools" 1.02 release (a clone of github.com/Sporesirius/fakk2):
# design documents in docs\, tool sources in source\utils, example maps in fakk\maps\example,
# the tools in bin\. Override with FAKK2_TOOLS. It stays outside the repository: Ritual's
# licence (LICENSE-re.MD there) does not allow copying it, and bin\ holds Miles files that
# are not redistributable. Read from it; never commit anything taken from it.
$RitualTools = if ($env:FAKK2_TOOLS) { $env:FAKK2_TOOLS } else { 'C:\Users\Chse\Desktop\git-repos\fakk2' }
if (-not (Test-Path (Join-Path $RitualTools 'readme-re.txt'))) {
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
}

# The retail sandboxes (build\sandbox\retail and retail-snd) keep their config.cfg between
# runs, and retail archives what a run sets (in_mouse 0, developer, r_mode, fps). Each
# retail run starts from the install's default.cfg and an empty config.cfg instead.
function Reset-RetailConfig([string]$GameDir) {
    $fakk = Join-Path $GameDir 'fakk'
    if (-not (Test-Path $fakk)) { return }
    Copy-Item (Join-Path $GameInstall 'fakk\default.cfg') (Join-Path $fakk 'default.cfg') -Force
    Set-Content -Path (Join-Path $fakk 'config.cfg') -Value '// empty on purpose: harness runs start from default.cfg (Reset-RetailConfig)' -Encoding ascii
}

# --- one game at a time -------------------------------------------------------------
# Every runner takes the harness lock before it starts a game and lets it go once the game
# has stopped, so sessions in several worktrees take turns. The lock is a file in the main
# checkout's build\ held open with no sharing: a second opener fails until the holder
# closes it, and Windows closes it when the holder's process ends, however it ends. The
# holder writes who it is to harness.lock.owner. Nested runners (compare.ps1 calling
# run-retail.ps1) share the lock of the process that took it.
#
# A game started by Start-HarnessGame is put in a job object that kills it when this
# PowerShell process ends, so a run cut short (Ctrl+C, a pipeline stopped early, a killed
# shell) leaves no game behind, and none that writes into a home after it has gone.
$HarnessLockFile = Join-Path $MainRoot 'build\harness.lock'
$HarnessQueue = "$HarnessLockFile.queue"
New-Item -ItemType Directory -Force (Split-Path $HarnessLockFile) | Out-Null

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

function Enter-HarnessLock([string]$What = '') {
    if ($global:OpenFakk2HarnessLock) { $global:OpenFakk2HarnessLock.Depth++; return }
    $waitSecs = if ($env:FAKK2_HARNESS_WAIT) { [int]$env:FAKK2_HARNESS_WAIT } else { 3600 }
    $deadline = (Get-Date).AddSeconds($waitSecs)
    $owner = "$HarnessLockFile.owner"
    $lastWhy = ''
    # Runners queue for the lock in the order they asked: each leaves a ticket named by the
    # time it asked, and only the oldest ticket of a live process may take the lock. A run
    # that takes the lock game after game (refresh-retail.ps1) would otherwise take it back
    # the moment it let go, before a waiter polling every second could see it free.
    New-Item -ItemType Directory -Force $HarnessQueue | Out-Null
    $ticket = Join-Path $HarnessQueue ('{0:D20}-{1}' -f [DateTime]::UtcNow.Ticks, $PID)
    Set-Content -Path $ticket -Value "pid $PID`: $What ($RepoRoot)" -Encoding utf8
    try {
        while ($true) {
            $first = $null
            foreach ($t in @(Get-ChildItem $HarnessQueue -File | Sort-Object Name)) {
                $tpid = [int]($t.Name -replace '^\d+-', '')
                if (-not (Get-Process -Id $tpid -ErrorAction SilentlyContinue)) { Remove-Item $t.FullName -Force -ErrorAction SilentlyContinue; continue }
                $first = $t; break
            }
            $why = ''
            if ($first -and $first.FullName -ne $ticket) {
                $why = "waiting in line behind $(Get-Content $first.FullName -ErrorAction SilentlyContinue | Select-Object -First 1)"
            }
            else {
                $stream = $null
                try { $stream = [IO.File]::Open($HarnessLockFile, 'OpenOrCreate', 'ReadWrite', 'None') } catch [IO.IOException] { }
                if ($stream) {
                    $games = @(Get-FakkGames)
                    $player = @($games | Where-Object { -not $_.Harness })
                    if ($player.Count) {
                        $stream.Dispose()
                        throw ("The player's own game is running ({0}, pid {1}, {2}). The harness does not start a game beside it: close it and run this again." -f $player[0].Name, $player[0].Id, $player[0].Path)
                    }
                    # a harness game whose runner has gone (killed before it could stop the game,
                    # by a runner older than the job object) is stopped, or every later run would wait on it
                    foreach ($o in @($games | Where-Object Orphan)) {
                        Write-Host "harness: stopping $($o.Name) (pid $($o.Id)), left running by a harness run that has ended"
                        Stop-Process -Id $o.Id -Force -ErrorAction SilentlyContinue
                    }
                    $busy = @($games | Where-Object { -not $_.Orphan })
                    if (-not $busy.Count) {
                        $global:OpenFakk2HarnessLock = @{ Stream = $stream; Depth = 1 }
                        Set-Content -Path $owner -Value "pid $PID since $(Get-Date -Format s): $What ($RepoRoot)" -Encoding utf8
                        return
                    }
                    $stream.Dispose()
                    $why = "waiting for a game another harness run started without the lock: $($busy[0].Name) (pid $($busy[0].Id))"
                }
                else {
                    $who = Get-Content $owner -ErrorAction SilentlyContinue | Select-Object -First 1
                    $why = "waiting for the harness lock, held by $(if ($who) { $who } else { 'a run that is starting' })"
                }
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
    Remove-Item "$HarnessLockFile.owner" -ErrorAction SilentlyContinue
    $lock.Stream.Dispose()
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
    $source = Join-Path $RitualTools 'fakk\maps\example'
    if (-not (Test-Path $source)) { throw "no example maps at $source; set FAKK2_TOOLS to a clone of Ritual's tools release" }
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
