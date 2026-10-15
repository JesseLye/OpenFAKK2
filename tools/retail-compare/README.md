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
- `probe-mnwalk` holds `+forward`, opens the main menu (which pauses the game), closes it
  again and reads the player every 0.25 s; `probe-mswalk` saves the game under the menu
  as well. Real clients only (`probe-client.ps1`, `-Classname none -Fields Origin`). The
  steps must stay even across the menu: before the client's clock was held through a
  pause, the player stood still and then jumped ahead (and retail does this too; its
  `wait` counts milliseconds, so its menu is up for a shorter time).
- `probe-savedmenu` saves the game on `zoo`, shoots the "GAME SAVED" print and then the
  main menu over it, which must hide it (retail draws the prints under the menus):
  `.claude\skills\retail-capture\scripts\capture.ps1 -Probe probe-savedmenu -Map zoo
  -MaskNotify`. The menu shots differ from retail's in the menu's fade and its clouds,
  as the menu's waits count milliseconds on retail and frames on ours; compare whether
  the print shows. Retail's own "Wrote screenshots/..." takes the print's place, so its
  second shot shows that rather than "GAME SAVED".
- `probe-oneprint` puts up a centre print and, a second later, a location print, which
  replaces it (retail keeps one print), then a location print of
  `@textures/menu/gamesaved`, which retail writes as text:
  `capture.ps1 -Probe probe-oneprint -Map zoo -MaskNotify`.
- `probe-pfade` shoots a centre print 375 ms into its 750 ms fade-in; crop the text
  (`shots.py pair ... --crop 200 220 440 260`) to compare its strength with retail's.
- `probe-lsload` loads a saved game, `fakkhouse0004`, and opens the main menu and the
  load/save menu over it, taking a screenshot of the list. It is the regression probe for
  the renderer's crash on the main menu's first frame (`FixRenderCommandList`, see
  `docs/known-differences.md`), which only that save showed: copy the crashed session's
  `config_fakkhouse0004.*` into `build\sandbox\home\fakk\save` first. Real clients only:
  `.claude\skills\retail-probe\scripts\probe-client.ps1 -Probe probe-lsload -Map fakkhouse`.
  It finishes ("loadsave menu up") or the client exits with code 2.

When the engine crashes on Windows it prints the fault and the stack into the console log
as module-relative addresses and writes `crash.dmp` beside the log (`Sys_CrashReport`,
`code/sys/sys_win32.c`). `python tools/crash-symbols.py <log>` names the functions from
the linker maps of the build in `build\x86-Release\Release` (pass another build's folder
as a second argument); it needs the build that crashed, unchanged.

Weapon effects are drawn by the client, so these probes are for the real client on
`zoo` and are compared by their screenshots, run on both games by the `retail-capture`
skill (`.claude\skills\retail-capture\scripts\capture.ps1 -Probe <probe> -Map zoo`):

- `probe-shotgun` fires the shotgun's plasma blast (its holes and green puffs) and a
  grenade; `probe-rocket` fires a rocket at the floor (explosion, smoke, soot);
  `probe-sword` swings the green sword and the fire sword (swipes).
- `probe-fx-guns` (chaingun, uzi, hand gun), `probe-fx-range` (crossbow, flamethrower,
  soul sucker) and `probe-fx-melee` (electric sword, axe, chain sword, flashbang, gas
  sling) sweep the other weapons.
- `probe-muzzle` fires the uzi through two script cameras beside and ahead of Julie's
  hand and shoots its muzzle burst, smoke and shells in bursts, after a shot of
  `fx\test_muzzle.tik` (the flash sprite and model held still); `probe-muzz2` does the
  chaingun, hand gun, shotgun and rocket launcher through the side camera, and
  `probe-muzz3` the hand gun alone with a shot every server frame. A flash lives 0.05 s,
  so which shots catch one varies from run to run. `probe-muzz4` frames the uzi's muzzle
  close up, then the floor where its casings land; `probe-muzz5` takes twenty shots of
  the muzzle 0.1 s apart, to count how often the burst is up, and with
  `-Extra "+set cg_showemitters 1"` both games log every particle each emitter spawns
  (19 of each of the uzi's four in that burst, on both).
- `probe-fx-expl` spawns the flashbang's, gas pod's and soul sucker's explosion models.
  `probe-sprites`, `probe-fx-order` and `probe-muzzle` spawn the test models in `fx\`
  (still sprites, the fade rules, overlapping sprites, muzzle flashes); copy `fx\*.tik`
  into each engine's `fakk\models` first. A model spawned by a script does not start its animation, so the
  probes name it and send `anim idle`.
- `probe-dlight` (on `training`, not `zoo`) holds `fx\test_dlight.tik`, a light the
  hand gun's muzzle flash colour and radius, at four places over a glowing pad
  (`padfloor_target1`, a brush model) and shoots each from a script camera.

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
`probe-lod` frames Julie and a spawned statue on `zoo` with `r_showlod 1` (each model's
kept and total triangles as figures over it) at `r_lodscale` 5 and 0.5, then the meshes
at 0.1; `probe-lodsoup` shoots a swamp tree baked into `swamp1` at `r_lodscale` 5 and
0.05, where retail and ours drop the same pieces of it.
`probe-tiki` runs `tiki` and `tikilist` on `zoo` for their log output, between `====`
markers; compare the two logs' sections.
`probe-shadows` frames Julie's feet on `zoo` with `cg_shadows 2` (stencil, which retail
cannot draw without a stencil buffer) and `cg_shadows 3` (projected).
`probe-fogvol` frames `cemetery`'s blood and water entrance fogs from three script cameras,
40 seconds in (after the level's own cameras), each cued twice. `probe-uihud` takes `zoo`
with the HUD and after `ui_hud 0`. `probe-dialog` brings up a `dialog` on `zoo`; retail
crashes on quitting with it open, which the capture survives. `probe-tlist` does the same
with `ui_testlist`; retail shows only the first floating window of a run. `probe-vidmode`
runs `vidmode 4` on `zoo` and shoots at 800x600; remove `r_mode` from both sandboxes'
`config.cfg` afterwards. `probe-resetcv` prints two of the GameCamera menu's cvars before and
after `ui_resetcvars`; compare the logs (it takes no screenshot).
`probe-restart` and `probe-rsnd` latch `r_picmip` and `s_loadas8bit` under their menus and run
`ui_checkrestart`; compare the logs from the `==== check` marker (retail's script stops after
the restart, so pass `-Secs 60`), then remove both cvars from both sandboxes' `config.cfg`.
`probe-popmenu` prints `cg_cameradist` after `popmenu 1` and `popmenu 0` under GameCamera.

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
front, or DirectSound silences it, so the script keeps bringing it back there (another
game started meanwhile, say by another worktree's harness run, would still be heard in
the recording). `-Volume` (0.25) is kept low because the Windows output path can compress loud
sound; play a tone of known level through it first if in doubt. The probes put
`sound/test/tone.wav` (from `sound_tone.py`) in the game directory:

- `probe-sound-pan`: the tone 48 units away at 14 bearings from ahead to behind.
- `probe-sound-distance`: the tone ahead at 32 to 1024 units (minimum distance 64), then
  64 to either side.
- `probe-sound-turn`: the tone ahead, then the player turns left about 84 degrees; it
  should end up in the right ear.
- `probe-music`: swamp1's soundtrack on `zoo`, which is otherwise silent (`-Music 0.6`).
- `probe-uzi`: Julie dual wields the two uzis and fires the left one, the right one and
  both, 3 s each with 3 s between; the game's own sounds, not the tone, so read it with
  `rms` over each burst. Use `-Volume 0.06`: the uzis are loud enough at 0.25 to reach
  the output path's compression.
- `probe-fire-loops`: a burning creature's fire loop (`flame2.wav`, `maxfactor 1`) 80
  units ahead, then three of them, then one put out and another lit, 4 s each; the
  retail engine plays loops of one sound as one, so all three stretches should read
  alike with `rms`.

`save-load.ps1 -Map <map>` checks saved games against themselves rather than against
retail: it plays the map with `probe-save`, which saves the game three seconds after the
player enters and reads every entity two seconds later, then starts a second server with
`+loadgame probesave`. The game archives its script threads, so the probe resumes in the
second run and takes the same readings at the same game time; the script compares the two
runs as `compare-player.ps1` compares retail with ours. See "Saved games" in
`docs/known-differences.md` for what it found.

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
