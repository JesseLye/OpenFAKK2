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
- `probe-sling` gives the player the sling, charges and fires it standing still, and runs
  `whatis` on entities 0 to 299 a third of a second later, so the rock is among them.
  `probe-sling-down` does the same after holding `+lookdown`; the test player cannot
  pitch, so that one is for the real client (`run-client.ps1 -Run "+map zoo +script
  thread probe-sling-down.scr::probe"`).
- `probe-gaspod-down` gives the gas sling and its ammo (`$player ammo "Gas Pod" 5`, since
  the sling starts empty), fires it after 0.6 s of `+lookdown`, and reads the pod's slot
  every 0.05 s because it explodes where it lands. Real client only; it is the regression
  probe for the trace owner rule (the pod parked in Julie's chest before the fix).
- `probe-walk-jump` walks two seconds, jumps, lands and stops, reading the player every
  0.1 s through each slow-down. On the headless test player the final position reads 4 to
  6 units short of retail (see `docs/known-differences.md`, "With scripted input");
  `-Extra "+set sv_fps 40"` closes it.

Weapon effects are drawn by the client, so these probes are for the real client on
`zoo` (`.claude\skills\retail-probe\scripts\probe-client.ps1` for ours,
`run-retail-client.ps1 -After "+script thread <probe>.scr::probe"` for retail, the probe
copied to `build\sandbox\retail\fakk\maps`) and are compared by their screenshots:

- `probe-shotgun` fires the shotgun's plasma blast (its holes and green puffs) and a
  grenade; `probe-rocket` fires a rocket at the floor (explosion, smoke, soot);
  `probe-sword` swings the green sword and the fire sword (swipes).
- `probe-fx-guns` (chaingun, uzi, hand gun), `probe-fx-range` (crossbow, flamethrower,
  soul sucker) and `probe-fx-melee` (electric sword, axe, chain sword, flashbang, gas
  sling) sweep the other weapons.
- `probe-fx-expl` spawns the flashbang's, gas pod's and soul sucker's explosion models.
  `probe-sprites` and `probe-fx-order` spawn the test models in `fx\` (still sprites,
  the fade rules, overlapping sprites); copy `fx\*.tik` into each engine's
  `fakk\models` first. A model spawned by a script does not start its animation, so the
  probes name it and send `anim idle`.

A dual-handed weapon's primary fire is `+attackleft` and its alternate `+attackright`
(`global/julie_torso.st`); one-handed weapons attack with `+attackright`.

The retail client drops the end of a long command line without a word: with
`run-retail-client.ps1 -Extra "+set r_mode 4"` a `+script thread` naming a probe longer
than 14 characters never ran (the game loaded and the probe did nothing). Keep client
probe names short, or the other arguments.

`-RetailTwice` runs the retail game a second time in place of OpenFAKK2. The game seeds
its random numbers from the clock, so retail differs from itself on some maps; this shows
by how much. `-Extra "<arguments>"` adds to OpenFAKK2's command line only.

A probe must end with `pendingevents`; that is how the script knows it has finished.

OpenFAKK2 runs with the install as `fs_basepath` and `build\sandbox\home` as
`fs_homepath`; the retail runs use a clean copy of the install. So that ours does not
read the player's own `autoexec.cfg` from the install, `common.ps1` keeps an empty one in
the sandbox home. Archived cvars persist in the sandbox's `config.cfg` between runs: if
a client-side default changes (as `cl_run` did), delete the old `seta` line there.

## The client and the map viewer

    tools\retail-compare\run-client.ps1 -Run "+viewmap zoo +wait 30 +screenshotJPEG +wait 5 +quit"

runs the OpenFAKK2 client in a window against the retail data, copies its console log
to `build\compare\client.log` (`-Name` changes the name) and stops it after `-Secs`
seconds if it has not quit. Screenshots land in `build\sandbox\home\fakk\screenshots`.
A run stopped that way leaves ioquake3's pid file behind, which would make the next
start wait on the "didn't exit properly, start in safe mode?" dialog; the script passes
`+set com_abnormalExitDialog 0`, which skips the dialog and starts as normal. Use the
same when launching the game by hand from a sandbox a harness run has used.

`-Run "+map zoo +wait 900 +screenshotJPEG +wait 10 +quit"` joins a local game instead:
the client game module draws the level from the game's camera. The module is looked for
in `build\sandbox\home\fakk` first, then beside the executable, so copying the retail
`cgamex86.dll` into the sandbox home tests the engine against Ritual's own build; remove
it again to go back to the rebuilt module.
`viewpos <x> <y> <z> [yaw] [pitch]` places the camera before a screenshot. `viewsky` prints the sky
portal camera's position, `viewsky <x> <y> <z>` moves it and `viewsky off` turns it off, which
also shows the sky's models as ordinary scenery. With `developer 1` the log names each
model that was skipped and each one sorted into the sky.
