# Comparing against the retail engine

These scripts run the retail `fakk2.exe` and `openfakk2ded` as dedicated servers on the
same map and compare what the game module prints. The retail engine needs the game disc
in a drive. Nothing is written to the game install: the retail engine runs from a copy
under `build\sandbox`, and logs go to `build\compare`.

    # one map, developer-level spawn output
    tools\retail-compare\compare.ps1 -Map zoo

    # with state dumps taken right after spawn
    tools\retail-compare\compare.ps1 -Map zoo -Tag '-dump' -After '+levelvars +gamevars +pendingevents'

Retail output is cached per map and tag in `build\compare`; delete `ded-retail-*.log` to
refresh it. OpenFAKK2 looks for the game module in `build\sandbox\home\fakk` first, so put
the retail `gamex86.dll` there to compare engines with the same game code, or remove it to
test the rebuilt module.

- `bsp_entities.py <pak> <map> <regex>` prints matching entities from a map's entity lump.
- `check_tiki_against_cik.py <pak0> <pak1>` checks the model bounds and radius rules
  against the definitions the retail engine cached in `.cik` files.

## With a player

Most level scripts wait for a player. OpenFAKK2 supplies a headless one (`sv_testplayer 1`);
the retail reference is the real game with nobody at the controls, which shows a small
window while it runs.

    tools\retail-compare\compare-player.ps1 -Map training

Both sides run a probe script, which waits for the player and prints state at fixed game
times, so they are sampled at the same point. The script compares level variables,
pending events by kind, and every `whatis` reading field by field (numbers within
`-Tolerance`). The retail result is cached per map and probe; pass `-Refresh` to run the
retail game again. Choose the probe with `-Probe`:

- `probe` (default) waits ten seconds and dumps `levelvars` and `pendingevents`.
- `probe-walk` prints the player with `whatis 0`, holds `+forward` for three seconds and
  prints it again.
- `probe-entities` runs `whatis` on entities 0 to 299 after ten seconds.
- `probe-entry` prints the player six times in the first five seconds after entry.

`-RetailTwice` runs the retail game a second time in place of OpenFAKK2. The game seeds
its random numbers from the clock, so retail differs from itself on some maps; this shows
by how much. `-Extra "<arguments>"` adds to OpenFAKK2's command line only.

A probe must end with `pendingevents`; that is how the script knows it has finished.

## The client and the map viewer

    toolsetail-compareun-client.ps1 -Run "+viewmap zoo +wait 30 +screenshotJPEG +wait 5 +quit"

runs the OpenFAKK2 client in a window against the retail data, copies its console log
to `build\compare\client.log` (`-Name` changes the name) and stops it after `-Secs`
seconds if it has not quit. Screenshots land in `build\sandbox\homeakk\screenshots`.
`viewpos <x> <y> <z> [yaw] [pitch]` places the camera before a screenshot.
