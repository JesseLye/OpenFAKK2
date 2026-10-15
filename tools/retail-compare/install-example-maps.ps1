# Installs Ritual's example maps into the retail and OpenFAKK2 sandboxes under build\sandbox,
# so probes can run on them as example/<map> (see probe-scripting.md, "Example maps").
# Takes them from the tools release clone ($RitualTools, FAKK2_TOOLS); nothing is copied
# into the repository.
. (Join-Path $PSScriptRoot 'common.ps1')
Install-ExampleMaps
