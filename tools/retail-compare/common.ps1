# Shared settings for the retail comparison scripts.
# Override the install location with the FAKK2_INSTALL environment variable.
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$GameInstall = if ($env:FAKK2_INSTALL) { $env:FAKK2_INSTALL } else { 'C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2' }
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
