param(
    [ValidateSet('ours', 'retail')][string]$Side = 'ours',
    [string]$Bin = '',
    [int]$Secs = 900,
    [int]$StallSecs = 90,
    [string]$Out = ''
)
# The level chain: plays through the whole game by gamemap on the real client (ours, or
# retail with -Side retail) with tools\retail-compare\chain.scr, which waits on each level,
# prints a "==== chain sN <map>" marker, the inventory and the player, and moves on to the
# next level. The run ends at "==== chain end", when no new marker has come for
# -StallSecs, or after -Secs.
#
# Writes the console log and a summary (chain.json) to -Out (default
# build\compare\chain-<side>): for each step, whether the level was reached, whether it
# made its "Starting <level>" autosave, the inventory and the player's health; and the
# step it stalled on, if any. run-suite.ps1 compares ours' summary with retail's.
. (Join-Path $PSScriptRoot '..\retail-compare\common.ps1')

if (-not $Out) { $Out = Join-Path $LogDir "chain-$Side" }
New-Item -ItemType Directory -Force $Out | Out-Null
$probe = Join-Path $PSScriptRoot '..\retail-compare\chain.scr'
$steps = @(Select-String -Path $probe -Pattern '^stuffcmd "echo ==== chain (s\d+) (\S+)"' | ForEach-Object { [pscustomobject]@{ Step = $_.Matches[0].Groups[1].Value; Map = $_.Matches[0].Groups[2].Value } })

if ($Side -eq 'retail') {
    $gameDir = Join-Path $Sandbox 'retail'
    if (-not (Test-Path "$gameDir\fakk2.exe")) { & (Join-Path $PSScriptRoot '..\retail-compare\run-retail.ps1') -Name retail -Map zoo -Secs 5 | Out-Null }
    $exe = Join-Path $gameDir 'fakk2.exe'; $workDir = $gameDir; $fakk = Join-Path $gameDir 'fakk'
    $arguments = '+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 +map intro +script thread chain.scr::s0'
}
else {
    if (-not $Bin) { $Bin = $EngineBin }
    $exe = Join-Path $Bin 'openfakk2.exe'; $workDir = $Bin; $fakk = $TestFakk
    $arguments = "$OursHomeArgs +set r_fullscreen 0 +set r_mode 3 +set in_nograb 1 +set in_mouse 0 +set s_initsound 0 +set logfile 2 +set developer 1 +map intro +script thread chain.scr::s0"
}
New-Item -ItemType Directory -Force (Join-Path $fakk 'maps') | Out-Null
Copy-Item $probe (Join-Path $fakk 'maps\chain.scr') -Force
$console = Join-Path $fakk 'qconsole.log'
Remove-Item $console -ErrorAction SilentlyContinue
$saveDir = Join-Path $fakk 'save'
New-Item -ItemType Directory -Force $saveDir | Out-Null
$savesBefore = @{}
Get-ChildItem $saveDir -File | ForEach-Object { $savesBefore[$_.Name] = $_.LastWriteTimeUtc }

function Read-Shared($file) {
    if (-not (Test-Path $file)) { return '' }
    $fs = [IO.File]::Open($file, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $t = $sr.ReadToEnd(); $sr.Dispose(); $t
}

$p = $null; $state = ''
try {
    $p = Start-HarnessGame -Side $Side -Exe $exe -WorkDir $workDir -Arguments $arguments -WindowStyle Minimized -What "level-chain $Side"
    $deadline = (Get-Date).AddSeconds($Secs)
    $lastCount = -1; $lastChange = Get-Date
    while ($true) {
        Start-Sleep -Seconds 2
        $text = Read-Shared $console
        if ($text -match '==== chain end') { Start-Sleep -Seconds 2; $state = 'finished'; break }
        $count = ([regex]::Matches($text, '(?m)^==== chain s\d+')).Count
        if ($count -ne $lastCount) { $lastCount = $count; $lastChange = Get-Date }
        if ($p.HasExited) { $state = "the game exited (code $($p.ExitCode))"; break }
        if (((Get-Date) - $lastChange).TotalSeconds -gt $StallSecs) { $state = "stalled: no new step for ${StallSecs}s"; break }
        if ((Get-Date) -gt $deadline) { $state = "stopped after ${Secs}s"; break }
    }
}
finally { Stop-HarnessGame $p }

$log = Join-Path $Out 'console.log'
if (Test-Path $console) { Copy-Item $console $log -Force } else { Set-Content $log '' }
$lines = @(Get-Content $log)

# split the log at each "Server: <map>" (the level starting) and each step marker
$result = New-Object System.Collections.Generic.List[object]
$byStep = @{}
$current = $null; $inInventory = $false
$serverMap = ''; $autosaveFor = @{}
foreach ($line in $lines) {
    if ($line -match '^serverCommand:') { continue }
    if ($line -match '^Server: (\S+)') { $serverMap = $Matches[1]; $inInventory = $false; continue }
    if ($line -match '^Saving game\.\.\.(\S+)') { $autosaveFor[$serverMap] = $Matches[1]; continue }
    if ($line -match '^==== chain (s\d+) (\S+)') {
        $current = [ordered]@{ step = $Matches[1]; map = $Matches[2]; reached = $true; inventory = [ordered]@{}; health = '' }
        $byStep[$current.step] = $current; $inInventory = $false; continue
    }
    if ($current -and $line -match "^'Name' : 'Amount'") { $inInventory = $true; continue }
    if ($current -and $inInventory -and $line -match "^'(.*)' : '(.*)'\s*$") { $current.inventory[$Matches[1]] = $Matches[2]; continue }
    $inInventory = $false
    if ($current -and -not $current.health -and $line -match '^Health\s*: (\S+)') { $current.health = $Matches[1] }
}
$stalledAt = $null
foreach ($s in $steps) {
    $r = $byStep[$s.Step]
    if (-not $r) {
        $r = [ordered]@{ step = $s.Step; map = $s.Map; reached = $false; inventory = [ordered]@{}; health = '' }
        if (-not $stalledAt) { $stalledAt = $s }
    }
    $r.autosave = [bool]$autosaveFor[$s.Map]
    $r.loaded = [bool]($lines -match ('^Server: ' + [regex]::Escape($s.Map) + '$'))
    $result.Add([pscustomobject]$r)
}
$newSaves = @(Get-ChildItem $saveDir -File -Filter '*.ssv' | Where-Object { -not $savesBefore.ContainsKey($_.Name) -or $savesBefore[$_.Name] -ne $_.LastWriteTimeUtc } | ForEach-Object Name)
$summary = [ordered]@{
    side = $Side; state = $state; steps = $steps.Count
    reached = @($result | Where-Object reached).Count
    stalledAt = if ($stalledAt) { "$($stalledAt.Step) $($stalledAt.Map)" } else { '' }
    saveFiles = $newSaves
    result = $result
}
$summary | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Out 'chain.json') -Encoding utf8
"[level-chain $Side] $state; reached $($summary.reached) of $($steps.Count) steps" + $(if ($stalledAt) { "; first missing: $($summary.stalledAt)" } else { '' }) + "; autosaves $(@($result | Where-Object autosave).Count); log $log"
