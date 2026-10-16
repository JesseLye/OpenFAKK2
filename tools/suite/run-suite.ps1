param(
    [ValidateSet('quick', 'standard', 'full')][string]$Level = 'quick',
    [string[]]$Areas = @(),
    [string[]]$Only = @(),
    [switch]$Changed,
    [switch]$Sound,
    [switch]$NoBuild,
    [switch]$UpdateBaseline,
    [string]$Since = '',
    [switch]$AcceptMoves,
    [switch]$Noise,
    [switch]$List,
    [string]$Bin = ''
)
# Runs the test suite's checks (tools\suite\suite.json) on this checkout's build and
# writes a report to build\suite\runs\<time>\report.md: a summary, then the checks that are
# newly failing with their evidence, then known differences that still differ, then passes.
#
#   -Level     quick (default): the build, the data checks, spawn output on every map, the
#              invariants, and the quick checks of the areas;
#              standard: as quick, plus every standard check of the areas and their neighbours;
#              full: every check (sound ones only with -Sound).
#   -Areas     build, data, server, rendering, interface, saves, sound, invariants (default all);
#   -Changed   the areas from what this branch changed (suite.json "paths"), as verify does;
#   -Only      just these checks, by name or group, wildcards allowed (spawn/zoo, shots/*, chain).
#   -Sound     include the sound checks, which play out loud.
#   -NoBuild   do not build first (the build check is skipped).
#   -UpdateBaseline  keep every check's result as ours' last good one, against which later
#              runs tell "newly broken" from "never matched". Do it on main, on a build you
#              have looked at; after a merge, -AcceptMoves is the careful form.
#   -Since     the revision the changes count from (default: the branch's merge base with
#              main), for -Changed and for the commits' declared moves; on main after a
#              merge, the commit before it (HEAD^1).
#   -AcceptMoves  keep as ours' last good results only what is meant to have moved: the
#              checks the commits since -Since declare (a "Suite-moves:" trailer), the ones
#              that got better, and the ones whose retail reference was made again. A newly
#              failing check nobody declared stays failing and keeps its old result. The
#              merge skill runs this on main after a merge.
#   -Noise     run each check twice and record how far ours differs from itself, per
#              measure, in the references' noise.json; tolerances are at least 1.5 times that.
#   -List      print the selection and stop.
#
# Never starts the retail game: retail's results come from the references that
# refresh-retail.ps1 makes. A check whose reference is missing, or was made from an older
# version of its probe, is skipped and says so.
. (Join-Path $PSScriptRoot 'suite-lib.ps1')
. (Join-Path $PSScriptRoot 'changes.ps1')

$config = Read-SuiteConfig
$all = @(Get-SuiteChecks $config)
$rank = @{ quick = 0; standard = 1; full = 2 }
$allAreas = @($config.areas.PSObject.Properties.Name)

# --- which checks -------------------------------------------------------------------
$changedFiles = @()
if ($Changed) {
    $changedFiles = @(Get-ChangedFiles $RepoRoot @('code/', 'tools/', '.claude/skills/') $Since)
    $Areas = @()
    foreach ($f in $changedFiles) {
        foreach ($rule in $config.paths) {
            if ($f -like $rule.pattern) { $Areas += @(Get-Prop $rule 'areas' @() | Where-Object { $_ }); if (Get-Prop $rule 'checks') { $Only += @($rule.checks) } }
        }
        # a probe that changed brings its own checks
        if ($f -match '^tools/retail-compare/(probe[^/]*)\.scr$') { $p = $Matches[1]; $Only += @($all | Where-Object { (Get-Prop $_ 'probe') -eq $p } | ForEach-Object group) }
    }
    $Areas = @($Areas | Sort-Object -Unique)
    $Only = @($Only | Sort-Object -Unique)
    if (-not $Areas.Count -and -not $Only.Count) { $Areas = @('build') }
}
$areaSet = if ($Areas.Count) { @($Areas) } else { $allAreas }
if ($Level -eq 'standard') { foreach ($a in @($areaSet)) { $areaSet += @(Get-Prop $config.areas $a @()) } }
$areaSet = @($areaSet | Sort-Object -Unique)

$selected = @($all | Where-Object {
    $c = $_
    if ((Get-Prop $c 'sound' $false) -and -not $Sound) { return $false }
    if ($NoBuild -and $c.kind -eq 'build') { return $false }
    if ($Only.Count -and -not $Changed) { return [bool]@($Only | Where-Object { $c.name -like $_ -or $c.group -like $_ }).Count }
    if ($Only.Count -and @($Only | Where-Object { $c.name -like $_ -or $c.group -like $_ }).Count) { return $true }
    if (Get-Prop $c 'always' $false) { return $true }
    if ($Level -eq 'full') { return $true }
    $inArea = @($c.areas | Where-Object { $areaSet -contains $_ }).Count -gt 0
    $inArea -and $rank[$c.level] -le $rank[$Level]
})
if ($List) {
    "$($selected.Count) checks ($Level; areas $($areaSet -join ', '))" + $(if ($changedFiles.Count) { "; from $($changedFiles.Count) changed files" } else { '' })
    $selected | ForEach-Object { "  {0,-28} {1,-9} {2,-8} {3}" -f $_.name, $_.level, $_.kind, ($_.areas -join ',') }
    return
}

# --- run ----------------------------------------------------------------------------
if (-not $Bin) { $Bin = $EngineBin }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $SuiteLocal "runs\$stamp"
New-Item -ItemType Directory -Force $runDir | Out-Null
$branch = (git -C $RepoRoot rev-parse --abbrev-ref HEAD 2>$null)
$commit = (git -C $RepoRoot rev-parse --short HEAD 2>$null)
$changeHash = Get-ChangeHash $RepoRoot
# the checks this branch's commits (or those since -Since) say they move on purpose
$moves = @(Get-DeclaredMoves $RepoRoot $Since)
function Get-Move($c) { $moves | Where-Object { $c.name -like $_.Pattern -or $c.group -like $_.Pattern } | Select-Object -First 1 }
$started = Get-Date
$playConfig = Join-Path $PlayHome 'fakk\config.cfg'
$playHashBefore = if (Test-Path $playConfig) { (Get-FileHash $playConfig -Algorithm SHA1).Hash } else { 'missing' }
$noiseFile = Join-Path $SuiteShared 'reference\noise.json'
$noiseAll = @{}
if (Test-Path $noiseFile) { $j = Get-Content $noiseFile -Raw | ConvertFrom-Json; foreach ($p in $j.PSObject.Properties) { $noiseAll[$p.Name] = $p.Value } }

# The tolerance for one measure: suite.json's "tolerance" (by name, or "*"; a number or
# "N%" of ours' last good value), the kind's default, no less than "toleranceMin" (by
# name or "*"; for a percentage of a value so small that machine load moves it more),
# and at least 1.5 times the noise measured for it (-Noise) or, for the match with
# retail, retail's own spread.
function Get-Tolerance($c, [string]$metric, $base, [string]$which = 'base') {
    $t = 0.0
    if (($c.kind -in 'capture', 'menus') -or $metric -like 'shot*') { $t = 1.0 }
    $cfg = Get-Prop $c 'tolerance'
    $v = Get-Prop $cfg $metric (Get-Prop $cfg '*' $null)
    if ($null -ne $v) {
        if ([string]$v -match '^([\d.]+)%$') { $t = if ($null -ne $base) { [double]$base * [double]$Matches[1] / 100 } else { [double]::MaxValue } }
        else { $t = [double]$v }
    }
    $minCfg = Get-Prop $c 'toleranceMin'
    $min = Get-Prop $minCfg $metric (Get-Prop $minCfg '*' $null)
    if ($null -ne $min) { $t = [Math]::Max($t, [double]$min) }
    $n = Get-Prop (Get-Prop $noiseAll[$c.name] 'ours') $metric $null
    if ($null -ne $n) { $t = [Math]::Max($t, 1.5 * [double]$n) }
    if ($which -eq 'retail') {
        $rn = Get-Prop (Get-Prop $noiseAll[$c.name] 'retail') $metric $null
        if ($null -ne $rn) { $t = [Math]::Max($t, [double]$rn) }
    }
    $t
}

$results = New-Object System.Collections.Generic.List[object]
$buildFailed = $false
$i = 0
foreach ($c in $selected) {
    $i++
    $r = [ordered]@{ name = $c.name; group = $c.group; areas = @($c.areas); kind = $c.kind; level = $c.level; status = ''; reason = ''; metrics = [ordered]@{}; baseline = $null; details = @(); evidence = @(); images = @(); crash = $null; seconds = 0; improved = $false; staleBaseline = $false }
    $t0 = Get-Date
    $checkDir = Join-Path $runDir ("checks\" + ($c.name -replace '/', '\'))
    $oursDir = Join-Path $checkDir 'ours'
    $refDir = Get-RefDir $RetailRefs $c.name
    $baseDir = Get-RefDir $OursRefs $c.name
    Write-Host ("[{0}/{1}] {2}" -f $i, $selected.Count, $c.name) -NoNewline
    try {
        if ($buildFailed -and $c.kind -ne 'build') { $r.status = 'skipped'; $r.reason = 'the build failed'; throw 'skip' }
        $hasRetail = Test-HasRetail $c
        if ($hasRetail) {
            $meta = Join-Path $refDir 'meta.json'
            if (-not (Test-Path $meta)) { $r.status = 'skipped'; $r.reason = "no retail reference (tools\suite\refresh-retail.ps1 -Only $($c.name))"; throw 'skip' }
            $m = Get-Content $meta -Raw | ConvertFrom-Json
            $hash = Get-CheckInputHash $c
            if ($hash -and $m.input -ne $hash) { $r.status = 'skipped'; $r.reason = "the retail reference was made from an older version of $(Get-Prop $c 'probe' 'its probe') (tools\suite\refresh-retail.ps1 -Only $($c.name))"; throw 'skip' }
        }
        # a fresh test home for each check: no saves, screenshots or crash dump from the last
        foreach ($d in 'save', 'screenshots') { Remove-Item (Join-Path $TestFakk $d) -Recurse -Force -ErrorAction SilentlyContinue }
        Remove-Item (Join-Path $TestFakk 'crash.dmp') -Force -ErrorAction SilentlyContinue

        $runs = if ($Noise) { 2 } else { 1 }
        $measured = @()
        for ($k = 0; $k -lt $runs; $k++) {
            $dir = if ($k) { "$oursDir-2" } else { $oursDir }
            if ($k) { foreach ($d in 'save', 'screenshots') { Remove-Item (Join-Path $TestFakk $d) -Recurse -Force -ErrorAction SilentlyContinue } }
            $note = Invoke-CheckSide $c 'ours' $dir $Bin
            $logs = @(Get-ChildItem $dir -File | Where-Object { $_.Extension -eq '.log' -or $_.Name -eq 'output.txt' } | ForEach-Object FullName)
            $crash = @(Find-Crash $logs $Bin)
            if (-not $crash.Count -and (Test-Path (Join-Path $TestFakk 'crash.dmp'))) { $crash = @('the engine wrote crash.dmp (no stack in the log)') }
            if ($crash.Count) {
                $r.crash = $crash; $r.status = 'fail'; $r.reason = "crashed: $($crash[0])"
                if (Test-Path (Join-Path $TestFakk 'crash.dmp')) { Copy-Item (Join-Path $TestFakk 'crash.dmp') $checkDir }
                throw 'crashed'
            }
            if ($c.kind -eq 'build' -and (Get-Content (Join-Path $dir 'exit.txt') -ErrorAction SilentlyContinue) -ne '0') { $buildFailed = $true }
            $measured += , (Measure-Check $c $dir $(if ($hasRetail) { $refDir } else { $null }) (Join-Path $checkDir $(if ($k) { 'evidence-2' } else { 'evidence' })) $(if (Test-Path $baseDir) { $baseDir } else { '' }))
        }
        $ms = $measured[0]
        $r.metrics = $ms.metrics; $r.evidence = @($ms.evidence); $r.images = @($ms.images | ForEach-Object { $_.Substring($runDir.Length + 1) })
        if ($Noise -and $measured.Count -eq 2) {
            $nz = [ordered]@{}
            foreach ($key in $ms.metrics.Keys) { $nz[$key] = [Math]::Abs([double]$ms.metrics[$key] - [double]$measured[1].metrics[$key]) }
            $entry = if ($noiseAll[$c.name]) { $noiseAll[$c.name] } else { [pscustomobject]@{} }
            $entry | Add-Member -NotePropertyName ours -NotePropertyValue ([pscustomobject]$nz) -Force
            $noiseAll[$c.name] = $entry
            $r.details += "noise (two runs of ours): " + (($nz.GetEnumerator() | ForEach-Object { "$($_.Key) $([Math]::Round($_.Value, 2))" }) -join ', ')
        }

        # judge each measure against retail and ours' last good result
        $base = $null
        if (Test-Path (Join-Path $baseDir 'baseline.json')) {
            $base = Get-Content (Join-Path $baseDir 'baseline.json') -Raw | ConvertFrom-Json
            # numbers measured against another retail reference do not compare: judged
            # against retail alone until the baseline is made again
            if ($hasRetail -and (Get-Prop $base 'reference') -and (Get-Prop $base 'reference') -ne (Get-ReferenceStamp $refDir)) {
                $r.details += "the retail reference was made again after ours' last good result: run with -UpdateBaseline on main"
                $r.staleBaseline = $true
                $base = $null
            }
            else { $r.baseline = $base.metrics }
        }
        $worse = @(); $differs = @(); $unknown = @(); $better = @()
        foreach ($key in $ms.metrics.Keys) {
            $v = [double]$ms.metrics[$key]
            $b = Get-Prop (Get-Prop $base 'metrics') $key $null
            $tolBase = Get-Tolerance $c $key $b 'base'
            $tolRetail = Get-Tolerance $c $key $b 'retail'
            if ($null -ne $b -and $v -lt [double]$b - $tolBase) { $better += "$key $v (last good $b)" }
            if ($ms.growth -contains $key) {
                if ($null -ne $b -and $v -gt [double]$b + $tolBase) { $worse += "$key $v (last good $b, tolerance $([Math]::Round($tolBase, 2)))" }
                elseif ($null -eq $b) { $unknown += "$key $v (no last good result)" }
                continue
            }
            if ($v -le $tolRetail) { continue }
            if ($null -eq $b) { $differs += "$key $v (no last good result)"; continue }
            if ($v -gt [double]$b + $tolBase) { $worse += "$key $v (last good $b, tolerance $([Math]::Round($tolBase, 2)))" }
            else { $differs += "$key $v (last good $b)" }
        }
        $r.improved = [bool]($better.Count -and -not $worse.Count)
        if ($better.Count) { $r.details += "better than ours' last good result: " + ($better -join '; ') }
        $move = Get-Move $c
        if ($worse.Count -and $move) { $r.status = 'moved'; $r.reason = "declared moved in $($move.Commit) ($($move.Reason)); worse than ours' last good result: " + ($worse -join '; ') }
        elseif ($worse.Count) { $r.status = 'fail'; $r.reason = "worse than ours' last good result: " + ($worse -join '; ') }
        elseif ($differs.Count) { $r.status = if ($base) { 'known' } else { 'unmatched' }; $r.reason = ($differs -join '; ') }
        else { $r.status = 'pass'; $r.reason = if ($unknown.Count) { $unknown -join '; ' } else { '' } }
    }
    catch {
        if ($_.Exception.Message -notin 'skip', 'crashed') { $r.status = 'error'; $r.reason = "the check itself failed: $($_.Exception.Message)"; $r.details += ($_.ScriptStackTrace -split "`n" | Select-Object -First 4) }
    }
    # a check that suite.json says has a known issue, recorded in known-differences.md and
    # left for its own fix, reports its failure among the known differences
    $known = Get-Prop $c 'knownIssue'
    if ($known -and $r.status -eq 'fail') { $r.status = 'known'; $r.reason = "known issue: $known (this run: $($r.reason))" }
    elseif ($known -and $r.status -eq 'pass') { $r.reason = "passes, so its known issue may be fixed: take knownIssue out of suite.json and known-differences.md" }
    $r.seconds = [int]((Get-Date) - $t0).TotalSeconds
    Write-Host (" {0} ({1}s){2}" -f $r.status, $r.seconds, $(if ($r.status -in 'fail', 'error', 'skipped') { ": $($r.reason)" } else { '' }))
    # what -AcceptMoves keeps: declared moves, improvements, results against a remade reference
    $accept = $AcceptMoves -and ($r.status -eq 'moved' -or $r.improved -or $r.staleBaseline -or ($r.status -eq 'unmatched' -and (Get-Move $c)))
    if (($UpdateBaseline -or $accept) -and $r.status -in 'pass', 'known', 'unmatched', 'fail', 'moved' -and -not $r.crash -and $r.metrics.Count) {
        $r.details += $(if ($UpdateBaseline) { 'kept as ours'' last good result (-UpdateBaseline)' } else { 'kept as ours'' last good result (-AcceptMoves)' })
        New-Item -ItemType Directory -Force $baseDir | Out-Null
        Get-ChildItem $baseDir -File | Remove-Item -Force
        Get-ChildItem $oursDir -File | Where-Object { $_.Extension -in '.tga', '.log', '.json', '.txt' } | Copy-Item -Destination $baseDir -Force
        [ordered]@{ metrics = $r.metrics; commit = $commit; branch = $branch; date = (Get-Date -Format s); reference = $(if (Test-HasRetail $c) { Get-ReferenceStamp $refDir } else { '' })
            why = $(if ($UpdateBaseline) { '-UpdateBaseline' } elseif ($r.status -eq 'moved' -or (Get-Move $c)) { "declared: $((Get-Move $c).Reason) ($((Get-Move $c).Commit))" } elseif ($r.staleBaseline) { 'the retail reference was made again' } else { 'improved' }) } | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $baseDir 'baseline.json') -Encoding utf8
    }
    $results.Add([pscustomobject]$r)
}
if ($Noise) {
    New-Item -ItemType Directory -Force (Split-Path $noiseFile) | Out-Null
    $noiseAll | ConvertTo-Json -Depth 5 | Set-Content $noiseFile -Encoding utf8
}

# the invariant: the player's own config.cfg is as it was
$playHashAfter = if (Test-Path $playConfig) { (Get-FileHash $playConfig -Algorithm SHA1).Hash } else { 'missing' }
$playOk = $playHashBefore -eq $playHashAfter
$results.Add([pscustomobject][ordered]@{ name = 'play-home'; group = 'play-home'; areas = @('invariants'); kind = 'invariant'; level = 'quick'
    status = $(if ($playOk) { 'pass' } else { 'fail' }); reason = $(if ($playOk) { '' } else { "the player's $playConfig changed during the run" })
    metrics = [ordered]@{ changed = $(if ($playOk) { 0 } else { 1 }) }; baseline = $null; details = @(); evidence = @(); images = @(); crash = $null; seconds = 0 })

# --- report -------------------------------------------------------------------------
$took = (Get-Date) - $started
$by = @{}; foreach ($s in 'fail', 'error', 'moved', 'known', 'unmatched', 'pass', 'skipped') { $by[$s] = @($results | Where-Object status -eq $s) }
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Suite run $stamp")
$md.Add('')
$md.Add("$Level on $branch @ $commit" + $(if ($changeHash) { " with changes" } else { '' }) + "; areas $($areaSet -join ', '); took $([int]$took.TotalMinutes) min $($took.Seconds) s.")
$md.Add('')
$md.Add("**$($results.Count) checks: $($by.fail.Count) newly failing, $($by.error.Count) could not run, $($by.moved.Count) moved as declared, $($by.known.Count) differ from retail as before, $($by.unmatched.Count) differ with no last good result, $($by.pass.Count) pass, $($by.skipped.Count) skipped.**")
$md.Add('')
$md.Add("The player's config.cfg: $(if ($playOk) { 'unchanged' } else { '**changed**' }).")
function Add-Section($title, $list, [switch]$Full) {
    if (-not $list.Count) { return }
    $md.Add(''); $md.Add("## $title"); $md.Add('')
    foreach ($x in $list) {
        if ($Full) {
            $md.Add("### $($x.name) ($($x.areas -join ', '))"); $md.Add('')
            $md.Add($x.reason); $md.Add('')
            if ($x.crash) { $md.Add('```'); $x.crash | ForEach-Object { $md.Add($_) }; $md.Add('```'); $md.Add('') }
            if ($x.evidence.Count -or $x.details.Count) { $md.Add('```'); @($x.evidence) + @($x.details) | ForEach-Object { $md.Add([string]$_) }; $md.Add('```'); $md.Add('') }
            foreach ($img in $x.images) { $md.Add("- [$img]($($img -replace '\\', '/'))") }
            $md.Add("- logs: checks/$($x.name)/ours/")
            $md.Add('')
        }
        else { $md.Add("- **$($x.name)**: $($x.reason)" + $(if ($x.evidence.Count) { " - $($x.evidence[0])" } else { '' })) }
    }
}
Add-Section 'Newly failing' $by.fail -Full
Add-Section 'Could not run' $by.error -Full
Add-Section 'Moved as declared (Suite-moves)' $by.moved -Full
$improved = @($results | Where-Object { $_.improved })
if ($improved.Count) { $md.Add(''); $md.Add('## Better than ours'' last good result'); $md.Add(''); foreach ($x in $improved) { $md.Add("- **$($x.name)**: $(@($x.details | Where-Object { $_ -like 'better than*' })[0])") } }
$unused = @($moves | Where-Object { $m = $_; -not @($results | Where-Object { $_.name -like $m.Pattern -or $_.group -like $m.Pattern }).Count })
if ($unused.Count) { $md.Add(''); $md.Add('Declared moves this run did not select: ' + (($unused | ForEach-Object { "$($_.Pattern) ($($_.Commit))" }) -join ', ') + '. Run them with -Only.') }
Add-Section 'Known differences that still differ' $by.known
Add-Section 'Differ from retail, no last good result yet' $by.unmatched
Add-Section 'Skipped' $by.skipped
if ($by.pass.Count) { $md.Add(''); $md.Add('## Passes'); $md.Add(''); $md.Add((($by.pass | ForEach-Object name) -join ', ')) }
$report = Join-Path $runDir 'report.md'
$md | Set-Content $report -Encoding utf8
$results | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runDir 'results.json') -Encoding utf8

# what verify-gate.ps1 -Stamp reads: whether this run passed, for which changes
$passed = -not ($by.fail.Count + $by.error.Count)
[ordered]@{ run = $stamp; level = $Level; passed = $passed; changeHash = $changeHash; commit = $commit; checks = $results.Count } | ConvertTo-Json | Set-Content (Join-Path $SuiteLocal 'last-run.json') -Encoding utf8

''
"$($results.Count) checks: $($by.fail.Count) newly failing, $($by.error.Count) could not run, $($by.moved.Count) moved as declared, $($by.known.Count) known, $($by.unmatched.Count) unmatched, $($by.pass.Count) pass, $($by.skipped.Count) skipped ($([int]$took.TotalMinutes) min)"
foreach ($x in @($by.fail) + @($by.error) + @($by.moved)) { "  $($x.status.ToUpper()) $($x.name): $($x.reason)" }
if ($AcceptMoves) { $kept = @($results | Where-Object { $_.details -match 'AcceptMoves' }); "kept as ours' last good results: $(if ($kept.Count) { ($kept | ForEach-Object name) -join ', ' } else { 'none' })" }
"report: $report"
if (-not $passed) { exit 1 }
exit 0
