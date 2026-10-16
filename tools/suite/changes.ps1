# What a branch has changed: the files under code\ and tools\ that differ from where the
# branch started (its merge base with main, or a given revision), committed or not,
# untracked ones included, and a hash of those changes' content; and the checks its
# commits declare they move. Dot-sourced by run-suite.ps1 and verify-gate.ps1; it only
# runs git, so it is quick enough for a hook.

function Get-BranchBase([string]$Root, [string]$Since = '') {
    if ($Since) { return [string](git -C $Root rev-parse $Since 2>$null) }
    $base = (git -C $Root merge-base HEAD main 2>$null)
    if (-not $base) { $base = (git -C $Root rev-parse HEAD 2>$null) }
    [string]$base
}

# The changed files (repository paths, forward slashes) under the given folders.
function Get-ChangedFiles([string]$Root, [string[]]$Under = @('code/', 'tools/'), [string]$Since = '') {
    $base = Get-BranchBase $Root $Since
    $files = @()
    if ($base) { $files += @(git -C $Root diff --name-only $base -- 2>$null) }
    $files += @(git -C $Root ls-files --others --exclude-standard 2>$null)
    @($files | Where-Object { $f = $_; $_ -and @($Under | Where-Object { $f.StartsWith($_) }).Count } | Sort-Object -Unique)
}

# A hash of the changes under code\ and tools\: the diff from the base (which includes
# uncommitted edits) and the content of untracked files. Empty when nothing there changed.
function Get-ChangeHash([string]$Root) {
    $files = @(Get-ChangedFiles $Root)
    if (-not $files.Count) { return '' }
    $base = Get-BranchBase $Root
    $text = New-Object System.Text.StringBuilder
    if ($base) { [void]$text.Append((git -C $Root diff $base -- code tools 2>$null | Out-String)) }
    foreach ($f in @(git -C $Root ls-files --others --exclude-standard -- code tools 2>$null)) {
        $p = Join-Path $Root $f
        if (Test-Path $p -PathType Leaf) { [void]$text.Append("$f`n"); [void]$text.Append([Convert]::ToBase64String([IO.File]::ReadAllBytes($p))) }
    }
    $sha = [Security.Cryptography.SHA1]::Create()
    ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text.ToString()))) -replace '-', '').Substring(0, 16)
}

# The checks the commits since the base say they move on purpose, from trailers
#   Suite-moves: crosshair, shots/*: the crosshair is now drawn at retail's size
# (check names or groups, wildcards allowed, then a colon and why). run-suite.ps1 reports
# such a check as moved rather than newly failing; -AcceptMoves makes its result the new
# last good one. Only real git trailers count (the message's last paragraph, beside
# Co-Authored-By), as git itself reads them: a Suite-moves line quoted in a message's
# body, as an example, declares nothing.
function Get-DeclaredMoves([string]$Root, [string]$Since = '') {
    $base = Get-BranchBase $Root $Since
    if (-not $base) { return }
    $records = (git -C $Root log --format='%h%x1f%(trailers:key=Suite-moves,valueonly,unfold)%x1e' "$base..HEAD" 2>$null | Out-String) -split [char]0x1e
    foreach ($rec in $records) {
        $parts = $rec.Trim() -split [char]0x1f, 2
        if ($parts.Count -lt 2) { continue }
        foreach ($line in ($parts[1] -split "`n")) {
            if ($line -match '^\s*([^:]+?)\s*(?::\s*(.*))?$') {
                $why = if ($Matches[2]) { $Matches[2].Trim() } else { '' }
                foreach ($p in ($Matches[1] -split ',')) {
                    if ($p.Trim()) { [pscustomobject]@{ Pattern = $p.Trim(); Reason = $why; Commit = $parts[0] } }
                }
            }
        }
    }
}
