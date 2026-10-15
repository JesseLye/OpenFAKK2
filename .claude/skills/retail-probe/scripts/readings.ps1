param(
    [Parameter(Mandatory = $true)][string]$Log,
    [string[]]$Classname = @(),
    [int[]]$Entity = @(),
    [string[]]$Fields = @('Animname', 'Origin', 'Velocity', 'Movetype', 'Solidtype', 'Health', 'Edict Owner'),
    [switch]$All,
    [switch]$List
)
# Prints "whatis" readings from a harness log (retail, headless ours, or the real client).
# Readings are the blocks from "Entity #   : n" to "Edict Owner". The client log carries
# each print twice, once inside a "serverCommand: ... print" line; those are dropped.
#
#   -Classname  print blocks with these classnames (e.g. Projectile, Player, Actor)
#   -Entity     print blocks for these entity numbers
#   -Fields     which fields to show (default: the ones that tell where something is and
#               what it is doing); -All shows every field
#   -List       one line per block (reading index, entity, classname, origin) for the whole log
# With no -Classname and no -Entity, behaves as -List.

if (-not (Test-Path $Log)) { throw "no such log: $Log" }

$blocks = New-Object System.Collections.Generic.List[object]
$cur = $null
foreach ($line in Get-Content $Log) {
    if ($line -match 'Entity #\s*: (\d+)\s*$') {
        if ($line -match '^serverCommand:') { $cur = $null; $skipping = $true; continue }
        $skipping = $false
        $cur = [ordered]@{ 'Entity #' = $Matches[1] }
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

if (-not $blocks.Count) { "no whatis readings in $Log"; return }

$selectAll = $List -or (-not $Classname.Count -and -not $Entity.Count)
$i = 0
foreach ($b in $blocks) {
    $i++
    $num = [int]$b['Entity #']
    $cls = $b['Classname']
    $wanted = $selectAll -or ($Entity -contains $num) -or ($Classname -contains $cls)
    if (-not $wanted) { continue }
    if ($selectAll) {
        "{0,4}  entity {1,4}  {2,-16} {3,-28} {4}" -f $i, $num, $cls, $b['Modelname'], $b['Origin']
        continue
    }
    "--- reading $i : entity $num $cls $($b['Modelname'])"
    $show = if ($All) { $b.Keys } else { $Fields }
    foreach ($f in $show) { if ($b.Contains($f)) { "  {0,-12} {1}" -f $f, $b[$f] } }
}
