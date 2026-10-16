param([string]$Name, [string]$Map = 'zoo', [int]$Secs = 30, [string]$Extra = '', [string]$After = '')
# Runs the retail fakk2.exe from a sandbox copy as a dedicated server and saves its console text.
. (Join-Path $PSScriptRoot 'common.ps1')
$g = $GameInstall
$out = Join-Path $LogDir "ded-$Name.log"
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class WC {
 delegate bool EP(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(EP f, IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EP f, IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
 public static string Console(uint pid) { string best = ""; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h, out p); if (p==pid) { EnumChildWindows(h,(c,l2)=>{ var cn = new StringBuilder(64); GetClassName(c,cn,64); if (cn.ToString()=="Edit") { var t = new StringBuilder(2000000); SendMessage(c, 0x000D, (IntPtr)2000000, t); if (t.Length > best.Length) best = t.ToString(); } return true; }, IntPtr.Zero);} return true; }, IntPtr.Zero); return best; }
}
'@
$dir = "$Sandbox\$Name"
if (-not (Test-Path "$dir\fakk2.exe")) {
    # first use: copy the executable and game modules, link the paks
    New-Item -ItemType Directory -Force "$dir\fakk" | Out-Null
    Copy-Item "$g\fakk2.exe" $dir
    Copy-Item "$g\fakk\gamex86.dll", "$g\fakk\cgamex86.dll" "$dir\fakk"
    foreach ($pak in 'pak0.pk3', 'pak1.pk3') { try { New-Item -ItemType HardLink -Path "$dir\fakk\$pak" -Target "$g\fakk\$pak" -ErrorAction Stop | Out-Null } catch { Copy-Item "$g\fakk\$pak" "$dir\fakk\$pak" } }
}
$p = $null; $txt = ''
try {
    $p = Start-HarnessGame -Side retail -Exe "$dir\fakk2.exe" -WorkDir $dir -WindowStyle Minimized -What "run-retail $Name $Map" `
        -Arguments "+set dedicated 1 +set developer 1 $Extra +map $Map $After"
    $deadline = (Get-Date).AddSeconds($Secs)
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) { Start-Sleep -Milliseconds 500 }
    if ($p.HasExited) { $txt = "<process exited with code $($p.ExitCode)>" } else { $txt = [WC]::Console([uint32]$p.Id) }
}
finally { Stop-HarnessGame $p }
Set-Content $out $txt
"[$Name] $($txt.Length) chars, $(($txt -split "`n").Count) lines -> $out"
