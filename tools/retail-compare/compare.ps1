param([string]$Map = 'zoo', [int]$Secs = 30, [int]$Show = 30, [string]$After = '', [string]$Tag = '', [int]$RetailSecs = 40, [int]$MinSecs = 0)
# Runs OpenFAKK2 on a map and compares its game-side log against the saved retail log for that map.
. (Join-Path $PSScriptRoot 'common.ps1')
$sp = $LogDir
$retail = "$sp\ded-retail-$Map$Tag.log"
if (-not (Test-Path $retail)) {
    & "$PSScriptRoot\run-retail.ps1" -Name retail -Map $Map -Secs $RetailSecs -After $After | Out-Null
    Copy-Item "$sp\ded-retail.log" $retail -Force
}
& "$PSScriptRoot\run-openfakk2.ps1" -Map $Map -Secs $Secs -After $After -MinSecs $MinSecs | Out-Null

# keep only what the game module and map spawn print: everything after the server banner
function GameLines($file) {
    $l = Get-Content $file | Where-Object { $_ -match '\S' }
    $start = ($l | Select-String '^Server: ' | Select-Object -First 1).LineNumber
    if (-not $start) { return @() }
    $l[$start..($l.Count - 1)] | ForEach-Object { $_.TrimEnd() } | Where-Object {
        $_ -notmatch '^(Hunk_Clear|LoadLibrary|----- FS_Startup|We are looking|Current search path|C:\\|handle \d|-+$|\d+ files in pk3|Hitch warning|Warning: cvar|[^ ]+\.tik: dropping animation|WARNING: CM_|SV_ArchivePersistantFile|Com_WipeSaveGame|Working directory)'
    }
}
$a = GameLines $retail
$b = GameLines "$sp\of-$Map.log"
$d = Compare-Object $a $b
$onlyRetail = @($d | Where-Object SideIndicator -eq '<=')
$onlyOurs = @($d | Where-Object SideIndicator -eq '=>')
"[$Map] retail $($a.Count) lines, ours $($b.Count) lines; only in retail: $($onlyRetail.Count), only in ours: $($onlyOurs.Count)"
$onlyRetail | Select-Object -First $Show | ForEach-Object { "  retail: $($_.InputObject)" }
$onlyOurs | Group-Object { $_.InputObject -replace '\d+(\.\d+)?', '#' -replace "'[^']*'", "'..'" } | Sort-Object Count -Descending | Select-Object -First $Show | ForEach-Object { "  ours x$($_.Count): $($_.Group[0].InputObject)" }
