param([string]$Run = '', [int]$Secs = 20, [string]$Build = 'x86-Release', [string]$Name = 'client', [string]$Bin = '')
# Runs the OpenFAKK2 client in a window for up to $Secs seconds (or until it quits by
# itself) and copies its console log to build\compare\<Name>.log. Screenshots it takes
# land in build\sandbox\test-home\fakk\screenshots.
. (Join-Path $PSScriptRoot 'common.ps1')

if (-not $Bin) { $Bin = Join-Path $RepoRoot "build\$Build\Release" }
$console = Join-Path $TestFakk 'qconsole.log'
Remove-Item $console -ErrorAction SilentlyContinue
$log = Join-Path $LogDir "$Name.log"
Remove-Item "$log.stdout", "$log.stderr" -ErrorAction SilentlyContinue
$arguments = "$OursHomeArgs +set r_fullscreen 0 +set r_mode 4 +set in_nograb 1 +set s_initsound 0 +set logfile 2 +set developer 1 $Run"
$p = $null
try {
    $p = Start-HarnessGame -Side ours -Exe "$Bin\openfakk2.exe" -Arguments $arguments -What "run-client $Name" -Stdout "$log.stdout" -Stderr "$log.stderr"
    if (-not $p.WaitForExit($Secs * 1000)) { "client: stopped after ${Secs}s" } else { "client: exited with code $($p.ExitCode)" }
}
finally {
    Stop-HarnessGame $p
    if (Test-Path $console) { Copy-Item $console $log -Force } elseif (Test-Path "$log.stdout") { Get-Content "$log.stdout", "$log.stderr" -ErrorAction SilentlyContinue | Set-Content $log }
}
$log
