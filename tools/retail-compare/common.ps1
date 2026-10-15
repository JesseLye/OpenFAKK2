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

# The OpenFAKK2 runs use the retail install as fs_basepath with this sandbox as
# fs_homepath. The home directory is searched first, so a file here shadows the
# install's. The retail runs use a clean copy of the install (run-retail.ps1 copies
# only the paks, the DLLs and default.cfg), so they never see the autoexec.cfg or
# config.cfg a player keeps in the install. Give the home an empty autoexec.cfg so
# ours does not read the player's either (it set cl_run 1 and hid retail's default).
$homeFakk = Join-Path $Sandbox 'home\fakk'
New-Item -ItemType Directory -Force $homeFakk | Out-Null
$homeAutoexec = Join-Path $homeFakk 'autoexec.cfg'
if (-not (Test-Path $homeAutoexec)) {
    Set-Content -Path $homeAutoexec -Value "// empty on purpose: shadows the install's autoexec.cfg so harness runs use engine defaults" -Encoding ascii
}
# The same for config.cfg, which the client execs and writes back (the dedicated server's
# config_server.cfg is not in the install). Without it a fresh home's first client run
# read the player's config.cfg from the install, ran with its cl_run 1 (retail's sandbox
# walks), and archived every setting in it here. Once written, the client keeps this file.
$homeConfig = Join-Path $homeFakk 'config.cfg'
if (-not (Test-Path $homeConfig)) {
    Set-Content -Path $homeConfig -Value "// empty on purpose: shadows the install's config.cfg so harness runs use engine defaults" -Encoding ascii
}
elseif (Select-String -Path $homeConfig -Pattern '^\s*seta\s+cl_run\s+"?1' -Quiet) {
    Write-Warning "$homeConfig has cl_run 1: the client runs where retail's sandbox walks. Delete that line for movement probes."
}

# Ritual's example maps (fakk\maps\example in the tools release: one small map per feature)
# as test levels, named example/<map> on the command line. Copies the compiled maps, their
# level scripts and path files into both sandboxes' fakk\maps\example: the retail game's
# and ours' home, which ours searches before the install. Never into the repository (see
# $RitualTools above). Files already there with the same size and time are left alone.
function Install-ExampleMaps {
    $source = Join-Path $RitualTools 'fakk\maps\example'
    if (-not (Test-Path $source)) { throw "no example maps at $source; set FAKK2_TOOLS to a clone of Ritual's tools release" }
    $files = Get-ChildItem $source -File | Where-Object { $_.Extension -in '.bsp', '.scr', '.pth' }
    foreach ($game in 'retail', 'home') {
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

# The probe runners start ours with "+set in_mouse 0" so that a stray mouse cannot turn
# the player mid-probe. The engine registers in_mouse as archived, so a run that quits
# writes "seta in_mouse 0" into this home's config.cfg, and the next game played from
# here starts with no mouse. Call this after such a run to take the line out again.
function Clear-HarnessMouse {
    $config = Join-Path $homeFakk 'config.cfg'
    if (Test-Path $config) {
        $lines = Get-Content $config
        $kept = $lines | Where-Object { $_ -notmatch '^\s*seta\s+in_mouse\s' }
        if ($kept.Count -ne $lines.Count) { Set-Content -Path $config -Value $kept -Encoding ascii }
    }
}
