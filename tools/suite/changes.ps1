# What a branch has changed: the files under code\ and tools\ that differ from where the
# branch started (its merge base with main), committed or not, untracked ones included,
# and a hash of those changes' content. Dot-sourced by run-suite.ps1 and verify-gate.ps1;
# it only runs git, so it is quick enough for a hook.

function Get-BranchBase([string]$Root) {
    $base = (git -C $Root merge-base HEAD main 2>$null)
    if (-not $base) { $base = (git -C $Root rev-parse HEAD 2>$null) }
    [string]$base
}

# The changed files (repository paths, forward slashes) under the given folders.
function Get-ChangedFiles([string]$Root, [string[]]$Under = @('code/', 'tools/')) {
    $base = Get-BranchBase $Root
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
