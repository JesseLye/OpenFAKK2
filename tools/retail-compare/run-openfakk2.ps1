param([string]$Map = 'zoo', [int]$Secs = 60, [string]$Extra = '', [int]$QuietSecs = 6, [string]$After = '', [int]$MinSecs = 0)
# Runs the OpenFAKK2 dedicated server against the installed game data and saves its output.
# Stops once the map has spawned and the log has been quiet for $QuietSecs, or after $Secs.
. (Join-Path $PSScriptRoot 'common.ps1')
$r = $RepoRoot
$b = $EngineBin
$g = $GameInstall
$out = Join-Path $LogDir "of-$(Get-MapTag $Map).log"
Remove-Item "$out.stdout", "$out.stderr" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$b\openfakk2ded.exe" -WorkingDirectory $b -ArgumentList "+set fs_basepath `"$g`" +set fs_homepath `"$r\build\sandbox\home`" +set developer 1 $Extra +map $Map $After" -PassThru -NoNewWindow -RedirectStandardOutput "$out.stdout" -RedirectStandardError "$out.stderr"
$started = Get-Date; $deadline = $started.AddSeconds($Secs)
$lastSize = -1; $quiet = 0; $spawned = $false
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    Start-Sleep -Seconds 1
    $size = 0; foreach ($part in "$out.stdout", "$out.stderr") { $fs = [IO.File]::Open($part, 'Open', 'Read', 'ReadWrite'); $size += $fs.Length; $fs.Dispose() }
    if ($size -eq $lastSize) { $quiet++ } else { $quiet = 0; $lastSize = $size }
    if (-not $spawned -and $quiet -ge 1) {
        $text = ''; foreach ($part in "$out.stdout", "$out.stderr") { $fs = [IO.File]::Open($part, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $text += $sr.ReadToEnd(); $sr.Dispose() }
        $spawned = $text -match 'entities spawned'
    }
    if ($spawned -and $quiet -ge $QuietSecs -and ((Get-Date) - $started).TotalSeconds -ge $MinSecs) { break }
}
if ($p.HasExited) { $state = "exited with code $($p.ExitCode)" } else { $p.Kill(); $p.WaitForExit(); $state = if ($spawned) { 'spawned, stopped after going quiet' } else { "no spawn after ${Secs}s (killed)" } }
Get-Content "$out.stdout", "$out.stderr" | Set-Content $out
"[$Map] $state, $((Get-Content $out).Count) lines -> $out"
