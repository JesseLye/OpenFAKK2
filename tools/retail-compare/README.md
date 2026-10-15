# Comparing against the retail engine

These scripts run the retail `fakk2.exe` and `openfakk2ded` as dedicated servers on the
same map and compare what the game module prints. The retail engine needs the game disc
in a drive. Nothing is written to the game install: the retail engine runs from a copy
under `build\sandbox`, and logs go to `build\compare`.

`common.ps1` holds the shared paths. `$GameInstall` is the retail install
(`FAKK2_INSTALL` overrides it). `$RitualTools` is a clone of Ritual's Tools 1.02 release
(github.com/Sporesirius/fakk2; `FAKK2_TOOLS` overrides it): design documents, tool
sources, example maps and the tools. Read from it, but never commit anything taken from
it: Ritual's licence does not allow copying it, and its Miles files are not
redistributable. `tools\ritual-docs.ps1` converts its design documents into searchable
text in `build\ritual-docs`.

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
- `probe-exit` runs on `fakkhouse` and leaves it as the trainer cinematic does,
  `map training$spawn_fakkhouse`, with `probe-client.ps1`. The log must show
  `Server: training$spawn_fakkhouse` and no "Can't find map"; the probe ends with the
  level, so it never prints `pendingevents` and the runner waits out `-Secs`.
- `probe-cine` is a picture probe for `training` (run it with the `retail-capture`
  skill): it holds `+forward` from entry and takes a shot every 0.05 s while the entry
  cinematic takes over. No shot may show the player's own view or the HUD; before the
  server imposed the entry stall on real clients, one did.
- `probe-sling` gives the player the sling, charges and fires it standing still, and runs
  `whatis` on entities 0 to 299 a third of a second later, so the rock is among them.
  `probe-sling-down` does the same after holding `+lookdown`; the test player cannot
  pitch, so that one is for the real clients:
  `.claude\skills\retail-probe\scripts\probe-client.ps1 -Probe probe-sling-down -Map zoo`
  runs it on ours, and with `-Retail` on the retail game for the reference.
- `probe-gaspod-down` gives the gas sling and its ammo (`$player ammo "Gas Pod" 5`, since
  the sling starts empty), fires it after 0.6 s of `+lookdown`, and reads the pod's slot
  every 0.05 s because it explodes where it lands. Real clients only (`probe-client.ps1`,
  with `-Retail` for the reference); it is the regression probe for the trace owner rule
  (the pod parked in Julie's chest before the fix).
- `probe-walk-jump` walks two seconds, jumps, lands and stops, reading the player every
  0.1 s through each slow-down. On the headless test player the final position reads 4 to
  6 units short of retail (see `docs/known-differences.md`, "With scripted input");
  `-Extra "+set sv_fps 40"` closes it.

Weapon effects are drawn by the client, so these probes are for the real client on
`zoo` and are compared by their screenshots, run on both games by the `retail-capture`
skill (`.claude\skills\retail-capture\scripts\capture.ps1 -Probe <probe> -Map zoo`):

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
How to write one (keys, weapons, ammo, aim, movement, scene set-up) is in
`probe-scripting.md`.

Screenshot probes end with `stuffcmd "quit"` instead and are run on both games by the
`retail-capture` skill (`.claude\skills\retail-capture\scripts\capture.ps1 -Probe <p>
-Map <m>`), which pairs the shots. `probe-crosshair` wields the shotgun in `training` and
shoots the crosshair on a target and on the floor; `probe-swbranch` cues three cameras on
`swamp1`; `probe-hud` steps the health and water gauges; `probe-font` puts up a centre
print and a location print in `facfont-20` (glyph spacing and height); `probe-menu`
brings up the weapon bar and then the main menu over it (the menu pauses the game, so its
shots and the quit are chained client commands with frame waits; `capture.ps1`'s warning
that it does not end with `stuffcmd "quit"` is expected); `probe-shot-*` shoot the intro
at fixed times. `probe-load` takes one shot after entry and quits: does a map load.
`probe-ambient` spawns a box and a statue on `zoo`, lit from the light grid, before a
script camera (`r_ambientScale`). `probe-wgroup` saves the green sword as weapon group 3
on `zoo`, holsters and takes the group up again (`saveweaps`, `weapgroup3`). `probe-fps`
frames `zoo` from a script camera for the `fps` counter; run it with `-Extra "+set fps 1"`
and remove the archived `fps` from both sandboxes' `config.cfg` afterwards.
`probe-lightoff` puts a plain statue beside `fx\test_lightoff.tik` (a TIKI `lightoffset`)
on `zoo`; copy that tik into both sandboxes' `fakk\models` first.
`probe-hay` frames the hay (`tcMod macro`) and the slime pool (`rgbGen colorwave`) on
`creeperpens` from two script cameras.
`probe-dot` frames the suck juice pool on `swamp2` (`alphaGen oneMinusDot`, an `rgbGen`
after `nextBundle`).
`probe-dlines` draws the `csys_draw` axes (game debug lines) from inside a box on `zoo`,
depth tested and then with `r_debuglines_depthmask 1`; remove that archived cvar from both
sandboxes' `config.cfg` afterwards. `probe-llines` frames Julie (lit from a light cube) on
`zoo` with `r_light_lines 1` and `r_light_sun_line 1`, then `r_light_nolight 1`; check
both sandboxes' `config.cfg` for them afterwards.
`probe-skel` draws Julie's skeleton on `zoo` (`r_showskel 1`), then sets `r_place_sunflare 1`
and prints `r_sunflare`, the point it wrote, to the log.

Ritual's 32 example maps (one small map per feature: doors, beams, rain, cameras, exploders
and more) install into both sandboxes with `install-example-maps.ps1` and run as
`-Map example/<map>`; `probe-scripting.md` lists what each one shows.

`capture-menus.ps1 [-Menus loadsave,controls] [-Side retail|ours]` shoots the `.urc` menus
with no map loaded, one run per menu on each game, and pairs them in
`build\compare\captures\menus`. It clears the retail sandbox's archived `developer` (retail
opens its console window at startup while it is set, dimming the menus). For the load and
save menu, copy the install's `save\*.ssv` and `*.tga` into
`build\sandbox\retail\fakk\save` first, so retail lists the games ours finds in the install.

OpenFAKK2 runs with the install as `fs_basepath` and `build\sandbox\home` as
`fs_homepath`; the retail runs use a clean copy of the install. So that ours does not
read the player's own `autoexec.cfg` from the install, `common.ps1` keeps an empty one in
the sandbox home. Archived cvars persist in the sandbox's `config.cfg` between runs: if
a client-side default changes (as `cl_run` did), delete the old `seta` line there.

## Sound

    tools\retail-compare\sound-record.ps1 -Engine retail -Probe probe-sound-pan
    tools\retail-compare\sound-record.ps1 -Engine openal -Probe probe-sound-pan
    python tools\retail-compare\sound_levels.py tone build\compare\sound-retail-probe-sound-pan.npy build\compare\sound-openal-probe-sound-pan.npy

runs a sound probe on the retail game (`-Engine retail`) or on OpenFAKK2's OpenAL or
software-mixer backend (`openal`, `dma`) while recording what the speakers play, and
`sound_levels.py` prints the levels side by side (`tone` for the probes' 1 kHz tone, one
row per position; `rms <from> <to>` for music or a whole scene). The recorder needs
`pip install pyaudiowpatch numpy` (`-Python` names another interpreter). The sound plays
out loud and anything else playing is recorded with it. The retail game runs from
`build\sandbox\retail-snd`, which keeps the install's Miles drivers but no DirectSound
wrapper, so it opens Miles' "Fast 2D Positional Audio" provider; its window has to be in
front. `-Volume` (0.25) is kept low because the Windows output path can compress loud
sound; play a tone of known level through it first if in doubt. The probes put
`sound/test/tone.wav` (from `sound_tone.py`) in the game directory:

- `probe-sound-pan`: the tone 48 units away at 14 bearings from ahead to behind.
- `probe-sound-distance`: the tone ahead at 32 to 1024 units (minimum distance 64), then
  64 to either side.
- `probe-sound-turn`: the tone ahead, then the player turns left about 84 degrees; it
  should end up in the right ear.
- `probe-music`: swamp1's soundtrack on `zoo`, which is otherwise silent (`-Music 0.6`).

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
