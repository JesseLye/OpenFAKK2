param([string[]]$Menus = @('main', 'loadsave', 'options', 'advancedoptions', 'controls', 'credits'),
      [ValidateSet('both', 'retail', 'ours')][string]$Side = 'both', [string]$Name = 'ours')
# Screenshots FAKK2's menus on the retail game and on ours, one run per menu, and pairs
# them as build\compare\captures\menus\pair-<Name>-<menu>.png (retail | ours | difference).
# No map is loaded: each run pushes the menu (a comma-separated list pushes several, one
# after another), waits for its half-second fade and takes one screenshot at 640x480.
#
# Retail draws its menus dimmed under its console window, which it opens at startup while
# developer is above 0, and saves developer to its config.cfg, so this takes "seta
# developer" out of the retail sandbox's config and runs both games with developer 0.
# Retail runs its menus far faster than 60 fps, so its waits are long; and a second
# pushmenu in the same frame as the first is lost, so pushes are a wait apart.
# For the load and save menu, copy the install's save\*.ssv and *.tga into
# build\sandbox\retail\fakk\save so retail lists the same games ours finds in the install.
. (Join-Path $PSScriptRoot 'common.ps1')

$out = Join-Path $LogDir 'captures\menus'
New-Item -ItemType Directory -Force $out | Out-Null
$shots = Join-Path $RepoRoot '.claude\skills\retail-capture\scripts\shots.py'
$retailDir = Join-Path $Sandbox 'retail'
if ($Side -ne 'ours' -and -not (Test-Path "$retailDir\fakk2.exe")) {
    & (Join-Path $PSScriptRoot 'run-retail.ps1') -Name retail -Map zoo -Secs 5 | Out-Null
}
$retailConfig = "$retailDir\fakk\config.cfg"
if (Test-Path $retailConfig) {
    $lines = Get-Content $retailConfig
    $kept = $lines | Where-Object { $_ -notmatch '^\s*seta\s+developer\s' }
    if ($kept.Count -ne $lines.Count) { Set-Content -Path $retailConfig -Value $kept -Encoding ascii }
}

foreach ($m in $Menus) {
    $menu = ($m -split ',')[-1]
    $cfg = @('wait 60')
    foreach ($push in ($m -split ',')) { $cfg += "pushmenu $push"; $cfg += 'wait 300' }
    $cfg += @('wait 900', 'screenshot', 'wait 30', 'quit')

    if ($Side -ne 'ours') {
        Set-Content "$retailDir\fakk\menushot.cfg" $cfg -Encoding ascii
        Remove-Item "$retailDir\fakk\screenshots" -Recurse -ErrorAction SilentlyContinue
        $p = Start-Process -FilePath "$retailDir\fakk2.exe" -WorkingDirectory $retailDir -PassThru -ArgumentList `
            "+set r_fullscreen 0 +set r_mode 3 +set s_initsound 0 +set developer 0 +exec menushot.cfg"
        if (-not $p.WaitForExit(90000)) { $p.Kill(); $p.WaitForExit() }
        $f = Get-ChildItem "$retailDir\fakk\screenshots\shot*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($f) { Copy-Item $f.FullName "$out\retail-$menu.tga" -Force } else { "[retail] ${menu}: no screenshot" }
    }
    if ($Side -ne 'retail') {
        Set-Content "$Sandbox\home\fakk\menushot.cfg" $cfg -Encoding ascii
        Remove-Item "$Sandbox\home\fakk\screenshots" -Recurse -ErrorAction SilentlyContinue
        & (Join-Path $PSScriptRoot 'run-client.ps1') -Run '+set r_mode 3 +set developer 0 +exec menushot.cfg' -Secs 90 -Name "menus-$Name" | Out-Null
        $f = Get-ChildItem "$Sandbox\home\fakk\screenshots\shot*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($f) { Copy-Item $f.FullName "$out\$Name-$menu.tga" -Force } else { "[$Name] ${menu}: no screenshot" }
    }
    if ((Test-Path "$out\retail-$menu.tga") -and (Test-Path "$out\$Name-$menu.tga")) {
        $fig = python -I $shots pair "$out\retail-$menu.tga" "$out\$Name-$menu.tga" "$out\pair-$Name-$menu.png" | Select-String 'mean'
        $fig = ([string]$fig).Trim() -replace '\s+\(0-255.*$', ''
        "${menu}: $fig"
    }
}
