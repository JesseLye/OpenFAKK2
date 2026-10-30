param([int]$X = 477, [int]$Y = 270, [int]$Shots = 16, [int]$StepMs = 50, [string]$Name = 'click', [string]$Menu = 'main', [switch]$NoEnter, [double]$Scale = 1.54, [int]$X2 = -1, [int]$Y2 = -1, [int]$BetweenMs = 1500, [switch]$Escape2)
# Clicks a point of the retail game's menu with posted mouse messages and shoots the
# frames after it with posted F12 presses (bound to screenshot). Shots land in
# build\compare\captures\retail-<Name>\retail-<n>.tga.
$Repo = 'C:\Users\Chse\Desktop\OpenFAKK2'
Set-Location $Repo
. (Join-Path $Repo 'tools\retail-compare\common.ps1')
$retailDir = Join-Path $Sandbox 'retail'
$out = Join-Path $Repo "build\compare\captures\retail-$Name"
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out -File | Remove-Item -Force
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, IntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    public struct POINT { public int X; public int Y; }
}
"@
$cfg = @('wait 60')
foreach ($m in ($Menu -split ',')) { $cfg += "pushmenu $m"; $cfg += 'wait 300' }
$cfg += @('bind F12 screenshot', 'echo ==== ready')
Set-Content "$retailDir\fakk\click.cfg" $cfg -Encoding ascii
Remove-Item "$retailDir\fakk\screenshots" -Recurse -ErrorAction SilentlyContinue
$log = "$retailDir\fakk\qconsole.log"
Remove-Item $log -ErrorAction SilentlyContinue
$p = $null
try {
    $p = Start-HarnessGame -Side retail -Exe "$retailDir\fakk2.exe" -WorkDir $retailDir -What "retail click $Name" `
        -Arguments "+set r_fullscreen 0 +set r_mode 3 +set s_initsound 0 +set developer 0 +set logfile 2 +exec click.cfg"
    $t0 = Get-Date
    while (-not (Test-Path $log) -or -not (Select-String -Path $log -SimpleMatch '==== ready' -Quiet)) {
        if ($p.HasExited -or ((Get-Date) - $t0).TotalSeconds -gt 60) { throw "retail never got ready" }
        Start-Sleep -Milliseconds 200
    }
    Start-Sleep -Milliseconds 500
    $h = [W]::FindWindow('Heavy Metal: FAKK2', $null)
    if ($h -eq [IntPtr]::Zero) { $p.Refresh(); $h = $p.MainWindowHandle }
    "window $h"
    # the menu cursor follows the real Windows cursor: move it onto the button for
    # the click and put it back after
    $saved = New-Object W+POINT; [W]::GetCursorPos([ref]$saved) | Out-Null
    $prevFg = [W]::GetForegroundWindow()
    [W]::ShowWindow($h, 9) | Out-Null       # SW_RESTORE
    [W]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 400
    # the game's own menu cursor moves by relative mouse motion (in_mouse 1): pin it
    # to the top left corner, then walk it to the button in steps
    $pt = New-Object W+POINT; $pt.X = $X; $pt.Y = $Y; [W]::ClientToScreen($h, [ref]$pt) | Out-Null
    [W]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 200
    # the menu cursor starts at the screen's centre and moves about 0.44 units a count
    $steps = 10
    # pin the cursor to the top left corner first; it then moves about 1.54 units a count
    foreach ($i in 1..8) { [W]::mouse_event(0x01, -400, -400, 0, [IntPtr]0); Start-Sleep -Milliseconds 20 }
    Start-Sleep -Milliseconds 300
    $dx = $X / $Scale; $dy = $Y / $Scale
    "moving $([int]$dx),$([int]$dy) counts"
    foreach ($i in 1..$steps) { [W]::mouse_event(0x01, [int]($dx / $steps), [int]($dy / $steps), 0, [IntPtr]0); Start-Sleep -Milliseconds 30 }
    Start-Sleep -Milliseconds 1200
    # DirectInput takes the injected motion but not the buttons: press Enter on the
    # hovered button instead (posted keys reach the game)
    $kd = [IntPtr](1 -bor (0x1C -shl 16)); $ku = [IntPtr](0xC0000001 -bor (0x1C -shl 16))
    if (-not $NoEnter) {
        [W]::PostMessage($h, 0x100, [IntPtr]0x0D, $kd) | Out-Null
        Start-Sleep -Milliseconds 40
        [W]::PostMessage($h, 0x101, [IntPtr]0x0D, $ku) | Out-Null
    }
    if ($Escape2) {
        # a second action: Escape after the first's transition
        Start-Sleep -Milliseconds $BetweenMs
        $ed = [IntPtr](1 -bor (0x01 -shl 16)); $eu = [IntPtr](0xC0000001 -bor (0x01 -shl 16))
        [W]::PostMessage($h, 0x100, [IntPtr]0x1B, $ed) | Out-Null
        Start-Sleep -Milliseconds 40
        [W]::PostMessage($h, 0x101, [IntPtr]0x1B, $eu) | Out-Null
    }
    if ($X2 -ge 0) {
        # a second activation: wait for the first's transition, walk the cursor from
        # the corner again to the second button, Enter
        Start-Sleep -Milliseconds $BetweenMs
        foreach ($i in 1..8) { [W]::mouse_event(0x01, -400, -400, 0, [IntPtr]0); Start-Sleep -Milliseconds 20 }
        Start-Sleep -Milliseconds 300
        $dx = $X2 / $Scale; $dy = $Y2 / $Scale
        foreach ($i in 1..$steps) { [W]::mouse_event(0x01, [int]($dx / $steps), [int]($dy / $steps), 0, [IntPtr]0); Start-Sleep -Milliseconds 30 }
        Start-Sleep -Milliseconds 1200
        [W]::PostMessage($h, 0x100, [IntPtr]0x0D, $kd) | Out-Null
        Start-Sleep -Milliseconds 40
        [W]::PostMessage($h, 0x101, [IntPtr]0x0D, $ku) | Out-Null
    }
    Start-Sleep -Milliseconds 30
    [W]::SetCursorPos($saved.X, $saved.Y) | Out-Null
    $down = [IntPtr](1 -bor (0x58 -shl 16)); $up = [IntPtr](0xC0000001 -bor (0x58 -shl 16))
    for ($i = 0; $i -lt $Shots; $i++) {
        [W]::PostMessage($h, 0x100, [IntPtr]0x7B, $down) | Out-Null   # F12 down
        [W]::PostMessage($h, 0x101, [IntPtr]0x7B, $up) | Out-Null     # F12 up
        Start-Sleep -Milliseconds $StepMs
    }
    Start-Sleep -Seconds 2
    if ($prevFg -ne [IntPtr]::Zero) { [W]::SetForegroundWindow($prevFg) | Out-Null }
}
finally { Stop-HarnessGame $p }
$n = 0
Get-ChildItem "$retailDir\fakk\screenshots\shot*" -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { Copy-Item $_.FullName "$out\retail-$n.tga" -Force; $n++ }
Copy-Item $log "$out\retail.log" -ErrorAction SilentlyContinue
"[retail $Name] $n shots"
