# Shared settings for the retail comparison scripts.
# Override the install location with the FAKK2_INSTALL environment variable.
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$GameInstall = if ($env:FAKK2_INSTALL) { $env:FAKK2_INSTALL } else { 'C:\Program Files (x86)\Ritual Entertainment\Heavy Metal - FAKK2' }
$Sandbox = Join-Path $RepoRoot 'build\sandbox'
$EngineBin = Join-Path $RepoRoot 'build\x86-Release\Release'
$LogDir = Join-Path $RepoRoot 'build\compare'
New-Item -ItemType Directory -Force $LogDir | Out-Null
