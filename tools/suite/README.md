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

## What a run does

1. Selects checks from `suite.json`: every check marked `always` (the build, the data
   checks, spawn output on all 30 maps, the invariants), and the checks of the chosen
   areas up to the chosen level. `quick` takes each area's quick checks; `standard` every
   check up to standard in the areas and their neighbours (`areas` in suite.json);
   `full` all of them. `-Changed` takes the areas from the files this branch changed
   (suite.json `paths`) and the checks of any probe that changed.
2. Runs each on ours (the build in `build\x86-Release`, built first), with a fresh test
   home: no saves, screenshots or crash dump from the check before.
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
  the retail reference it was measured against and why it was kept, and the logs and
  shots, against which a failing shot is also paired). A worktree compares against
  main's. Changed only on main: by the `merge` skill's
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
level. `sound: true` runs it only with `-Sound`. `tolerance` per measure (or `"*"`), a
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
| `build` | `scripts\build-msvc.cmd x86 Release` (copied from the main checkout if missing) | failure; warnings, filed per target in `build\suite\warnings-ledger.json`, so an incremental build counts as a full one would |
| `cik` | `check_tiki_against_cik.py` | models whose bounds or radius differ from the .cik caches |
| `tiki` | the client's `cachealltikis` | warnings and errors loading every .tik |
| `spawn` | `run-openfakk2.ps1` / `run-retail.ps1` on a map | game lines only one side prints (compare.ps1's filter); console warnings |
| `player` | `compare-player.ps1` (headless test player / retail client) | level variables, pending event kinds, whatis readings |
| `client` | `probe-client.ps1` on both real clients | the readings of `select`ed entities, `fields` within `valueTolerance`; `oursOnly`: whether the probe finished |
| `capture` | `capture.ps1` | % of pixels differing per shot (`--mask-notify`) |
| `menus` | `capture-menus.ps1` | the same |
| `widescreen` | `capture.ps1` on ours only, at the `wide` and `narrow` sizes (`r_mode -1`) | % of pixels differing between each wide shot's middle and the narrow shot below row `top` (`shots.py middle`), and between the two runs' saved game pictures (`saveShot`, the name the probe saves under) |
| `windowshots` | the client, pictures of its window at `times` seconds | the same (the startup intro, which a config's waits would hold up) |
| `cfgrun` | the client with a config (`cfg`, `{maps} ...` or `{each} ...` per map) | `doneMarker` reached; console warnings (`countWarnings`); frame times (`frameTimes`, com_speeds) |
| `saveload` | `save-load.ps1` | level variables and readings differing between the saved and the loaded game |
| `chain` | `level-chain.ps1` | levels reached, autosaves, inventory against retail's run |
| `sound` | `sound-record.ps1` | the largest level difference in dB (`sound_metric.py`): the probe tone's plateaus (`tone`), or the 50th, 75th and 90th percentiles of the levels while sound plays (`rms`, music and game sounds, so the two games' start times do not matter) |
| `demo` | retail: `probe` records `demos/<probe>.dm3` (kept in the reference); both: that demo played with `cl_avidemo` (`avidemo` frames a second, every `every`-th kept) | % of pixels differing per frame, as `capture` |
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

## Demos

OpenFAKK2 and the retail engine read each other's demos (`docs/known-differences.md`,
"Demos"), so a demo recorded on retail is the same input to both clients: same
snapshots, entities, sounds and prints. A `demo` check's retail reference holds such a
demo, recorded by its probe on retail (`refresh-retail.ps1`; the probe sets
`g_syncronousClients`, records, plays and quits), and retail's frames of it. Playing a
demo with `cl_avidemo N` makes every frame 1/N s of the demo and takes a screenshot of
it on both engines, so frame n is the same moment on both: no held keys, no frame
timing. Two runs of ours differed by 0.05 points or less on swamp1 and by 0.3 or less
on zoo but for two frames of explosions (particles drawn at random, 1.1 and 2.6), where
probes that aim with `+lookdown` move by tens of points. What still differs from retail
is the client side: the renderer, the client game module, the HUD.

- `demo-zoo` (`probe-dzoo`): two creepers fight Julie; chaingun, rockets, her death; 15
  s, 2 frames a second.
- `demo-swamp1` (`probe-dsw1`): swamp1's opening cinematic, then rockets and a walk into
  the water; 36 s, 1 frame a second.
- `demo-muzzle` (`probe-dmuz`): `muzzle`'s scene (the still flash model, then the uzi
  fired through two cameras by her hand); 5.5 s at 20 frames a second, so each flash
  (0.05 s) is in the same frames on both games. Two runs of ours differed by 0.45 points
  or less over its 111 frames; `muzzle`'s screenshots had moved by up to 10.
- `demo-crosshair` (`probe-dxhr`): `crosshair`'s scene (the shotgun's crosshair on the
  wall, then on the floor after `+lookdown`), at the pitch retail reached; 2.5 s, 4
  frames a second. Two runs of ours differed by 0.11 points or less; `crosshair`'s
  floor shot moved by 10 to 12, as its pitch depends on frame timing.
- `timedemo` times both demos, uncapped (1.2 ms a frame on 2026-10-16), in place of a
  camera over an empty map.

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
