param([string]$Run = '', [int]$Secs = 20, [string]$Build = 'x86-Release', [string]$Name = 'client')
# Runs the OpenFAKK2 client in a window for up to $Secs seconds (or until it quits by
# itself) and copies its console log to build\compare\<Name>.log. Screenshots it takes
# land in build\sandbox\home\fakk\screenshots.
. (Join-Path $PSScriptRoot 'common.ps1')

$bin = Join-Path $RepoRoot "build\$Build\Release"
$homeDir = Join-Path $Sandbox 'home'
$console = Join-Path $homeDir 'fakk\qconsole.log'
Remove-Item $console -ErrorAction SilentlyContinue
$log = Join-Path $LogDir "$Name.log"
# com_abnormalExitDialog 0: a run stopped at its timeout leaves the pid file behind, and the
# next start would otherwise wait for ever on the "didn't exit properly" dialog
$arguments = "+set fs_basepath `"$GameInstall`" +set fs_homepath `"$homeDir`" +set com_abnormalExitDialog 0 +set r_fullscreen 0 +set r_mode 4 +set in_nograb 1 +set s_initsound 0 +set logfile 2 +set developer 1 $Run"
$p = Start-Process -FilePath "$bin\openfakk2.exe" -WorkingDirectory $bin -ArgumentList $arguments -PassThru -RedirectStandardOutput "$log.stdout" -RedirectStandardError "$log.stderr"
if (-not $p.WaitForExit($Secs * 1000)) { $p.Kill(); $p.WaitForExit(); "client: stopped after ${Secs}s" } else { "client: exited with code $($p.ExitCode)" }
if (Test-Path $console) { Copy-Item $console $log -Force } else { Get-Content "$log.stdout", "$log.stderr" | Set-Content $log }
$log
