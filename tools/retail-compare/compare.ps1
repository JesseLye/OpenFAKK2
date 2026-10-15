param([string]$Map = 'zoo', [int]$Secs = 30, [int]$Show = 30, [string]$After = '', [string]$Tag = '', [int]$RetailSecs = 40, [int]$MinSecs = 0)
# Runs OpenFAKK2 on a map and compares its game-side log against the saved retail log for that map.
. (Join-Path $PSScriptRoot 'common.ps1')
$sp = $LogDir
$retail = "$sp\ded-retail-$(Get-MapTag $Map)$Tag.log"
if (-not (Test-Path $retail)) {
    & "$PSScriptRoot\run-retail.ps1" -Name retail -Map $Map -Secs $RetailSecs -After $After | Out-Null
    Copy-Item "$sp\ded-retail.log" $retail -Force
}
& "$PSScriptRoot\run-openfakk2.ps1" -Map $Map -Secs $Secs -After $After -MinSecs $MinSecs | Out-Null

. (Join-Path $PSScriptRoot 'compare-lib.ps1')
$a = Get-GameLines $retail
$b = Get-GameLines "$sp\of-$(Get-MapTag $Map).log"
$d = Compare-Object $a $b
$onlyRetail = @($d | Where-Object SideIndicator -eq '<=')
$onlyOurs = @($d | Where-Object SideIndicator -eq '=>')
"[$Map] retail $($a.Count) lines, ours $($b.Count) lines; only in retail: $($onlyRetail.Count), only in ours: $($onlyOurs.Count)"
$onlyRetail | Select-Object -First $Show | ForEach-Object { "  retail: $($_.InputObject)" }
$onlyOurs | Group-Object { $_.InputObject -replace '\d+(\.\d+)?', '#' -replace "'[^']*'", "'..'" } | Sort-Object Count -Descending | Select-Object -First $Show | ForEach-Object { "  ours x$($_.Count): $($_.Group[0].InputObject)" }
