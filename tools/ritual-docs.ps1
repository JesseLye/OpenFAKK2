# Converts the design documents in Ritual's Tools 1.02 release ($RitualTools, see
# tools\retail-compare\common.ps1) into text under build\ritual-docs, so they can be
# searched. Every docs\*.doc becomes a .txt of the same name, one paragraph per line;
# docs\commands.txt and entities\entities.def are copied as they are. Run it again to
# refresh; it overwrites what it wrote before.
#
# The output stays under build\, which git ignores: Ritual's licence does not allow
# copying the release, so nothing converted here may be committed.
#
# The documents describe what Ritual intended, not what fakk2.exe does. Use them as leads
# and confirm against the binary or a retail run before relying on them.
#
# Needs antiword, which Git for Windows ships in mingw64\bin. Override with ANTIWORD.
param(
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'retail-compare\common.ps1')
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'build\ritual-docs' }

$docs = Join-Path $RitualTools 'docs'
if (-not (Test-Path $docs)) { throw "No docs folder in $RitualTools; set FAKK2_TOOLS to the clone of Ritual's tools release" }

$antiword = $env:ANTIWORD
if (-not $antiword) {
    $found = Get-Command antiword -ErrorAction SilentlyContinue
    if ($found) { $antiword = $found.Source }
}
if (-not $antiword) {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        # git.exe is in <Git>\cmd or <Git>\mingw64\bin
        foreach ($root in @((Split-Path (Split-Path $git.Source)), (Split-Path (Split-Path (Split-Path $git.Source))))) {
            $candidate = Join-Path $root 'mingw64\bin\antiword.exe'
            if (Test-Path $candidate) { $antiword = $candidate; break }
        }
    }
}
if (-not $antiword -or -not (Test-Path $antiword)) { throw 'antiword not found; install it or set ANTIWORD to antiword.exe' }

# antiword looks for its character maps in ANTIWORDHOME, then in HOME (and warns when HOME
# is unset, as it is outside Git Bash). Point it at the maps that ship beside it.
$share = Join-Path (Split-Path (Split-Path $antiword)) 'share\antiword'
if (-not $env:ANTIWORDHOME -and (Test-Path $share)) { $env:ANTIWORDHOME = $share }
if (-not $env:HOME) { $env:HOME = $env:USERPROFILE }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false
$savedEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = $utf8
try {
    foreach ($doc in Get-ChildItem $docs -Filter *.doc | Sort-Object Name) {
        # -w 0: one paragraph per line, so a search hit shows the whole paragraph
        $text = & $antiword -m UTF-8.txt -w 0 $doc.FullName
        if ($LASTEXITCODE -ne 0) { throw "antiword failed on $($doc.Name)" }
        $out = Join-Path $OutDir ($doc.BaseName + '.txt')
        [System.IO.File]::WriteAllLines($out, [string[]]$text, $utf8)
        $words = ($text -join ' ' -split '\s+' | Where-Object { $_ }).Count
        '{0,6} words  {1}' -f $words, ($doc.BaseName + '.txt')
    }
} finally {
    [Console]::OutputEncoding = $savedEncoding
}

foreach ($file in @((Join-Path $docs 'commands.txt'), (Join-Path $RitualTools 'entities\entities.def'))) {
    if (Test-Path $file) {
        Copy-Item $file $OutDir -Force
        '        copied {0}' -f (Split-Path $file -Leaf)
    } else {
        Write-Warning "$file is missing from the release"
    }
}
"Wrote $OutDir"
