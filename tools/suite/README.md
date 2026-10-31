# The test suite

An on-demand suite that tells when something breaks: it runs OpenFAKK2 through the
retail-compare harness's probes and compares what it does with the retail game's cached
results and with ours' last good results. Nothing is scheduled; a person or an agent
starts it. The `verify` skill (`.claude/skills/verify`) tells an agent how to pick the
checks for what it changed, and the project's Stop hook asks for it when code changed.

    tools\suite\run-suite.ps1 -Level quick                 # minutes
    tools\suite\run-suite.ps1 -Level standard -Changed     # the areas this branch touched, and neighbours
    tools\suite\run-suite.ps1 -Level full                  # everything but sound (-Sound adds it)
    tools\suite\run-suite.ps1 -Only chain, spawn/zoo -NoBuild
    tools\suite\run-suite.ps1 -List -Level standard -Areas rendering
    tools\suite\run-suite.ps1 -Level quick -Changed -Since HEAD^1 -AcceptMoves   # on main after a merge (the merge skill)
    tools\suite\run-suite.ps1 -Level quick -Changed -Arch x64                     # the x64 build, and each check on x86 beside it

## What a run does

1. Selects checks from `suite.json`: every check marked `always` (the build, the data
   checks, spawn output on all 30 maps, the invariants), and the checks of the chosen
   areas up to the chosen level. `quick` takes each area's quick checks; `standard` every
   check up to standard in the areas and their neighbours (`areas` in suite.json);
   `full` all of them. `-Changed` takes the areas from the files this branch changed
   (suite.json `paths`) and the checks of any probe that changed.
2. Runs each on ours (the build in `build\<arch>-Release`, x86 unless `-Arch x64`, built
   first), with a fresh test home: no saves, screenshots or crash dump from the check
   before.
3. Measures it: each check gives one or more numbers, 0 when ours matches retail (spawn
   lines differing, readings differing, % of pixels differing per screenshot, steps of
   the level chain missed). A number within tolerance of 0 matches retail; one within
   tolerance of ours' last good result is a known difference; one beyond it is **newly
   failing**. Counts that only must not grow (build warnings, console warnings, frame
   time) are judged against ours' last good result only. A crash (a crash report in the
   log, `crash.dmp`, or a game that exited with a code other than 0) fails the check.
4. Writes `build\suite\runs\<time>\`: `report.md` (summary; newly failing with evidence,
   pair pictures and crash stacks; known differences; passes), `results.json`, and each
   check's logs and shots under `checks\<name>\`.

The player's `config.cfg` (`build\sandbox\home\fakk\config.cfg` in the main checkout) is
hashed before and after; a change fails the run (`play-home`). Exit code 0 when nothing
failed, 1 otherwise. `build\suite\last-run.json` records the result for
`verify-gate.ps1 -Stamp`.

## References

Kept in the **main checkout's** `build\suite\reference\` and shared by every worktree;
never committed (they are made from Ritual's data).

- `retail\<check>\`: retail's result for each check that has a retail side: its log,
  shots or level-chain summary, and `meta.json` with a hash of the probe it was made
  from. `refresh-retail.ps1` makes them; routine runs never start retail. A reference
  whose probe has changed since is reported as stale and its check skipped:

      tools\suite\refresh-retail.ps1 -Only fstrafe        # one check
      tools\suite\refresh-retail.ps1 -Missing             # every missing or stale one, up to standard
      tools\suite\refresh-retail.ps1 -Level full -Missing

  It needs the game disc in a drive and puts the retail game's windows on screen.
- `ours\<check>\`: ours' last good result (`baseline.json` with the numbers, the commit,
  the architecture, a hash of the probe, the retail reference it was measured against
  and why it was kept, and the logs and shots, against which a failing shot is also
  paired). A worktree compares against main's. `ours-x64\<check>\` is the x64 lane's
  (below). Changed only on main: by the `merge` skill's
  `run-suite.ps1 -Since HEAD^1 -AcceptMoves` after a merge, which keeps the declared
  moves, the improvements and the results against remade references; or by
  `-UpdateBaseline`, which keeps everything, to set the suite up.

## Declared moves

A change that moves a check on purpose (right by the binary, but further from the cached
result; a changed probe) says so in its commit:

    Suite-moves: crosshair, shots/*: the crosshair is now drawn at retail's size

Check names or groups, wildcards allowed, then why. `run-suite.ps1` reads these
trailers from the commits since the branch's merge base with main (or `-Since`), and a
check one names that comes out worse is reported as **moved as declared**, not newly
failing, so the branch can pass `verify`. After the merge, `-AcceptMoves` makes its
result the new last good one. A new check is declared the same way, so its first result
on main is kept even when it passes; otherwise a check without a last good result that
later differs is only reported as unmatched. A run's statuses: newly failing, could not run, moved as
declared, known (differs from retail as before), unmatched (no last good result yet),
pass, skipped; and a check whose numbers dropped is also listed as better than its last
good result.
- `noise.json`: how far each check's numbers move between two runs of the same build
  (`run-suite.ps1 -Noise`) and between two retail runs (`refresh-retail.ps1 -Twice`,
  as the game seeds its random numbers from the clock). A tolerance is at least 1.5
  times the first; a check matches retail when it is within the second.

## suite.json

Each check: `name`, `areas`, `level` (quick, standard, full), `kind`, and what the kind
needs. `maps` (a list, or `"all"`) or `menus` expands one entry into a check per item
(`spawn/zoo`); `quickMaps` makes some of those quick and `skipMaps` leaves some out. `always: true` runs it at every
level. `sound: true` runs it only with `-Sound`. `exclusive` runs its games with no
other harness game on the machine, and says why: `timing` (it measures frame times, or
moves the player with held keys, which a busy machine shifts: `frametime`, `timedemo`,
the strafe checks), `sound` (it records the speakers; every `sound: true` check is
exclusive anyway) or `focus` (its game must be the window in front). Any other value
stops the run. Without it a check's games share the machine with other worktrees'
(`tools/retail-compare/README.md`, "Games side by side, or alone"); give a new check
`exclusive` when its result would move under load, and say so in its `note`.
`tolerance` per measure (or per map of a `maps` check, or `"*"`), a
number or `"N%"` of the last good value; by default 0, and 1 percentage point for
screenshots; `toleranceMin` (per measure or `"*"`) is the smallest tolerance allowed,
for a percentage of numbers so small that machine load moves them more (`frametime`'s
sub-millisecond medians); `toleranceWhy` says where a larger one comes from. Two runs measured with
`-Noise` can agree by luck: the probes that aim with `+lookdown` for a fixed time reach a
pitch that depends on frame timing, and their shots moved by 2 to 5 points between runs
of one build that `-Noise` had seen agree within one, so those have explicit tolerances. `knownIssue` reports a failure among the known differences, for a bug
recorded in `docs/known-differences.md` and left for its own fix. `note` says what the
check is for.

| Kind | Runs | Measures |
|---|---|---|
| `build` | `scripts\build-msvc.cmd <arch> Release` (copied from the main checkout if missing) | failure; warnings, filed per target in `build\suite\warnings-ledger.json` (`-x64` for that lane), so an incremental build counts as a full one would |
| `cik` | `check_tiki_against_cik.py` | models whose bounds or radius differ from the .cik caches |
| `tiki` | the client's `cachealltikis` | warnings and errors loading every .tik |
| `spawn` | `run-openfakk2.ps1` / `run-retail.ps1` on a map | game lines only one side prints (compare.ps1's filter); console warnings |
| `player` | `compare-player.ps1` (headless test player / retail client) | level variables, pending event kinds, whatis readings (all of them, or those of `select`ed entities and their `fields`, as for `client`) |
| `client` | `probe-client.ps1` on both real clients | the readings of `select`ed entities, `fields` within `valueTolerance`; `oursOnly`: whether the probe finished, and with `logCount` the lines of the log matching its `pattern` (as `matches`, judged against the last good result) |
| `capture` | `capture.ps1` | % of pixels differing per shot (`--mask-notify`) |
| `menus` | `capture-menus.ps1` | the same |
| `widescreen` | `capture.ps1` on ours only, at the `wide` and `narrow` sizes (`r_mode -1`) | % of pixels differing between each wide shot's middle and the narrow shot below row `top` (`shots.py middle`), and between the two runs' saved game pictures (`saveShot`, the name the probe saves under) |
| `windowshots` | the client, pictures of its window at `times` seconds | the same (the startup intro, which a config's waits would hold up) |
| `cfgrun` | the client with a config (`cfg`, `{maps} ...` or `{each} ...` per map) | `doneMarker` reached; console warnings (`countWarnings`); frame times (`frameTimes`, com_speeds) |
| `saveload` | `save-load.ps1` (`probe` and `thread` name the script that saves; `probe-save`'s `probe` by default; `loadArch` has the other architecture's build load the save, see below) | level variables and readings differing between the saved and the loaded game |
| `chain` | `level-chain.ps1` | levels reached, autosaves, inventory against retail's run |
| `sound` | `sound-record.ps1` | the largest level difference in dB (`sound_metric.py`): the probe tone's plateaus (`tone`), or the 50th, 75th and 90th percentiles of the levels while sound plays (`rms`, music and game sounds, so the two games' start times do not matter) |
| `demo` | retail: `probe` records `demos/<probe>.dm3` (kept in the reference); both: that demo played with `cl_avidemo` (`avidemo` frames a second, every `every`-th kept, only those in the `keep` windows when it has them), ours with `cl_demoSeed 1` | % of pixels differing per frame, as `capture` |
| `timedemo` | ours: each of the `demos` checks' retail demos with `timedemo 1`, uncapped | the mean frame time without the first `skipFrames` (20) frames (`cl_timedemoLog`), judged against ours' last good result; whether each played to the end |

The sound checks record the speakers by WASAPI loopback, so they play out loud and record
anything else playing; run them with `-Sound` when nobody minds. They need pyaudiowpatch
and numpy, which a virtual environment in the main checkout's `build\suite\sound-venv`
gets on first use (`FAKK2_SOUND_PYTHON` names another Python). Two runs of ours differ by
0.2 dB or less; ours against retail, 2026-10-16: pan 0.7, turn 0.4, music 0.5, fire
loops 0.2, uzi 1.6 and distance 5.5 dB (far sounds; retail's Miles volumes are whole
numbers).

Console warnings are counted without two kinds every run prints by the hundred: the
`.tga` lookups that fall back to `.ftx`, and the collision model's bevel notes.

## The example maps

The `x*` checks run on Ritual's example maps (`tools\retail-compare\maps\example`, one
small map per feature; `tools\retail-compare\probe-scripting.md` lists them), one
subsystem of the game code each, so a change to the port (the x64 build above all) that
breaks one shows up by name: `xdoor` an actor walking through a `func_door`, `xpatrol` an
actor patrolling, killed and removed, `xpickup` an actor carrying and throwing another
entity, `xphys` script-thrown physics objects, `xexpl` a `func_multi_exploder`'s delayed
events, `xspawn` a `func_spawn`, `xcam` orbit and spline cameras, `xevent` an actor
removed with its thread and events pending. Each is a `player` check (the probe's
`probe` thread against retail) with an `x*-save` sibling (`saveload` on the probe's
`save` thread: the same drive saved part way and loaded in a new process). The two
things the save checks show in every one of them, a trigger's or exploder's `Contents`
`ffffffff` after a load (0 when saved) and `playerammo_edenwater` read as `50` rather
than `50.000000`, are the game's own behaviour after a load, retail's too
(`docs/known-differences.md`, "Saved games"), and part of their last good results.
What a death or an idle leaves is drawn at random, so `xpatrol`
compares the creeper and the player only and its save thread does not kill; `xcam`
compares the cameras and the player (Gruff's idle nods).

## The x64 lane

`-Arch x64` runs the x64 build (`build\x64-Release`, built first) through the same checks.
Both builds are judged against the same retail references: retail is the source of
truth for both, and the x64 build is not meant to differ from the x86 one in anything
retail shows. Each lane keeps its own last good results (`reference\ours` for x86,
`reference\ours-x64`), and a check the x64 lane has no last good result for yet is judged
against x86's, so the lane starts with x86's history rather than from nothing. Runs of
the lane are stamped `<time>-x64`, and the build's warnings have their own ledger.

The two builds run the same code, so beyond matching retail they should agree with each
other, and `-Against x86` (on by default with `-Arch x64`; `-Against none` turns it off)
runs every check on the x86 build too, right after, and compares the two results
directly: readings, level variables, pending events and game lines exactly, shots within
the check's own tolerance (a live shot moves by a frame of animation between two runs of
one build; a menu's clouds drift by about a point, so menus get 2), and a check that
holds keys on the real client (`exclusive: timing`) within its own `valueTolerance`,
since its two runs cannot be frame-exact. Lines that name the
build (the module's file and checksums, the version string) are left out. A difference
is newly failing, "against x86" in the evidence, with the pair pictures under
`checks\<name>\against-x86`. A count that only must not grow (build warnings, console
warnings, frame times) is judged against the lane's own history alone, never x86's. It needs the x86 build to exist and doubles
the run time. With the x86 build standing in for x64 (`-Bin build\x86-Release\Release`),
every check came out the same, pictures included, and the real x64 build then did too on
its first day (2026-10-31: spawn output on zoo and training, walk-jump, the eight
example-map checks, save-load and two saves, shots/zoo to 0.03% of pixels).

The cross-build save checks (`save-cross`, `xdoor-cross`, `xphys-cross`, `xexpl-cross`,
`xcam-cross`: `"loadArch": "other"`) save the game with the lane's build and load it with
the other architecture's (`save-load.ps1 -LoadBin`), so the x86 lane has x64 load x86's
saves and the x64 lane the reverse. They are skipped until the other build exists. They
are where a layout difference in the save format shows: the raw-archived client
structure was 16 bytes longer from x64 until it was archived run by run
(`docs/known-differences.md`, "The x64 build"); since then both directions give the same
results as a same-build load.

To set the lane up on main, run it once over everything and keep the results:
`tools\suite\run-suite.ps1 -Arch x64 -Level full -UpdateBaseline` (the one use of
`-UpdateBaseline` that is not a re-baseline: the lane has no results to lose). After
that the merge skill keeps its results as it does x86's.

`verify-gate.ps1 -Stamp` records the last run whichever lane it was; while the port is
in progress, verifying a change means running both lanes. The merge skill runs
`-AcceptMoves` on each lane that has results.

## Demos

OpenFAKK2 and the retail engine read each other's demos (`docs/known-differences.md`,
"Demos"), so a demo recorded on retail is the same input to both clients: same
snapshots, entities, sounds and prints. A `demo` check's retail reference holds such a
demo, recorded by its probe on retail (`refresh-retail.ps1`; the probe sets
`g_syncronousClients`, records, plays and quits), and retail's frames of it. Playing a
demo with `cl_avidemo N` makes every frame 1/N s of the demo and takes a screenshot of
it on both engines, so frame n is the same moment on both: no held keys, no frame
timing. What still differs from retail is the client side: the renderer, the client
game module, the HUD. One thing is not fixed by the demo: whether a black frame is
captured before the first active snapshot draws depends on load timing, in both engines
(the same build took one in one run and none an hour later), so the frames are paired
from the first one with a picture on each side, and the evidence says how many were
dropped.

The client game module draws its effects (particles, smoke, flash bursts, an
explosion's streaks) with `rand()`, which the engine seeds from the OS at start, so two
playbacks of one demo drew different effects: on demos close to the guns, frames with
effects moved by up to 10 points between runs of one build. Ours plays the suite's demos
with `cl_demoSeed 1`, an OpenFAKK2 cvar that seeds the random numbers when a demo starts,
before the client game module loads; two runs of ours then differed by 0.04 points or
less on every frame of `demo-flashes`, `demo-guns` and `demo-muzzle` (2026-10-16). Retail draws its own random effects, so frames full of them differ
from retail by more (an explosion close up, 30 points) without anything being wrong;
they are judged against ours' last good result.

A demo check's `keep` windows ("from-to" in seconds of the demo; frame n is n/`avidemo`
s) keep the frames around the moments that matter, so a demo can hold several scenes
with the weapon changes between them left out. The windows are part of the reference:
changing them makes it stale, as changing the probe does.

A demo someone recorded by hand (anything too fiddly to script) makes a check with
`"demoFile": "<name>.dm3"` in place of `"probe"` and `"map"`: the file in the main
checkout's `build\demos`, which `refresh-retail.ps1` copies into the reference before
playing it on retail. The demo is part of the reference as a probe is, so a new take of
it makes the reference stale. It lives only on the machine it was recorded on (demos are
made from Ritual's data and never committed): elsewhere the check is skipped for want of
a reference. The `retail-demo` skill records, plays and compares demos outside the
suite, and says how to turn one into a check.

- `demo-zoo` (`probe-dzoo`): two creepers fight Julie; chaingun, rockets, her death; 15
  s, 2 frames a second.
- `demo-julie-turn` (hand-made, `build\demos\julie-turn.dm3` on this machine only): Julie
  turning on fakkhouse's dock under its sun and lights, for her light cube at the 255 cap
  (the cap scales a face by its brightest channel; by its length she flickered and ran a
  quarter dark); 15 s at 20 frames a second, every fourth kept.
- `demo-swamp1` (`probe-dsw1`): swamp1's opening cinematic, then rockets and a walk into
  the water; 36 s, 1 frame a second.
- `demo-muzzle` (`probe-dmuz`): `muzzle`'s scene (the still flash model, then the uzi
  fired through two cameras by her hand); 5.5 s at 20 frames a second, so each flash
  (0.05 s) is in the same frames on both games. `muzzle`'s screenshots had moved by up
  to 10 points between runs.
- `demo-crosshair` (`probe-dxhr`): `crosshair`'s scene (the shotgun's crosshair on the
  wall, then on the floor after `+lookdown`), at the pitch retail reached; 2.5 s, 4
  frames a second. `crosshair`'s floor shot moved by 10 to 12, as its pitch depends on
  frame timing.
- `demo-flashes` (`probe-dfl`): the scenes of `probe-muzz2` to `probe-muzz5`, which are
  no longer checks of their own: the chaingun, hand gun, shotgun and rocket launcher
  fired through a camera beside the barrel, the uzi through one beside its muzzle and
  one over its casings; 28 s at 20 frames a second, 212 kept. Those checks' shots had
  moved by up to 10 points.
- `demo-guns` (`probe-dgun`): the scenes of `fx-guns`, `fx-range`, `shotgun` and
  `rocket` in one take: nine weapons fired at the floor ahead; 41 s at 10 frames a
  second, 156 kept. Those four stay as full-level checks of the whole game (their shots
  moved by up to 16 points); this one runs at standard.
- `demo-melee` (`probe-dmel`): the scenes of `probe-pfade`, `probe-fx-melee` and
  `probe-sword` on zoo in one take: a centre print's whole 750 ms fade-in, the electric
  sword, axe and chain sword, a gas pod's cloud, the green and fire swords' swipes; 37 s
  at 20 frames a second, 190 kept. As in `probe-fx-melee`, the flashbang is not thrown
  on retail (she still holds the chain sword). `sword` and `pfade` are no longer checks;
  `fx-melee` stays at the full level as a check of the whole game.
- `demo-hud` (`probe-dhud`): the scenes of `probe-ammofl`, `probe-flash` and `probe-pot`
  on training in one take: the ammo bars' and the health and water gauges' whole
  flashes, frame by frame, and the targeting reticle on an autoaim pot; 16 s at 20
  frames a second, 89 kept. Its shot is aimed away from the trainer by the wall, as
  hitting her fails the level. `ammofl`, `flash` and `pot` (whose shots moved by up to 2
  points) are no longer checks.
- `demo-slime` (`probe-dsb`): Julie on zoo through a camera in front of her, before,
  while and after she is coated in the anti-Sucknblaugh juice (the slime a juice pond
  leaves); 8 s, 4 frames a second. Ours drew the slime in place of her skin, a faint
  outline of her.
- `demo-flash` (`probe-dhit`): on zoo, Julie hurt by slime and then lava, a creeper hurt
  and then poisoned, each flashing green or red; then Gruff's smoking animation, whose
  `entcolor` commands make his cigar tip glow; 22 s, 10 frames a second.
- `demo-invisible` (`probe-dinv`): an Invisible Grawlix on fog beside the map's -300
  light, the only kind of light that shows it; 6 s, 4 frames a second.
- `timedemo` times `demo-zoo`'s and `demo-swamp1`'s demos, uncapped (1.2 ms a frame on
  2026-10-16), in place of a camera over an empty map.

A probe that changes makes its reference stale, as for any check; `refresh-retail.ps1
-Only demo-zoo` records the demo again and the `timedemo` check uses the new one.

## The level chain

`level-chain.ps1 [-Side retail]` plays through the whole game by `gamemap` on the real
client with `tools\retail-compare\chain.scr`: intro, fakkhouse, training and back,
homes1, landersroost and back, creeperpens, homes2good, towncenter_good, under, over,
shield, homes2evil, otto, towncenter_evil, cliff1, cliff2, swamp1 to 3, gruff,
gruff_cinema, cemetery with fog, water and blood, oracleway, oracle, end; the order and
spawn spots come from the maps' `trigger_changelevel` entities and cinematic scripts. On
each level it waits four seconds, prints the inventory and moves on; the player is given
weapons on fakkhouse. It writes `chain.json` (per level: reached, loaded, autosaved,
inventory). It needs the real client because only a non-dedicated server autosaves.

## The hook

`.claude/settings.json` runs `verify-gate.ps1` on Stop and TaskCompleted. It runs no
tests: when files under `code\` or `tools\` differ from the branch's base (committed or
not) and their hash is not the one `verify-gate.ps1 -Stamp` recorded after a passing
run (`build\suite\verified.stamp`), it asks once for that set of changes to run the
verify skill, and otherwise exits silently. It never asks while a Stop hook is already
active, and asks again only when the changes change.

## Files

- `run-suite.ps1`, `refresh-retail.ps1`, `level-chain.ps1`, `verify-gate.ps1`
- `suite-lib.ps1`: running each kind on either game and measuring it
- `changes.ps1`: the branch's changed files and their hash
- `sound_metric.py`: level differences between two recordings
- `..\retail-compare\compare-lib.ps1`: the log comparisons the runners and the suite share
