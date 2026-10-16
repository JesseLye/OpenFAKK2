param([ValidateSet('retail', 'openal', 'dma')][string]$Engine = 'openal', [string]$Probe = 'probe-sound-pan',
      [string]$Map = 'zoo', [double]$Volume = 0.25, [double]$Music = 0, [int]$Secs = 55, [string]$Tag = '',
      [string]$Extra = '', [string]$Python = 'python', [string]$Bin = '', [string]$Demo = '')
# Runs a sound probe on the retail game or on OpenFAKK2 (its OpenAL or its software-mixer
# backend) while recording what the speakers play, to build\compare\sound-<engine>-<probe><tag>.npy;
# read the recordings with sound_levels.py. The sound plays out loud, and anything else
# playing at the time is recorded too. The retail game runs from build\sandbox\retail-snd,
# a copy that keeps the install's Miles drivers (snddrivers) but no DirectSound wrapper, so
# with no EAX card it opens Miles' "Fast 2D Positional Audio" provider; it must be the
# window in front or DirectSound silences it, and the script keeps it there. Keep -Volume
# low: the Windows output path here compresses anything above about -20 dBFS, and at 0.25
# everything stays under it.
#
# -Demo plays a .dm3 at its own speed instead of running a probe (the retail-demo skill):
# both games hear the same sound events, so what differs is how each plays them. Each also
# logs every sound it starts (retail's s_show_sounds; ours' s_show, which only the software
# mixer, -Engine dma, prints). The recording is sound-<engine>-<demo><tag>.npy.
. (Join-Path $PSScriptRoot 'common.ps1')

$what = if ($Demo) { [IO.Path]::GetFileNameWithoutExtension($Demo) } else { $Probe }
$name = "sound-$Engine-$what$Tag"
$out = Join-Path $LogDir "$name.npy"
$retailDir = Join-Path $Sandbox 'retail-snd'
$homeFakk = $TestFakk

if ($Engine -eq 'retail' -and -not (Test-Path "$retailDir\fakk2.exe")) {
    New-Item -ItemType Directory -Force "$retailDir\fakk", "$retailDir\snddrivers" | Out-Null
    Copy-Item "$GameInstall\fakk2.exe" $retailDir
    Copy-Item "$GameInstall\snddrivers\*" "$retailDir\snddrivers"
    Copy-Item "$GameInstall\fakk\gamex86.dll", "$GameInstall\fakk\cgamex86.dll" "$retailDir\fakk"
    foreach ($pak in 'pak0.pk3', 'pak1.pk3') { try { New-Item -ItemType HardLink -Path "$retailDir\fakk\$pak" -Target "$GameInstall\fakk\$pak" -ErrorAction Stop | Out-Null } catch { Copy-Item "$GameInstall\fakk\$pak" "$retailDir\fakk\$pak" } }
}
# the probe and its tone, which is not in the game's paks
$gameDir = if ($Engine -eq 'retail') { "$retailDir\fakk" } else { $homeFakk }
if ($Demo) {
    if (-not (Test-Path $Demo)) { throw "no demo at $Demo" }
    New-Item -ItemType Directory -Force "$gameDir\demos" | Out-Null
    Copy-Item $Demo "$gameDir\demos\sndplay.dm3" -Force
    $run = "+set s_volume $Volume +set s_musicvolume $Music +set s_show_sounds 1 +set s_show 1 $Extra +set nextdemo quit +demo sndplay"
}
else {
    New-Item -ItemType Directory -Force "$gameDir\maps", "$gameDir\sound\test" | Out-Null
    Copy-Item (Join-Path $PSScriptRoot "$Probe.scr") "$gameDir\maps" -Force
    & $Python (Join-Path $PSScriptRoot 'sound_tone.py') "$gameDir\sound\test\tone.wav"
    $run = "+set s_volume $Volume +set s_musicvolume $Music $Extra +map $Map +script thread $Probe.scr::probe"
}
# the lock is taken before the recording starts, so the game it records is not left
# waiting on another session's run while the recorder runs
Enter-HarnessLock "sound-record $Engine $what" -Mode exclusive   # the loopback records every game's sound
$capture = $null
try {
    $capture = Start-Process -FilePath $Python -ArgumentList "`"$(Join-Path $PSScriptRoot 'sound_capture.py')`" `"$out`" $Secs" -PassThru -WindowStyle Hidden -RedirectStandardOutput "$out.txt"
    if ($Engine -eq 'retail') {
        Remove-Item "$retailDir\fakk\qconsole.log" -ErrorAction SilentlyContinue
        $p = $null
        try {
            $p = Start-HarnessGame -Side retail -Exe "$retailDir\fakk2.exe" -WorkDir $retailDir -What "sound-record retail $what" `
                -Arguments "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set logfile 2 +set developer 1 +set s_milesdriver eax2 +set s_khz 44 $run"
            # a window that comes up in front of it (another game run, a dialog) silences it, so
            # keep bringing it back for as long as the recording lasts
            Add-Type -Namespace RetailCompare -Name User32 -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hWnd);'
            while (-not $capture.WaitForExit(1000)) {
                $p.Refresh()
                if (-not $p.HasExited -and $p.MainWindowHandle -ne [IntPtr]::Zero) { [RetailCompare.User32]::SetForegroundWindow($p.MainWindowHandle) | Out-Null }
            }
        }
        finally { Stop-HarnessGame $p }
        Copy-Item "$retailDir\fakk\qconsole.log" (Join-Path $LogDir "$name.log") -ErrorAction SilentlyContinue
    } else {
        $backend = if ($Engine -eq 'openal') { 1 } else { 0 }
        & (Join-Path $PSScriptRoot 'run-client.ps1') -Run "+set s_initsound 1 +set s_useOpenAL $backend $run" -Secs ($Secs - 2) -Name $name -Bin $Bin | Out-Null
        $capture.WaitForExit()
    }
}
finally {
    if ($capture -and -not $capture.HasExited) { $capture.Kill() }
    Exit-HarnessLock
}
Get-Content "$out.txt"
Select-String -Path (Join-Path $LogDir "$name.log") -Pattern 'provider opened|OpenAL output mode|dma buffer' | Select-Object -First 2 | ForEach-Object Line
