---
name: retail-capture
description: Compare what OpenFAKK2 draws with what the retail FAKK2 game draws, from screenshots of the same moment taken by one probe script on both engines, paired with a difference image and figures. Use this whenever a report or a change is about how something looks rather than where it is (a sprite, the crosshair, the HUD, a menu, fog, lighting, a model, a weapon effect, a decal, a swipe, a lens flare, the loading screen), whenever someone attaches a retail and an OpenFAKK2 screenshot, says "looks different", "match retail" or "doesn't look right", and whenever a renderer or UI fix needs showing against retail, even if nobody says "capture", "screenshot" or "probe".
---

# Capturing the picture against retail

OpenFAKK2 is reverse engineered from `fakk2.exe`, so a picture is right when it matches
retail's at the same game moment, not when it looks plausible. This skill takes that
picture: one level script (a *probe*) waits for the player, sets the scene, takes
`screenshot`s and quits; `scripts\capture.ps1` runs it on the retail game and on ours and
pairs the shots as `retail | ours | difference`. For where things are and what state
they hold (positions, velocities, `whatis`), use the `retail-probe` skill instead.

## Where to look first

Before reading the binary for what a shader keyword, a `.urc` widget command, a cvar or
a particle or TIKI command is meant to draw, search Ritual's design documents
(`shader_manual_new`, `GUI Documentation`, `Ghost Particle System`, `TIKI Model System`,
`CVAR DOCUMENTATION`, ...). `tools\ritual-docs.ps1` converts them into
`build\ritual-docs` (run it once per worktree; it needs the clone `$RitualTools` names in
`tools\retail-compare\common.ps1`), one paragraph per line, so a `grep -i` hit shows the
whole paragraph. They say what Ritual intended, not what `fakk2.exe` does: the shader
manual gives `spriteGen`'s four modes a line each, but the quad layout came from the
binary. Treat a hit as a lead and confirm it with a capture or the binary. Never commit
anything from them.

## Run it

```powershell
# both games; shots and pairs land in build\compare\captures\<probe>-<map>\
.claude\skills\retail-capture\scripts\capture.ps1 -Probe probe-crosshair -Map training -MaskNotify

# after a fix, rerun only ours; it pairs against the retail shots already in the folder
.claude\skills\retail-capture\scripts\capture.ps1 -Probe probe-crosshair -Map training -Side ours

# the unpatched build in the main checkout, for a before picture
.claude\skills\retail-capture\scripts\capture.ps1 -Probe probe-crosshair -Map training -Side ours `
    -Name before -Bin <main checkout>\build\x86-Release\Release
```

A run takes the map's load plus the probe's waits on each side, about 35 s for
`training`. The retail game runs minimized from the sandbox copy in `build\sandbox\retail`
(made on first use with hard-linked paks, so a worktree gets its own in seconds) and needs
the game disc in a drive. Ours needs a build: in a fresh worktree copy
`scripts\build-msvc.cmd` from the main checkout (it is gitignored), then
`scripts\build-msvc.cmd x86 Release`.

Each shot is kept as `<side>-<n>.tga` and `.png`, and each pair as `pair-<name>-<n>.png`
(`<name>` is `-Name`, `ours` by default), which the Read tool shows. The printed figures
are the mean absolute difference per channel, the share of pixels whose largest channel
differs by more than 24, and the box around them.

## Write the probe

Probes live in `tools\retail-compare\<name>.scr`; start from `probe-crosshair.scr` (the
player aims, two shots), `probe-swbranch.scr` (three script cameras) or `probe-hud.scr`
(a script line changes the state between shots). The weapon effect probes run on `zoo`:
`probe-shotgun`, `probe-rocket`, `probe-sword` (swipes) and `probe-fx-guns`,
`probe-fx-range`, `probe-fx-melee` fire each weapon at the floor and catch the burst,
puffs, decals and explosions; `probe-fx-expl`, `probe-sprites` and `probe-fx-order` spawn
one effect model alone.

Before writing a probe that fires, aims, moves or sets up a scene, read
`tools\retail-compare\probe-scripting.md`, shared with the retail-probe skill: weapons,
ammo, aim timing, movement, cameras, script objects, and spawning one effect model alone
(`spawn <model>.tik ...` then `$name anim idle`, since a spawned model does not start its
animation by itself). The two mistakes that cost a picture most often:

- **The wrong button.** A dual-handed weapon fires its primary mode with `+attackleft`
  and its alternate with `+attackright` (the shotgun's plasma blast is left, its grenade
  right); a right-hand weapon (swords, uzi, hand gun) attacks with `+attackright`. The
  wrong one shows the other mode's effect.
- **No ammo.** A weapon whose tik says `startammo 0` (the gas sling) fires nothing,
  silently, and the shot comes out empty, which reads as "ours draws nothing". Give it
  with a script line before the `use`: `$player ammo "Gas Pod" 5`.

`retail-probe`'s `make_probe.py` expands `@cmd` and `@hold` shorthand, but its `@end`
does not quit: end a picture probe with `stuffcmd "quit"` yourself.

`tools\retail-compare\fx` holds test models (still sprites, the fade rules, overlapping
sprites). `capture.ps1` does not install them: copy `fx\*.tik` into
`build\sandbox\retail\fakk\models` and `build\sandbox\home\fakk\models` first.

```
probe:
waitforplayer                   // returns when the player spawns: the same moment on both games
wait 25                         // outlast the map's intro (training, swamp1); 2 is enough without one
stuffcmd "give weapon_shotgun.tik"
wait 0.5
stuffcmd "use Shotgun dual"     // use <item name> <right|left|dual>: the hand is required
wait 3
stuffcmd "screenshot"
wait 1
stuffcmd "+lookdown"            // client keys work: this is the real client on both sides
wait 0.25
stuffcmd "-lookdown"
wait 1.5
stuffcmd "screenshot"
wait 1
stuffcmd "quit"                 // ends the run; without it each side runs the full -Secs
end
```

What makes the two pictures comparable:

- **One file for both games.** `capture.ps1` starts the probe with
  `+map <m> +script thread <probe>.scr::probe` before the player spawns, on both engines,
  so `waitforplayer` returns at the player's entry and every wait lands on the same game
  time. Never put a `+wait` on either command line: the player then spawns before the
  thread starts, `waitforplayer` never returns, and older recipes that dropped it to cope
  (a `+devmap <m> +wait 3300` run of an edited copy) showed a different moment, with the
  cinematic letterbox and no HUD where retail had the HUD.
- **Cameras give an exact view; keys do not.** `spawn Camera targetname cam1 origin "x y z"
  angles "pitch yaw 0"`, then `cuecamera $cam1` and a one-second wait, frames the same
  view on both games (the HUD stays up on both). A held key such as `+lookdown` is timed by
  rendered frames, and the two games render at different rates, so the view lands a
  little differently on each: the crosshair shots, whose crosshair matched, still differed
  from retail's by a mean of 3 to 6 per channel, mostly from the shifted view, against
  about 2 for the swamp camera shots (a rerun of ours repeated its figures to within 0.1).
  Use a camera when the comparison is about pixels; use keys when the player's own view is
  the point (the crosshair, the weapon, the HUD reacting).
- **The "Wrote screenshots/..." line.** Each `screenshot` prints it for about three
  seconds, centred on retail and at the top left on ours, so it lands in the next shot.
  Space shots further apart, or pass `-MaskNotify` so the figures ignore it.
- **Change one thing between shots to measure it.** Take the same view without and with
  an effect (a cvar, `rendereffects -x` / `+x`, a script line), on both games, and compare
  what it adds (below). `+set r_farplane_nofog 1` (not a cheat cvar) on both sides separates
  fog from lighting: the fog factor at a pixel is
  (fogged - unfogged) / (fog colour - unfogged), and comparing fogged pixels alone once hid
  that the swamp branches' base colours already matched.
- **Script lines for game state.** `$player health 50`, `$player ammo "Gas Pod" 5` and
  similar work as plain lines in the probe. `$player origin (...)` does not move the player
  (her think copies the player state back each frame); use a camera for a viewpoint.

## Read the result

Look at `pair-<name>-<n>.png` first: the difference panel (scaled by 4) shows whether what
differs is the thing in question or the scene around it. Then zoom in on the region:

```powershell
$cap = 'build\compare\captures\probe-crosshair-training'
python -I .claude\skills\retail-capture\scripts\shots.py pair "$cap\retail-1.tga" "$cap\ours-1.tga" `
    "$cap\zoom-1.png" --crop 300 240 390 290 --zoom 5
```

Read the figures against the noise of the scene. Two runs never match to the pixel:
animations, particles and the water gauge move, and held keys shift the view. Before
calling a small difference a bug, run retail twice (`-Side retail`, keeping a copy of the
first run's `retail-*.tga`) and pair those with `shots.py pair`; a difference of the same
size is noise. Camera shots repeat closely on ours: two runs of `probe-swbranch` gave
the same figures against retail to within 0.01, so between two builds any change in the
figures is the builds' doing. A real rendering difference is usually structured: an
object in the wrong plane, a wrong colour over a whole surface, something missing.

To measure an effect rather than look at it, pair the shot without it and the shot with
it from the same game:

```powershell
python -I .claude\skills\retail-capture\scripts\shots.py delta "$cap\retail-0.tga" "$cap\retail-1.tga" --grid 4x3 --mask-notify
python -I .claude\skills\retail-capture\scripts\shots.py delta "$cap\ours-0.tga" "$cap\ours-1.tga" --grid 4x3 --mask-notify
```

and compare the two games' sums per cell; that is how swapped flare sets showed up as light
added to the wrong cells.

## Menus

The `.urc` menus need no map or probe: `tools\retail-compare\capture-menus.ps1 -Menus
loadsave,controls` pushes each menu on both games and pairs the shots in
`build\compare\captures\menus`. A matching menu differs only in the animated clouds and
in values that come from each game's own config (a slider's place, a check box, a cvar
shown in a label). The script's header says why it clears retail's `developer` and why
its waits are long.

## Traps

- **Retail keeps archived cvars.** Retail saves its archived cvars in
  `build\sandbox\retail\fakk\config.cfg` on exit, and the sandbox is reused. A
  `-RetailExtra "+set r_lightcoronasize 0"` run left the corona off in the next "default"
  retail capture. Set such a cvar back explicitly, or check that file, before trusting a
  default capture.
- **Cheat cvars do nothing on ours** under `+map` (cheats are off): a `CVAR_CHEAT` cvar
  stays at its default and the "experiment" shows no change. Cheat *commands* (`give`,
  `whatis`) do work on both. Read a cvar's value back in the log before trusting a result
  that depends on it.
- **A log that stops at `Hunk_Clear`** was blocked by the "safe mode?" dialog; the runner
  passes `+set com_abnormalExitDialog 0`, so something else launched the game.
- **The loading screen** cannot be shot from a probe (the renderer is down until the
  gamestate's `R_Init`). Run the client with `+set sv_fps 1 +set com_maxfps 20 +map <m>
  +exec <cfg>`, the cfg in `build\sandbox\home\fakk` holding `wait 1` / `screenshot` pairs
  (the startup command line is capped at 32 lines); the shots before `CS_PRIMED to
  CS_ACTIVE` in the log are the load.
- **Keep probe names short.** The retail client silently drops the end of a long command
  line, so a long probe name plus `-Extra` or `-RetailExtra` means the probe never starts
  on retail: the map loads and nothing happens (14 characters was too long with
  `+set r_mode 4`). A retail log with no probe output points here.
- **Retail cuts long file paths.** Paths over 127 characters lose their end, so from a
  deep worktree retail writes `shot0000.tg`; `capture.ps1` takes any `shot*` file as a
  TGA.
- **Image sizes.** `-Mode 3` is 640x480 on both engines. `shots.py` reads uncompressed TGA
  only, which is what `screenshot` writes; PIL is not installed, so do not reach for it.

## Record what you learned

- What retail does, with the `fakk2.exe` addresses it was read from, goes in
  `docs/known-differences.md`, with the capture that confirmed the fix.
- Commit the probe with the fix it verified and list it in `tools/retail-compare/README.md`;
  a probe that showed the bug is its regression test.
