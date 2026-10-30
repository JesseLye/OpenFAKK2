# Installs Ritual's example maps (tools\retail-compare\maps\example, from the fakk2-sdk
# release) into the retail and OpenFAKK2 sandboxes under build\sandbox, so probes can run
# on them as example/<map> (see probe-scripting.md, "Example maps"). Every runner given
# such a map does this by itself; this does it by hand.
. (Join-Path $PSScriptRoot 'common.ps1')
Install-ExampleMaps
