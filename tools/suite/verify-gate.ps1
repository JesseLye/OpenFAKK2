param([ValidateSet('Stop', 'TaskCompleted')][string]$Event = 'Stop', [switch]$Stamp)
# The verify gate, run by the project's Stop and TaskCompleted hooks (.claude/settings.json).
# It never runs a test: hooks time out in seconds. When this branch has changed files under
# code\ or tools\ since the last passing verify run, it asks once to run the verify skill;
# otherwise it says nothing.
#
#   (no switch)  the hook: reads the hook's JSON on stdin. Stop prints a block decision;
#                TaskCompleted writes the reason to stderr and exits 2. Asks at most once
#                for each distinct set of changes (build\suite\gate-asked.txt), and never
#                while a Stop hook is already active, so an agent that has said why it
#                cannot verify can finish.
#   -Stamp       after a passing run-suite.ps1 run: records the changes it verified in
#                build\suite\verified.stamp, if build\suite\last-run.json says the last run
#                passed for exactly the current changes.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
function Get-Prop($Object, [string]$Name, $Default = $null) { if ($null -ne $Object -and $Object.PSObject.Properties[$Name]) { return $Object.$Name }; $Default }
. (Join-Path $PSScriptRoot 'changes.ps1')
$suiteDir = Join-Path $root 'build\suite'
$stampFile = Join-Path $suiteDir 'verified.stamp'

if ($Stamp) {
    $last = Join-Path $suiteDir 'last-run.json'
    if (-not (Test-Path $last)) { 'no suite run to stamp: run tools\suite\run-suite.ps1 first'; exit 1 }
    $run = Get-Content $last -Raw | ConvertFrom-Json
    $hash = Get-ChangeHash $root
    if (-not $run.passed) { "the last suite run ($($run.run)) did not pass; fix or explain its failures and run it again"; exit 1 }
    if ($run.changeHash -ne $hash) { "the code changed after the last suite run ($($run.run)); run it again"; exit 1 }
    New-Item -ItemType Directory -Force $suiteDir | Out-Null
    Set-Content $stampFile $hash -Encoding ascii
    "verified: $(if ($hash) { $hash } else { 'no changes' }) (run $($run.run), $($run.level)$(if (Get-Prop $run 'arch') { ", $($run.arch)" }))"
    exit 0
}

# --- the hook ------------------------------------------------------------------------
$hookInput = $null
try { $text = [Console]::In.ReadToEnd(); if ($text.Trim()) { $hookInput = $text | ConvertFrom-Json } } catch { }
if ($hookInput -and $hookInput.PSObject.Properties['stop_hook_active'] -and $hookInput.stop_hook_active) { exit 0 }

$files = @(Get-ChangedFiles $root)
if (-not $files.Count) { exit 0 }
$hash = Get-ChangeHash $root
if ((Test-Path $stampFile) -and ((Get-Content $stampFile -TotalCount 1) -eq $hash)) { exit 0 }
$asked = Join-Path $suiteDir 'gate-asked.txt'
if ((Test-Path $asked) -and (@(Get-Content $asked) -contains $hash)) { exit 0 }
New-Item -ItemType Directory -Force $suiteDir | Out-Null
Add-Content $asked $hash -Encoding ascii

$names = @($files | ForEach-Object { Split-Path $_ -Leaf } | Select-Object -First 4)
$more = if ($files.Count -gt 4) { " and $($files.Count - 4) more" } else { '' }
$reason = "You changed $($names -join ', ')$more since the last passing verify run. Run the verify skill (quick) before finishing: tools\suite\run-suite.ps1 -Level quick -Changed, then tools\suite\verify-gate.ps1 -Stamp on a pass. If it cannot run (no game disc for a reference, the player's game open), say so and finish; this asks once per set of changes."
if ($Event -eq 'TaskCompleted') { [Console]::Error.WriteLine($reason); exit 2 }
[ordered]@{ decision = 'block'; reason = $reason } | ConvertTo-Json -Compress
exit 0
