param([string]$Map = 'zoo', [int]$Secs = 60, [string]$Extra = '', [int]$QuietSecs = 6, [string]$After = '', [int]$MinSecs = 0, [string]$Bin = '')
# Runs the OpenFAKK2 dedicated server against the installed game data and saves its output.
# Stops once the map has spawned and the log has been quiet for $QuietSecs, or after $Secs.
. (Join-Path $PSScriptRoot 'common.ps1')
if (-not $Bin) { $Bin = $EngineBin }
$out = Join-Path $LogDir "of-$(Get-MapTag $Map).log"
Remove-Item "$out.stdout", "$out.stderr" -ErrorAction SilentlyContinue
$p = $null
try {
    $p = Start-HarnessGame -Side ours -Exe "$Bin\openfakk2ded.exe" -What "run-openfakk2 $Map" -NoNewWindow -Stdout "$out.stdout" -Stderr "$out.stderr" `
        -Arguments "$OursHomeArgs +set developer 1 $Extra +map $Map $After"
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
    if ($p.HasExited) { $state = "exited with code $($p.ExitCode)" } else { $state = if ($spawned) { 'spawned, stopped after going quiet' } else { "no spawn after ${Secs}s (killed)" } }
}
finally {
    Stop-HarnessGame $p
    if (Test-Path "$out.stdout") { Get-Content "$out.stdout", "$out.stderr" -ErrorAction SilentlyContinue | Set-Content $out }
}
"[$Map] $state, $((Get-Content $out).Count) lines -> $out"
