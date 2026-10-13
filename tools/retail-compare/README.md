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

Both sides run `probe.scr`, which waits for the player, waits ten seconds of game time
and dumps `levelvars` and `pendingevents`, so they are sampled at the same point. The
retail result is cached per map; pass `-Refresh` to run the retail game again.
