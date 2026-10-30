# How the runners compare two logs: compare.ps1's spawn output, compare-player.ps1's and
# save-load.ps1's level variables, pending events and "whatis" readings. Dot-source it;
# tools\suite uses the same functions, so a check reads a log as the runner does.

# The lines the game module and the map spawn print: everything after the server banner,
# without the engine's own lines (file system, library loading, hitches, the collision
# model's warnings), which differ between the engines for reasons of their own. Retail
# run from a worktree's deep sandbox also prints "Com_sprintf: overflow of 128 in 128"
# (a path longer than its 128-character buffer), which a run from the main checkout does not.
# The event system's "N total memory in response list" counts pointers, so the x64 build
# prints twice retail's figure for the same table; the figure is dropped from the line.
function Get-GameLines([string]$File) {
    if (-not (Test-Path $File)) { return @() }
    $l = @(Get-Content $File | Where-Object { $_ -match '\S' })
    $start = ($l | Select-String '^Server: ' | Select-Object -First 1).LineNumber
    if (-not $start) { return @() }
    $l[$start..($l.Count - 1)] | ForEach-Object { $_.TrimEnd() -replace '\d+ total memory in response list', 'N total memory in response list' } | Where-Object {
        $_ -notmatch '^(Hunk_Clear|LoadLibrary|----- FS_Startup|We are looking|Current search path|C:\\|handle \d|-+$|\d+ files in pk3|Hitch warning|Warning: cvar|[^ ]+\.tik: dropping animation|WARNING: CM_|SV_ArchivePersistantFile|Com_WipeSaveGame|Working directory|Com_sprintf: overflow)'
    }
}

# The lines from the last "Level Variables" heading on: what a probe's levelvars and
# pendingevents print at its end.
function Get-Dump([string]$File) {
    if (-not (Test-Path $File)) { return @() }
    $lines = @(Get-Content $File)
    $start = ($lines | Select-String '^Level Variables' | Select-Object -Last 1).LineNumber
    if (-not $start) { return @() }
    $lines[$start..($lines.Count - 1)]
}

# pending events counted by kind ("<class> <event>")
function Get-EventKinds($Lines) {
    $kinds = @{}
    $Lines | Select-String '^\s*[\d.]+:\d+: (\w+) \(\*\d+\)(?: ''[^'']*'')? : \w+ :(\w+)' | ForEach-Object {
        $k = "$($_.Matches[0].Groups[1].Value) $($_.Matches[0].Groups[2].Value)"; $kinds[$k] = 1 + [int]$kinds[$k]
    }
    $kinds
}

# "whatis" readings in order of appearance: a list of ordered field tables, from
# "Entity # : n" to "Edict Owner". A client's log carries each print twice, once inside a
# "serverCommand: ... print" line (whose text runs on over the next lines); that copy is
# skipped up to the next reading.
function Get-Readings([string]$File) {
    $blocks = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path $File)) { return ,$blocks }
    $cur = $null; $skipping = $false
    foreach ($line in Get-Content $File) {
        if ($line -match 'Entity #\s*: (\d+)\s*$') {
            $number = $Matches[1]
            if ($line -match '^serverCommand:') { $cur = $null; $skipping = $true; continue }
            if (-not $line.StartsWith('Entity #')) { continue }
            $skipping = $false
            $cur = [ordered]@{ 'Entity #' = $number }
            $blocks.Add($cur)
            continue
        }
        if ($skipping) { continue }
        if ($cur -and $line -match '^(\w[\w ]*?)\s*: (.*)$') {
            $cur[$Matches[1].Trim()] = $Matches[2].Trim()
            if ($Matches[1].Trim() -eq 'Edict Owner') { $cur = $null }
        }
        elseif ($cur -and $line -notmatch '^\s*$') { $cur = $null }
    }
    return ,$blocks
}

# equal when the text is the same, or every number in it is within the tolerance
function Test-SameValue([string]$X, [string]$Y, [double]$Tolerance) {
    if ($X -eq $Y) { return $true }
    $nx = [regex]::Matches($X, '-?\d+(\.\d+)?'); $ny = [regex]::Matches($Y, '-?\d+(\.\d+)?')
    if ($nx.Count -ne $ny.Count -or $nx.Count -eq 0) { return $false }
    if (([regex]::Replace($X, '-?\d+(\.\d+)?', '#')) -ne ([regex]::Replace($Y, '-?\d+(\.\d+)?', '#'))) { return $false }
    for ($i = 0; $i -lt $nx.Count; $i++) { if ([Math]::Abs([double]$nx[$i].Value - [double]$ny[$i].Value) -gt $Tolerance) { return $false } }
    return $true
}

# Compares two lists of readings in order, field by field (only -Fields when given).
# Labels name the two sides in the examples ("retail"/"ours", "saved"/"loaded").
function Compare-Readings($A, $B, [double]$Tolerance, [int]$Show = 12, [string[]]$Fields = @(), [string[]]$Labels = @('retail', 'ours')) {
    $n = [Math]::Min($A.Count, $B.Count)
    $same = 0; $fieldCounts = @{}; $examples = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $n; $i++) {
        $x = $A[$i]; $y = $B[$i]
        $keys = if ($Fields.Count) { $Fields } else { @($x.Keys) + @($y.Keys) | Select-Object -Unique }
        $bad = @($keys | Where-Object { -not (Test-SameValue ([string]$x[$_]) ([string]$y[$_]) $Tolerance) })
        if (-not $bad.Count) { $same++; continue }
        foreach ($f in $bad) { $fieldCounts[$f] = 1 + [int]$fieldCounts[$f] }
        if ($examples.Count -lt $Show) { $f = $bad[0]; $examples.Add(("  reading {0} (entity {1}, {2}) {3}: {6} {4} | {7} {5}" -f ($i + 1), $x['Entity #'], $x['Classname'], $f, $x[$f], $y[$f], $Labels[0], $Labels[1])) }
    }
    [pscustomobject]@{ CountA = $A.Count; CountB = $B.Count; Compared = $n; Identical = $same; FieldCounts = $fieldCounts; Examples = $examples }
}

function Format-ReadingsLine($R, [string[]]$Labels = @('retail', 'ours')) {
    "  whatis readings: $($R.CountA) $($Labels[0]) / $($R.CountB) $($Labels[1]); identical $($R.Identical) of $($R.Compared)" +
        $(if ($R.FieldCounts.Count) { "; fields differing: " + (($R.FieldCounts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ', ') } else { '' })
}
