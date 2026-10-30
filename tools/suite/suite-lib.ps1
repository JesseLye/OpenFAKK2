# The test suite's shared code: where references live, the check list, and for each kind
# of check how to run it on either game and how to measure ours against retail.
# Dot-sourced by run-suite.ps1 and refresh-retail.ps1.
. (Join-Path $PSScriptRoot '..\retail-compare\common.ps1')
. (Join-Path $PSScriptRoot '..\retail-compare\compare-lib.ps1')

$SuiteDir = $PSScriptRoot
$RetailCompare = Join-Path $RepoRoot 'tools\retail-compare'
# References are shared by every worktree, in the main checkout's build\ (never committed:
# they are made from Ritual's data). Runs and the verify stamp are the worktree's own.
$SuiteShared = Join-Path $MainRoot 'build\suite'
$RetailRefs = Join-Path $SuiteShared 'reference\retail'
$OursRefs = Join-Path $SuiteShared 'reference\ours'
# the lane: run-suite.ps1 -Arch sets these (x64: its build, reference\ours-x64)
$SuiteArch = 'x86'
$SuiteLocal = Join-Path $RepoRoot 'build\suite'
$ShotsPy = Join-Path $RepoRoot '.claude\skills\retail-capture\scripts\shots.py'
$CaptureScript = Join-Path $RepoRoot '.claude\skills\retail-capture\scripts\capture.ps1'
$ProbeClient = Join-Path $RepoRoot '.claude\skills\retail-probe\scripts\probe-client.ps1'
$Python = if ($env:FAKK2_PYTHON) { $env:FAKK2_PYTHON } else { 'python' }

# The Python the sound checks record and measure with: it needs pyaudiowpatch (WASAPI
# loopback) and numpy, which the system Python lacks. FAKK2_SOUND_PYTHON names one;
# otherwise a virtual environment in the main checkout's build\suite\sound-venv, made on
# first use (pip install pyaudiowpatch numpy).
function Get-SoundPython {
    if ($env:FAKK2_SOUND_PYTHON) { return $env:FAKK2_SOUND_PYTHON }
    $venv = Join-Path $SuiteShared 'sound-venv'
    $py = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path $py)) {
        New-Item -ItemType Directory -Force $SuiteShared | Out-Null
        & $Python -m venv $venv | Out-Null
        & $py -m pip install --quiet pyaudiowpatch numpy | Out-Null
        if ($LASTEXITCODE) { throw "could not install pyaudiowpatch and numpy into $venv" }
    }
    $py
}

function Read-SuiteConfig { Get-Content (Join-Path $SuiteDir 'suite.json') -Raw | ConvertFrom-Json }

function Get-Prop($Object, [string]$Name, $Default = $null) {
    if ($null -ne $Object -and $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    $Default
}

# suite.json's entries, with "maps" and "menus" lists expanded to one check per item
# (spawn/zoo, menus/main). "maps": "all" is the 30 retail maps. An item in "quickMaps"
# makes that check a quick one.
function Get-SuiteChecks($Config) {
    foreach ($c in $Config.checks) {
        Get-CheckLockMode $c | Out-Null      # a misspelt "exclusive" stops the run here, not mid-way
        $items = @($null); $field = $null
        $maps = Get-Prop $c 'maps'
        $menus = Get-Prop $c 'menus'
        if ($maps) {
            $field = 'map'; $items = if ($maps -eq 'all') { @($Config.maps) } else { @($maps) }
            $skip = @(Get-Prop $c 'skipMaps' @())
            $items = @($items | Where-Object { $skip -notcontains $_ })
        }
        elseif ($menus) { $field = 'menu'; $items = @($menus) }
        foreach ($item in $items) {
            $o = [ordered]@{}
            foreach ($p in $c.PSObject.Properties) { $o[$p.Name] = $p.Value }
            if ($field) {
                $o[$field] = $item
                $o.name = "$($c.name)/$(Get-MapTag $item)"
                $o.group = $c.name
                if (@(Get-Prop $c 'quickMaps' @()) -contains $item) { $o.level = 'quick' }
            }
            else { $o.group = $c.name }
            if (-not $o.Contains('map')) { $o.map = $null }
            [pscustomobject]$o
        }
    }
}

function Get-RefDir([string]$Root, [string]$Name) { Join-Path $Root ($Name -replace '/', '\') }

# Which version of a retail reference a result was measured against: when it was made.
function Get-ReferenceStamp([string]$RefDir) {
    $meta = Join-Path $RefDir 'meta.json'
    if (-not (Test-Path $meta)) { return '' }
    [string](Get-Content $meta -Raw | ConvertFrom-Json).date
}

# Whether a check is measured against a retail reference (the rest are judged against
# ours' last good result only).
function Test-HasRetail($c) {
    switch ($c.kind) {
        { $_ -in 'spawn', 'player', 'capture', 'menus', 'chain', 'sound', 'windowshots', 'demo' } { return $true }
        'client' { return -not (Get-Prop $c 'oursOnly' $false) }
        'cfgrun' { return [bool](Get-Prop $c 'retailCfg') }
        default { return $false }
    }
}

# What a retail reference was made from: the probe (or other script) the check runs, so a
# reference whose probe has changed since is reported as stale rather than compared.
function Get-CheckInputHash($Check) {
    $files = @()
    $probe = Get-Prop $Check 'probe'
    if ($probe) { $files += Join-Path $RetailCompare "$probe.scr" }
    if ($Check.kind -eq 'chain') { $files += Join-Path $RetailCompare 'chain.scr' }
    # a demo check made from a demo someone recorded: that demo, so a new take of it is stale
    $demoFile = Get-Prop $Check 'demoFile'
    if ($demoFile) { $files += Join-Path $DemoDir $demoFile }
    $files = @($files | Where-Object { Test-Path $_ })
    if (-not $files.Count) { return '' }
    $sha = [Security.Cryptography.SHA1]::Create()
    $bytes = New-Object System.Collections.Generic.List[byte]
    foreach ($f in $files) { $bytes.AddRange([IO.File]::ReadAllBytes($f)) }
    # a demo check that keeps windows of frames: retail's frames were kept by them
    $keep = @(Get-Prop $Check 'keep' @())
    if ($Check.kind -eq 'demo' -and $keep.Count) {
        $bytes.AddRange([Text.Encoding]::UTF8.GetBytes("$(Get-Prop $Check 'avidemo' 2)/$(Get-Prop $Check 'every' 1)/$($keep -join ',')"))
    }
    ([BitConverter]::ToString($sha.ComputeHash($bytes.ToArray())) -replace '-', '').Substring(0, 12)
}

# Copies the test models in tools\retail-compare\fx into a game's models folder.
function Install-FxModels([string]$Fakk) {
    $dest = Join-Path $Fakk 'models\fx'
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item (Join-Path $RetailCompare 'fx\*.tik') $dest -Force
    # the probes name them as models/fx/<name>.tik and as <name>.tik; both resolve
    Copy-Item (Join-Path $RetailCompare 'fx\*.tik') (Join-Path $Fakk 'models') -Force
}

# The retail-recorded demos a check plays: a "demo" check its own (made with its retail
# reference), a "timedemo" check those of the demo checks it names. Each is
# @{ check; file }; run-suite.ps1 skips a check whose demo is not there yet.
function Get-CheckDemos($Check) {
    $names = switch ($Check.kind) { 'demo' { @($Check.name) } 'timedemo' { @(Get-Prop $Check 'demos' @()) } default { @() } }
    foreach ($n in $names) { @{ check = $n; file = Join-Path (Get-RefDir $RetailRefs $n) 'demo.dm3' } }
}

# Plays a demo on either game with cl_avidemo: every frame is 1/$Rate s of the demo and
# takes a screenshot, on both engines (retail's CL_Frame 0x421f45), so frame n is the
# same moment on both; nextdemo quits at the end. Keeps every $Every-th shot in $Out as
# shot-<frame>.tga, and the console log as game.log; with $Keep ("a-b", seconds of the
# demo: frame n is n/$Rate s), only the frames in those windows.
function Invoke-DemoPlayback([ValidateSet('ours', 'retail')][string]$Side, [string]$Demo, [string]$Out, [int]$Rate, [int]$Every, [int]$Secs, [string]$Extra, [string]$Bin, [string[]]$Keep = @()) {
    $windows = @(foreach ($w in $Keep) { if ($w -match '^\s*([\d.]+)\s*-\s*([\d.]+)\s*$') { , @([double]$Matches[1], [double]$Matches[2]) } else { throw "demo keep window '$w' is not 'from-to' in seconds" } })
    $retailDir = Join-Path $Sandbox 'retail'
    $fakk = if ($Side -eq 'ours') { $TestFakk } else { Join-Path $retailDir 'fakk' }
    New-Item -ItemType Directory -Force (Join-Path $fakk 'demos') | Out-Null
    Copy-Item $Demo (Join-Path $fakk 'demos\suite.dm3') -Force
    $shotDir = Join-Path $fakk 'screenshots'
    Remove-Item $shotDir -Recurse -Force -ErrorAction SilentlyContinue
    $console = Join-Path $fakk 'qconsole.log'
    Remove-Item $console -ErrorAction SilentlyContinue
    # 640x480 on both, as capture.ps1 runs them, so shots pair; kept short for retail, which
    # drops the end of a long command line
    $play = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 $Extra +set cl_avidemo $Rate +set nextdemo quit +demo suite"
    if ($Side -eq 'ours') { $exe = Join-Path $(if ($Bin) { $Bin } else { $EngineBin }) 'openfakk2.exe'; $wd = Split-Path $exe; $play = "$OursHomeArgs +set in_nograb 1 +set developer 0 +set cl_demoSeed 1 $play" }
    else { $exe = "$retailDir\fakk2.exe"; $wd = $retailDir }
    if (-not (Test-Path $exe)) { throw "no client at $exe (build it, or pass -Bin)" }
    $p = $null
    try {
        $p = Start-HarnessGame -Side $Side -Exe $exe -WorkDir $wd -Arguments $play -What "suite demo $Side"
        $state = if (-not $p.WaitForExit($Secs * 1000)) { "stopped after ${Secs}s" } else { "exited with code $($p.ExitCode)" }
    }
    finally { Stop-HarnessGame $p }
    Copy-IfThere $console "$Out\game.log" | Out-Null
    $n = 0; $kept = 0
    Get-ChildItem $shotDir -File -Filter 'shot*' -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object {
        $t = $n / $Rate
        $inWindow = -not $windows.Count -or @($windows | Where-Object { $t -ge $_[0] -and $t -le $_[1] }).Count
        if ($inWindow -and $n % $Every -eq 0) { Copy-Item $_.FullName (Join-Path $Out "shot-$n.tga"); $kept++ }
        $n++
    }
    "$Side`: demo played, $state; $n frames, $kept shots kept"
}

function Copy-IfThere([string]$From, [string]$To) {
    if (Test-Path $From) { Copy-Item $From $To -Force; return $true }
    $false
}

# A picture of a game window's client area (for moments a console "screenshot" cannot be
# timed to, such as the startup intro, which runs through the same command buffer a
# config's waits would hold up). Saved as an uncompressed TGA, top-down, as shots.py
# reads. The window's own picture is asked for with PrintWindow (PW_CLIENTONLY |
# PW_RENDERFULLCONTENT), which works with the window covered: copying the screen took
# whatever was on top, since Windows refuses SetForegroundWindow to a background process
# (an agent's terminal over the intro failed the check). Only when PrintWindow fails is
# the screen copied, and then only if the window really is in front; otherwise no shot
# is taken. (An all-black picture is not taken as a failure: the intro fades on black.)
function Save-WindowShot($Process, [string]$Path) {
    if (-not ('OpenFakk2Harness.Window' -as [type])) {
        Add-Type -Namespace OpenFakk2Harness -Name Window -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct POINT { public int X, Y; }
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(System.IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
'@
        [OpenFakk2Harness.Window]::SetProcessDPIAware() | Out-Null
        Add-Type -AssemblyName System.Drawing
    }
    $Process.Refresh()
    $h = $Process.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { return $false }
    [OpenFakk2Harness.Window]::SetForegroundWindow($h) | Out-Null
    $rc = New-Object OpenFakk2Harness.Window+RECT
    [OpenFakk2Harness.Window]::GetClientRect($h, [ref]$rc) | Out-Null
    $pt = New-Object OpenFakk2Harness.Window+POINT
    [OpenFakk2Harness.Window]::ClientToScreen($h, [ref]$pt) | Out-Null
    $w = $rc.Right - $rc.Left; $hh = $rc.Bottom - $rc.Top
    if ($w -le 0 -or $hh -le 0) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $printed = [OpenFakk2Harness.Window]::PrintWindow($h, $hdc, 3)	# PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $hh), 'ReadOnly', $bmp.PixelFormat)
    $row = $w * 3
    $bytes = New-Object byte[] ($row * $hh)
    for ($y = 0; $y -lt $hh; $y++) { [Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $bytes, $y * $row, $row) }
    $bmp.UnlockBits($data)
    if (-not $printed) {
        if ([OpenFakk2Harness.Window]::GetForegroundWindow() -ne $h) { $g.Dispose(); $bmp.Dispose(); return $false }
        $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size $w, $hh))
        $data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $hh), 'ReadOnly', $bmp.PixelFormat)
        for ($y = 0; $y -lt $hh; $y++) { [Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $bytes, $y * $row, $row) }
        $bmp.UnlockBits($data)
    }
    $g.Dispose(); $bmp.Dispose()
    $header = New-Object byte[] 18
    $header[2] = 2; $header[12] = $w -band 255; $header[13] = $w -shr 8; $header[14] = $hh -band 255; $header[15] = $hh -shr 8; $header[16] = 24; $header[17] = 0x20
    $fs = [IO.File]::Create($Path); $fs.Write($header, 0, 18); $fs.Write($bytes, 0, $bytes.Length); $fs.Dispose()
    $true
}

# The console lines counted as warnings and errors: what a log says went wrong, without
# two kinds every run prints by the hundred that say nothing about a change (retail's
# textures are .ftx, so every .tga lookup falls back; the collision model's bevel notes).
$WarningPattern = '(?i)^\^?\d?(WARNING|ERROR)|couldn''t|can''t find'
$BenignPattern = 'not present, using .* instead|CM_AddFacetBevels'
function Get-WarningLines([string]$Log) {
    if (-not (Test-Path $Log)) { return @() }
    @(Select-String -Path $Log -Pattern $WarningPattern | Where-Object { $_.Line -notmatch $BenignPattern -and $_.Line -notmatch '^serverCommand:' } | ForEach-Object Line)
}
function Format-WarningGroups([string[]]$Lines, [int]$Top = 6) {
    @($Lines | Group-Object { $_ -replace '\d+', '#' } | Sort-Object Count -Descending | Select-Object -First $Top | ForEach-Object { "  x$($_.Count): $($_.Group[0])" })
}

# Writes a config file into a game's fakk folder, for +exec.
function Set-GameCfg([string]$Fakk, [string]$Name, [string[]]$Lines) {
    New-Item -ItemType Directory -Force $Fakk | Out-Null
    Set-Content -Path (Join-Path $Fakk $Name) -Value $Lines -Encoding ascii
}

# ------------------------------------------------------------------------------------
# Running a check on one game. Each writes what it made to $Out: game.log (the main
# console log), shot-<n>.tga, output.txt (what the runner printed) and kind-specific
# files. Returns a short note.
# ------------------------------------------------------------------------------------
# How a check's games take the harness lock (common.ps1): alone when suite.json says
# "exclusive" and why (timing: frame times or held keys, which a busy machine moves; sound:
# the loopback recording takes every game's sound; focus: the game must be the window in
# front), and every sound check; otherwise shared with other worktrees' games.
function Get-CheckLockMode($Check) {
    $why = [string](Get-Prop $Check 'exclusive' '')
    if ($why -and $why -notin 'timing', 'sound', 'focus') { throw "check $($Check.name): ""exclusive"" is '$why'; it takes timing, sound or focus" }
    if ($why -or (Get-Prop $Check 'sound' $false)) { 'exclusive' } else { 'shared' }
}

function Invoke-CheckSide($Check, [ValidateSet('ours', 'retail')][string]$Side, [string]$Out, [string]$Bin = '') {
    $saved = $env:FAKK2_HARNESS_MODE
    $env:FAKK2_HARNESS_MODE = Get-CheckLockMode $Check
    try { Invoke-CheckSideGames $Check $Side $Out $Bin }
    finally { $env:FAKK2_HARNESS_MODE = $saved }
}

function Invoke-CheckSideGames($Check, [ValidateSet('ours', 'retail')][string]$Side, [string]$Out, [string]$Bin = '') {
    New-Item -ItemType Directory -Force $Out | Out-Null
    Get-ChildItem $Out -File -ErrorAction SilentlyContinue | Remove-Item -Force
    $map = $Check.map
    $tag = if ($map) { Get-MapTag $map } else { '' }
    $probe = Get-Prop $Check 'probe'
    $secs = [int](Get-Prop $Check 'secs' 0)
    $extra = [string](Get-Prop $Check 'extra' '')
    $sideExtra = if ($Side -eq 'ours') { [string](Get-Prop $Check 'oursExtra' '') } else { [string](Get-Prop $Check 'retailExtra' '') }
    $retailDir = Join-Path $Sandbox 'retail'
    if ($Side -eq 'retail' -and -not (Test-Path "$retailDir\fakk2.exe")) { & (Join-Path $RetailCompare 'run-retail.ps1') -Name retail -Map zoo -Secs 5 | Out-Null }
    $fakk = if ($Side -eq 'ours') { $TestFakk } else { Join-Path $retailDir 'fakk' }
    if (@(Get-Prop $Check 'setup' @()) -contains 'fx') { Install-FxModels $fakk }
    $output = @()

    switch ($Check.kind) {
        'spawn' {
            if ($Side -eq 'ours') {
                $output = & (Join-Path $RetailCompare 'run-openfakk2.ps1') -Map $map -Secs $(if ($secs) { $secs } else { 60 }) -QuietSecs 4 -Bin $Bin -Extra $extra
                Copy-IfThere (Join-Path $LogDir "of-$tag.log") "$Out\game.log" | Out-Null
            }
            else {
                $output = & (Join-Path $RetailCompare 'run-retail.ps1') -Name retail -Map $map -Secs $(if ($secs) { $secs } else { 40 }) -Extra $extra
                Copy-IfThere (Join-Path $LogDir 'ded-retail.log') "$Out\game.log" | Out-Null
            }
        }
        'player' {
            $cached = Join-Path $LogDir "player-retail-$tag-$probe.log"
            if ($Side -eq 'ours') {
                # compare-player.ps1 compares with the cached retail log; the suite measures
                # by itself, so any retail log will do to let it run ours only
                if (-not (Test-Path $cached)) { Set-Content $cached '' }
                $output = & (Join-Path $RetailCompare 'compare-player.ps1') -Map $map -Probe $probe -NoRetail -Bin $Bin -Extra "$extra $sideExtra" -Secs $(if ($secs) { $secs } else { 150 })
                Copy-IfThere (Join-Path $LogDir "player-ours-$tag-$probe.log") "$Out\game.log" | Out-Null
            }
            else {
                $output = & (Join-Path $RetailCompare 'compare-player.ps1') -Map $map -Probe $probe -Refresh -RetailOnly -Secs $(if ($secs) { $secs } else { 150 })
                Copy-IfThere $cached "$Out\game.log" | Out-Null
            }
        }
        'client' {
            $name = if ($Side -eq 'ours') { 'suite' } else { 'retail' }
            $pc = @{ Probe = $probe; Map = $map; Name = $name; Secs = $(if ($secs) { $secs } else { 90 }); Extra = "$extra $sideExtra".Trim() }
            if ($Side -eq 'retail') { $pc.Retail = $true } elseif ($Bin) { $pc.Bin = $Bin }
            $before = Get-Prop $Check 'copySaves'
            if ($before) {
                # saves the probe loads, taken from the player's home (read only) or the install
                New-Item -ItemType Directory -Force (Join-Path $fakk 'save') | Out-Null
                foreach ($pattern in @($before)) {
                    foreach ($dir in (Join-Path $PlayHome 'fakk\save'), (Join-Path $GameInstall 'fakk\save')) {
                        Get-ChildItem (Join-Path $dir $pattern) -ErrorAction SilentlyContinue | Copy-Item -Destination (Join-Path $fakk 'save') -Force
                    }
                }
            }
            $output = & $ProbeClient @pc -Classname none -Entity -1
            Copy-IfThere (Join-Path $LogDir "client-$name-$tag-$probe.log") "$Out\game.log" | Out-Null
        }
        'capture' {
            $cap = @{ Probe = $probe; Map = $map; Side = $Side; Name = 'suite'; Secs = $(if ($secs) { $secs } else { 120 }); Extra = $extra }
            if ($Side -eq 'ours') { $cap.OursExtra = $sideExtra; if ($Bin) { $cap.Bin = $Bin } } else { $cap.RetailExtra = $sideExtra }
            $mode = Get-Prop $Check 'mode'
            if ($mode) { $cap.Mode = [int]$mode }
            $output = & $CaptureScript @cap
            $label = if ($Side -eq 'ours') { 'suite' } else { 'retail' }
            $dir = Join-Path $LogDir "captures\$probe-$tag"
            Get-ChildItem $dir -File -Filter "$label-*.tga" -ErrorAction SilentlyContinue | ForEach-Object {
                Copy-Item $_.FullName (Join-Path $Out ($_.Name -replace "^$label-", 'shot-')) -Force
            }
            Copy-IfThere (Join-Path $LogDir "capture-$label-$tag-$probe.log") "$Out\game.log" | Out-Null
        }
        'widescreen' {
            # ours only, at a wide window and at a 4:3 one of its height: wide-<n>.tga and
            # narrow-<n>.tga, and each run's saved game picture (the probe's "savegame
            # <saveShot>") as wide-save.tga and narrow-save.tga
            if ($Side -ne 'ours') { throw 'a widescreen check has no retail side' }
            foreach ($s in 'wide', 'narrow') {
                $size = @(Get-Prop $Check $s)
                $label = "suite-$s"
                $cap = @{ Probe = $probe; Map = $map; Side = 'ours'; Name = $label; Mode = -1; Secs = $(if ($secs) { $secs } else { 120 })
                    OursExtra = "+set r_customwidth $($size[0]) +set r_customheight $($size[1]) $extra $sideExtra".Trim() }
                if ($Bin) { $cap.Bin = $Bin }
                Remove-Item (Join-Path $TestFakk 'save') -Recurse -Force -ErrorAction SilentlyContinue
                $output += & $CaptureScript @cap
                Get-ChildItem (Join-Path $LogDir "captures\$probe-$tag") -File -Filter "$label-*.tga" -ErrorAction SilentlyContinue | ForEach-Object {
                    Copy-Item $_.FullName (Join-Path $Out ($_.Name -replace "^suite-", '')) -Force
                }
                $saveShot = Get-Prop $Check 'saveShot'
                if ($saveShot) { Get-ChildItem (Join-Path $TestFakk 'save') -File -Filter "*_$saveShot.tga" -ErrorAction SilentlyContinue | Select-Object -First 1 | Copy-Item -Destination "$Out\$s-save.tga" -Force }
                Copy-IfThere (Join-Path $LogDir "capture-$label-$tag-$probe.log") $(if ($s -eq 'wide') { "$Out\game.log" } else { "$Out\game-$s.log" }) | Out-Null
            }
        }
        'menus' {
            $menu = $Check.menu
            if ($Side -eq 'retail' -and $menu -eq 'loadsave') {
                # retail lists the saves in its own folder; give it the install's, which ours
                # finds through the install
                New-Item -ItemType Directory -Force "$fakk\save" | Out-Null
                Get-ChildItem "$GameInstall\fakk\save\*" -Include *.ssv, *.tga -ErrorAction SilentlyContinue | Copy-Item -Destination "$fakk\save" -Force
            }
            $cm = @{ Menus = @($menu); Side = $Side; Name = 'suite' }
            if ($Side -eq 'ours' -and $Bin) { $cm.Bin = $Bin }
            $output = & (Join-Path $RetailCompare 'capture-menus.ps1') @cm
            $label = if ($Side -eq 'ours') { 'suite' } else { 'retail' }
            Copy-IfThere (Join-Path $LogDir "captures\menus\$label-$menu.tga") "$Out\shot-0.tga" | Out-Null
            if ($Side -eq 'ours') { Copy-IfThere (Join-Path $LogDir 'menus-suite.log') "$Out\game.log" | Out-Null }
        }
        'chain' {
            $output = & (Join-Path $SuiteDir 'level-chain.ps1') -Side $Side -Out $Out -Bin $Bin -Secs $(if ($secs) { $secs } else { 900 })
            Copy-IfThere "$Out\console.log" "$Out\game.log" | Out-Null
        }
        'saveload' {
            # probe and thread: which script saves the game (probe-save's probe thread by default)
            $sp = if ($probe) { $probe } else { 'probe-save' }; $st = [string](Get-Prop $Check 'thread' 'probe')
            # loadArch: the save is loaded by the other architecture's build (a cross-build check)
            $sl = @{ Map = $map; Probe = $sp; Thread = $st; Bin = $Bin; Extra = $extra }
            $otherArch = Get-OtherArch $Check $SuiteArch
            if ($otherArch) { $sl.LoadBin = Join-Path $RepoRoot "build\$otherArch-Release\Release" }
            $output = & (Join-Path $RetailCompare 'save-load.ps1') @sl
            foreach ($n in 'saved', 'loaded') { Copy-IfThere (Join-Path $LogDir "save-load-$tag-$sp-$n.log") "$Out\$n.log" | Out-Null }
            if (Test-Path "$Out\loaded.log") { Get-Content "$Out\saved.log", "$Out\loaded.log" | Set-Content "$Out\game.log" }
        }
        'sound' {
            $engine = if ($Side -eq 'ours') { 'openal' } else { 'retail' }
            $sr = @{ Engine = $engine; Probe = $probe; Map = $(if ($map) { $map } else { 'zoo' }); Tag = '-suite'; Extra = $extra }
            $vol = Get-Prop $Check 'volume'; if ($vol) { $sr.Volume = [double]$vol }
            $mus = Get-Prop $Check 'music'; if ($mus) { $sr.Music = [double]$mus }
            if ($secs) { $sr.Secs = $secs }
            if ($Side -eq 'ours' -and $Bin) { $sr.Bin = $Bin }
            $sr.Python = Get-SoundPython
            $output = & (Join-Path $RetailCompare 'sound-record.ps1') @sr
            Copy-IfThere (Join-Path $LogDir "sound-$engine-$probe-suite.npy") "$Out\sound.npy" | Out-Null
            Copy-IfThere (Join-Path $LogDir "sound-$engine-$probe-suite.log") "$Out\game.log" | Out-Null
        }
        'build' { $output = Invoke-SuiteBuild $Out $SuiteArch }
        'cik' {
            $output = & $Python -I (Join-Path $RetailCompare 'check_tiki_against_cik.py') "$GameInstall\fakk\pak0.pk3" "$GameInstall\fakk\pak1.pk3" 2>&1
            $output | Set-Content "$Out\output.txt"
        }
        'tiki' {
            $output = & (Join-Path $RetailCompare 'run-client.ps1') -Run '+cachealltikis +quit' -Secs $(if ($secs) { $secs } else { 300 }) -Name suite-tiki -Bin $Bin
            Copy-IfThere (Join-Path $LogDir 'suite-tiki.log') "$Out\game.log" | Out-Null
        }
        'cfgrun' {
            # a config of console commands on the client, from suite.json's "cfg" (one list for
            # both games, or "oursCfg"/"retailCfg"). A line "{maps} a; b {map}" stands for
            # its commands once per retail map with {map} filled in; "{each} ..." the same
            # for the check's own "each" list.
            $lines = @(Get-Prop $Check $(if ($Side -eq 'ours') { 'oursCfg' } else { 'retailCfg' }) (Get-Prop $Check 'cfg' @()))
            $cfg = @()
            foreach ($l in $lines) {
                if ($l -match '^\{maps\}\s*(.*)$') { $tmpl = $Matches[1]; foreach ($m in (Read-SuiteConfig).maps) { $cfg += ($tmpl -replace '\{map\}', $m) -split ';\s*' } }
                elseif ($l -match '^\{each\}\s*(.*)$') { $tmpl = $Matches[1]; foreach ($m in @(Get-Prop $Check 'each' @())) { $cfg += ($tmpl -replace '\{map\}', $m) -split ';\s*' } }
                else { $cfg += $l }
            }
            $cfgName = "suite-$($Check.group).cfg"
            # 640x480 on both, as capture.ps1 runs them, so shots pair
            $run = "+set r_mode 3 $extra $sideExtra +exec $cfgName"
            $s = if ($secs) { $secs } else { 180 }
            $shotDir = Join-Path $fakk 'screenshots'
            Remove-Item $shotDir -Recurse -Force -ErrorAction SilentlyContinue
            Set-GameCfg $fakk $cfgName $cfg
            if ($Side -eq 'ours') {
                $output = & (Join-Path $RetailCompare 'run-client.ps1') -Run $run -Secs $s -Name "suite-$($Check.group)" -Bin $Bin
                Copy-IfThere (Join-Path $LogDir "suite-$($Check.group).log") "$Out\game.log" | Out-Null
            }
            else {
                $console = Join-Path $fakk 'qconsole.log'
                Remove-Item $console -ErrorAction SilentlyContinue
                $p = $null
                try {
                    $p = Start-HarnessGame -Side retail -Exe "$retailDir\fakk2.exe" -WorkDir $retailDir -What "suite $($Check.name) retail" `
                        -Arguments "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 $run"
                    if (-not $p.WaitForExit($s * 1000)) { $output = "retail: stopped after ${s}s" } else { $output = "retail: exited with code $($p.ExitCode)" }
                }
                finally { Stop-HarnessGame $p }
                Copy-IfThere $console "$Out\game.log" | Out-Null
            }
            $n = 0
            Get-ChildItem $shotDir -File -Filter 'shot*' -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { Copy-Item $_.FullName (Join-Path $Out "shot-$n.tga"); $n++ }
        }
        'windowshots' {
            # the client started with no map and pictures of its window at fixed seconds
            # after it starts (suite.json "times")
            $args0 = "+set r_fullscreen 0 +set r_mode 3 +set in_mouse 0 +set s_initsound 0 +set logfile 2 $extra $sideExtra"
            if ($Side -eq 'ours') { $exe = Join-Path $(if ($Bin) { $Bin } else { $EngineBin }) 'openfakk2.exe'; $wd = Split-Path $exe; $args0 = "$OursHomeArgs +set in_nograb 1 $args0" }
            else { $exe = "$retailDir\fakk2.exe"; $wd = $retailDir }
            $console = Join-Path $fakk 'qconsole.log'
            Remove-Item $console -ErrorAction SilentlyContinue
            $p = $null; $n = 0
            try {
                $p = Start-HarnessGame -Side $Side -Exe $exe -WorkDir $wd -Arguments $args0 -What "suite $($Check.name) $Side"
                $t0 = Get-Date
                foreach ($t in @(Get-Prop $Check 'times' @(3, 8, 14))) {
                    $wait = $t0.AddSeconds([double]$t) - (Get-Date)
                    if ($wait.TotalMilliseconds -gt 0) { Start-Sleep -Milliseconds ([int]$wait.TotalMilliseconds) }
                    if ($p.HasExited) { break }
                    if (Save-WindowShot $p (Join-Path $Out "shot-$n.tga")) { $n++ }
                }
                $output = "$Side`: $n window shot(s)"
            }
            finally { Stop-HarnessGame $p }
            Copy-IfThere $console "$Out\game.log" | Out-Null
        }
        'demo' {
            # retail: the probe records demos/<probe>.dm3 and quits, or "demoFile" names a demo
            # someone recorded (in build\demos); the demo is kept in the reference and both
            # games play that one (ours from the reference)
            $rate = [int](Get-Prop $Check 'avidemo' 2); $every = [int](Get-Prop $Check 'every' 1); $keep = @(Get-Prop $Check 'keep' @())
            $play = if ($secs) { $secs } else { 120 }
            $demoFile = Get-Prop $Check 'demoFile'
            if ($Side -eq 'retail' -and $demoFile) {
                $given = Join-Path $DemoDir $demoFile
                if (-not (Copy-IfThere $given "$Out\demo.dm3")) { $output += "no demo: $given is not there (copy the recorded .dm3 into $DemoDir)"; break }
                $output += Invoke-DemoPlayback retail "$Out\demo.dm3" $Out $rate $every $play "$extra $sideExtra".Trim() '' $keep
            }
            elseif ($Side -eq 'retail') {
                $recorded = Join-Path $fakk "demos\$probe.dm3"
                Remove-Item $recorded -ErrorAction SilentlyContinue
                $output = @(& $ProbeClient -Probe $probe -Map $map -Name retail -Retail -Secs $(Get-Prop $Check 'recordSecs' 120) -Extra $extra -Classname none -Entity -1)
                # nothing kept without a demo, so refresh-retail.ps1 leaves the old reference
                if (-not (Copy-IfThere $recorded "$Out\demo.dm3")) { $output += "no demo: the probe did not record demos/$probe.dm3 (see $LogDir\client-retail-$tag-$probe.log)"; break }
                Copy-IfThere (Join-Path $LogDir "client-retail-$tag-$probe.log") "$Out\record.log" | Out-Null
                $output += Invoke-DemoPlayback retail "$Out\demo.dm3" $Out $rate $every $play "$extra $sideExtra".Trim() '' $keep
            }
            else {
                $demo = (Get-CheckDemos $Check | Select-Object -First 1).file
                if (-not (Test-Path $demo)) { throw "no retail demo at $demo (tools\suite\refresh-retail.ps1 -Only $($Check.name))" }
                $output = Invoke-DemoPlayback ours $demo $Out $rate $every $play "$extra $sideExtra".Trim() $Bin $keep
            }
        }
        'timedemo' {
            # ours only: each named demo check's retail demo with timedemo 1, uncapped; the
            # frame durations are logged (cl_timedemoLog) for the measure
            $output = @()
            foreach ($d in @(Get-CheckDemos $Check)) {
                New-Item -ItemType Directory -Force (Join-Path $TestFakk 'demos') | Out-Null
                Copy-Item $d.file (Join-Path $TestFakk 'demos\bench.dm3') -Force
                $durations = Join-Path $TestFakk 'timedemo.txt'
                Remove-Item $durations -ErrorAction SilentlyContinue
                $tag2 = $d.check -replace '[\\/]', '-'
                $output += & (Join-Path $RetailCompare 'run-client.ps1') -Name "suite-timedemo-$tag2" -Bin $Bin -Secs $(if ($secs) { $secs } else { 120 }) `
                    -Run "+set r_mode 3 +set com_maxfps 0 +set r_swapInterval 0 $extra $sideExtra +set cl_timedemoLog timedemo.txt +set timedemo 1 +set nextdemo quit +demo bench"
                Copy-IfThere (Join-Path $LogDir "suite-timedemo-$tag2.log") "$Out\game-$tag2.log" | Out-Null
                Copy-IfThere $durations "$Out\durations-$tag2.txt" | Out-Null
            }
        }
        default { throw "unknown check kind '$($Check.kind)'" }
    }
    $output | Out-String -Width 400 | Set-Content "$Out\output.txt"
    $output
}

# ------------------------------------------------------------------------------------
# The build and its warnings. Ninja prints each compiler's output after the line naming
# its target, so the warnings can be filed by target; the ledger keeps each target's
# warnings from the last build that compiled it, and its total is what a full build would
# print. A ledger is started from a full build's log.
# ------------------------------------------------------------------------------------
function Update-WarningLedger([string]$Log, [string]$LedgerFile) {
    $ledger = @{}
    if (Test-Path $LedgerFile) {
        $saved = Get-Content $LedgerFile -Raw | ConvertFrom-Json
        foreach ($p in $saved.PSObject.Properties) { $ledger[$p.Name] = @($p.Value) }
    }
    $target = $null
    foreach ($line in Get-Content $Log) {
        if ($line -match '^\[\d+/\d+\] (?:Building \S+ object|Linking \S+ \S+(?: library)?) (.+)$') { $target = $Matches[1].Trim(); $ledger[$target] = @(); continue }
        if ($line -match '^\[\d+/\d+\] ') { $target = $null; continue }
        if ($target -and $line -match '\bwarning [A-Z]+\d+') { $ledger[$target] = @($ledger[$target]) + ($line -replace '^.*?([^\\/]+\(\d+\)):', '$1:') }
    }
    $ledger | ConvertTo-Json -Depth 4 | Set-Content $LedgerFile -Encoding utf8
    $total = 0; foreach ($v in $ledger.Values) { $total += @($v).Count }
    $total
}

function Invoke-SuiteBuild([string]$Out, [string]$Arch = 'x86') {
    $script = Join-Path $RepoRoot 'scripts\build-msvc.cmd'
    if (-not (Test-Path $script)) {
        # build* is in .gitignore, so a new worktree has no build script
        Copy-Item (Join-Path $MainRoot 'scripts\build-msvc.cmd') $script
    }
    $log = Join-Path $Out 'build.log'
    $ledgerFile = Join-Path $SuiteLocal $(if ($Arch -eq 'x86') { 'warnings-ledger.json' } else { "warnings-ledger-$Arch.json" })
    New-Item -ItemType Directory -Force $SuiteLocal | Out-Null
    if (-not (Test-Path $ledgerFile)) {
        # no ledger yet: without its objects every target is compiled again, so each
        # target's warnings are seen once
        Get-ChildItem (Join-Path $RepoRoot "build\$Arch-Release") -Recurse -File -Include *.obj -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    cmd /c "`"$script`" $Arch Release > `"$log`" 2>&1"
    $code = $LASTEXITCODE
    Set-Content "$Out\exit.txt" $code
    if ($code -ne 0) { return "build failed (exit $code)" }
    $total = Update-WarningLedger $log $ledgerFile
    Set-Content "$Out\warnings.txt" $total
    "build ok, $total warnings"
}

# ------------------------------------------------------------------------------------
# Measuring. Each returns @{ metrics = ordered name -> number (0 is a match; lower is
# better); growth = names judged only against ours' last good result; evidence = lines;
# images = pair pictures made }. $Retail is the reference folder (or $null for checks
# without one), $Ours the run's folder.
# ------------------------------------------------------------------------------------
function Get-Percent([string[]]$Text) {
    $m = [regex]::Match(($Text -join "`n"), 'pixels differing by more than \d+: \d+ \(([\d.]+)%\)')
    if ($m.Success) { [double]$m.Groups[1].Value } else { $null }
}

function Measure-Shots([string]$Retail, [string]$Ours, [string]$Evidence, [string]$Baseline = '', [string]$Label = 'retail') {
    $metrics = [ordered]@{}; $lines = @(); $images = @()
    $names = @(Get-ChildItem $Retail, $Ours -File -Filter 'shot-*.tga' -ErrorAction SilentlyContinue | ForEach-Object Name | Sort-Object -Unique { [int]($_ -replace '\D', '') })
    if (-not $names.Count) { $metrics['shots'] = 100; return @{ metrics = $metrics; evidence = @('no screenshots on either side'); images = @() } }
    foreach ($n in $names) {
        $i = [int]($n -replace '\D', '')
        $a = Join-Path $Retail $n; $b = Join-Path $Ours $n
        if (-not (Test-Path $a)) { $metrics["shot$i"] = 100; $lines += "shot $i`: ours took it, retail did not"; continue }
        if (-not (Test-Path $b)) { $metrics["shot$i"] = 100; $lines += "shot $i`: missing (retail took it)"; continue }
        $png = Join-Path $Evidence "pair-$Label-$i.png"
        $text = & $Python -I $ShotsPy pair $a $b $png --mask-notify 2>&1
        $pct = Get-Percent $text
        $metrics["shot$i"] = if ($null -eq $pct) { 100 } else { $pct }
        $lines += "shot $i`: $(@($text | Select-String 'mean abs|pixels differing') -join '; ')"
        $images += $png
        if ($Baseline -and (Test-Path (Join-Path $Baseline $n))) {
            $png2 = Join-Path $Evidence "pair-lastgood-$i.png"
            $t2 = & $Python -I $ShotsPy pair (Join-Path $Baseline $n) $b $png2 --mask-notify 2>&1
            $p2 = Get-Percent $t2
            $lines += "shot $i against ours' last good: $p2% of pixels differ"
            $images += $png2
        }
    }
    @{ metrics = $metrics; evidence = $lines; images = $images }
}

# A widescreen check's shots: each wide shot's middle against the 4:3 one of its height,
# below the HUD (suite.json "top", in the shots' pixels), and the two saved game pictures
# whole. Both should match whatever the scene: the 3D view keeps the 4:3 view's height,
# and a saved game's picture is the 4:3 middle.
function Measure-Widescreen($Check, [string]$Ours, [string]$Evidence) {
    $metrics = [ordered]@{}; $lines = @(); $images = @()
    $top = [int](Get-Prop $Check 'top' 0)
    $names = @(Get-ChildItem $Ours -File -Filter 'wide-*.tga' | Where-Object Name -match '^wide-\d+\.tga$' | ForEach-Object Name | Sort-Object { [int]($_ -replace '\D', '') })
    if (-not $names.Count) { $metrics['shots'] = 100; return @{ metrics = $metrics; evidence = @('no wide screenshots'); images = @() } }
    foreach ($n in $names) {
        $i = [int]($n -replace '\D', '')
        $narrow = Join-Path $Ours "narrow-$i.tga"
        if (-not (Test-Path $narrow)) { $metrics["shot$i"] = 100; $lines += "shot $i`: the 4:3 run did not take it"; continue }
        $png = Join-Path $Evidence "middle-$i.png"
        $text = & $Python -I $ShotsPy middle (Join-Path $Ours $n) $narrow $png --top $top 2>&1
        $pct = Get-Percent $text
        $metrics["shot$i"] = if ($null -eq $pct) { 100 } else { $pct }
        $lines += "shot $i, the wide one's middle against the 4:3 one below row $top`: $(@($text | Select-String 'mean abs|pixels differing') -join '; ')"
        $images += $png
    }
    if (Get-Prop $Check 'saveShot') {
        $a = Join-Path $Ours 'wide-save.tga'; $b = Join-Path $Ours 'narrow-save.tga'
        if ((Test-Path $a) -and (Test-Path $b)) {
            $png = Join-Path $Evidence 'save.png'
            $text = & $Python -I $ShotsPy pair $a $b $png 2>&1
            $pct = Get-Percent $text
            $metrics['save'] = if ($null -eq $pct) { 100 } else { $pct }
            $lines += "saved game pictures, wide against 4:3: $(@($text | Select-String 'mean abs|pixels differing') -join '; ')"
            $images += $png
        }
        else { $metrics['save'] = 100; $lines += 'a run made no saved game picture' }
    }
    @{ metrics = $metrics; evidence = $lines; images = $images }
}

function Measure-PlayerLogs([string]$RetailLog, [string]$OursLog, [double]$Tol, [string[]]$Fields = @(), [switch]$ReadingsOnly, [int[]]$Entities = @(), [string[]]$Classnames = @(), [string[]]$Labels = @('retail', 'ours')) {
    $metrics = [ordered]@{}; $evidence = @()
    if (-not $ReadingsOnly) {
        $a = Get-Dump $RetailLog; $b = Get-Dump $OursLog
        if (-not $b.Count) { $metrics['dump'] = 1; $evidence += 'ours printed no levelvars/pendingevents dump (the probe did not finish)' }
        else {
            $varsA = @($a | Where-Object { $_ -match '^\S.* = ' }); $varsB = @($b | Where-Object { $_ -match '^\S.* = ' })
            $varDiff = @(Compare-Object $varsA $varsB)
            $ka = Get-EventKinds $a; $kb = Get-EventKinds $b
            $kindDiff = @(($ka.Keys + $kb.Keys | Sort-Object -Unique) | Where-Object { [int]$ka[$_] -ne [int]$kb[$_] })
            $metrics['vars'] = $varDiff.Count
            $metrics['events'] = $kindDiff.Count
            $evidence += "variables: $($varsA.Count) $($Labels[0]) / $($varsB.Count) $($Labels[1]), differing lines $($varDiff.Count); event kinds differing $($kindDiff.Count)"
            $evidence += @($varDiff | Select-Object -First 8 | ForEach-Object { "  var {0} {1}" -f ($_.SideIndicator -replace '<=', "$($Labels[0]):" -replace '=>', "$($Labels[1]):  "), $_.InputObject })
            $evidence += @($kindDiff | Select-Object -First 8 | ForEach-Object { "  event {0,-40} $($Labels[0]) {1,4}  $($Labels[1]) {2,4}" -f $_, [int]$ka[$_], [int]$kb[$_] })
        }
    }
    $ra = Get-Readings $RetailLog; $rb = Get-Readings $OursLog
    if ($Entities.Count -or $Classnames.Count) {
        $keep = { param($r) ($Entities -contains [int]$r['Entity #']) -or ($Classnames -contains $r['Classname']) }
        $ra = @($ra | Where-Object { & $keep $_ }); $rb = @($rb | Where-Object { & $keep $_ })
    }
    if ($ra.Count -or $rb.Count) {
        $r = Compare-Readings $ra $rb $Tol 8 $Fields -Labels $Labels
        $metrics['readings'] = ($r.Compared - $r.Identical) + [Math]::Abs($r.CountA - $r.CountB)
        $evidence += Format-ReadingsLine $r -Labels $Labels
        $evidence += @($r.Examples)
    }
    @{ metrics = $metrics; evidence = $evidence; images = @() }
}

# A cross-build save check: "loadArch" names the build that loads the save ("other" is
# the other architecture than the lane's), so that an x86 lane's run has x64 load x86's
# save and an x64 lane's run the reverse. Empty for an ordinary check.
function Get-OtherArch($Check, [string]$Arch) {
    $la = [string](Get-Prop $Check 'loadArch' '')
    if (-not $la) { return '' }
    if ($la -eq 'other') { return $(if ($Arch -eq 'x86') { 'x64' } else { 'x86' }) }
    if ($la -eq $Arch) { return '' }
    $la
}

# One lane's result against the other's (run-suite.ps1 -Against): the same measures as
# against retail, with the other build's last good result in retail's place. Its shots
# when both have them, else its readings when the log has whatis blocks, else the game
# lines of its log; $null when there is nothing of it to compare.
function Measure-Against($Check, [string]$Ours, [string]$Other, [string]$Evidence) {
    New-Item -ItemType Directory -Force $Evidence | Out-Null
    $oursLog = Join-Path $Ours 'game.log'; $otherLog = Join-Path $Other 'game.log'
    $shots = @(Get-ChildItem $Other -File -Filter 'shot-*.tga' -ErrorAction SilentlyContinue)
    if ($shots.Count -and @(Get-ChildItem $Ours -File -Filter 'shot-*.tga' -ErrorAction SilentlyContinue).Count) { return Measure-Shots $Other $Ours $Evidence -Label x86 }
    if (-not (Test-Path $oursLog) -or -not (Test-Path $otherLog)) { return $null }
    $sel = Get-Prop $Check 'select'
    $ents = @(Get-Prop $sel 'entities' @() | ForEach-Object { [int]$_ }); $cls = @(Get-Prop $sel 'classnames' @())
    if (Select-String -Path $otherLog -Pattern 'Entity #\s*: \d+' -Quiet) {
        return Measure-PlayerLogs $otherLog $oursLog 0 -Fields @(Get-Prop $Check 'fields' @()) -Entities $ents -Classnames $cls -Labels x86, ours
    }
    $a = @(Get-GameLines $otherLog); $b = @(Get-GameLines $oursLog)
    $d = @(Compare-Object $a $b)
    $onlyA = @($d | Where-Object SideIndicator -eq '<='); $onlyB = @($d | Where-Object SideIndicator -eq '=>')
    $ev = @("x86 $($a.Count) lines, ours $($b.Count) lines; only in x86: $($onlyA.Count), only in ours: $($onlyB.Count)")
    $ev += @($onlyA | Select-Object -First 8 | ForEach-Object { "  x86:  $($_.InputObject)" })
    $ev += @($onlyB | Select-Object -First 8 | ForEach-Object { "  ours: $($_.InputObject)" })
    @{ metrics = [ordered]@{ lines = $onlyA.Count + $onlyB.Count }; evidence = $ev; images = @() }
}

function Measure-Check($Check, [string]$Ours, [string]$Retail, [string]$Evidence, [string]$Baseline = '') {
    New-Item -ItemType Directory -Force $Evidence | Out-Null
    $growth = @()
    $oursLog = Join-Path $Ours 'game.log'
    $tol = [double](Get-Prop $Check 'valueTolerance' 0.01)
    $result = $null
    switch ($Check.kind) {
        'spawn' {
            $a = @(Get-GameLines (Join-Path $Retail 'game.log')); $b = @(Get-GameLines $oursLog)
            $d = @(Compare-Object $a $b)
            $onlyRetail = @($d | Where-Object SideIndicator -eq '<=')
            $onlyOurs = @($d | Where-Object SideIndicator -eq '=>')
            $metrics = [ordered]@{ spawn = $(if (-not $b.Count) { 1000 } else { $onlyRetail.Count + $onlyOurs.Count }) }
            $warn = Get-WarningLines $oursLog
            $metrics['warnings'] = $warn.Count
            $growth += 'warnings'
            $ev = @("retail $($a.Count) lines, ours $($b.Count) lines; only in retail: $($onlyRetail.Count), only in ours: $($onlyOurs.Count); warnings and errors in ours' log: $($metrics.warnings)")
            $ev += @($onlyRetail | Select-Object -First 8 | ForEach-Object { "  retail: $($_.InputObject)" })
            $ev += @($onlyOurs | Select-Object -First 8 | ForEach-Object { "  ours:   $($_.InputObject)" })
            $ev += Format-WarningGroups $warn 4
            $result = @{ metrics = $metrics; evidence = $ev; images = @() }
        }
        'player' {
            # select and fields, as for client: which readings are compared, and which of their fields
            $sel = Get-Prop $Check 'select'
            $ents = @(Get-Prop $sel 'entities' @() | ForEach-Object { [int]$_ }); $cls = @(Get-Prop $sel 'classnames' @())
            $result = Measure-PlayerLogs (Join-Path $Retail 'game.log') $oursLog $tol -Fields @(Get-Prop $Check 'fields' @()) -Entities $ents -Classnames $cls
        }
        'client' {
            $sel = Get-Prop $Check 'select'
            $ents = @(Get-Prop $sel 'entities' @() | ForEach-Object { [int]$_ }); $cls = @(Get-Prop $sel 'classnames' @())
            $fields = @(Get-Prop $Check 'fields' @())
            if (Get-Prop $Check 'oursOnly' $false) {
                $done = [bool](Select-String -Path $oursLog -Pattern 'pending events as of' -Quiet -ErrorAction SilentlyContinue)
                $metrics = [ordered]@{ finished = $(if ($done) { 0 } else { 1 }) }
                $ev = @($(if ($done) { 'the probe finished' } else { 'the probe did not finish' }))
                # logCount: how many lines of the log match a pattern (a count that is judged
                # against the last good result, so give it a tolerance when it varies)
                $lc = Get-Prop $Check 'logCount'
                if ($lc) {
                    $pattern = Get-Prop $lc 'pattern'
                    $matches = @(Select-String -Path $oursLog -Pattern $pattern -ErrorAction SilentlyContinue).Count
                    $metrics['matches'] = $matches
                    $ev += "lines matching '$pattern': $matches"
                }
                $result = @{ metrics = $metrics; evidence = $ev; images = @() }
            }
            else { $result = Measure-PlayerLogs (Join-Path $Retail 'game.log') $oursLog $tol -Fields $fields -ReadingsOnly -Entities $ents -Classnames $cls }
        }
        { $_ -in 'capture', 'menus', 'windowshots', 'demo' } { $result = Measure-Shots $Retail $Ours $Evidence $Baseline }
        'timedemo' {
            # the mean frame time of each demo, without its first frames (where textures are
            # uploaded as they are first seen); ioquake3's own line for the evidence
            $metrics = [ordered]@{ finished = 0 }; $ev = @()
            foreach ($d in @(Get-CheckDemos $Check)) {
                $tag2 = $d.check -replace '[\\/]', '-'
                $ms = @(Get-Content (Join-Path $Ours "durations-$tag2.txt") -ErrorAction SilentlyContinue | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
                $line = Select-String -Path (Join-Path $Ours "game-$tag2.log") -Pattern '^\d+ frames .* ms$' -ErrorAction SilentlyContinue | Select-Object -Last 1
                $skip = [int](Get-Prop $Check 'skipFrames' 20)
                if ($ms.Count -le $skip -or -not $line) { $metrics.finished = 1; $ev += "$($d.check): the demo did not play to the end"; continue }
                $rest = @($ms | Select-Object -Skip $skip)
                $mean = [Math]::Round(($rest | Measure-Object -Average).Average, 3)
                $sorted = @($rest | Sort-Object)
                $metrics["ms_$tag2"] = $mean
                $growth += "ms_$tag2"
                $ev += "$($d.check): mean $mean ms over frames $skip to $($ms.Count) (median $($sorted[[int]($sorted.Count / 2)]), max $($sorted[-1])); $($line.Line)"
            }
            $result = @{ metrics = $metrics; evidence = $ev; images = @() }
        }
        'widescreen' { $result = Measure-Widescreen $Check $Ours $Evidence }
        'cfgrun' {
            $metrics = [ordered]@{}; $ev = @(); $images = @()
            $marker = Get-Prop $Check 'doneMarker'
            if ($marker) {
                $done = [bool](Select-String -Path $oursLog -SimpleMatch $marker -Quiet -ErrorAction SilentlyContinue)
                $metrics['finished'] = if ($done) { 0 } else { 1 }
                $ev += $(if ($done) { "reached '$marker'" } else { "never reached '$marker'" })
            }
            if (Get-Prop $Check 'countWarnings' $false) {
                $warn = Get-WarningLines $oursLog
                $metrics['warnings'] = $warn.Count
                $growth += 'warnings'
                $ev += "warnings and errors in the log: $($metrics.warnings)"
                $ev += Format-WarningGroups $warn
            }
            if (Get-Prop $Check 'frameTimes' $false) {
                # com_speeds lines, split by "==== frametime <map>" markers printed after each
                # map's run; the mean, as the frames take a millisecond or less and com_speeds
                # prints whole milliseconds
                $cur = @(); $per = [ordered]@{}
                foreach ($line in Get-Content $oursLog -ErrorAction SilentlyContinue) {
                    if ($line -match '^frame:\s*\d+\s+all:\s*(\d+)') { $cur += [int]$Matches[1]; continue }
                    if ($line -match '^==== frametime (\S+)') { if ($cur.Count) { $per[$Matches[1]] = [Math]::Round(($cur | Measure-Object -Average).Average, 2) }; $cur = @() }
                }
                foreach ($k in $per.Keys) { $metrics["ms_$k"] = $per[$k]; $growth += "ms_$k" }
                $ev += "mean frame time (ms, com_speeds 'all'): " + (($per.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', ')
                if (-not $per.Count) { $metrics['finished'] = 1; $ev += 'no com_speeds output' }
            }
            if ($Retail -and (Test-Path $Retail)) { $shots = Measure-Shots $Retail $Ours $Evidence $Baseline; foreach ($k in $shots.metrics.Keys) { $metrics[$k] = $shots.metrics[$k] }; $ev += $shots.evidence; $images += $shots.images }
            $result = @{ metrics = $metrics; evidence = $ev; images = $images }
        }
        'chain' {
            $a = Get-Content (Join-Path $Retail 'chain.json') -Raw | ConvertFrom-Json
            $b = Get-Content (Join-Path $Ours 'chain.json') -Raw | ConvertFrom-Json
            $steps = 0; $saves = 0; $inv = 0; $ev = @("retail reached $($a.reached) of $($a.steps) steps, ours $($b.reached) ($($b.state))")
            for ($i = 0; $i -lt @($a.result).Count; $i++) {
                $x = $a.result[$i]; $y = $b.result | Where-Object step -eq $x.step | Select-Object -First 1
                if (-not $y) { $steps++; continue }
                if ($x.reached -ne $y.reached) { $steps++; $ev += "  $($x.step) $($x.map): reached on retail $($x.reached), ours $($y.reached)"; continue }
                if ($x.autosave -ne $y.autosave) { $saves++; $ev += "  $($x.step) $($x.map): autosave on retail $($x.autosave), ours $($y.autosave)" }
                $ia = ($x.inventory.PSObject.Properties | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
                $ib = ($y.inventory.PSObject.Properties | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
                if ($ia -ne $ib) { $inv++; if ($inv -le 6) { $ev += "  $($x.step) $($x.map): inventory retail [$ia] ours [$ib]" } }
            }
            $result = @{ metrics = [ordered]@{ steps = $steps; autosaves = $saves; inventory = $inv }; evidence = $ev; images = @() }
        }
        'saveload' {
            $text = Get-Content (Join-Path $Ours 'output.txt') -ErrorAction SilentlyContinue
            $vars = [regex]::Match(($text -join "`n"), 'level variables differing: (\d+)')
            $rd = [regex]::Match(($text -join "`n"), 'whatis readings: (\d+) saved / (\d+) loaded; identical (\d+) of (\d+)')
            $metrics = [ordered]@{}
            if (-not $vars.Success -or -not $rd.Success) { $metrics['finished'] = 1 }
            else {
                $metrics['vars'] = [int]$vars.Groups[1].Value
                $metrics['readings'] = ([int]$rd.Groups[4].Value - [int]$rd.Groups[3].Value) + [Math]::Abs([int]$rd.Groups[1].Value - [int]$rd.Groups[2].Value)
            }
            $result = @{ metrics = $metrics; evidence = @($text | Where-Object { $_ -match '\S' } | Select-Object -First 12); images = @() }
        }
        'sound' {
            $text = & (Get-SoundPython) -I (Join-Path $SuiteDir 'sound_metric.py') (Join-Path $Retail 'sound.npy') (Join-Path $Ours 'sound.npy') ([string](Get-Prop $Check 'measure' 'tone')) 2>&1
            $m = [regex]::Match(($text -join "`n"), 'max level difference: ([\d.]+) dB')
            $metrics = [ordered]@{ db = $(if ($m.Success) { [double]$m.Groups[1].Value } else { 100 }) }
            $result = @{ metrics = $metrics; evidence = @($text | ForEach-Object { "$_" }); images = @() }
        }
        'build' {
            $code = [int](Get-Content (Join-Path $Ours 'exit.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)
            $metrics = [ordered]@{ failed = $(if ($code) { 1 } else { 0 }) }
            $ev = @()
            if ($code) { $ev += @(Select-String -Path (Join-Path $Ours 'build.log') -Pattern ': (fatal )?error ' | Select-Object -First 12 | ForEach-Object Line) }
            else {
                $metrics['warnings'] = [int](Get-Content (Join-Path $Ours 'warnings.txt'))
                $growth += 'warnings'
                $ev += "warnings in a full build: $($metrics.warnings)"
                $ev += @(Select-String -Path (Join-Path $Ours 'build.log') -Pattern '\bwarning [A-Z]+\d+' | Select-Object -First 6 | ForEach-Object Line)
            }
            $result = @{ metrics = $metrics; evidence = $ev; images = @() }
        }
        'cik' {
            $text = Get-Content (Join-Path $Ours 'output.txt')
            $bad = @($text | Select-String '^\s*\[(bounds|radius)\] (\S+)' | ForEach-Object { $_.Matches[0].Groups[2].Value } | Sort-Object -Unique)
            $result = @{ metrics = [ordered]@{ models = $bad.Count }; evidence = @("models whose bounds or radius differ from the retail engine's .cik cache: $($bad.Count)") + @($bad | Select-Object -First 8 | ForEach-Object { "  $_" }); images = @() }
        }
        'tiki' {
            $lines = @(Get-Content $oursLog -ErrorAction SilentlyContinue)
            $start = ($lines | Select-String 'tiki files need to be cached' | Select-Object -First 1).LineNumber
            $metrics = [ordered]@{}
            if (-not $start) { $metrics['finished'] = 1; $ev = @('cachealltikis did not run') }
            else {
                $section = $lines[$start..($lines.Count - 1)]
                $bad = @($section | Where-Object { $_ -match '(?i)^\^?\d?(WARNING|ERROR)|couldn''t|can''t|failed|unknown|bad ' })
                $count = ($lines | Select-String 'tiki files need to be cached').Line -replace '\D', ''
                $metrics['problems'] = $bad.Count
                $growth += 'problems'
                $ev = @("$count definitions loaded; warnings and errors: $($bad.Count)") + @($bad | Group-Object { $_ -replace '\S+\.(tik|tan|skb|ska)', '<file>' } | Sort-Object Count -Descending | Select-Object -First 8 | ForEach-Object { "  x$($_.Count): $($_.Group[0])" })
            }
            $result = @{ metrics = $metrics; evidence = $ev; images = @() }
        }
        default { throw "unknown check kind '$($Check.kind)'" }
    }
    $result.growth = $growth
    $result
}

# Lines in a log that say the engine crashed, and the named stack (tools\crash-symbols.py).
function Find-Crash([string[]]$Logs, [string]$Bin) {
    foreach ($log in $Logs) {
        if (-not (Test-Path $log)) { continue }
        if ([IO.Path]::GetFileName($log) -eq 'output.txt') {
            # the runners' own lines: a game that exited with a code other than 0 crashed,
            # even when it could not write a report (0xC0000028, a bad stack, writes none)
            $exit = Select-String -Path $log -Pattern '(?:exited with code|exited early \(code|server exited \(code) (-?\d+)' | Where-Object { $_.Matches[0].Groups[1].Value -ne '0' } | Select-Object -First 1
            if ($exit) { $code = [int64]$exit.Matches[0].Groups[1].Value; if ($code -lt 0) { $code += 4294967296 }; return @(("the game exited with code 0x{0:X8} ({1})" -f $code, $exit.Line.Trim())) }
            continue
        }
        $hit = Select-String -Path $log -Pattern '^(Fatal exception 0x|Sys_Error|DOUBLE SIGNAL FAULT)|Received signal \d' | Select-Object -First 1
        if ($hit) {
            $symArgs = @((Join-Path $RepoRoot 'tools\crash-symbols.py'), $log)
            if ($Bin) { $symArgs += $Bin }
            $stack = & $Python -I @symArgs 2>&1 | Select-Object -First 30
            return @("$($hit.Line) ($([IO.Path]::GetFileName($log)))") + @($stack)
        }
    }
    # nothing found: no output, so @(Find-Crash ...) is empty
}
