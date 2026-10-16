param(
    [string[]]$Only = @(),
    [ValidateSet('quick', 'standard', 'full')][string]$Level = 'standard',
    [switch]$Missing,
    [switch]$Twice,
    [switch]$Sound,
    [switch]$List
)
# Runs the retail game for the suite's checks and keeps what it gives as their references,
# in build\suite\reference\retail\<check> in the main checkout (shared by every worktree,
# never committed: they come from Ritual's data). Routine suite runs never start retail;
# refresh a check's reference when its probe changes (run-suite.ps1 reports it as stale).
# Needs the game disc in a drive, and puts the retail game's windows on screen.
#
#   -Only     these checks, by name or group (wildcards allowed); default every check up to
#             -Level (standard) that has a retail side
#   -Missing  only the checks with no reference, or one made from an older probe
#   -Twice    run retail a second time and record how far it differs from itself (the game
#             seeds its random numbers from the clock) in noise.json; a check matches retail
#             when ours is within that spread
#   -Sound    include the sound checks (they play out loud)
. (Join-Path $PSScriptRoot 'suite-lib.ps1')

$config = Read-SuiteConfig
$rank = @{ quick = 0; standard = 1; full = 2 }
$checks = @(Get-SuiteChecks $config | Where-Object { Test-HasRetail $_ })
$checks = @($checks | Where-Object {
    $c = $_
    if ((Get-Prop $c 'sound' $false) -and -not $Sound) { return $false }
    if ($Only.Count) { return [bool]@($Only | Where-Object { $c.name -like $_ -or $c.group -like $_ }).Count }
    (Get-Prop $c 'always' $false) -or $rank[$c.level] -le $rank[$Level]
})
if ($Missing) {
    $checks = @($checks | Where-Object {
        $meta = Join-Path (Get-RefDir $RetailRefs $_.name) 'meta.json'
        if (-not (Test-Path $meta)) { return $true }
        $hash = Get-CheckInputHash $_
        $hash -and (Get-Content $meta -Raw | ConvertFrom-Json).input -ne $hash
    })
}
if ($List) { "$($checks.Count) retail references to make"; $checks | ForEach-Object { "  $($_.name)" }; return }

$disc = @(Get-CimInstance Win32_CDROMDrive -ErrorAction SilentlyContinue | Where-Object MediaLoaded)
if (-not $disc.Count) { Write-Warning 'no disc in a drive: the retail game will refuse to start without the FAKK2 disc' }

$noiseFile = Join-Path $SuiteShared 'reference\noise.json'
$noiseAll = @{}
if (Test-Path $noiseFile) { $j = Get-Content $noiseFile -Raw | ConvertFrom-Json; foreach ($p in $j.PSObject.Properties) { $noiseAll[$p.Name] = $p.Value } }

$i = 0
foreach ($c in $checks) {
    $i++
    $ref = Get-RefDir $RetailRefs $c.name
    Write-Host ("[{0}/{1}] {2}" -f $i, $checks.Count, $c.name) -NoNewline
    $t0 = Get-Date
    try {
        $tmp = "$ref.new"
        Invoke-CheckSide $c 'retail' $tmp | Out-Null
        $made = @(Get-ChildItem $tmp -File | Where-Object { $_.Name -ne 'output.txt' })
        if (-not $made.Count) { Write-Host " nothing made; see $tmp\output.txt"; continue }
        if (Test-Path $ref) { Remove-Item $ref -Recurse -Force }
        Move-Item $tmp $ref
        [ordered]@{ input = (Get-CheckInputHash $c); date = (Get-Date -Format s); files = @(Get-ChildItem $ref -File | ForEach-Object Name) } | ConvertTo-Json | Set-Content (Join-Path $ref 'meta.json') -Encoding utf8
        $note = ''
        if ($Twice) {
            $second = "$ref.twice"
            Invoke-CheckSide $c 'retail' $second | Out-Null
            $m = Measure-Check $c $second $ref (Join-Path $second 'evidence')
            $entry = if ($noiseAll[$c.name]) { $noiseAll[$c.name] } else { [pscustomobject]@{} }
            $entry | Add-Member -NotePropertyName retail -NotePropertyValue ([pscustomobject]$m.metrics) -Force
            $noiseAll[$c.name] = $entry
            $note = '; retail against itself: ' + (($m.metrics.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', ')
            Remove-Item $second -Recurse -Force
        }
        Write-Host (" ok ({0}s){1}" -f [int]((Get-Date) - $t0).TotalSeconds, $note)
    }
    catch { Write-Host " failed: $($_.Exception.Message)" }
}
if ($Twice) { $noiseAll | ConvertTo-Json -Depth 5 | Set-Content $noiseFile -Encoding utf8 }
# ours' last good results were measured against the old references
if ($checks.Count) { "Remade references: make ours' last good results again on main, tools\suite\run-suite.ps1 -NoBuild -UpdateBaseline -Only $(($checks | ForEach-Object name) -join ', ')" }
